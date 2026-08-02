// ============================================================================
//  StutterScope — komut satiri arayuzu
//  ----------------------------------------------------------------------
//  Motoru calistirilabilir bir urune donusturur. Bugun calisan tek yol:
//
//      PresentMon.exe -captureall -output_file trace.csv
//      ss_cli analyze trace.csv --desktop-stutter --am5 --ram=6000 --jedec=4800
//
//  ETW toplayicisi geldiginde ayni CLI "capture" alt komutuyla PresentMon'a
//  bagimli kalmadan calisacak; rapor bicimi degismeyecek.
//
//  TASARIM NOTU: Bu dosya Windows API'sine bagimli DEGILDIR. Konsol kod
//  sayfasi disinda hicbir platform ozel cagri yok. Kullanici metinleri
//  core.cpp ile ayni kurali izler: Turkce, ama saf ASCII (aksansiz).
// ============================================================================
#include "frame_source.h"
#include "report_html.h"
#include "version.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shellapi.h>
#  include "etw_frame_source.h"
#  include "event_log.h"
#  include "system_probe.h"
#  include "storage_probe.h"
#  include "dpc_source.h"
#  include "i18n.h"
#endif

namespace {

// ----------------------------------------------------------------------------
//  Argüman yardimcilari
// ----------------------------------------------------------------------------

// "--ram=6000" -> anahtar "--ram", deger "6000"
bool splitFlag(const std::string& arg, std::string& key, std::string& value) {
    const size_t eq = arg.find('=');
    if (eq == std::string::npos) { key = arg; value.clear(); return false; }
    key   = arg.substr(0, eq);
    value = arg.substr(eq + 1);
    return true;
}

bool toUint(const std::string& s, uint32_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = static_cast<uint32_t>(v);
    return true;
}

// ----------------------------------------------------------------------------
//  Bicimlendirme
// ----------------------------------------------------------------------------
std::string fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

void rule(char c = '=') {
    std::cout << std::string(64, c) << "\n";
}

// JSON string kacisi — sadece gerekli minimum
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// ----------------------------------------------------------------------------
//  Kullanim
// ----------------------------------------------------------------------------
void printUsage() {
    std::cout <<
SS_APP_NAME " " SS_VERSION_STRING " — oyun kararliligi teshis araci\n"
"\n"
"KULLANIM\n"
"  ss_cli capture [secenekler]            Kendi ETW oturumuyla canli yakalar\n"
"  ss_cli analyze <dosya.csv> [secenekler] PresentMon CSV'sini okur\n"
"  ss_cli eventlog [--days=<n>]           Olay gunlugunu tarar ve dokum verir\n"
"  ss_cli storage                         Disk envanteri ve SMART dokumu\n"
"  ss_cli dpc [--seconds=<n>]             DPC dokumu (yonetici hakki gerekir)\n"
"  ss_cli --help\n"
"\n"
"CAPTURE (yonetici hakki gerekir)\n"
"  --seconds=<n>         Yakalama suresi (varsayilan 60)\n"
"  --pid=<n>             Yalnizca bu surecin kareleri (varsayilan: hepsi)\n"
"  Ornek: ss_cli capture --seconds=120 --html\n"
"\n"
"  analyze icin CSV uretmek isterseniz:\n"
"      PresentMon.exe -captureall -output_file trace.csv -timed 120\n"
"\n"
"DAVRANIS SECENEKLERI (en degerli ayirt ediciler)\n"
"  --desktop-stutter     Takilma oyun disinda da oluyor (masaustu, tarayici)\n"
"  --monitor-changed     Yakin zamanda monitor/tazeleme hizi degisti\n"
"  --driver-updated      Yakin zamanda ekran karti surucusu guncellendi\n"
"  --note=\"metin\"        Yakin zamandaki degisiklik notu\n"
"\n"
"DONANIM SECENEKLERI\n"
"  --cpu=\"ad\"            Islemci adi\n"
"  --gpu=\"ad\"            Ekran karti adi\n"
"  --am5                 Platform AM5 (Ryzen 7000/9000)\n"
"  --intel-ddr5          Platform Intel + DDR5 (12. nesil ve sonrasi)\n"
"  --ram=<MT/s>          Yapilandirilmis bellek hizi (orn. 6000)\n"
"  --jedec=<MT/s>        JEDEC taban hizi (orn. 4800)\n"
"  --dimms=<adet>        Takili bellek modulu sayisi\n"
"  --expo                EXPO/XMP profili acik\n"
"  --curve-optimizer     Curve Optimizer / undervolt aktif\n"
"  --gpu-oc              Ekran karti hiz asirtmali\n"
"  --psu-watts=<W>       Guc kaynagi gucu\n"
"  --psu-tier=<1-4>      Guc kaynagi kalitesi (1=zayif, 4=cok iyi)\n"
"  --peak-watts=<W>      Tahmini tepe guc tuketimi\n"
"\n"
"OLAY GUNLUGU (capture bunlari KENDISI okur; asagidakiler elle gecmek icin)\n"
"  --no-event-log        Olay gunlugunu hic okuma\n"
"  --whea-corrected=<n>  Duzeltilmis WHEA olayi (ID 17/19/46/47)\n"
"  --whea-fatal=<n>      Olumcul WHEA olayi (ID 18)\n"
"  --kernel-power41=<n>  Kernel-Power 41 (kontrolsuz kapanma)\n"
"  --bugchecks=<n>       Mavi ekran sayisi (olay 1001)\n"
"  --live-kernel=<n>     LiveKernelReports (mavi ekransiz GPU cokmesi)\n"
"\n"
"CIKTI\n"
"  --html[=<dosya>]      Gorsel rapor uret (varsayilan: stutterscope-rapor.html)\n"
"                        ve tarayicida ac. Metin ciktisi basilmaz.\n"
"  --no-open             --html ile birlikte: dosyayi uret ama acma\n"
"  --json                Raporu JSON olarak yaz\n"
"  --quiet               Sadece teshis bolumunu yaz\n"
"\n"
"CIKIS KODLARI\n"
"  0 basarili   1 kullanim hatasi   2 kaynak okunamadi\n";
}

// ----------------------------------------------------------------------------
//  Metin raporu
// ----------------------------------------------------------------------------
void printTextReport(const ss::AnalysisResult& r,
                     const ss::FrameSourceInfo& src,
                     bool quiet) {
    if (!quiet) {
        rule();
        std::cout << SS_APP_NAME " " SS_VERSION_STRING " — analiz raporu\n";
        rule();

        std::cout << "Kaynak    : " << src.kind << "\n";
        if (!src.application.empty())
            std::cout << "Uygulama  : " << src.application << "\n";
        if (src.processId != 0)
            std::cout << "Surec ID  : " << src.processId << "\n";
        if (!src.note.empty())
            std::cout << "Veri      : " << src.note << "\n";

        const ss::SessionStats& s = r.stats;

        std::cout << "\nKARE ZAMANLARI\n";
        std::cout << "  Ortalama FPS       : " << fixed(s.avgFps, 1) << "\n";
        std::cout << "  Medyan FPS         : " << fixed(s.medianFps, 1) << "\n";
        std::cout << "  %1 dusuk FPS       : " << fixed(s.onePercentLowFps, 1) << "\n";
        std::cout << "  Medyan kare suresi : " << fixed(s.medianFrameTimeMs, 2) << " ms\n";
        std::cout << "  P99 kare suresi    : " << fixed(s.p99FrameTimeMs, 2) << " ms\n";

        std::cout << "\nTAKILMALAR\n";
        std::cout << "  Toplam             : " << s.stutterCount << "\n";
        std::cout << "  Mikro-takilma      : " << s.microStutterCount;
        if (s.periodicMicroStutter) {
            std::cout << "  (DUZENLI, ~"
                      << fixed(s.microStutterPeriodMs, 0) << " ms araliklarla)";
        }
        std::cout << "\n";
        std::cout << "  Sicrama (hitch)    : " << s.hitchCount << "\n";
        std::cout << "  Donma (freeze)     : " << s.freezeCount << "\n";

        if (s.lowFpsNotStutter) {
            std::cout << "\n  NOT: Takilma tespit edilmedi. Kare suresi dagilimi dar "
                         "ve duzenli;\n       sorun kararlilik degil, ham performans.\n";
        }
        std::cout << "\n";
    }

    rule('-');
    std::cout << "TESHIS\n";
    rule('-');

    const ss::Diagnosis& d = r.diagnosis;

    std::cout << d.headline << "\n";
    std::cout << "Guven: %" << d.confidence << "\n";

    if (d.inconclusive) {
        std::cout << "\nMotor hukum vermeyi reddediyor. Yanlis teshis, teshissizlikten\n"
                     "daha pahalidir. Daha uzun bir yakalama al (en az 5 dakika) ya da\n"
                     "eksik baglami --am5 / --ram / --whea-* secenekleriyle ver.\n";
    }

    if (!d.ranked.empty()) std::cout << "\n";
    int index = 1;
    for (const ss::Hypothesis& h : d.ranked) {
        std::cout << "  " << index++ << ") %" << h.percent << "  " << h.label << "\n";
        if (!h.evidence.empty())
            std::cout << "        Kanit     : " << h.evidence << "\n";
        if (!h.action.empty())
            std::cout << "        Yapilacak : " << h.action << "\n";
        std::cout << "\n";
    }

    if (src.kind == "csv") {
        std::cout << "SINIR: CSV modunda yalnizca kare deseni tabanli kurallar calisir.\n"
                     "DPC, VRAM ve disk sinyalleri icin ETW toplayicisi gerekir.\n";
    }
}

// ----------------------------------------------------------------------------
//  JSON raporu
// ----------------------------------------------------------------------------
void printJsonReport(const ss::AnalysisResult& r, const ss::FrameSourceInfo& src) {
    const ss::SessionStats& s = r.stats;
    const ss::Diagnosis&    d = r.diagnosis;

    std::cout << "{\n";
    std::cout << "  \"source\": {\n";
    std::cout << "    \"kind\": \""        << jsonEscape(src.kind)        << "\",\n";
    std::cout << "    \"application\": \"" << jsonEscape(src.application) << "\",\n";
    std::cout << "    \"processId\": "     << src.processId               << ",\n";
    std::cout << "    \"note\": \""        << jsonEscape(src.note)        << "\"\n";
    std::cout << "  },\n";

    std::cout << "  \"stats\": {\n";
    std::cout << "    \"frameCount\": "        << s.frameCount                    << ",\n";
    std::cout << "    \"avgFps\": "            << fixed(s.avgFps, 2)              << ",\n";
    std::cout << "    \"medianFps\": "         << fixed(s.medianFps, 2)           << ",\n";
    std::cout << "    \"durationSec\": "       << fixed(s.durationSec, 2)         << ",\n";
    std::cout << "    \"onePercentLowFps\": "  << fixed(s.onePercentLowFps, 2)    << ",\n";
    std::cout << "    \"medianFrameTimeMs\": " << fixed(s.medianFrameTimeMs, 3)   << ",\n";
    std::cout << "    \"p99FrameTimeMs\": "    << fixed(s.p99FrameTimeMs, 3)      << ",\n";
    std::cout << "    \"stutterCount\": "      << s.stutterCount                  << ",\n";
    std::cout << "    \"microStutterCount\": " << s.microStutterCount             << ",\n";
    std::cout << "    \"hitchCount\": "        << s.hitchCount                    << ",\n";
    std::cout << "    \"freezeCount\": "       << s.freezeCount                   << ",\n";
    std::cout << "    \"periodicMicroStutter\": "
              << (s.periodicMicroStutter ? "true" : "false")                      << ",\n";
    std::cout << "    \"microStutterPeriodMs\": "
              << fixed(s.microStutterPeriodMs, 2)                                 << ",\n";
    std::cout << "    \"lowFpsNotStutter\": "
              << (s.lowFpsNotStutter ? "true" : "false")                          << "\n";
    std::cout << "  },\n";

    std::cout << "  \"diagnosis\": {\n";
    std::cout << "    \"headline\": \""  << jsonEscape(d.headline) << "\",\n";
    std::cout << "    \"confidence\": "  << d.confidence           << ",\n";
    std::cout << "    \"inconclusive\": "
              << (d.inconclusive ? "true" : "false")               << ",\n";
    std::cout << "    \"hypotheses\": [\n";
    for (size_t i = 0; i < d.ranked.size(); ++i) {
        const ss::Hypothesis& h = d.ranked[i];
        std::cout << "      {\n";
        std::cout << "        \"cause\": \""    << ss::causeKey(h.cause)               << "\",\n";
        std::cout << "        \"percent\": "    << h.percent                           << ",\n";
        std::cout << "        \"label\": \""    << jsonEscape(h.label)                 << "\",\n";
        std::cout << "        \"evidence\": \"" << jsonEscape(h.evidence)              << "\",\n";
        std::cout << "        \"action\": \""   << jsonEscape(h.action)                << "\"\n";
        std::cout << "      }" << (i + 1 < d.ranked.size() ? "," : "") << "\n";
    }
    std::cout << "    ]\n";
    std::cout << "  }\n";
    std::cout << "}\n";
}

// ----------------------------------------------------------------------------
//  HTML raporu diske yaz, istenirse varsayilan tarayicida ac
// ----------------------------------------------------------------------------
int writeHtmlReport(const std::string& path,
                    const std::string& html,
                    bool openInBrowser) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Rapor yazilamadi: " << path << "\n";
        return 2;
    }
    out << html;
    out.close();

    // Tam yol: hem kullaniciya gosterirken hem tarayiciya verirken gerekli
    std::string full = path;
#ifdef _WIN32
    char buf[MAX_PATH];
    if (GetFullPathNameA(path.c_str(), MAX_PATH, buf, nullptr) != 0) full = buf;
#endif

    std::cout << "Rapor hazir: " << full << "\n";

    if (openInBrowser) {
#ifdef _WIN32
        const HINSTANCE rc = ShellExecuteA(nullptr, "open", full.c_str(),
                                           nullptr, nullptr, SW_SHOWNORMAL);
        // ShellExecute basari olcutu: donen deger 32'den BUYUK olmali.
        if (reinterpret_cast<INT_PTR>(rc) <= 32) {
            std::cout << "Tarayici acilamadi, dosyayi elle acin.\n";
        }
#else
        std::cout << "(Tarayicida acma yalnizca Windows'ta destekleniyor)\n";
#endif
    }
    return 0;
}

} // namespace

#ifdef _WIN32
// ----------------------------------------------------------------------------
//  eventlog — olay gunlugu dokumu
// ----------------------------------------------------------------------------
//  Bu komut HUKUM VERMEZ, yalnizca ne okundugunu gosterir. Amaci iki tane:
//  kullanicinin kendi makinesinde neyin birikmis oldugunu gormesi ve bizim
//  okuyucunun gercek makinede dogru calistigini dogrulayabilmemiz.
std::string fileTimeText(uint64_t ft) {
    if (ft == 0) return "-";
    FILETIME f;
    f.dwLowDateTime  = static_cast<DWORD>(ft & 0xFFFFFFFFull);
    f.dwHighDateTime = static_cast<DWORD>(ft >> 32);

    SYSTEMTIME utc, loc;
    if (!FileTimeToSystemTime(&f, &utc)) return "-";
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &loc)) loc = utc;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                  loc.wYear, loc.wMonth, loc.wDay, loc.wHour, loc.wMinute);
    return buf;
}

// printf'in "%-42s" alan genisligi BAYT sayar, karakter degil. Etiketlerde
// Turkce harf oldugu icin (u -> 2 bayt) sutunlar kayiyordu. Devam baytlarini
// (10xxxxxx) saymayarak gercek karakter sayisina gore dolgu yapiyoruz.
std::string padRight(const char* s, size_t width) {
    std::string out(s);
    size_t chars = 0;
    for (unsigned char c : out) if ((c & 0xC0) != 0x80) chars++;
    while (chars < width) { out.push_back(' '); chars++; }
    return out;
}

int dumpEventLog(uint32_t days) {
    std::cerr << "Olay gunlugu taraniyor (" << days << " gun)...\n";
    const sslog::Scan s = sslog::scan(days);

    rule();
    std::cout << "Syspect — olay gunlugu dokumu (son " << days << " gun)\n";
    rule();

    if (!s.ok) {
        std::cout << "Okunamadi: " << s.error << "\n";
        return 2;
    }

    std::printf("%s %6s %6s %6s  %s\n",
                padRight("OLAY", 44).c_str(), "24sa", "7gun", "30gun", "EN SON");
    bool anything = false;
    for (size_t i = 0; i < static_cast<size_t>(sslog::Kind::Count_); ++i) {
        const sslog::Series& se = s.series[i];
        if (se.empty()) continue;
        anything = true;
        std::printf("%s %6u %6u %6u  %s%s\n",
                    padRight(sslog::kindLabel(se.kind), 44).c_str(),
                    se.last24h, se.last7d, se.last30d,
                    fileTimeText(se.newestFileTime).c_str(),
                    se.spikingToday() ? "  << son 24 saatte ARTIS" : "");
    }

    if (s.liveKernelReports > 0) {
        anything = true;
        std::printf("%s %6s %6s %6u  %s\n",
                    padRight("Mavi ekransiz surucu cokmesi", 44).c_str(),
                    "-", "-", s.liveKernelReports,
                    s.newestLiveKernelReport.c_str());
    }

    if (!anything)
        std::cout << "Kayda deger bir olay bulunamadi.\n";

    // Suclanan ekran surucusu ve mavi ekran kodu ornekleri
    const sslog::Series& tdr = s.at(sslog::Kind::Tdr);
    for (const sslog::Event& e : tdr.samples)
        if (!e.detail.empty()) {
            std::cout << "\nEkran surucusu sifirlamasinda adi gecen surucu: "
                      << e.detail << "\n";
            break;
        }
    const sslog::Series& crash = s.at(sslog::Kind::AppCrash);
    if (!crash.samples.empty()) {
        std::cout << "\nSon çöken uygulamalar:\n";
        for (const sslog::Event& e : crash.samples)
            std::cout << "  " << fileTimeText(e.fileTimeUtc) << "  "
                      << (e.detail.empty() ? "(ad okunamadi)" : e.detail) << "\n";
    }

    const sslog::Series& bug = s.at(sslog::Kind::BugCheck);
    if (!bug.samples.empty()) {
        std::cout << "\nSon mavi ekran kayitlari:\n";
        for (const sslog::Event& e : bug.samples)
            std::cout << "  " << fileTimeText(e.fileTimeUtc) << "  "
                      << (e.detail.empty() ? "(kod okunamadi)" : e.detail) << "\n";
    }

    rule();
    std::cout <<
        "Bu dokum bir teshis DEGILDIR. Saglikli makinelerde de bu kayitlarin\n"
        "cogu birikir; belirleyici olan sayilar degil, takilma anlariyla\n"
        "zaman cakismasidir. Hukum icin: ss_cli capture\n";
    rule();
    return 0;
}

// ----------------------------------------------------------------------------
//  storage — disk envanteri dokumu
// ----------------------------------------------------------------------------
//  Asinma yuzdesi BILGI olarak basilir, hukum olarak degil. Bkz.
//  storage_probe.h, "yorum siniri".
int dumpStorage() {
    const ssstore::StorageScan s = ssstore::scan();

    rule();
    std::cout << "Syspect — depolama envanteri\n";
    rule();

    if (!s.ok) {
        std::cout << "Okunamadi: " << s.error << "\n";
        return 2;
    }
    if (!s.elevated)
        std::cout << "NOT: yonetici hakki yok — SMART alanlari bos kalabilir.\n\n";

    for (const auto& d : s.drives) {
        std::cout << "Disk " << d.index << " — "
                  << (d.model.empty() ? "(model okunamadi)" : d.model) << "\n";
        std::cout << "  Veri yolu / tip   : " << ssstore::busName(d.bus)
                  << " · " << ssstore::kindName(d.kind) << "\n";
        if (d.sizeMb) std::cout << "  Kapasite          : "
                                << (d.sizeMb / 1024) << " GB\n";

        std::cout << "  Ariza tahmini     : "
                  << (d.failurePredicted == ssstore::Drive::Tri::Yes ? "EVET — SMART esigi asildi"
                    : d.failurePredicted == ssstore::Drive::Tri::No  ? "hayir"
                                                                     : "okunamadi") << "\n";

        if (d.smartRead) {
            if (ssstore::known(d.wearPct))
                std::cout << "  Asinma            : %" << d.wearPct
                          << "  (BAGLAM — sebep degil)\n";
            if (ssstore::known(d.spareLeftPct))
                std::cout << "  Kalan yedek blok  : %" << d.spareLeftPct << "\n";
            if (ssstore::known(d.tempC))
                std::cout << "  Sicaklik          : " << d.tempC << " C\n";
            if (ssstore::known(d.powerOnHours))
                std::cout << "  Calisma suresi    : " << d.powerOnHours
                          << " saat (" << (d.powerOnHours / 24) << " gun)\n";
            if (d.writtenGb >= 0)
                std::cout << "  Yazilan veri      : " << d.writtenGb << " GB\n";
            if (d.criticalWarning)
                std::cout << "  KRITIK UYARI      : disk kendi bayragini kaldirdi\n";
        } else if (d.bus == ssstore::Bus::Nvme) {
            std::cout << "  SMART             : okunamadi\n";
        }

        for (const auto& v : d.volumes) {
            std::cout << "  Bolum " << v.letter
                      << (v.hasSystem ? " (Windows)" : "        ")
                      << "  : " << (v.totalMb / 1024) << " GB, bos "
                      << (v.freeMb / 1024) << " GB (%"
                      << static_cast<int>(v.freePct() + 0.5) << ")\n";
        }
        std::cout << "\n";
    }

    rule();
    std::cout <<
        "Asinma yuzdesi bir OMUR sayacidir, hiz gostergesi degildir. Takilma\n"
        "sebebi olarak sayilmaz. Dogrudan bulgu olan uc sey vardir: donen disk\n"
        "uzerinde Windows, neredeyse dolu bolum, ureticinin ariza tahmini.\n";
    rule();
    return 0;
}
// ----------------------------------------------------------------------------
//  dpc — DPC suclusu dokumu
// ----------------------------------------------------------------------------
//  Bu komut HUKUM VERMEZ. Amaci iki tane: cekirdek oturumunun gercek makinede
//  calistigini dogrulamak ve KONTROL GRUBU mantiginin gozle gorulmesi.
//
//  Takilma penceresi verilmeden calistirildiginda taban orani hesaplanamaz;
//  cikti bunu acikca soyler. "Listenin basindaki surucu suclu" diye okunmasin
//  diye uyari her seferinde basiliyor.
int dumpDpc(uint32_t seconds) {
    ssdpc::Options o;
    o.seconds = seconds;

    std::cerr << "Cekirdek DPC oturumu aciliyor (" << seconds << " sn)...\n";
    const ssdpc::Capture c = ssdpc::run(o);

    rule();
    std::cout << "Syspect — DPC dokumu\n";
    rule();

    if (!c.ok) {
        std::cout << "Toplanamadi: " << c.error << "\n";
        return 2;
    }

    std::printf("Sure                : %.1f sn\n", c.durationSec);
    std::printf("Toplam DPC olayi    : %llu\n",
                static_cast<unsigned long long>(c.totalDpcEvents));
    std::printf("Cozulemeyen adres   : %llu\n",
                static_cast<unsigned long long>(c.unresolvedAddresses));
    std::printf("Uzun DPC'lerin payi : %%%.2f (olcum suresine oran)\n\n",
                c.longDpcTimePct);

    // Sifir olay "sorun yok" DEGILDIR — cogu zaman oturumun hic calismadigi
    // anlamina gelir. Ikisini karistirmak yanlis negatif uretir.
    if (c.totalDpcEvents == 0) {
        std::cout <<
            "HIC OLAY GELMEDI. Bu 'surucu sorunu yok' demek DEGILDIR; buyuk\n"
            "ihtimalle oturum veri akitamadi. Baska bir izleme araci acik mi\n"
            "kontrol edin.\n";
        rule();
        return 2;
    }

    if (c.drivers.empty()) {
        std::cout << "Esigi asan (>1 ms) hicbir DPC gorulmedi.\n";
        rule();
        return 0;
    }

    std::printf("%s %8s %9s %9s\n",
                padRight("SURUCU", 34).c_str(), "UZUN", "EN UZUN", "TOPLAM");
    size_t shown = 0;
    for (const auto& d : c.drivers) {
        if (shown++ >= 12) break;
        std::printf("%s %8llu %7.2f ms %7.1f ms\n",
                    padRight(d.name.c_str(), 34).c_str(),
                    static_cast<unsigned long long>(d.longCount),
                    d.maxMs, d.totalMs);
    }

    rule();
    std::cout <<
        "Bu liste bir SUCLAMA DEGILDIR. Her sistemde saniyede binlerce DPC\n"
        "calisir; listenin basinda olmak tek basina hicbir sey ifade etmez.\n"
        "Suclu tespiti icin takilma anlarindaki oranin, takilma DISINDAKI\n"
        "taban oranin belirgin ustunde olmasi gerekir — o karsilastirma\n"
        "yalnizca kare olcumuyle birlikte yapilabilir.\n";
    rule();
    return 0;
}
#endif // _WIN32

// ============================================================================
//  main
// ============================================================================
int main(int argc, char** argv) {
#ifdef _WIN32
    // Konsolun kod sayfasini UTF-8'e cek. Metinlerimiz saf ASCII oldugu icin
    // sart degil, ama ilerde dosya yolu gibi kullanici verisi basildiginda
    // bozulmayi onler.
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty()) { printUsage(); return 1; }
    if (args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        printUsage();
        return 0;
    }
#ifdef _WIN32
    // Olay gunlugu dokumu — tek basina calisan bir tani komutu. Kare olcumu
    // gerektirmez ve yonetici hakki istemez.
    if (args[0] == "eventlog") {
        uint32_t days = 30;
        for (size_t i = 1; i < args.size(); ++i) {
            std::string k, v;
            uint32_t n = 0;
            if (splitFlag(args[i], k, v) && k == "--days" && toUint(v, n) && n > 0)
                days = n;
        }
        return dumpEventLog(days);
    }

    // Depolama envanteri. eventlog ile ayni gerekce: hukum vermez, ne
    // okundugunu gosterir ve okuyucunun gercek makinede dogru calistigini
    // dogrulamamizi saglar.
    if (args[0] == "storage") return dumpStorage();

    // DPC toplama. Ayri bir komut cunku ana olcume baglanmadan once tek
    // basina dogrulanmasi gerekiyor: cekirdek oturumu sistemde tek olabilir
    // ve olay hacmi Present oturumunu bogabilir.
    if (args[0] == "dpc") {
        uint32_t secs = 20;
        for (size_t i = 1; i < args.size(); ++i) {
            std::string k, v;
            uint32_t n = 0;
            if (splitFlag(args[i], k, v) && k == "--seconds" && toUint(v, n) && n > 0)
                secs = n;
        }
        return dumpDpc(secs);
    }
#endif

    const bool isCapture = (args[0] == "capture");
    if (args[0] != "analyze" && !isCapture) {
        std::cerr << "Bilinmeyen komut: " << args[0]
                  << "\nKullanilabilir komutlar: capture, analyze, eventlog, "
                     "storage, dpc\n";
        return 1;
    }

    std::string csvPath;
    size_t      firstOption = 1;
    if (!isCapture) {
        if (args.size() < 2 || args[1].rfind("--", 0) == 0) {
            std::cerr << "analyze bir CSV dosyasi bekliyor.\n"
                         "Ornek: ss_cli analyze trace.csv\n";
            return 1;
        }
        csvPath     = args[1];
        firstOption = 2;
    }

    ss::SystemInfo sys;
    bool        asJson   = false;
    bool        quiet    = false;
    bool        asHtml   = false;
    bool        noOpen   = false;
    // capture olay gunlugunu KENDISI okur. analyze icin okumaz: CSV baska bir
    // makineden gelmis olabilir ve o kaydin uzerine bu makinenin gecmisini
    // yapistirmak duz bir yanlis teshis uretir.
    bool        noEventLog = false;
    std::string htmlPath = "stutterscope-rapor.html";
    uint32_t    capSeconds = 60;
    uint32_t    capPid     = 0;

    for (size_t i = firstOption; i < args.size(); ++i) {
        std::string key, value;
        const bool hasValue = splitFlag(args[i], key, value);

        // --- deger istemeyen bayraklar ---
        if      (key == "--desktop-stutter")  { sys.desktopStutterObserved = true; continue; }
        else if (key == "--monitor-changed")  { sys.monitorRecentlyChanged = true; continue; }
        else if (key == "--driver-updated")   { sys.driverRecentlyUpdated  = true; continue; }
        else if (key == "--am5")              { sys.isAM5                  = true; continue; }
        else if (key == "--intel-ddr5")       { sys.isIntelDdr5            = true; continue; }
        else if (key == "--expo")             { sys.expoActive             = true; continue; }
        else if (key == "--curve-optimizer")  { sys.curveOptimizerActive   = true; continue; }
        else if (key == "--gpu-oc")           { sys.gpuOverclocked         = true; continue; }
        else if (key == "--json")             { asJson                     = true; continue; }
        else if (key == "--quiet")            { quiet                      = true; continue; }
        else if (key == "--no-open")          { noOpen                     = true; continue; }
        else if (key == "--no-event-log")     { noEventLog                 = true; continue; }
        else if (key == "--html") {
            asHtml = true;
            if (hasValue && !value.empty()) htmlPath = value;
            continue;
        }

        // --- deger isteyen bayraklar ---
        if (!hasValue) {
            std::cerr << "Bu secenek bir deger bekliyor: " << key
                      << "\nOrnek: " << key << "=<deger>\n";
            return 1;
        }

        uint32_t n = 0;
        if      (key == "--seconds"      && toUint(value, n)) capSeconds = n;
        else if (key == "--pid"          && toUint(value, n)) capPid     = n;
        else if (key == "--cpu")   sys.cpuName   = value;
        else if (key == "--gpu")   sys.gpuName   = value;
        else if (key == "--note")  sys.recentChangeNote = value;
        else if (key == "--ram"          && toUint(value, n)) sys.ramConfiguredMTs   = n;
        else if (key == "--jedec"        && toUint(value, n)) sys.ramJedecMTs        = n;
        else if (key == "--dimms"        && toUint(value, n)) sys.ramModuleCount     = n;
        else if (key == "--psu-watts"    && toUint(value, n)) sys.psuWatts           = n;
        else if (key == "--peak-watts"   && toUint(value, n)) sys.estimatedPeakWatts = n;
        else if (key == "--psu-tier"     && toUint(value, n)) {
            if (n > 4) { std::cerr << "--psu-tier 0 ile 4 arasinda olmali.\n"; return 1; }
            sys.psuQualityTier = static_cast<int>(n);
        }
        else if (key == "--whea-corrected" && toUint(value, n)) sys.wheaCorrected     = n;
        else if (key == "--whea-fatal"     && toUint(value, n)) sys.wheaFatal         = n;
        else if (key == "--kernel-power41" && toUint(value, n)) sys.kernelPower41     = n;
        else if (key == "--bugchecks"      && toUint(value, n)) sys.bugcheckCount     = n;
        else if (key == "--live-kernel"    && toUint(value, n)) sys.liveKernelReports = n;
        else {
            std::cerr << "Bilinmeyen ya da gecersiz secenek: " << args[i] << "\n"
                      << "Secenek listesi icin: ss_cli --help\n";
            return 1;
        }
    }

    // JEDEC verilmediyse EXPO cikarimini yapilandirilmis hizdan turet.
    // CLAUDE.md: SPD profili SMBus'tan okunur (ring 0); burada cikarim yeterli.
    if (!sys.expoActive && sys.ramConfiguredMTs > 0 && sys.ramJedecMTs > 0 &&
        sys.ramConfiguredMTs > sys.ramJedecMTs) {
        sys.expoActive = true;
    }

    ss::AnalysisResult  result;
    ss::FrameSourceInfo srcInfo;

    if (isCapture) {
#ifdef _WIN32
        ss::EtwCaptureOptions o;
        o.seconds   = capSeconds;
        o.processId = capPid;

        // SMBIOS bellek yoklamasi. Elle verilen bayraklar EZILMEZ — kullanici
        // --ram / --dimms / --am5 yazdiysa onun dedigi gecerlidir; yoklama
        // yalnizca bos alanlari doldurur.
        {
            const ssprobe::MemorySpec mem = ssprobe::readMemorySpec();
            if (!mem.modules.empty()) {
                if (sys.ramConfiguredMTs == 0) sys.ramConfiguredMTs = mem.configuredMTs;
                if (sys.ramModuleCount   == 0)
                    sys.ramModuleCount = static_cast<uint32_t>(mem.modules.size());
                if (!sys.isAM5 && !sys.isIntelDdr5) {
                    sys.isAM5       = mem.likelyAM5;
                    sys.isIntelDdr5 = mem.likelyIntelDdr5;
                }
                if (!sys.expoActive)
                    sys.expoActive = (mem.profile == ssprobe::MemorySpec::Profile::On);
                sys.ramModulesMismatched = mem.mixedModules;
            }
        }

        // Ilerleme bilgisi stderr'e gider; stdout rapor icin temiz kalsin.
        std::cerr << "ETW yakalamasi basladi (" << capSeconds << " sn). "
                  << "Oyunu simdi calistirin...\n";

        const uint64_t winStart = sslog::nowFileTime();
        const ss::EtwCaptureResult cap = ss::runEtwCapture(o);
        if (!cap.ok()) {
            std::cerr << "Yakalama basarisiz: " << cap.error << "\n";
            return 2;
        }
        const uint64_t winEnd = sslog::nowFileTime();
        std::cerr << "Yakalama bitti: " << cap.info.note << "\n";

        // Olay gunlugu olcum BITTIKTEN sonra okunur; olcum sirasinda dusen
        // TDR / depolama kayitlari en degerli olanlar. Elle verilen bayraklar
        // (--whea-corrected gibi) EZILMEZ: kullanici bilerek yazdiysa onun
        // dedigi gecerlidir.
        if (!noEventLog) {
            const ss::SystemInfo manual = sys;
            const sslog::Scan log = sslog::scan(30);
            sslog::applyTo(sys, log, winStart, winEnd);

            if (manual.wheaCorrected)     sys.wheaCorrected     = manual.wheaCorrected;
            if (manual.wheaFatal)         sys.wheaFatal         = manual.wheaFatal;
            if (manual.kernelPower41)     sys.kernelPower41     = manual.kernelPower41;
            if (manual.bugcheckCount)     sys.bugcheckCount     = manual.bugcheckCount;
            if (manual.liveKernelReports) sys.liveKernelReports = manual.liveKernelReports;

            if (log.ok)
                std::cerr << "Olay gunlugu okundu (" << log.totalRead
                          << " kayit tarandi).\n";
            else
                std::cerr << "Olay gunlugu okunamadi: " << log.error << "\n";
        }

        ss::VectorFrameSource vs(cap.frames, cap.info);
        result  = ss::analyzeSource(vs, sys);
        srcInfo = cap.info;
#else
        std::cerr << "capture yalnizca Windows'ta calisir.\n";
        return 1;
#endif
    } else {
        ss::CsvFrameSource source(csvPath);
        if (!source.error().empty()) {
            std::cerr << "Kaynak okunamadi: " << source.error() << "\n";
            return 2;
        }
        result  = ss::analyzeSource(source, sys);
        srcInfo = source.info();
    }

    if (asHtml) {
        return writeHtmlReport(htmlPath,
                               // Kullanicinin arayuzde sectigi dil burada da
                               // gecerli: iki arac ayni tercihi okuyor.
                               ss::renderHtmlReport(result, srcInfo, sys,
                                                    ss18::translator()),
                               !noOpen);
    }
    if (asJson) printJsonReport(result, srcInfo);
    else        printTextReport(result, srcInfo, quiet);

    return 0;
}
