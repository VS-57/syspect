// ============================================================================
//  StutterScope — Cekirdek motor testleri
//  ----------------------------------------------------------------------
//  Vaka kulliyatindaki gercek forum sikayetleri sentetik kare izlerine
//  cevrilir ve motorun DOGRU hukmu verip vermedigi olculur.
//  Her senaryo bir regresyon testidir: yeni kural eklendiginde bunlarin
//  hepsi gecmeye devam etmelidir.
// ============================================================================
#include "core.h"
#include "frame_source.h"

#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace ss;

// ============================================================================
//  Test altyapisi
// ============================================================================
static int g_fail = 0;
static int g_pass = 0;

static void check(const std::string& what, bool cond) {
    std::printf("    [%s] %s\n", cond ? " OK " : "FAIL", what.c_str());
    cond ? ++g_pass : ++g_fail;
}

static void header(const std::string& title) {
    std::printf("\n===========================================================\n");
    std::printf(" %s\n", title.c_str());
    std::printf("===========================================================\n");
}

// ============================================================================
//  Sentetik kare izi uretici
// ============================================================================
class TraceBuilder {
public:
    explicit TraceBuilder(double targetFps, unsigned seed = 42)
        : baseMs_(1000.0 / targetFps), rng_(seed) {}

    // Normal kareler ekle (kucuk dogal dalgalanma ile)
    TraceBuilder& normal(int count, double jitterMs = 0.4) {
        std::normal_distribution<double> d(baseMs_, jitterMs);
        for (int i = 0; i < count; ++i) emit(std::max(0.5, d(rng_)), {});
        return *this;
    }

    // Tek bir takilma ekle
    TraceBuilder& spike(double frameMs, const SignalSnapshot& sig = {}) {
        emit(frameMs, sig);
        return *this;
    }

    // Duzenli araliklarla tekrar eden mikro-takilma (VRR parmak izi)
    TraceBuilder& periodicMicro(int repeats, int gapFrames, double spikeMs) {
        for (int i = 0; i < repeats; ++i) {
            normal(gapFrames);
            emit(spikeMs, {});
        }
        return *this;
    }

    // Izi verilen sureye kadar NORMAL karelerle uzatir.
    //
    // NEDEN GEREKLI: Motor artik kisa kayitlarda hukum vermiyor
    // (kMinSessionSec / kMinSessionFrames). Sentetik izler desen olarak
    // dogruydu ama 2-7 saniyelikti; gercek bir olcum hic oyle olmaz.
    // Eklenen kareler normal oldugu icin takilma sayisi degismez, yalnizca
    // sure ve kare sayisi kosulu saglanir.
    TraceBuilder& padTo(double seconds) {
        const uint64_t targetUs = static_cast<uint64_t>(seconds * 1'000'000.0);
        std::normal_distribution<double> d(baseMs_, 0.4);
        while (nowUs_ < targetUs) emit(std::max(0.5, d(rng_)), {});
        return *this;
    }

    const std::vector<FrameSample>& frames() const { return frames_; }
    const std::vector<SignalSnapshot>& signals() const { return signals_; }

private:
    void emit(double ms, const SignalSnapshot& sig) {
        nowUs_ += static_cast<uint64_t>(ms * 1000.0);
        frames_.push_back({nowUs_, ms});
        signals_.push_back(sig);
    }

    double   baseMs_;
    uint64_t nowUs_ = 0;
    std::vector<FrameSample>    frames_;
    std::vector<SignalSnapshot> signals_;
    std::mt19937 rng_;
};

// Izi dedektorden gecirip olaylari toplar
static std::vector<StutterEvent> runDetector(const TraceBuilder& tb) {
    StutterDetector det(120);
    std::vector<StutterEvent> events;
    const auto& f = tb.frames();
    const auto& s = tb.signals();
    for (size_t i = 0; i < f.size(); ++i)
        if (auto e = det.push(f[i], s[i])) events.push_back(*e);
    return events;
}

// Teshis ciktisini yazdirir
static void printDiagnosis(const Diagnosis& d) {
    std::printf("\n    --> %s\n", d.headline.c_str());
    std::printf("        Guven: %%%d%s\n", d.confidence,
                d.inconclusive ? "  (YETERSIZ)" : "");
    int i = 1;
    for (const auto& h : d.ranked) {
        std::printf("        %d) %%%-3d %s\n", i++, h.percent, h.label.c_str());
        if (!h.evidence.empty())
            std::printf("             kanit: %s\n", h.evidence.c_str());
    }
    std::printf("\n");
}

static bool topCauseIs(const Diagnosis& d, Cause c) {
    return !d.ranked.empty() && d.ranked.front().cause == c;
}

static int percentOf(const Diagnosis& d, Cause c) {
    for (const auto& h : d.ranked) if (h.cause == c) return h.percent;
    return 0;
}

static bool causePresent(const Diagnosis& d, Cause c) {
    for (const auto& h : d.ranked) if (h.cause == c) return true;
    return false;
}

// ============================================================================
//  BOLUM 1 — Dedektor birim testleri
// ============================================================================
static void testDetectorBasics() {
    header("BOLUM 1 — Takilma dedektoru");

    // Esik formulu
    check("60 FPS tabaninda esik 33.3 ms (2x kurali)",
          std::abs(StutterDetector::thresholdFor(16.67) - 33.34) < 0.1);
    check("240 FPS tabaninda esik 12.2 ms (+8ms kurali devrede)",
          std::abs(StutterDetector::thresholdFor(4.17) - 12.17) < 0.1);

    // Temiz oturumda yanlis alarm olmamali
    {
        TraceBuilder tb(144.0);
        tb.normal(1200);
        auto ev = runDetector(tb);
        check("Temiz 144 Hz oturumunda yanlis alarm yok", ev.empty());
    }

    // 60 Hz'de 25 ms normal degil ama 30 FPS'te normaldir -> gorelilik testi
    {
        TraceBuilder a(60.0); a.normal(300).spike(40.0).normal(100);
        auto evA = runDetector(a);
        check("60 FPS'te 40 ms takilma yakalandi", evA.size() == 1);

        TraceBuilder b(25.0); b.normal(300).spike(40.0).normal(100);
        auto evB = runDetector(b);
        check("25 FPS'te ayni 40 ms takilma DEGIL (gorelilik)", evB.empty());
    }

    // Siniflandirma
    {
        TraceBuilder tb(144.0);
        tb.normal(200).spike(120.0).normal(200).spike(800.0).normal(200);
        auto ev = runDetector(tb);
        check("Iki olay tespit edildi", ev.size() == 2);
        if (ev.size() == 2) {
            check("120 ms -> Hitch",  ev[0].kind == StutterKind::Hitch);
            check("800 ms -> Donma",  ev[1].kind == StutterKind::Freeze);
        }
    }

    // Isinma: ilk karelerde hukum verilmemeli
    {
        TraceBuilder tb(144.0);
        tb.spike(500.0).normal(300);
        auto ev = runDetector(tb);
        check("Isinma tamamlanmadan alarm uretilmiyor", ev.empty());
    }
}

// ============================================================================
//  BOLUM 2 — Vaka senaryolari
// ============================================================================

// --- VAKA 4: 7800X3D + 5070, CPU-yogun anlarda takilma, sicaklik normal ----
//     Beklenen: surucu DPC en ust sirada
static void testCase_DriverDpc() {
    header("VAKA 4 — CPU-yogun anlarda takilma (surucu DPC)");

    TraceBuilder tb(240.0);
    tb.normal(300);
    SignalSnapshot dpc;
    dpc.dpcMaxMs = 4.2;
    dpc.dpcDriver = "rtkvhd64.sys";
    dpc.cpuPerfPercent = 99.0;
    for (int i = 0; i < 12; ++i) { tb.spike(28.0, dpc); tb.normal(120); }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.isAM5 = true; sys.ramConfiguredMTs = 6000; sys.ramModuleCount = 2;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("Olaylar tespit edildi", ev.size() >= 10);
    check("En olasi sebep: surucu DPC", topCauseIs(d, Cause::DRIVER_DPC));
    check("Guven >= %60", d.confidence >= 60);
    check("Sucla anilan surucu kanitta gecıyor",
          !d.ranked.empty() && d.ranked.front().evidence.find("rtkvhd64.sys") != std::string::npos);
}

// --- VAKA 7: 9800X3D + 5070Ti, 240 Hz OLED'e gecince mikro-takilma ---------
//     Beklenen: VRR / frame pacing
static void testCase_VrrPacing() {
    header("VAKA 7 — Monitor degisiminden sonra mikro-takilma");

    TraceBuilder tb(240.0);
    tb.normal(200);
    tb.periodicMicro(20, 60, 15.0);   // her ~60 karede bir duzenli sicrama

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.isAM5 = true; sys.ramConfiguredMTs = 6000; sys.ramModuleCount = 2;
    sys.monitorRecentlyChanged = true;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("Mikro-takilmalar duzenli olarak tespit edildi", st.periodicMicroStutter);
    check("En olasi sebep: VRR / kare zamanlama", topCauseIs(d, Cause::VRR_PACING));
    check("Monitor degisikligi kanitta gecıyor",
          !d.ranked.empty() && d.ranked.front().evidence.find("onit") != std::string::npos);
}

// --- VAKA 5: RAM hic 4800 ustu calismadi, dump uretmeden donuyor -----------
//     Beklenen: bellek / EXPO kararsizligi
static void testCase_MemoryInstability() {
    header("VAKA 5 — Dump uretmeyen sert donmalar (bellek kararsizligi)");

    TraceBuilder tb(144.0);
    tb.normal(400);
    for (int i = 0; i < 4; ++i) { tb.spike(900.0, {}); tb.normal(200); }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.isAM5            = true;
    sys.ramConfiguredMTs = 6000;
    sys.ramModuleCount   = 2;
    sys.expoActive       = true;
    sys.wheaCorrected    = 480;    // yuksek
    sys.kernelPower41    = 6;      // dump uretmeden kapanma
    sys.bugcheckCount    = 0;      // hic mavi ekran kaydi yok
    sys.desktopStutterObserved = true;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("En olasi sebep: bellek / EXPO", topCauseIs(d, Cause::MEMORY_EXPO));
    check("WHEA kaniti gosteriliyor",
          !d.ranked.empty() && d.ranked.front().evidence.find("WHEA") != std::string::npos);
    check("Donma olaylari siniflandirildi", st.freezeCount >= 3);
}

// --- VAKA 1: 9950X3D, CO -25, Opera donuyor, fare takiliyor ----------------
//     Beklenen: undervolt / Curve Optimizer
//     AYIRT EDICI: takilma oyun DISINDA da var
static void testCase_CurveOptimizer() {
    header("VAKA 1 — Oyun disinda da takilma (Curve Optimizer)");

    TraceBuilder tb(240.0);
    tb.normal(300);
    SignalSnapshot s;
    s.inGame = false;                 // masaustunde de oluyor
    s.cpuPerfPercent = 100.0;         // termal degil
    for (int i = 0; i < 8; ++i) { tb.spike(180.0, s); tb.normal(150); }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.isAM5                 = true;
    sys.ramConfiguredMTs      = 6400;
    sys.ramModuleCount        = 2;
    sys.curveOptimizerActive  = true;
    sys.desktopStutterObserved = true;
    sys.wheaCorrected         = 60;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("En olasi sebep: undervolt / Curve Optimizer",
          topCauseIs(d, Cause::OVERCLOCK_CO));
    check("Oyun disi takilma kanit olarak kullanildi",
          !d.ranked.empty() &&
          d.ranked.front().evidence.find("oyun disinda") != std::string::npos);
    check("Bellek de ikincil suphe olarak listelendi",
          percentOf(d, Cause::MEMORY_EXPO) > 0);
}

// --- VAKA 8: 5600 + RX 9060 XT, LoL sorunsuz, Witcher'da dusuk FPS ---------
//     Beklenen: takilma YOK, donanim yetersiz
static void testCase_LowFpsNotStutter() {
    header("VAKA 8 — Takilma yok, sadece dusuk FPS");

    TraceBuilder tb(32.0);     // ~32 FPS, duzenli
    tb.normal(1500, 0.8);

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("Takilma olayi uretilmedi", ev.empty());
    check("Dusuk FPS bayragi kalkti", st.lowFpsNotStutter);
    check("Hukum: donanim yetersiz", topCauseIs(d, Cause::UNBALANCED_HW));
    check("Kullaniciya 'takilma yok' deniyor",
          d.headline.find("takılma yok") != std::string::npos);
}

// --- Shader derlemesi: ilk 10 dakikada yogun, sonra kayboluyor -------------
//     Beklenen: normal davranis, kullaniciyi telaslandirma
static void testCase_ShaderCompile() {
    header("EK SENARYO — Golgelendirici derlemesi (normal)");

    TraceBuilder tb(144.0);
    tb.normal(300);

    // Ilk 10 dakikada 10 takilma
    for (int i = 0; i < 10; ++i) {
        SignalSnapshot s;
        s.minutesSinceGameStart = static_cast<uint32_t>(1 + i % 8);
        s.cpuPerfPercent = 100.0;
        tb.spike(90.0, s);
        tb.normal(100);
    }
    // Oturum devam etti: 20. ve 28. dakikada birer takilma daha
    for (uint32_t m : {20u, 28u}) {
        SignalSnapshot s;
        s.minutesSinceGameStart = m;
        s.cpuPerfPercent = 100.0;
        tb.spike(90.0, s);
        tb.normal(100);
    }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("En olasi sebep: shader derlemesi", topCauseIs(d, Cause::SHADER_COMPILE));
    check("Aksiyon 'bir sey yapmaniza gerek yok' diyor",
          !d.ranked.empty() &&
          d.ranked.front().action.find("gerek yok") != std::string::npos);

    // KRITIK: oyun zamani bilinmiyorsa bu kural HIC tetiklenmemeli
    {
        TraceBuilder t2(144.0);
        t2.normal(300);
        for (int i = 0; i < 10; ++i) { t2.spike(90.0, {}); t2.normal(100); }
        auto e2 = runDetector(t2);
        auto s2 = analyzeSession(t2.frames(), e2);
        auto d2 = diagnose(s2, e2, SystemInfo{});
        check("Oyun zamani bilinmiyorsa shader kurali tetiklenmiyor",
              percentOf(d2, Cause::SHADER_COMPILE) == 0);
    }

    // Oturum 10 dakikadan kisaysa ayrim yapilamaz, kural susmali
    {
        TraceBuilder t3(144.0);
        t3.normal(300);
        for (int i = 0; i < 10; ++i) {
            SignalSnapshot s;
            s.minutesSinceGameStart = static_cast<uint32_t>(i % 5);
            t3.spike(90.0, s);
            t3.normal(100);
        }
        auto e3 = runDetector(t3);
        auto s3 = analyzeSession(t3.frames(), e3);
        auto d3 = diagnose(s3, e3, SystemInfo{});
        check("Kisa oturumda shader kurali tetiklenmiyor",
              percentOf(d3, Cause::SHADER_COMPILE) == 0);
    }
}

// --- VAKA 6: zayif PSU + yuksek cekisli GPU, siyah ekran -------------------
//     Beklenen: guc kaynagi ust siralarda
static void testCase_Psu() {
    header("VAKA 6 — Guc kaynagi supheli (Power Brake + ani kapanma)");

    TraceBuilder tb(120.0);
    tb.normal(300);
    SignalSnapshot s;
    s.gpuPowerBrake  = true;
    s.gpuUtilPercent = 99.0;
    for (int i = 0; i < 10; ++i) { tb.spike(250.0, s); tb.normal(120); }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.kernelPower41      = 4;
    sys.bugcheckCount      = 0;
    sys.psuQualityTier     = 1;      // zayif
    sys.psuWatts           = 750;
    sys.estimatedPeakWatts = 700;
    sys.liveKernelReports  = 2;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("En olasi sebep: guc kaynagi", topCauseIs(d, Cause::PSU));
    check("Power Brake kanit olarak kullanildi",
          !d.ranked.empty() &&
          d.ranked.front().evidence.find("Power Brake") != std::string::npos);
    check("GPU cokme kayitlari da listelendi", percentOf(d, Cause::GPU_DRIVER) > 0);
}

// --- Veri yetersiz: motor "bilmiyorum" diyebiliyor mu? --------------------
static void testCase_Inconclusive() {
    header("EK SENARYO — Veri yetersiz (motor 'bilmiyorum' demeli)");

    TraceBuilder tb(144.0);
    tb.normal(400).spike(60.0).normal(200);

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;    // hicbir baglam yok
    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("Motor kesin hukum vermiyor", d.inconclusive);
    check("Kullaniciya daha uzun kayit oneriliyor",
          d.headline.find("bulunamadı") != std::string::npos);
}

// --- Arka plan programi ---------------------------------------------------
static void testCase_BackgroundApp() {
    header("EK SENARYO — Arka plandaki program cakisiyor");

    TraceBuilder tb(144.0);
    tb.normal(300);
    SignalSnapshot s;
    s.topBackgroundProcess   = "MsMpEng.exe";
    s.backgroundCpuPercent   = 62.0;
    for (int i = 0; i < 9; ++i) { tb.spike(75.0, s); tb.normal(140); }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("En olasi sebep: arka plan programi", topCauseIs(d, Cause::BACKGROUND_APP));
    check("Program adi kanitta gecıyor",
          !d.ranked.empty() &&
          d.ranked.front().evidence.find("MsMpEng.exe") != std::string::npos);
}

// ============================================================================
//  BOLUM 3 — Tutarlilik testleri
// ============================================================================
// --- VAKA 9: RTX 5070 + 16 GB, Forza — GPU %70, CPU %99 ---------------------
//     Gercek forum sikayeti. Kullanici ekran kartini sucluyor; oysa ekran
//     karti bosta bekliyor. Beklenen: islemci darbogazi, ve "donanim
//     yetersiz" hukmunun VERILMEMESI.
static void testCase_CpuBottleneck() {
    header("VAKA 9 — GPU bosta, CPU dolu (gercek forum vakasi)");

    TraceBuilder tb(42.0);      // ~42 FPS, duzenli — takilma yok
    tb.normal(600);

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.gpuName             = "GeForce RTX 5070";
    sys.medianGpuUtilPct    = 72.0;
    sys.medianCpuUsagePct   = 95.0;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("Takilma yok olarak gorulmus", st.lowFpsNotStutter);
    check("En olasi sebep: islemci darbogazi",
          topCauseIs(d, Cause::CPU_BOTTLENECK));

    bool blamedHardware = false;
    for (const auto& h : d.ranked)
        if (h.cause == Cause::UNBALANCED_HW) blamedHardware = true;
    check("'Donanim yetersiz' hukmu VERILMIYOR", !blamedHardware);
}

// --- VAKA 9b: 16 GB RAM, oyun 17 GB taahhut ediyor --------------------------
//     Gercek forum vakasi. GPU %72'de bekliyor, CPU %95, RAM bitmis.
//     Beklenen: bellek/sayfalama en olasi sebep, 'donanim yetersiz' DEGIL.
static void testCase_MemoryPressure() {
    header("VAKA 9b — RAM bitti, sayfalama basladi (gercek forum vakasi)");

    TraceBuilder tb(45.0);
    tb.normal(400);
    SignalSnapshot none;
    for (int i = 0; i < 6; ++i) { tb.spike(85.0, none); tb.normal(80); }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.gpuName            = "GeForce RTX 5070";
    sys.ramTotalMb         = 16384;
    sys.medianGpuUtilPct   = 72.0;
    sys.medianCpuUsagePct  = 95.0;
    sys.commitExceededRam  = true;
    sys.maxCommitUsedPct   = 96.0;
    sys.minAvailPhysMb     = 220.0;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("En olasi sebep: bellek / sayfalama",
          topCauseIs(d, Cause::PAGEFILE_RAM));
    check("Islemci darbogazi da listede",
          causePresent(d, Cause::CPU_BOTTLENECK));

    bool blamedHardware = false;
    for (const auto& h : d.ranked)
        if (h.cause == Cause::UNBALANCED_HW) blamedHardware = true;
    check("'Donanim yetersiz' hukmu VERILMIYOR", !blamedHardware);
}

// --- VAKA 10: ne GPU ne CPU doluyor -----------------------------------------
//     FPS siniri, dikey esitleme ya da guc limiti. Beklenen: donanim tam
//     kullanilmiyor.
static void testCase_Underused() {
    header("VAKA 10 — Ne GPU ne CPU doluyor");

    TraceBuilder tb(40.0);
    tb.normal(600);

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.medianGpuUtilPct  = 45.0;
    sys.medianCpuUsagePct = 40.0;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("En olasi sebep: donanim tam kullanilmiyor",
          topCauseIs(d, Cause::GPU_UNDERUSED));
}

// --- VAKA 11: disk surekli doygun -------------------------------------------
static void testCase_DiskSaturated() {
    header("VAKA 11 — Disk surekli %100 aktif");

    TraceBuilder tb(120.0);
    tb.normal(300);
    SignalSnapshot none;
    for (int i = 0; i < 8; ++i) { tb.spike(90.0, none); tb.normal(120); }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.medianDiskActivePct = 97.0;
    sys.p95DiskLatencyMs    = 45.0;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("Depolama hipotezi uretildi", causePresent(d, Cause::STORAGE));
}

// --- VAKA 12: GPU power brake bildirdi --------------------------------------
static void testCase_PowerBrake() {
    header("VAKA 12 — Ekran karti donanimsal frenleme bildirdi");

    TraceBuilder tb(144.0);
    tb.normal(300);
    SignalSnapshot none;
    for (int i = 0; i < 5; ++i) { tb.spike(70.0, none); tb.normal(150); }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.gpuPowerBrakeSeen = true;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("Guc kaynagi hipotezi uretildi", causePresent(d, Cause::PSU));
}

// ============================================================================
//  Olay gunlugu kaynakli vakalar
// ----------------------------------------------------------------------------
//  Bu dort vaka olay gunlugu okuyucusuyla birlikte eklendi. Ucu yeni kurallari
//  dogruluyor, dorduncusu bir YANLIS POZITIFI kaliciolarak kilitliyor.
// ============================================================================

// Ayni kare izini uc senaryoda da kullanmak icin.
static void buildStutteryTrace(TraceBuilder& tb) {
    tb.normal(300);
    SignalSnapshot none;
    for (int i = 0; i < 6; ++i) { tb.spike(85.0, none); tb.normal(140); }
    tb.padTo(40.0);
}

// --- VAKA 13: olcum sirasinda ekran surucusu sifirlandi ---------------------
static void testCase_TdrDuringCapture() {
    header("VAKA 13 — Olcum sirasinda TDR (ekran surucusu sifirlandi)");

    TraceBuilder tb(144.0);
    buildStutteryTrace(tb);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.eventLogRead     = true;
    sys.tdrCount         = 3;
    sys.tdrDuringCapture = 2;
    sys.tdrDriver        = "nvlddmkm";

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("En olasi sebep: ekran karti surucusu",
          topCauseIs(d, Cause::GPU_DRIVER));

    bool namesDriver = false;
    for (const auto& h : d.ranked)
        if (h.cause == Cause::GPU_DRIVER &&
            h.evidence.find("nvlddmkm") != std::string::npos) namesDriver = true;
    check("Kanit metninde suclanan surucu adi geciyor", namesDriver);
}

// --- VAKA 14: TDR var ama gecmiste kalmis -----------------------------------
//  Zaman cakismasi olmadan ayni kanit cok daha zayif olmali. Bu ayrim
//  olmasaydi iki ay once bir kez sifirlanmis saglikli bir makine bugunku
//  takilmasi icin ekran surucusunu suclardi.
static void testCase_TdrHistoricalOnly() {
    header("VAKA 14 — TDR yalnizca gecmiste, olcumle cakismiyor");

    TraceBuilder tb(144.0);
    buildStutteryTrace(tb);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    // Yarisacak ikinci bir sebep sart: tek hipotez kalirsa normalizasyon
    // ikisini de tavana (kMaxSingleCausePercent) oturtur ve aradaki fark
    // olculemez hale gelir. Disk doygunlugu bilerek ekleniyor.
    SystemInfo hist;
    hist.eventLogRead        = true;
    hist.tdrCount            = 3;
    hist.tdrDuringCapture    = 0;
    hist.medianDiskActivePct = 97.0;

    SystemInfo live = hist;
    live.tdrDuringCapture = 2;

    auto dHist = diagnose(st, ev, hist);
    auto dLive = diagnose(st, ev, live);
    printDiagnosis(dHist);

    check("Gecmisteki TDR yine de hipotez uretiyor",
          causePresent(dHist, Cause::GPU_DRIVER));
    check("Cakisan TDR gecmistekinden belirgin daha agir",
          percentOf(dLive, Cause::GPU_DRIVER) >
          percentOf(dHist, Cause::GPU_DRIVER));

    // Asil ayrim burada: cakismayan TDR digerlerini gecemez, cakisan gecer.
    check("Cakismayan TDR en olasi sebep OLMUYOR",
          !topCauseIs(dHist, Cause::GPU_DRIVER));
    check("Cakisan TDR en olasi sebep oluyor",
          topCauseIs(dLive, Cause::GPU_DRIVER));

    bool saysNoOverlap = false;
    for (const auto& h : dHist.ranked)
        if (h.cause == Cause::GPU_DRIVER &&
            h.evidence.find("tekrarlamadi") != std::string::npos)
            saysNoOverlap = true;
    check("Kanit metni cakismadigini acikca soyluyor", saysNoOverlap);
}

// --- VAKA 15: depolama aygitinin kendi bildirdigi hata ----------------------
static void testCase_StorageEvents() {
    header("VAKA 15 — Depolama aygiti sifirlama/hata bildirdi");

    TraceBuilder tb(120.0);
    buildStutteryTrace(tb);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.eventLogRead         = true;
    sys.storageResetCount    = 4;
    sys.storageRetryCount    = 22;
    sys.storageDuringCapture = 1;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("Depolama hipotezi uretildi", causePresent(d, Cause::STORAGE));
    check("En olasi sebep: depolama", topCauseIs(d, Cause::STORAGE));
}

// --- VAKA 16: TEK BASINA duzeltilmis WHEA -----------------------------------
//  Yanlis pozitif kilidi. ID 17 bircok AM5 sisteminde semptomsuz binlerce kez
//  uretilir. Baska hicbir kanit yokken bu sayim bellek hipotezi URETMEMELI;
//  aksi halde motor saglikli her AM5 makinesine "bellegin kararsiz" der.
//  Bkz. CLAUDE.md, WHEA notu ve tasarim kurali 3.
static void testCase_WheaAloneIsNotEvidence() {
    header("VAKA 16 — Tek basina duzeltilmis WHEA sinyal sayilmamali");

    TraceBuilder tb(144.0);
    buildStutteryTrace(tb);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo alone;
    alone.eventLogRead  = true;
    alone.wheaCorrected = 480;          // yuksek ama BASKA kanit yok

    auto d = diagnose(st, ev, alone);
    printDiagnosis(d);

    check("Tek basina WHEA bellek hipotezi URETMIYOR",
          !causePresent(d, Cause::MEMORY_EXPO));

    // Taban cizgisinin ustune ciktiginda ise sayilmali.
    SystemInfo spiking = alone;
    spiking.wheaCorrectedSpiking = true;

    auto d2 = diagnose(st, ev, spiking);
    check("Taban cizgisini asan WHEA artisi sayiliyor",
          causePresent(d2, Cause::MEMORY_EXPO));
}

// --- VAKA 17: karisik bellek takimi ----------------------------------------
//  Gercek vaka: i5 9400F + B365M, 8 GB 2133 MHz HyperX + 8 GB 3600 MHz G.Skill.
//  Oyunda 10-35 dakikada bir komple kilitlenme, siyah ekran, yeniden baslatma.
//  Kullanici PSU'yu (3 kez), islemciyi, ekran kartini, HDD'yi ve Windows'u
//  eledi; RAM'i de "eledigini" saniyor cunku Memtest'i TEK TEK gecti.
//
//  Ayirt edici tam olarak bu: modul basina test karisik takimi ELEMEZ. Sorun
//  modulun kendisinde degil, iki takimin birlikte calismasindadir. Motor bunu
//  gormezse kullaniciya "elediginiz her sey elenmis, geriye anakart kaliyor"
//  demis olur — yani yanlis parca aldirir.
static void testCase_MixedMemoryKit() {
    header("VAKA 17 — Karisik bellek takimi (farkli hizda moduller)");

    TraceBuilder tb(60.0);
    buildStutteryTrace(tb);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.eventLogRead          = true;
    sys.ramModuleCount        = 2;
    sys.ramConfiguredMTs      = 2133;   // en yavas modulun hizina inmis
    sys.ramModulesMismatched  = true;
    sys.kernelPower41         = 4;      // kilitlenip yeniden basliyor
    sys.bugcheckCount         = 0;      // dump uretmiyor

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    check("En olasi sebep: bellek", topCauseIs(d, Cause::MEMORY_EXPO));
    check("Karisik takim kanit olarak gosteriliyor",
          !d.ranked.empty() &&
          d.ranked.front().evidence.find("aynisi degil") != std::string::npos);

    // Ayni makine, moduller ayni takimdan olsaydi bellek suphesi cok daha
    // zayif olmali — bayragin gercekten agirlik tasidigini dogrular.
    SystemInfo matched = sys;
    matched.ramModulesMismatched = false;
    auto d2 = diagnose(st, ev, matched);
    check("Ayni takim modullerde bellek payi daha dusuk",
          percentOf(d2, Cause::MEMORY_EXPO) < percentOf(d, Cause::MEMORY_EXPO));
}

// --- VAKA 18: Intel DDR5 hiz tavani -----------------------------------------
//  AM5'in 1:2 modunun Intel'deki karsiligi Gear 1 -> Gear 2 gecisidir ve
//  4 DIMM'de tavan orada da sert duser. Kural AM5'e ozel yazilmisti; ayni
//  olgunun Intel'de gorulmemesi icin bir sebep yok.
static void testCase_IntelMemoryCeiling() {
    header("VAKA 18 — Intel DDR5 guvenli hiz tavani");

    SystemInfo i2; i2.isIntelDdr5 = true; i2.ramModuleCount = 2;
    SystemInfo i4; i4.isIntelDdr5 = true; i4.ramModuleCount = 4;
    SystemInfo unknown;                                    // platform bilinmiyor

    check("Intel 2 DIMM tavan 6400", safeMemorySpeedFor(i2) == 6400);
    check("Intel 4 DIMM tavan 5600", safeMemorySpeedFor(i4) == 5600);
    check("Intel tavani AM5'inkinden yuksek",
          safeMemorySpeedFor(i2) > 6000);
    check("Platform bilinmiyorsa kural uygulanmiyor",
          safeMemorySpeedFor(unknown) == 0);

    // Tavani asan Intel makinesi artik bellek hipotezi uretmeli.
    TraceBuilder tb(144.0);
    buildStutteryTrace(tb);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo fast;
    fast.eventLogRead     = true;
    fast.isIntelDdr5      = true;
    fast.ramModuleCount   = 4;
    fast.ramConfiguredMTs = 6800;      // 4 DIMM'de 5600 tavaninin cok ustunde

    auto d = diagnose(st, ev, fast);
    printDiagnosis(d);
    check("Intel'de tavan asimi bellek hipotezi uretiyor",
          causePresent(d, Cause::MEMORY_EXPO));

    // Ayni hiz, platform bilinmiyorsa hukum verilmemeli.
    SystemInfo blind = fast;
    blind.isIntelDdr5 = false;
    auto d2 = diagnose(st, ev, blind);
    check("Platform bilinmiyorsa ayni hiz suphe URETMIYOR",
          !causePresent(d2, Cause::MEMORY_EXPO));
}

static void testInvariants() {
    header("BOLUM 3 — Tutarlilik");

    // Tum senaryolarda oranlar toplami 100 olmali
    TraceBuilder tb(144.0);
    tb.normal(300);
    SignalSnapshot dpc; dpc.dpcMaxMs = 3.0; dpc.dpcDriver = "nvlddmkm.sys";
    for (int i = 0; i < 10; ++i) { tb.spike(60.0, dpc); tb.normal(100); }

    tb.padTo(40.0);
    auto ev = runDetector(tb);
    auto st = analyzeSession(tb.frames(), ev);

    SystemInfo sys;
    sys.isAM5 = true; sys.ramConfiguredMTs = 6400; sys.ramModuleCount = 2;
    sys.wheaCorrected = 150; sys.curveOptimizerActive = true;
    sys.liveKernelReports = 1;

    auto d = diagnose(st, ev, sys);
    printDiagnosis(d);

    int sum = 0;
    for (const auto& h : d.ranked) sum += h.percent;
    check("Oranlar toplami tam %100", sum == 100);

    bool desc = true;
    for (size_t i = 1; i < d.ranked.size(); ++i)
        if (d.ranked[i-1].percent < d.ranked[i].percent) desc = false;
    check("Oranlar buyukten kucuge sirali", desc);

    bool allHaveAction = true;
    for (const auto& h : d.ranked) if (h.action.empty()) allHaveAction = false;
    check("Her maddenin bir aksiyonu var", allHaveAction);

    bool allHaveEvidence = true;
    for (const auto& h : d.ranked) if (h.evidence.empty()) allHaveEvidence = false;
    check("Her maddenin bir kaniti var", allHaveEvidence);

    check("Guven 0-100 araliginda", d.confidence >= 0 && d.confidence <= 100);

    // -----------------------------------------------------------------------
    //  Tasarim kurali 4: motor asla "kesin sebep bu" dememeli.
    //  Onceden tek kural atesledigi zaman normalizasyon o hipoteze %100
    //  veriyordu; artik tavan kMaxSingleCausePercent ve fark UNKNOWN_OTHER'a
    //  gidiyor.
    // -----------------------------------------------------------------------
    {
        bool underCap = true;
        for (const auto& h : d.ranked)
            if (h.cause != Cause::UNKNOWN_OTHER && h.percent > kMaxSingleCausePercent)
                underCap = false;
        check("Hicbir hipotez tavani asmiyor (cok kuralli senaryo)", underCap);
    }

    // Tek kuralin atesledigi senaryo: %100 iddiasi olusmamali
    {
        TraceBuilder solo(240.0);
        solo.normal(200);
        solo.periodicMicro(20, 60, 15.0);

        solo.padTo(40.0);
        auto sev = runDetector(solo);
        auto sst = analyzeSession(solo.frames(), sev);

        SystemInfo only;
        only.monitorRecentlyChanged = true;

        auto sd = diagnose(sst, sev, only);

        check("Tek sebep %100 olmuyor",
              !sd.ranked.empty() && sd.ranked.front().percent <= kMaxSingleCausePercent);
        check("Olculmemis pay hipotez olarak ekleniyor",
              sd.ranked.size() >= 2 &&
              sd.ranked.back().cause == Cause::UNKNOWN_OTHER);

        int ssum = 0;
        for (const auto& h : sd.ranked) ssum += h.percent;
        check("Tavan sonrasi oranlar hala %100", ssum == 100);

        const int resid = sd.ranked.empty() ? 0 : sd.ranked.back().percent;
        check("Guven olculmemis paydan buyuk olamaz",
              sd.confidence <= 100 - resid);
    }

    // Bos girdi coksun diye
    {
        SessionStats empty;
        auto d2 = diagnose(empty, {}, SystemInfo{});
        check("Bos girdide cokme yok, yetersiz hukmu veriliyor", d2.inconclusive);
    }

    // AM5 guvenli hiz tablosu
    {
        SystemInfo a; a.isAM5 = true; a.ramModuleCount = 2;
        SystemInfo b; b.isAM5 = true; b.ramModuleCount = 4;
        check("AM5 2 cubuk guvenli tavan 6000", safeMemorySpeedFor(a) == 6000);
        check("AM5 4 cubuk guvenli tavan 5600", safeMemorySpeedFor(b) == 5600);
    }
}

// ============================================================================
//  BOLUM 4 — PresentMon CSV kaynagi
// ============================================================================
static void testCsvSource() {
    header("BOLUM 4 — PresentMon CSV okuyucu");

    // --- PresentMon 1.x formati ---
    {
        std::string csv =
            "Application,ProcessID,SwapChainAddress,Runtime,SyncInterval,PresentFlags,"
            "Dropped,TimeInSeconds,MsBetweenPresents,MsInPresentAPI\n"
            "VALORANT.exe,4242,0x1234,DXGI,0,0,0,10.000,4.16,0.30\n"
            "VALORANT.exe,4242,0x1234,DXGI,0,0,0,10.004,4.17,0.31\n"
            "VALORANT.exe,4242,0x1234,DXGI,0,0,1,10.008,4.16,0.29\n"   // dusen kare
            "VALORANT.exe,4242,0x1234,DXGI,0,0,0,10.012,4.18,0.30\n";

        auto src = CsvFrameSource::fromString(csv);
        check("1.x CSV hatasiz okundu", src->error().empty());
        check("Dusen kare atlandi (3 gecerli kare)", src->totalRows() == 3);
        check("Dusen kare sayildi", src->droppedRows() == 1);
        check("Uygulama adi cikarildi", src->info().application == "VALORANT.exe");
        check("Process ID cikarildi", src->info().processId == 4242);

        FrameSample f;
        check("Ilk kare okunuyor", src->next(f));
        check("Kare suresi dogru", std::abs(f.frameTimeMs - 4.16) < 0.001);
        check("Zaman damgasi saniyeden cevrildi", f.timestampUs == 10'000'000);
    }

    // --- PresentMon 2.x formati (farkli sutun ADLARI, farkli SIRA) ---
    {
        std::string csv =
            "ProcessName,ProcessID,FrameTime,CPUStartTime,GPUBusy,DisplayLatency\n"
            "pubg.exe,900,4.10,0.000,3.9,8.1\n"
            "pubg.exe,900,4.20,0.004,4.0,8.2\n"
            "pubg.exe,900,20.50,0.008,4.1,8.0\n";

        auto src = CsvFrameSource::fromString(csv);
        check("2.x CSV hatasiz okundu", src->error().empty());
        check("Sutun ADINA gore bulundu, siraya gore degil", src->totalRows() == 3);
        check("2.x uygulama adi (ProcessName) cikarildi",
              src->info().application == "pubg.exe");
    }

    // --- Bozuk / alakasiz CSV nazikce reddedilmeli ---
    {
        auto src = CsvFrameSource::fromString("isim,soyisim\nali,veli\n");
        check("Alakasiz CSV reddedildi", !src->error().empty());
        check("Hata mesaji yol gosteriyor",
              src->error().find("PresentMon") != std::string::npos);
    }
    {
        auto src = CsvFrameSource::fromString("");
        check("Bos CSV reddedildi", !src->error().empty());
    }

    // --- Bozuk satirlar atlanmali, gecerliler okunmali ---
    {
        std::string csv =
            "TimeInSeconds,MsBetweenPresents\n"
            "0.000,4.16\n"
            "0.004,abc\n"          // sayi degil
            "0.008,-5\n"           // negatif
            "0.012,99999\n"        // absurt
            "0.016,4.20\n";
        auto src = CsvFrameSource::fromString(csv);
        check("Bozuk satirlar atlandi, 2 gecerli kare kaldi", src->totalRows() == 2);
        check("Hata verilmedi (kismi veri kabul edildi)", src->error().empty());
    }

    // --- Zaman sutunu olmadan da calismali ---
    {
        std::string csv = "MsBetweenPresents\n4.0\n4.0\n4.0\n";
        auto src = CsvFrameSource::fromString(csv);
        check("Zaman sutunu yokken kare surelerinden turetildi",
              src->error().empty() && src->totalRows() == 3);
        FrameSample a, b;
        src->next(a); src->next(b);
        check("Turetilen zaman damgalari artan", b.timestampUs > a.timestampUs);
    }

    // --- Uctan uca: CSV -> teshis ---
    {
        std::string csv = "TimeInSeconds,MsBetweenPresents\n";
        // Sure GERCEKCI olmali: motor kMinSessionSec altindaki kayitlarda
        // hukum vermiyor. 240 FPS'te 30 saniye ~7200 kare eder.
        double t = 0.0;
        for (int i = 0; i < 2000; ++i) {          // temiz 240 FPS
            csv += std::to_string(t) + ",4.16\n";
            t += 0.00416;
        }
        for (int i = 0; i < 60; ++i) {            // duzenli mikro-takilma
            for (int j = 0; j < 100; ++j) { csv += std::to_string(t) + ",4.16\n"; t += 0.00416; }
            csv += std::to_string(t) + ",14.0\n"; t += 0.014;
        }

        auto src = CsvFrameSource::fromString(csv);
        SystemInfo sys;
        sys.monitorRecentlyChanged = true;
        auto res = analyzeSource(*src, sys);

        std::printf("\n    CSV ucdan uca: %zu kare, %zu olay\n",
                    res.frames.size(), res.events.size());
        printDiagnosis(res.diagnosis);

        check("CSV'den kareler motora aktarildi", res.frames.size() > 400);
        check("Takilmalar tespit edildi", res.events.size() >= 10);
        check("Duzenli desen yakalandi", res.stats.periodicMicroStutter);
        check("Teshis uretildi: VRR / kare zamanlama",
              topCauseIs(res.diagnosis, Cause::VRR_PACING));
    }
}

// ============================================================================
//  main
// ============================================================================
int main() {
    std::printf("\n#############################################################\n");
    std::printf("#  StutterScope cekirdek motor testleri                      #\n");
    std::printf("#  Vaka kulliyati regresyon paketi                           #\n");
    std::printf("#############################################################\n");

    testDetectorBasics();

    testCase_DriverDpc();
    testCase_VrrPacing();
    testCase_MemoryInstability();
    testCase_CurveOptimizer();
    testCase_LowFpsNotStutter();
    testCase_CpuBottleneck();
    testCase_MemoryPressure();
    testCase_Underused();
    testCase_DiskSaturated();
    testCase_PowerBrake();
    testCase_ShaderCompile();
    testCase_Psu();
    testCase_Inconclusive();
    testCase_BackgroundApp();

    testCase_TdrDuringCapture();
    testCase_TdrHistoricalOnly();
    testCase_StorageEvents();
    testCase_WheaAloneIsNotEvidence();
    testCase_MixedMemoryKit();
    testCase_IntelMemoryCeiling();

    testCsvSource();
    testInvariants();

    std::printf("\n=============================================================\n");
    std::printf("  SONUC: %d gecti, %d basarisiz\n", g_pass, g_fail);
    std::printf("=============================================================\n\n");
    return g_fail == 0 ? 0 : 1;
}
