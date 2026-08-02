// ============================================================================
//  StutterScope — telemetri ornekleyici
//  ----------------------------------------------------------------------
//  Olcum boyunca saniyede bir sicaklik, kullanim, frekans ve guc degerlerini
//  kaydeder. Amac oyun sirasinda kullaniciya bir sey GOSTERMEK degil (zaten
//  bakamiyor) — kaydi tutup SONRADAN grafik olarak incelenebilir yapmak.
//
//  TASARIM KURALI 1 HATIRLATMASI: Sicaklik hicbir teshis kuralina girmez.
//  Vaka analizinde 8 gercek sikayetin hicbirinde sebep sicaklik degildi.
//  Buradaki degerler kullaniciya BILGI olarak sunulur; motor karar verirken
//  kullanmaz. Tek istisna, sicakligin degil, GPU'nun kendi bildirdigi
//  THROTTLE BAYRAKLARI'dir — onlar olculmus olgudur, tahmin degil.
//
//  KAYNAKLAR
//    GPU (NVIDIA) : nvml.dll — surucuyle birlikte gelir, ayrica kurulmaz.
//                   Dinamik yuklenir; yoksa GPU alanlari bos kalir.
//    GPU (AMD)    : HENUZ YOK. ADLX entegrasyonu bekliyor.
//    CPU frekansi : PDH "Processor Information\% Processor Performance".
//                   CLAUDE.md: PROCESSOR_POWER_INFORMATION.CurrentMhz olu
//                   API, MaxMhz ile ayni degeri donuyor — kullanmayin.
//    Cekirdek park: PDH "Processor Information\Parking Status".
//    CPU sicaklik : ACPI termal bolge (WMI). Cogu masaustunde YOKTUR ya da
//                   islemci die sicakligini degil kasa sensorunu verir.
//                   Gercek Tctl/Tdie icin ring 0 gerekir (HWiNFO bu yuzden
//                   kendi surucusunu kurar). Okunamazsa alan bos kalir ve
//                   arayuzde "ölçülemiyor" yazar — uydurma deger URETILMEZ.
// ============================================================================
#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <vector>

namespace sstelem {

// Okunamayan her alan icin sentinel. 0 KULLANILMAZ: 0 derece ya da %0
// kullanim gecerli olculerdir, "bilinmiyor" ile karistirilamaz.
// (Ayni gerekce SignalSnapshot::kUnknownMinute icin de gecerliydi.)
constexpr double kUnknown = -1000.0;

inline bool known(double v) { return v > kUnknown + 1.0; }

struct Sample {
    uint32_t tSec = 0;          // olcumun basindan bu yana gecen saniye

    double gpuTempC     = kUnknown;
    double gpuUtilPct   = kUnknown;
    double gpuClockMhz  = kUnknown;
    double gpuPowerW    = kUnknown;
    double gpuMemUsedMb = kUnknown;

    double cpuUsagePct  = kUnknown;  // klasik "islemci kullanimi"
    double cpuPerfPct   = kUnknown;  // %100 = taban frekans; ustu boost
    double cpuTempC     = kUnknown;
    double coreParkedPct= kUnknown;

    double ramUsedPct   = kUnknown;

    // Bellek baskisi. ramUsedPct "dolu yuzdesi"ni verir ama asil soru bu
    // degil: sistem RAM'den FAZLASINI taahhut etti mi? Ettiginde fark
    // sayfa dosyasina, yani diske gider ve takilma baslar.
    //   commitUsedPct : taahhut / taahhut siniri
    //   availPhysMb   : o an bos fiziksel bellek
    //   commitOverRam : taahhut, takili RAM'i asti mi
    double availPhysMb   = kUnknown;
    double commitUsedPct = kUnknown;
    bool   commitOverRam = false;

    // Disk. "% Disk Time" yerine "% Idle Time"den turetiliyor: Disk Time
    // coklu kuyrukta %100'u asabiliyor ve yaniltiyor. aktiflik = 100 - bosta.
    double diskActivePct  = kUnknown;
    double diskLatencyMs  = kUnknown;   // ortalama transfer suresi
    double diskMbPerSec   = kUnknown;

    // GPU'nun kendi bildirdigi kisitlama bayraklari
    bool thermalThrottle = false;   // isinma sebebiyle hiz dusurme
    bool powerCapThrottle= false;   // guc limiti
    bool powerBrake      = false;   // HW power brake — PSU frenleme sinyali
};

// ----------------------------------------------------------------------------
//  GPU sabitleri — olcum boyunca degismeyen degerler
// ----------------------------------------------------------------------------
//  Bunlar ornek DEGIL, kartin yapilandirmasidir. Iki gercek forum sorusuna
//  dogrudan cevap verirler:
//
//   "Resizable BAR acik mi?"  -> BAR1 penceresi VRAM kadar buyukse aciktir.
//                                Kapaliyken tipik olarak 256 MB'ta kalir.
//   "140 W kart neden 40 W cekiyor?" -> guc limiti kac watt, kart o limite
//                                dayaniyor mu, yoksa hic mi zorlanmiyor.
struct GpuStatic {
    bool     known = false;
    std::string name;

    double   powerLimitW        = kUnknown;  // su anki uygulanan limit
    double   defaultPowerLimitW = kUnknown;  // fabrika limiti
    double   maxPowerLimitW     = kUnknown;  // cikilabilecek en yuksek limit

    uint64_t vramTotalMb = 0;
    uint64_t bar1TotalMb = 0;

    enum class Tri { Unknown, No, Yes };
    Tri      resizableBar = Tri::Unknown;
    std::string rebarNote;
};

struct Capabilities {
    bool nvidiaGpu   = false;
    bool amdGpu      = false;   // tespit edildi ama okunamiyor
    bool cpuPerf     = false;
    bool cpuTemp     = false;
    std::string gpuName;
    std::string note;           // neyin neden okunamadigi
};

// Ornekleyici. Yakalama ipliginden bagimsiz calisir; start/stop cifti
// arasinda saniyede bir ornek toplar.
class Sampler {
public:
    Sampler();
    ~Sampler();

    Capabilities capabilities() const { return caps_; }
    GpuStatic    gpuStatic()    const { return gpu_; }

    // Son ornegi dondurur (canli gosterim icin). Ornekleme kalici bir
    // iplikte saniyede bir yapilir; bu cagri yalnizca onbellegi okur.
    Sample readNow(uint32_t tSec = 0);

    void start();
    void stop();
    std::vector<Sample> samples() const;   // kopya — iplik guvenli

private:
    Sample sampleOnce(uint32_t tSec);

    struct Impl;
    Impl*        impl_ = nullptr;
    Capabilities caps_;
    GpuStatic    gpu_;
};

} // namespace sstelem

#endif // _WIN32
