// ============================================================================
//  Syspect — DPC suclusu tespiti (uygulama)
//  Gerekce ve kontrol grubu tasarimi icin bkz. dpc_source.h
// ============================================================================
#ifdef _WIN32

#define INITGUID
#include "dpc_source.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <psapi.h>

#include <algorithm>
#include <atomic>

namespace ssdpc {
namespace {

// ----------------------------------------------------------------------------
//  Cekirdek olay tanimlari
// ----------------------------------------------------------------------------
//  PerfInfo saglayicisi. DPC ve kesme olaylari bu GUID altinda gelir.
const GUID kPerfInfoGuid = {
    0xce1dbfb4, 0x137e, 0x4da6, {0x87, 0xb0, 0x3f, 0x59, 0xaa, 0x10, 0x2c, 0xbc}
};

// PerfInfo opcode'lari. 68 = DPC, 69 = TimerDPC, 67 = ThreadedDPC.
// Ucu de bir surucunun DPC rutinini calistirmasidir; ayirmiyoruz cunku
// kullanici acisindan fark yok — islemci o surede baska is yapamiyor.
constexpr UCHAR kOpDpc         = 68;
constexpr UCHAR kOpTimerDpc    = 69;
constexpr UCHAR kOpThreadedDpc = 67;

// DPC olayinin verisi (x64). InitialTime DPC'nin BASLADIGI an, olay ise
// bittiginde yazilir; sure ikisinin farkidir. Ikisi de QPC birimindedir.
#pragma pack(push, 1)
struct DpcEventData64 {
    uint64_t initialTime;
    uint64_t routine;
};
struct DpcEventData32 {
    uint64_t initialTime;
    uint32_t routine;
};
#pragma pack(pop)

// ----------------------------------------------------------------------------
//  Toplama durumu
// ----------------------------------------------------------------------------
struct Collector {
    Options            opts;
    ModuleMap          modules;

    TRACEHANDLE        session  = 0;
    TRACEHANDLE        consumer = INVALID_PROCESSTRACE_HANDLE;
    std::atomic<bool>  stopping{false};

    uint64_t           qpcFreq   = 0;   // olay basliklarindan gelir
    uint64_t           firstQpc  = 0;   // ilk olayin zamani = t0

    // Surucu adi -> indis. Ham kayitlarda adi degil indisi tutuyoruz:
    // saniyede binlerce olayin her birine string kopyalamak gereksiz.
    std::map<std::string, uint32_t> driverIndex;
    std::vector<std::string>        driverNames;

    std::vector<LongDpc> events;
    uint64_t             totalEvents = 0;
    uint64_t             unresolved  = 0;
    double               longTotalMs = 0.0;

    uint32_t indexOf(const std::string& name) {
        auto it = driverIndex.find(name);
        if (it != driverIndex.end()) return it->second;
        const uint32_t idx = static_cast<uint32_t>(driverNames.size());
        driverNames.push_back(name);
        driverIndex[name] = idx;
        return idx;
    }
};

Collector* g_col = nullptr;

std::string lastErrorText(ULONG rc) {
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, rc, 0, reinterpret_cast<char*>(&msg), 0, nullptr);
    std::string out = msg ? msg : "bilinmeyen hata";
    if (msg) LocalFree(msg);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// ----------------------------------------------------------------------------
//  Olay isleyici
// ----------------------------------------------------------------------------
void WINAPI onEvent(EVENT_RECORD* rec) {
    Collector* c = g_col;
    if (!c || c->stopping.load()) return;

    if (!IsEqualGUID(rec->EventHeader.ProviderId, kPerfInfoGuid)) return;

    const UCHAR op = rec->EventHeader.EventDescriptor.Opcode;
    if (op != kOpDpc && op != kOpTimerDpc && op != kOpThreadedDpc) return;

    const bool is64 = (rec->EventHeader.Flags & EVENT_HEADER_FLAG_64_BIT_HEADER) != 0;
    const size_t need = is64 ? sizeof(DpcEventData64) : sizeof(DpcEventData32);
    if (!rec->UserData || rec->UserDataLength < need) return;

    uint64_t initialTime = 0, routine = 0;
    if (is64) {
        const auto* d = static_cast<const DpcEventData64*>(rec->UserData);
        initialTime = d->initialTime;
        routine     = d->routine;
    } else {
        const auto* d = static_cast<const DpcEventData32*>(rec->UserData);
        initialTime = d->initialTime;
        routine     = d->routine;
    }

    const uint64_t endQpc = static_cast<uint64_t>(rec->EventHeader.TimeStamp.QuadPart);
    if (c->firstQpc == 0) c->firstQpc = endQpc;
    ++c->totalEvents;

    if (c->qpcFreq == 0 || endQpc <= initialTime) return;

    const double ms = 1000.0 * static_cast<double>(endQpc - initialTime)
                    / static_cast<double>(c->qpcFreq);

    // Saglik kontrolu: saatler arasi tutarsizlik ya da bozuk veri sasma
    // degerler uretebilir. 1 saniyeden uzun bir DPC gercek degildir.
    if (ms <= 0.0 || ms > 1000.0) return;
    if (ms < c->opts.longDpcMs) return;

    std::string drv = c->modules.resolve(routine);
    if (drv.empty()) { ++c->unresolved; drv = "(bilinmeyen sürücü)"; }

    LongDpc e;
    e.qpc         = endQpc;      // MUTLAK — hizalama icin sart
    e.ms          = ms;
    e.driverIndex = c->indexOf(drv);
    c->events.push_back(e);

    c->longTotalMs += ms;
}

ULONG WINAPI onBuffer(EVENT_TRACE_LOGFILEW* log) {
    Collector* c = g_col;
    if (!c) return FALSE;
    // QPC frekansi ilk tampondan gelir; olay suresi bu olmadan hesaplanamaz.
    if (c->qpcFreq == 0 && log->LogfileHeader.PerfFreq.QuadPart > 0)
        c->qpcFreq = static_cast<uint64_t>(log->LogfileHeader.PerfFreq.QuadPart);
    return c->stopping.load() ? FALSE : TRUE;
}

// ----------------------------------------------------------------------------
//  Oturum ozellikleri
// ----------------------------------------------------------------------------
//  NT Kernel Logger'in adi SABITTIR ve sistemde tek olabilir. Baska bir arac
//  (xperf, WPR, bazi guvenlik yazilimlari) acmissa oturum acilmaz.
std::vector<uint8_t> makeKernelProperties() {
    const std::wstring name = KERNEL_LOGGER_NAMEW;
    const size_t bufSize = sizeof(EVENT_TRACE_PROPERTIES)
                         + (name.size() + 1) * sizeof(wchar_t);
    std::vector<uint8_t> buf(bufSize, 0);

    auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
    p->Wnode.BufferSize    = static_cast<ULONG>(bufSize);
    p->Wnode.Flags         = WNODE_FLAG_TRACED_GUID;
    p->Wnode.ClientContext = 1;                  // QPC zaman damgasi
    p->Wnode.Guid          = SystemTraceControlGuid;
    p->LogFileMode         = EVENT_TRACE_REAL_TIME_MODE;
    p->LoggerNameOffset    = sizeof(EVENT_TRACE_PROPERTIES);

    // Yalnizca DPC. INTERRUPT bayragi bilerek KAPALI: hacmi kat kat artiriyor
    // ve suclu tespitine bir sey katmiyor — kullanicinin sorusu "hangi surucu
    // islemciyi tutuyor", kesme sayisi degil.
    p->EnableFlags = EVENT_TRACE_FLAG_DPC;

    // DPC hacmi yuksek. Present oturumundan daha buyuk tamponlar veriliyor;
    // dusen olay burada dogrudan yanlis teshis demek.
    p->BufferSize     = 256;   // KB
    p->MinimumBuffers = 32;
    p->MaximumBuffers = 128;
    p->FlushTimer     = 1;

    return buf;
}

void stopKernelSession() {
    auto props = makeKernelProperties();
    ControlTraceW(0, KERNEL_LOGGER_NAMEW,
                  reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()),
                  EVENT_TRACE_CONTROL_STOP);
}

bool isElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD sz = sizeof(el);
    const bool ok = GetTokenInformation(tok, TokenElevation, &el, sz, &sz) != 0;
    CloseHandle(tok);
    return ok && el.TokenIsElevated;
}

} // namespace

// ============================================================================
//  Modul tablosu
// ============================================================================
//  EnumDeviceDrivers yuklu cekirdek modullerinin taban adreslerini verir.
//  Boyutlarini VERMEZ; bu yuzden "adresten kucuk ya da esit en buyuk taban"
//  araniyor. Moduller adres uzayinda bitisik yuklendigi icin bu yaklasim
//  pratikte dogru sonucu verir; tek hatasi, bir modulun sonundan sonraki
//  bosluga dusen adresin o module sayilmasidir. Sonuc adresler DPC rutini
//  oldugu icin bu durum gercekte olusmuyor.
bool ModuleMap::build() {
    sorted_.clear();

    DWORD needed = 0;
    if (!EnumDeviceDrivers(nullptr, 0, &needed) || needed == 0) return false;

    std::vector<LPVOID> bases(needed / sizeof(LPVOID) + 16);
    if (!EnumDeviceDrivers(bases.data(),
                           static_cast<DWORD>(bases.size() * sizeof(LPVOID)),
                           &needed))
        return false;

    const size_t count = needed / sizeof(LPVOID);
    sorted_.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        if (!bases[i]) continue;
        wchar_t nameW[MAX_PATH] = L"";
        if (GetDeviceDriverBaseNameW(bases[i], nameW, MAX_PATH) == 0) continue;

        char nameA[MAX_PATH] = "";
        WideCharToMultiByte(CP_UTF8, 0, nameW, -1, nameA, MAX_PATH, nullptr, nullptr);

        Entry e;
        e.base = reinterpret_cast<uint64_t>(bases[i]);
        e.name = nameA;
        sorted_.push_back(std::move(e));
    }

    std::sort(sorted_.begin(), sorted_.end(),
              [](const Entry& a, const Entry& b) { return a.base < b.base; });
    return !sorted_.empty();
}

std::string ModuleMap::resolve(uint64_t address) const {
    if (sorted_.empty() || address == 0) return {};

    // Adresten buyuk ilk girdi bulunur, bir onceki modul adaydir.
    auto it = std::upper_bound(sorted_.begin(), sorted_.end(), address,
                               [](uint64_t v, const Entry& e) { return v < e.base; });
    if (it == sorted_.begin()) return {};
    --it;

    // Cok uzak adresler modul degildir. 64 MB, en buyuk cekirdek modulunun
    // kat kat ustunde — bu sinirin otesi "bulunamadi" sayilir ki tablo
    // sonundaki adresler son module yapistirilmasin.
    if (address - it->base > 64ull * 1024 * 1024) return {};
    return it->name;
}

// ============================================================================
//  Toplama
// ============================================================================
void stop() { if (g_col) g_col->stopping.store(true); }

Capture run(const Options& opts) {
    Capture out;

    if (!isElevated()) {
        out.error = "DPC toplama yonetici hakki gerektiriyor.";
        return out;
    }

    Collector col;
    col.opts = opts;
    if (!col.modules.build()) {
        out.error = "Cekirdek modul listesi okunamadi.";
        return out;
    }

    // Onceki calistirmadan kalmis olabilir; sessizce kapat.
    stopKernelSession();

    auto props = makeKernelProperties();
    ULONG rc = StartTraceW(&col.session, KERNEL_LOGGER_NAMEW,
                           reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()));
    if (rc == ERROR_ACCESS_DENIED) {
        out.error = "Erisim reddedildi — yonetici olarak calistirin.";
        return out;
    }
    if (rc == ERROR_ALREADY_EXISTS) {
        out.error = "NT Kernel Logger baska bir program tarafindan kullaniliyor "
                    "(xperf, WPR ya da bir guvenlik yazilimi olabilir). "
                    "Sistemde ayni anda yalnizca bir tane olabilir.";
        return out;
    }
    if (rc != ERROR_SUCCESS) {
        out.error = "Cekirdek oturumu acilamadi: " + lastErrorText(rc);
        return out;
    }

    g_col = &col;

    EVENT_TRACE_LOGFILEW log{};
    log.LoggerName          = const_cast<LPWSTR>(KERNEL_LOGGER_NAMEW);
    log.ProcessTraceMode    = PROCESS_TRACE_MODE_REAL_TIME |
                              PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = onEvent;
    log.BufferCallback      = onBuffer;

    col.consumer = OpenTraceW(&log);
    if (col.consumer == INVALID_PROCESSTRACE_HANDLE) {
        out.error = "Oturum tuketicisi acilamadi: " +
                    lastErrorText(GetLastError());
        stopKernelSession();
        g_col = nullptr;
        return out;
    }

    // Sureyi ayri bir iplik sinirlar; ProcessTrace bloklayicidir.
    HANDLE timer = CreateThread(nullptr, 0,
        [](LPVOID p) -> DWORD {
            auto* c = static_cast<Collector*>(p);
            const DWORD ms = c->opts.seconds * 1000u;
            for (DWORD e = 0; e < ms && !c->stopping.load(); e += 100)
                Sleep(100);
            c->stopping.store(true);
            // Oturumu kapatmak ProcessTrace'i dondurur.
            stopKernelSession();
            return 0;
        }, &col, 0, nullptr);

    const ULONGLONG t0 = GetTickCount64();
    ProcessTrace(&col.consumer, 1, nullptr, nullptr);
    out.durationSec = (GetTickCount64() - t0) / 1000.0;

    col.stopping.store(true);
    if (timer) { WaitForSingleObject(timer, 3000); CloseHandle(timer); }
    CloseTrace(col.consumer);
    stopKernelSession();
    g_col = nullptr;

    out.totalDpcEvents      = col.totalEvents;
    out.unresolvedAddresses = col.unresolved;
    out.events              = std::move(col.events);
    out.driverNames         = std::move(col.driverNames);
    out.qpcFreq             = col.qpcFreq;

    if (out.durationSec > 0.0)
        out.longDpcTimePct = 100.0 * (col.longTotalMs / 1000.0) / out.durationSec;

    // Pencereler henuz bilinmiyor; ozet yine de kurulsun ki komut satiri
    // tek basina anlamli cikti verebilsin. Analiz katmani gercek
    // pencerelerle tekrar cagirir.
    summarize(out, {});

    out.ok = true;
    return out;
}

// ============================================================================
//  Kontrol grubu
// ============================================================================
void summarize(Capture& cap, const std::vector<Window>& windows) {
    cap.drivers.clear();

    const double freq = cap.qpcFreq > 0 ? static_cast<double>(cap.qpcFreq) : 0.0;

    double windowSec = 0.0;
    if (freq > 0.0)
        for (const auto& w : windows)
            if (w.endQpc > w.startQpc)
                windowSec += (w.endQpc - w.startQpc) / freq;
    cap.windowSecTotal = windowSec;

    const double outsideSec = std::max(0.0, cap.durationSec - windowSec);

    auto inWindow = [&](uint64_t qpc) {
        for (const auto& w : windows)
            if (qpc >= w.startQpc && qpc <= w.endQpc) return true;
        return false;
    };

    std::vector<DriverStats> stats(cap.driverNames.size());
    for (size_t i = 0; i < cap.driverNames.size(); ++i)
        stats[i].name = cap.driverNames[i];

    for (const auto& e : cap.events) {
        if (e.driverIndex >= stats.size()) continue;
        DriverStats& s = stats[e.driverIndex];
        ++s.longCount;
        s.totalMs += e.ms;
        if (e.ms > s.maxMs) s.maxMs = e.ms;
        if (inWindow(e.qpc)) ++s.longInWindow;
    }

    for (auto& s : stats) {
        if (s.longCount == 0) continue;
        const uint64_t outside = s.longCount - s.longInWindow;

        // Oranlar SANIYE basina. Ham sayilari karsilastirmak yanlis olurdu:
        // takilma pencereleri toplam surenin kucuk bir kismi, dolayisiyla
        // pencere disinda her zaman daha cok olay birikir ve karsilastirma
        // her surucuyu akliyla cikarirdi.
        s.inWindowRate = windowSec  > 0.0 ? s.longInWindow / windowSec  : 0.0;
        s.baselineRate = outsideSec > 0.0 ? outside        / outsideSec : 0.0;
        cap.drivers.push_back(s);
    }

    std::sort(cap.drivers.begin(), cap.drivers.end(),
              [](const DriverStats& a, const DriverStats& b) {
                  return a.longCount > b.longCount;
              });
}

// ============================================================================
//  Suclama esigi
// ============================================================================
//  Bu fonksiyonun tamami tek bir soruyu cevapliyor: elimizdeki kanit bir
//  surucuyu ADIYLA suclamaya yeter mi? Cevap cogu zaman HAYIR olmali.
const DriverStats* primeSuspect(const Capture& cap) {
    // Pencere yoksa ya da cok kisaysa oran hesaplanamaz. Bu durumda hicbir
    // surucu suclanmaz — "veri yetersiz" ile "sorun yok" ayni sey degil.
    if (cap.windowSecTotal < kMinWindowSec) return nullptr;

    // Aday secimi LIFT'e gore yapilir, olay SAYISINA gore DEGIL.
    //
    // Bu ayrim onemli ve bir testle yakalandi: gurultulu bir surucu her yere
    // dustugu icin pencerelere de cok duser, yani en yuksek pencere ici SAYI
    // ona ait olur. Sayiya gore secmek, tam da elemek istedigimiz surucuyu
    // one cikarir — kontrol grubunun butun anlamini tersine cevirir.
    const DriverStats* best = nullptr;
    double bestLift = 0.0;
    for (const auto& d : cap.drivers) {
        if (d.longInWindow < kMinInWindow)   continue;
        const double lift = d.liftOverBaseline();
        if (lift < kMinLift)                 continue;
        if (!best || lift > bestLift) { best = &d; bestLift = lift; }
    }
    return best;
}

} // namespace ssdpc

#endif // _WIN32
