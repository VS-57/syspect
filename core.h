// ============================================================================
//  StutterScope — Cekirdek analiz motoru (tasinabilir)
//  ----------------------------------------------------------------------
//  Bu baslik Windows API'sine HIC bagimli DEGILDIR. Amac: takilma tespiti ve
//  teshis mantigini isletim sisteminden ayirmak, boylece her platformda
//  derlenip sentetik verilerle test edilebilmesi.
//
//  Windows'a ozgu veri toplama (ETW, WMI, PDH, olay gunlugu) ayri bir
//  katmanda durur ve buradaki yapilara doldurup verir.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <optional>

namespace ss {

// ============================================================================
//  1. Temel veri tipleri
// ============================================================================

// Tek bir karenin sunum bilgisi (ETW DxgKrnl Present olayindan gelir)
struct FrameSample {
    uint64_t timestampUs = 0;   // mikrosaniye
    double   frameTimeMs = 0.0; // bir onceki kareden bu yana gecen sure
};

// Takilma aninda esanli olarak olculen diger sinyaller.
// Windows katmani doldurur; motor yalnizca okur.
struct SignalSnapshot {
    // DPC / kesme
    double      dpcMaxMs        = 0.0;   // olay penceresindeki en uzun DPC
    std::string dpcDriver;               // o DPC'yi yapan .sys

    // Bellek / disk
    uint32_t    hardFaults      = 0;     // hard page fault sayisi
    double      diskWaitMs      = 0.0;   // en uzun disk bekleme

    // GPU
    bool        vramOverBudget  = false; // VRAM butcesi asildi
    bool        gpuPowerBrake   = false; // NVML HwPowerBrakeSlowdown
    bool        gpuThermalThrottle = false;
    double      gpuUtilPercent  = 0.0;

    // CPU
    double      cpuPerfPercent  = 100.0; // PDH "% Processor Performance"
    double      coreParkedRatio = 0.0;   // 0..1

    // Arka plan
    std::string topBackgroundProcess;
    double      backgroundCpuPercent = 0.0;

    // Baglam
    bool        inGame          = true;  // oyun surecinde mi olustu

    // Oyun basladigindan bu yana gecen dakika.
    // DIKKAT: varsayilan deger BILINMIYOR anlamina gelir. 0 kullanmak,
    // "her olay ilk dakikada oldu" gibi yanlis bir cikarim uretir ve
    // shader-derleme kuralini yanlis tetikler. Windows katmani bu alani
    // dolduramiyorsa sentinel olarak birakmalidir.
    static constexpr uint32_t kUnknownMinute = 0xFFFFFFFFu;
    uint32_t    minutesSinceGameStart = kUnknownMinute;

    bool hasGameTime() const { return minutesSinceGameStart != kUnknownMinute; }
};

enum class StutterKind {
    MicroStutter,   // 1-2 kare, duzenli tekrar
    Hitch,          // 50-500 ms tek sicrama
    Freeze,         // > 500 ms, sistem geri geliyor
};

struct StutterEvent {
    uint64_t       timestampUs = 0;
    double         frameTimeMs = 0.0;
    double         baselineMs  = 0.0;
    double         severity    = 0.0;  // frameTimeMs / baselineMs
    StutterKind    kind        = StutterKind::Hitch;
    SignalSnapshot signals;
};

// ============================================================================
//  2. Takilma dedektoru
// ----------------------------------------------------------------------------
//  Sabit esik kullanilmaz: 60 FPS'te normal olan 144 Hz'de felakettir.
//  Kayan pencerenin MEDYANI taban kabul edilir (ortalama, takilmalarin
//  kendisi tarafindan kirletilir).
// ============================================================================
class StutterDetector {
public:
    explicit StutterDetector(size_t windowSize = 120);

    // Yeni bir kare besle. Takilma tespit edilirse olayi dondurur.
    std::optional<StutterEvent> push(const FrameSample& f,
                                     const SignalSnapshot& sig = {});

    double baselineMs() const;          // guncel medyan
    size_t sampleCount() const { return totalSamples_; }
    bool   warmedUp() const;            // pencere yeterince doldu mu

    // Esik hesabi: max(2.0 x taban, taban + 8 ms)
    static double thresholdFor(double baselineMs);

private:
    size_t             window_;
    std::deque<double> ring_;
    size_t             totalSamples_ = 0;
};

// ============================================================================
//  3. Oturum istatistikleri
// ============================================================================
struct SessionStats {
    size_t  frameCount        = 0;
    double  medianFrameTimeMs = 0.0;
    double  p99FrameTimeMs    = 0.0;

    // DIKKAT — bu ikisi ayni sey DEGILDIR:
    //   avgFps    : gercek ortalama. Toplam kare / toplam sure.
    //   medianFps : 1000 / medyan kare suresi. Takilmalardan etkilenmez.
    // Onceki surumde tek bir alan vardi, medyandan hesaplaniyordu ama adi
    // "avgFps" idi — etiket yaniltiyordu. Ikisi de gosteriliyor artik;
    // aralarindaki fark tek basina bilgi tasir: avgFps medianFps'ten belirgin
    // dusukse oturumda uzun takilmalar var demektir.
    double  avgFps            = 0.0;
    double  medianFps         = 0.0;
    double  onePercentLowFps  = 0.0;
    double  durationSec       = 0.0;

    size_t  stutterCount      = 0;
    size_t  microStutterCount = 0;
    size_t  hitchCount        = 0;
    size_t  freezeCount       = 0;

    // Mikro-takilma duzenli mi? (VRR / frame pacing parmak izi)
    bool    periodicMicroStutter = false;
    double  microStutterPeriodMs = 0.0;

    // Takilma yok ama FPS dusuk: bu bir takilma degil, yetersiz donanimdir
    bool    lowFpsNotStutter  = false;
};

SessionStats analyzeSession(const std::vector<FrameSample>& frames,
                            const std::vector<StutterEvent>& events);

// ============================================================================
//  4. Sistem envanteri
// ----------------------------------------------------------------------------
//  Windows katmani doldurur. Kural motorunun donanim baglamini gormesi icin.
// ============================================================================
struct SystemInfo {
    std::string cpuName;
    std::string gpuName;

    // Bellek
    uint32_t ramConfiguredMTs = 0;   // SMBIOS Type 17 yapilandirilmis hiz
    uint32_t ramJedecMTs      = 0;   // JEDEC taban hiz
    uint32_t ramModuleCount   = 0;

    // Platform. Ikisi de "bellek denetleyicisi belirli bir hizin ustunde
    // yarim hiz moduna gecer ve kararsizlik olasiligi artar" olgusunu isaret
    // eder; AM5'te 1:2 modu, Intel DDR5'te Gear 2. Tavanlar farkli oldugu
    // icin ayri bayraklar: bkz. safeMemorySpeedFor.
    bool     isAM5            = false;   // Ryzen 7000/8000/9000 masaustu
    bool     isIntelDdr5      = false;   // 12-14. nesil ve sonrasi, DDR5

    // Farkli hizda / boyutta / tipte moduller bir arada. Platformdan bagimsiz
    // bir kararsizlik kaynagi ve kullanicilarin en sik elediklerini SANDIGI
    // sey: modul basina Memtest gecer, cunku sorun modulde degil ikisinin
    // birlikte calismasindadir.
    bool     ramModulesMismatched = false;

    // Olay gunlugu sayaclari (son 30 gun)
    uint32_t wheaCorrected    = 0;   // ID 17/19/46/47 — cogu zaman zararsiz
    uint32_t wheaFatal        = 0;   // ID 18 — ciddi
    uint32_t kernelPower41    = 0;   // kontrolsuz guc kesintisi
    uint32_t bugcheckCount    = 0;   // olay 1001
    uint32_t liveKernelReports = 0;  // mavi ekransiz GPU cokmesi

    // ------------------------------------------------------------------
    //  Olay gunlugunden gelen ek sinyaller
    // ------------------------------------------------------------------
    //  Hepsi son 30 gunun sayimidir. Sayim TEK BASINA hukum degildir: her
    //  birinin yaninda "olcum sirasinda da oldu mu" alani var ve kurallar
    //  agirligi asil oradan aliyor. Saglikli makinede de birikmis olaylar
    //  bulunur; ayirt edici olan zaman cakismasidir.

    // TDR — ekran surucusu yanit vermeyip sifirlandi (olay 4101).
    // Kullanicinin "ekran bir an dondu sonra duzeldi" tarifinin makinedeki
    // tam karsiligi. Olcum sirasinda gorulduyse takilmanin sebebi budur.
    uint32_t tdrCount         = 0;
    uint32_t tdrDuringCapture = 0;
    std::string tdrDriver;           // olay verisinden: "nvlddmkm" gibi

    // Depolama. 129 = aygita sifirlama gonderildi (surucu takildi ve
    // toparlandi), 153 = G/C yeniden denendi, 7/11/51 = hatali blok.
    // Sifirlama en agir olani: o sirada tum G/C durur.
    uint32_t storageResetCount   = 0;
    uint32_t storageRetryCount   = 0;
    uint32_t storageErrorCount   = 0;
    uint32_t storageDuringCapture = 0;

    // Dosya sistemi bozulmasi (Ntfs 55). Sebep degil SONUC olabilir; yine de
    // bildirilir cunku diskin durumu hakkinda somut bilgidir.
    uint32_t ntfsCorruption   = 0;

    // Duzeltilmis WHEA olaylari taban cizgisinin cok ustunde mi?
    // Bu bayrak olmadan ID 17 gurultusu her AM5 makinesinde bellek kuralini
    // sahte bir yuzdeye oturtur — bkz. CLAUDE.md, WHEA notu.
    bool     wheaCorrectedSpiking = false;

    // Olay gunlugu gercekten okunabildi mi. Okunamadiysa sifir sayimlari
    // "sorun yok" diye YORUMLAMA — veri yok demektir.
    bool     eventLogRead     = false;

    // Kullanicidan / BIOS'tan
    bool     curveOptimizerActive = false;
    bool     expoActive           = false;
    bool     gpuOverclocked       = false;
    int      psuQualityTier       = 0;   // 0=bilinmiyor, 1=zayif .. 4=cok iyi
    uint32_t psuWatts             = 0;
    uint32_t estimatedPeakWatts   = 0;

    // ------------------------------------------------------------------
    //  Olcum boyunca toplanan ozet telemetri (Windows katmani doldurur)
    // ------------------------------------------------------------------
    //  Bunlar tek bir takilma anina degil, OTURUMUN TAMAMINA aittir; bu
    //  yuzden StutterEvent::signals'da degil burada duruyorlar.
    //  Olculemeyen alan icin sentinel: -1. 0 kullanilamaz cunku %0 kullanim
    //  gecerli ve cok anlamli bir olcumdur.
    static constexpr double kUnknownPct = -1.0;

    double medianGpuUtilPct     = kUnknownPct;
    double medianCpuUsagePct    = kUnknownPct;
    double medianDiskActivePct  = kUnknownPct;
    double p95DiskLatencyMs     = kUnknownPct;

    // Bellek baskisi — oturumun EN KOTU ani
    double maxCommitUsedPct     = kUnknownPct;   // taahhut / taahhut siniri
    double minAvailPhysMb       = kUnknownPct;   // en dusuk bos fiziksel bellek
    bool   commitExceededRam    = false;         // taahhut takili RAM'i asti
    uint32_t ramTotalMb         = 0;

    // GPU'nun KENDI bildirdigi kisitlama bayraklari. Sicaklik OKUMASI degil,
    // olculmus olgu olduklari icin tasarim kurali 1'i ihlal etmezler.
    bool   gpuPowerBrakeSeen      = false;   // PSU frenleme sinyali
    bool   gpuThermalThrottleSeen = false;
    bool   gpuPowerCapSeen        = false;

    static bool pctKnown(double v) { return v >= 0.0; }

    // Davranissal — en degerli ayirt edici
    bool     desktopStutterObserved = false; // oyun disinda da takiliyor mu
    bool     monitorRecentlyChanged = false;
    bool     driverRecentlyUpdated  = false;
    std::string recentChangeNote;
};

// ============================================================================
//  5. Teshis motoru
// ============================================================================
enum class Cause {
    DRIVER_DPC,      // belirli bir surucu DPC'yi kilitliyor
    GPU_DRIVER,
    MEMORY_EXPO,     // RAM / EXPO / IMC kararsizligi
    OVERCLOCK_CO,    // undervolt / Curve Optimizer
    VRAM,
    SHADER_COMPILE,
    VRR_PACING,
    PAGEFILE_RAM,    // RAM yetersiz, sayfalama
    STORAGE,
    THERMAL,
    POWER_PLAN,
    BACKGROUND_APP,
    PSU,
    UNBALANCED_HW,
    RECENT_CHANGE,

    // Ekran karti bosta beklerken islemci tam yuklu: darbogaz islemcide.
    CPU_BOTTLENECK,
    // Ne ekran karti ne islemci doluyor ama FPS dusuk — bir yerde tavan var
    // (FPS siniri, dikey esitleme, guc limiti, surucu).
    GPU_UNDERUSED,

    // Gercek bir sebep DEGILDIR. Motorun olcemedigi alan icin ayrilan paydir.
    // Tasarim kurali 4 ("asla kesin sebep bu deme") bununla uygulanir: tek
    // hipotez %100'e ciktiginda fark buraya aktarilir. CSV modunda DPC, VRAM
    // ve disk sinyalleri gercekten olculmedigi icin bu pay uydurma degildir.
    UNKNOWN_OTHER,
};

// Tek bir hipotezin alabilecegi en yuksek oran. Ustu UNKNOWN_OTHER'a gider.
constexpr int kMaxSingleCausePercent = 80;

// ----------------------------------------------------------------------------
//  Hukum verilebilmesi icin gereken EN AZ olcum
// ----------------------------------------------------------------------------
//  Dedektor kayan pencerenin medyanini taban alir ve 120 karelik bir isinma
//  ister. Bunun otesinde, kisa bir kayitta "takilma" saymak yaniltir:
//  masaustunde DWM hicbir sey degismezken seyrek, bir animasyon olunca sik
//  kare uretir; bu dogal dalgalanma esigi asar ve takilma gibi gorunur.
//  Gercek vaka: 6 saniyelik masaustu kaydinda 7 "takilma" raporlandi.
//
//  Bu esiklerin altinda motor SAYI VERIR ama HUKUM VERMEZ.
constexpr double kMinSessionSec    = 30.0;
constexpr size_t kMinSessionFrames = 600;

// Ekran kartinin "doygun" sayildigi esik. GPU-bound bir oyun %97-99'da
// oturur; %70-80 kullaniciya yuksek gorunur ama ekran karti bekliyor demektir.
constexpr double kGpuSaturatedPct = 90.0;

// Islemcinin "dolu" sayildigi esik.
constexpr double kCpuBusyPct      = 80.0;

// ----------------------------------------------------------------------------
//  Kanit parcasi — cevrilebilir olmasi icin SABLON + ARGUMAN
// ----------------------------------------------------------------------------
//  Kanit cumleleri calisma aninda kuruluyor:
//      "takilmalarin %40'i nvlddmkm.sys ile cakisiyor"
//  Bu haliyle bir ceviri tablosunda ASLA bulunamaz — icindeki sayi her
//  olcumde farkli. Bu yuzden sablon ve degerler ayri tutuluyor; sunum katmani
//  sablonu cevirip degerleri yerlestiriyor.
//
//  Neden Turkce'de ozellikle sart: ek sona gelen kelimeye ve sayiya gore
//  degisiyor ("%40'i", "%40'inda"), Ingilizce'de cumle bastan kuruluyor.
//  Duz birlestirmeyle cevrilemez.
//
//  core.h TASINABILIR kalmali (tasarim kurali 5) — bu yuzden burada ceviri
//  yok, yalnizca veri. Cevirme isi Windows katmaninda yapiliyor.
struct EvidencePart {
    std::string tpl;                    // "%1 MT/s, guvenli kabul edilen %2 ustunde"
    std::vector<std::string> args;

    // Sablonu argumanlarla doldurur. %1..%9 sirayla degistirilir.
    std::string format() const;
};

struct Hypothesis {
    Cause       cause;
    int         percent = 0;
    std::string label;

    // Birlestirilmis, kullanilmaya hazir Turkce metin. Ceviri yapmayan
    // cagiranlar (komut satiri, testler) bunu okumaya devam ediyor.
    std::string evidence;

    // Ayni kanit, cevrilebilir bicimde. Sunum katmani bunu tercih etmeli.
    std::vector<EvidencePart> evidenceParts;

    std::string action;     // kullanicinin yapacagi tek sey
};

struct Diagnosis {
    std::vector<Hypothesis> ranked;
    int         confidence = 0;      // 0-100
    bool        inconclusive = false;
    std::string headline;            // tek cumlelik ozet
};

Diagnosis diagnose(const SessionStats& stats,
                   const std::vector<StutterEvent>& events,
                   const SystemInfo& sys);

// Yardimcilar (test edilebilirlik icin disa acik)
const char* causeLabel(Cause c);

// Makine tarafindan okunan sabit anahtar ("MEMORY_EXPO" gibi).
// causeLabel kullanici metnidir ve degisebilir; JSON ciktisi ve ilerideki
// arayuz bu anahtara baglanmalidir.
const char* causeKey(Cause c);

const char* causeAction(Cause c);
const char* stutterKindName(StutterKind k);

// AM5 gibi platformlarda "guvenli" bellek hizi tavani
uint32_t safeMemorySpeedFor(const SystemInfo& sys);

} // namespace ss
