// ============================================================================
//  Syspect — Windows olay gunlugu okuyucu
// ============================================================================
#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "event_log.h"

#include <windows.h>
#include <winevt.h>

#include <algorithm>

namespace sslog {
namespace {

// ----------------------------------------------------------------------------
//  Sorgu gruplari
// ----------------------------------------------------------------------------
//  Tek dev sorgu yerine kucuk sorgular: her biri hizli doner, biri hata
//  verirse otekiler etkilenmez ve hangi kaynagin okunamadigi bellidir.
//
//  Saglayici adlari XPath'te BUYUK/kucuk harfe duyarlidir; ayni surucu
//  Windows surumune gore "disk" ya da "Disk" olarak gorunebildigi icin ikisi
//  de yaziliyor.
struct Group {
    const wchar_t* channel;
    const wchar_t* predicate;
    uint32_t       maxEvents;    // gurultulu kaynaklar icin tavan
};

const Group kGroups[] = {
    // Kontrolsuz kapanma. Tek basina cok guclu bir sinyal: makine kaydini
    // tutamadan gitmis demektir.
    { L"System",
      L"Provider[@Name='Microsoft-Windows-Kernel-Power'] and (EventID=41)",
      2000 },

    // WHEA. ID 18 olumcul; otekiler duzeltilmis hatalardir ve ozellikle 17
    // bircok AMD sisteminde semptomsuz binlerce kez uretilir — tavan bu
    // yuzden yuksek tutuluyor, sayabilmek icin.
    { L"System",
      L"Provider[@Name='Microsoft-Windows-WHEA-Logger'] and "
      L"(EventID=17 or EventID=18 or EventID=19 or EventID=46 or EventID=47)",
      20000 },

    // Mavi ekran kaydi. Dump kapali olsa bile bu olay dusulur; bugcheck kodu
    // olay verisinin icinde metin olarak durur. Dump okuyamadigimiz
    // makinelerde tek kanitimiz budur.
    { L"System",
      L"(Provider[@Name='Microsoft-Windows-WER-SystemErrorReporting'] or "
      L" Provider[@Name='BugCheck']) and (EventID=1001)",
      2000 },

    // TDR — ekran surucusu yanit vermedi ve sifirlandi. Kullanicinin
    // "ekran bir saniye dondu, sonra duzeldi" dedigi seyin tam karsiligi.
    { L"System",
      L"Provider[@Name='Display'] and (EventID=4101)",
      5000 },

    // Depolama. 129 = denetleyiciye sifirlama gonderildi (surucu takildi),
    // 153 = G/C yeniden denendi, 7/11/51 = hatali blok / denetleyici hatasi /
    // sayfalama hatasi.
    { L"System",
      L"(Provider[@Name='disk'] or Provider[@Name='Disk'] or "
      L" Provider[@Name='storahci'] or Provider[@Name='stornvme'] or "
      L" Provider[@Name='iaStorA'] or Provider[@Name='iaStorAC'] or "
      L" Provider[@Name='msahci'] or Provider[@Name='nvme'] or "
      L" Provider[@Name='amdsata'] or Provider[@Name='volmgr']) and "
      L"(EventID=7 or EventID=11 or EventID=51 or EventID=129 or EventID=153)",
      10000 },

    // Dosya sistemi bozulmasi.
    { L"System",
      L"(Provider[@Name='Ntfs'] or Provider[@Name='Microsoft-Windows-Ntfs']) "
      L"and (EventID=55)",
      2000 },

    // Beklenmedik kapanma — Kernel-Power 41'i destekler.
    { L"System",
      L"Provider[@Name='EventLog'] and (EventID=6008)",
      2000 },

    // Uygulama cokmesi. Oyunun kendisi cokuyorsa sorun sistemde olmayabilir;
    // bu yuzden sinyal degil BAGLAM olarak tasiniyor.
    { L"Application",
      L"Provider[@Name='Application Error'] and (EventID=1000)",
      5000 },
};

// ----------------------------------------------------------------------------
//  (saglayici, kimlik) -> anlam
// ----------------------------------------------------------------------------
bool startsWith(const std::wstring& s, const wchar_t* p) {
    const size_t n = wcslen(p);
    return s.size() >= n && _wcsnicmp(s.c_str(), p, n) == 0;
}

Kind classify(const std::wstring& provider, uint32_t id) {
    if (id == 41  && startsWith(provider, L"Microsoft-Windows-Kernel-Power"))
        return Kind::KernelPower41;

    if (startsWith(provider, L"Microsoft-Windows-WHEA-Logger")) {
        if (id == 18) return Kind::WheaFatal;
        return Kind::WheaCorrected;          // 17 / 19 / 46 / 47
    }

    if (id == 1001) return Kind::BugCheck;
    if (id == 4101) return Kind::Tdr;
    if (id == 55)   return Kind::NtfsCorruption;
    if (id == 6008) return Kind::UnexpectedShutdown;
    if (id == 1000) return Kind::AppCrash;

    if (id == 129) return Kind::StorageReset;
    if (id == 153) return Kind::StorageRetry;
    if (id == 7 || id == 11 || id == 51) return Kind::StorageBadBlock;

    return Kind::Count_;
}

// ----------------------------------------------------------------------------
//  Olay XML'inden ilk <Data> icerigi
// ----------------------------------------------------------------------------
//  Tam mesaj metnini uretmek icin EvtFormatMessage gerekir; o da saglayici
//  meta verisini acmayi ister ve kaldirilmis surucularde basarisiz olur.
//  Bize yalnizca tek bir alan lazim (TDR'de surucu adi, BugCheck'te kod), bu
//  yuzden XML'den dogrudan cekiliyor.
std::string firstDataText(const std::wstring& xml) {
    size_t p = xml.find(L"<Data");
    if (p == std::wstring::npos) return {};
    p = xml.find(L'>', p);
    if (p == std::wstring::npos) return {};
    const size_t end = xml.find(L"</Data>", ++p);
    if (end == std::wstring::npos) return {};

    std::wstring w = xml.substr(p, end - p);
    if (w.size() > 120) w.resize(120);

    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
}

// ----------------------------------------------------------------------------
//  LiveKernelReports — mavi ekransiz surucu cokmesi
// ----------------------------------------------------------------------------
void scanLiveKernelReports(Scan& out) {
    wchar_t root[MAX_PATH];
    if (GetWindowsDirectoryW(root, MAX_PATH) == 0) return;

    std::wstring base = std::wstring(root) + L"\\LiveKernelReports";

    // Kayitlar hem kokte hem alt klasorlerde (WATCHDOG, PoW32kWatchdog...)
    // durabilir. Bir seviye asagi inmek yetiyor.
    std::vector<std::wstring> dirs{ base };

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                fd.cFileName[0] != L'.')
                dirs.push_back(base + L"\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    ULONGLONG newest = 0;
    for (const std::wstring& d : dirs) {
        HANDLE f = FindFirstFileW((d + L"\\*.dmp").c_str(), &fd);
        if (f == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            out.liveKernelReports++;

            ULARGE_INTEGER t;
            t.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
            t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
            if (t.QuadPart > newest) {
                newest = t.QuadPart;
                std::string name;
                for (const wchar_t* c = fd.cFileName; *c; ++c)
                    name.push_back(*c < 128 ? static_cast<char>(*c) : '?');
                out.newestLiveKernelReport = name;
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
}

// ----------------------------------------------------------------------------
//  Tek bir grubu oku
// ----------------------------------------------------------------------------
bool readGroup(const Group& g, uint32_t days, uint64_t now, Scan& out,
               std::string* error) {
    const unsigned long long windowMs = 86400000ull * days;

    wchar_t query[1536];
    _snwprintf_s(query, _countof(query), _TRUNCATE,
                 L"*[System[%s and TimeCreated[timediff(@SystemTime) <= %llu]]]",
                 g.predicate, windowMs);

    EVT_HANDLE q = EvtQuery(nullptr, g.channel, query,
                            EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!q) {
        const DWORD e = GetLastError();
        // Kanal yoksa ya da sorgu reddedildiyse: bu bir hata degil, bu
        // makinede o kaynak yok demektir. Sessizce gec.
        if (e == ERROR_EVT_CHANNEL_NOT_FOUND || e == ERROR_EVT_INVALID_QUERY)
            return true;
        if (error && error->empty())
            *error = "Olay gunlugu sorgusu acilamadi (kod " +
                     std::to_string(static_cast<unsigned>(e)) + ")";
        return false;
    }

    LPCWSTR paths[] = { L"Event/System/EventID",
                        L"Event/System/TimeCreated/@SystemTime",
                        L"Event/System/Provider/@Name" };
    EVT_HANDLE ctx = EvtCreateRenderContext(3, paths, EvtRenderContextValues);
    if (!ctx) { EvtClose(q); return false; }

    const uint64_t cut24 = now > 864000000000ull  ? now - 864000000000ull  : 0;
    const uint64_t cut7d = now > 6048000000000ull ? now - 6048000000000ull : 0;

    std::vector<unsigned char> buf(4096);
    std::vector<wchar_t>       xml(8192);

    uint32_t seen = 0;
    EVT_HANDLE events[32];
    DWORD got = 0;

    while (seen < g.maxEvents &&
           EvtNext(q, 32, events, INFINITE, 0, &got) && got > 0) {
        for (DWORD i = 0; i < got; ++i) {
            DWORD used = 0, props = 0;
            if (!EvtRender(ctx, events[i], EvtRenderEventValues,
                           static_cast<DWORD>(buf.size()), buf.data(),
                           &used, &props)) {
                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    buf.resize(used);
                    if (!EvtRender(ctx, events[i], EvtRenderEventValues,
                                   static_cast<DWORD>(buf.size()), buf.data(),
                                   &used, &props)) { EvtClose(events[i]); continue; }
                } else { EvtClose(events[i]); continue; }
            }
            if (props < 3) { EvtClose(events[i]); continue; }

            const EVT_VARIANT* v = reinterpret_cast<const EVT_VARIANT*>(buf.data());

            uint32_t id = 0;
            if (v[0].Type == EvtVarTypeUInt16)      id = v[0].UInt16Val;
            else if (v[0].Type == EvtVarTypeUInt32) id = v[0].UInt32Val;

            uint64_t ft = 0;
            if (v[1].Type == EvtVarTypeFileTime) ft = v[1].FileTimeVal;

            std::wstring prov;
            if (v[2].Type == EvtVarTypeString && v[2].StringVal)
                prov = v[2].StringVal;

            const Kind k = classify(prov, id);
            if (k == Kind::Count_ || ft == 0) { EvtClose(events[i]); continue; }

            Series& s = out.series[static_cast<size_t>(k)];
            s.kind = k;
            s.last30d++;
            if (ft >= cut7d) s.last7d++;
            if (ft >= cut24) s.last24h++;
            if (ft > s.newestFileTime) s.newestFileTime = ft;
            if (s.oldestFileTime == 0 || ft < s.oldestFileTime)
                s.oldestFileTime = ft;

            // Ilk birkac ornegi sakla. Sorgu yeniden-eskiye dondugu icin
            // bunlar en yeni olaylardir.
            if (s.samples.size() < 8) {
                Event ev;
                ev.kind        = k;
                ev.eventId     = id;
                ev.fileTimeUtc = ft;
                for (wchar_t c : prov)
                    ev.provider.push_back(c < 128 ? static_cast<char>(c) : '?');

                // Ayrinti yalnizca ise yarayan turlerde cikariliyor: TDR'de
                // surucu adi, mavi ekranda bugcheck kodu, cokmede uygulama
                // adi.
                //
                // AppCrash burada yalnizca kullaniciya "hangi program coktu"
                // demek icin degil: TDR ve BugCheck kayitlarinin olmadigi
                // makinelerde firstDataText'in gercek Windows olay XML'inde
                // calistigini DOGRULAYABILDIGIMIZ tek tur bu. Ayni ayristirma
                // kodu ucunde de kullaniliyor.
                if (k == Kind::Tdr || k == Kind::BugCheck ||
                    k == Kind::AppCrash) {
                    DWORD xused = 0, xprops = 0;
                    if (!EvtRender(nullptr, events[i], EvtRenderEventXml,
                                   static_cast<DWORD>(xml.size() * sizeof(wchar_t)),
                                   xml.data(), &xused, &xprops) &&
                        GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                        xml.resize(xused / sizeof(wchar_t) + 1);
                        EvtRender(nullptr, events[i], EvtRenderEventXml,
                                  static_cast<DWORD>(xml.size() * sizeof(wchar_t)),
                                  xml.data(), &xused, &xprops);
                    }
                    if (xused > 0) ev.detail = firstDataText(xml.data());
                }
                s.samples.push_back(ev);
            }

            out.totalRead++;
            seen++;
            EvtClose(events[i]);
        }
        got = 0;
    }

    EvtClose(ctx);
    EvtClose(q);
    return true;
}

} // namespace

// ----------------------------------------------------------------------------
const char* kindLabel(Kind k) {
    switch (k) {
        case Kind::KernelPower41:      return "Kontrolsüz kapanma (Kernel-Power 41)";
        case Kind::WheaFatal:          return "Ölümcül donanım hatası (WHEA 18)";
        case Kind::WheaCorrected:      return "Düzeltilmiş donanım hatası (WHEA)";
        case Kind::BugCheck:           return "Mavi ekran kaydı";
        case Kind::Tdr:                return "Ekran sürücüsü sıfırlandı (TDR)";
        case Kind::StorageReset:       return "Depolama aygıtı sıfırlandı";
        case Kind::StorageRetry:       return "Disk okuma/yazma yeniden denendi";
        case Kind::StorageBadBlock:    return "Disk hatası / hatalı blok";
        case Kind::NtfsCorruption:     return "Dosya sistemi bozulması";
        case Kind::UnexpectedShutdown: return "Beklenmedik kapanma";
        case Kind::AppCrash:           return "Uygulama çökmesi";
        default:                       return "Bilinmeyen";
    }
}

const char* kindKey(Kind k) {
    switch (k) {
        case Kind::KernelPower41:      return "kernel_power_41";
        case Kind::WheaFatal:          return "whea_fatal";
        case Kind::WheaCorrected:      return "whea_corrected";
        case Kind::BugCheck:           return "bugcheck";
        case Kind::Tdr:                return "tdr";
        case Kind::StorageReset:       return "storage_reset";
        case Kind::StorageRetry:       return "storage_retry";
        case Kind::StorageBadBlock:    return "storage_error";
        case Kind::NtfsCorruption:     return "ntfs_corruption";
        case Kind::UnexpectedShutdown: return "unexpected_shutdown";
        case Kind::AppCrash:           return "app_crash";
        default:                       return "unknown";
    }
}

Kind kindFromKey(const std::string& key) {
    for (size_t i = 0; i < static_cast<size_t>(Kind::Count_); ++i) {
        const Kind k = static_cast<Kind>(i);
        if (key == kindKey(k)) return k;
    }
    return Kind::Count_;
}

bool kindIsSevere(Kind k) {
    // Tek bir olay bile anlam tasiyanlar. WheaCorrected ve AppCrash BILEREK
    // disarida: ikisi de saglikli makinede rutin olarak gorulur.
    switch (k) {
        case Kind::KernelPower41:
        case Kind::WheaFatal:
        case Kind::BugCheck:
        case Kind::StorageReset:
        case Kind::StorageBadBlock:
        case Kind::NtfsCorruption:
            return true;
        default:
            return false;
    }
}

uint64_t nowFileTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

uint32_t countInWindow(const Series& s, uint64_t startUtc, uint64_t endUtc,
                       uint32_t slackSec) {
    const uint64_t slack = static_cast<uint64_t>(slackSec) * 10000000ull;
    const uint64_t lo = startUtc > slack ? startUtc - slack : 0;
    const uint64_t hi = endUtc + slack;

    // DIKKAT — yalnizca saklanan ornekler taranir (en yeni 8 olay). Olcum
    // penceresi her zaman "simdi"ye bitisik oldugu icin cakisan olaylar
    // zaten bu kumenin icindedir. Gecmisteki bir olayi aramak icin bu
    // fonksiyon kullanilamaz; onun icin Series sayaclari var.
    uint32_t n = 0;
    for (const Event& e : s.samples)
        if (e.fileTimeUtc >= lo && e.fileTimeUtc <= hi) n++;
    return n;
}

void applyTo(ss::SystemInfo& sys, const Scan& log,
             uint64_t winStartUtc, uint64_t winEndUtc) {
    if (!log.ok) return;
    sys.eventLogRead = true;

    const Series& whea  = log.at(Kind::WheaCorrected);
    const Series& fatal = log.at(Kind::WheaFatal);
    const Series& kp41  = log.at(Kind::KernelPower41);
    const Series& bug   = log.at(Kind::BugCheck);
    const Series& tdr   = log.at(Kind::Tdr);
    const Series& rst   = log.at(Kind::StorageReset);
    const Series& rty   = log.at(Kind::StorageRetry);
    const Series& bad   = log.at(Kind::StorageBadBlock);
    const Series& ntfs  = log.at(Kind::NtfsCorruption);

    sys.wheaCorrected        = whea.last30d;
    sys.wheaCorrectedSpiking = whea.spikingToday();
    sys.wheaFatal            = fatal.last30d;
    sys.kernelPower41        = kp41.last30d;
    sys.bugcheckCount        = bug.last30d;
    sys.liveKernelReports    = log.liveKernelReports;

    sys.tdrCount             = tdr.last30d;
    sys.storageResetCount    = rst.last30d;
    sys.storageRetryCount    = rty.last30d;
    sys.storageErrorCount    = bad.last30d;
    sys.ntfsCorruption       = ntfs.last30d;

    // Suclanan ekran surucusunun adi olay verisinin icinde ("nvlddmkm").
    for (const Event& e : tdr.samples)
        if (!e.detail.empty()) { sys.tdrDriver = e.detail; break; }

    if (winStartUtc > 0 && winEndUtc >= winStartUtc) {
        sys.tdrDuringCapture = countInWindow(tdr, winStartUtc, winEndUtc);
        sys.storageDuringCapture =
            countInWindow(rst, winStartUtc, winEndUtc) +
            countInWindow(bad, winStartUtc, winEndUtc);
    }
}

Scan scan(uint32_t days) {
    Scan out;
    out.attempted = true;
    if (days == 0) days = 30;

    for (size_t i = 0; i < static_cast<size_t>(Kind::Count_); ++i)
        out.series[i].kind = static_cast<Kind>(i);

    const uint64_t now = nowFileTime();

    bool any = false;
    for (const Group& g : kGroups)
        if (readGroup(g, days, now, out, &out.error)) any = true;

    scanLiveKernelReports(out);

    out.ok = any;
    if (out.ok) out.error.clear();
    else if (out.error.empty()) out.error = "Olay günlüğü okunamadı";
    return out;
}

} // namespace sslog

#endif // _WIN32
