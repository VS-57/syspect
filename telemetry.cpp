// ============================================================================
//  StutterScope — telemetri ornekleyici
// ============================================================================
#ifdef _WIN32

#include "telemetry.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <wbemidl.h>
#include <objbase.h>
#include <psapi.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace sstelem {
namespace {

// ============================================================================
//  NVML — nvml.dll surucuyle gelir, dinamik yuklenir
// ----------------------------------------------------------------------------
//  NVML basligi ayrica indirilmez; ihtiyacimiz olan birkac imzayi burada
//  kendimiz bildiriyoruz. Bunlar kararli C ABI'si; NVIDIA surum atlarken
//  bozmaz (fonksiyon adlarindaki _v2 son ekleri tam da bunun icin var).
// ============================================================================
using nvmlDevice_t = void*;

struct nvmlUtilization_t { unsigned int gpu, memory; };
struct nvmlMemory_t      { unsigned long long total, free, used; };
struct nvmlBAR1Memory_t  { unsigned long long bar1Total, bar1Free, bar1Used; };

constexpr int NVML_TEMPERATURE_GPU = 0;
constexpr int NVML_CLOCK_GRAPHICS  = 0;

constexpr unsigned long long kThrottleSwPowerCap        = 0x0000000000000004ULL;
constexpr unsigned long long kThrottleSwThermalSlowdown = 0x0000000000000020ULL;
constexpr unsigned long long kThrottleHwThermalSlowdown = 0x0000000000000040ULL;
constexpr unsigned long long kThrottleHwPowerBrake      = 0x0000000000000080ULL;

struct Nvml {
    HMODULE dll = nullptr;

    int  (*init)()                                                        = nullptr;
    int  (*shutdown)()                                                    = nullptr;
    int  (*getCount)(unsigned int*)                                       = nullptr;
    int  (*getHandle)(unsigned int, nvmlDevice_t*)                        = nullptr;
    int  (*getName)(nvmlDevice_t, char*, unsigned int)                    = nullptr;
    int  (*getTemp)(nvmlDevice_t, int, unsigned int*)                     = nullptr;
    int  (*getUtil)(nvmlDevice_t, nvmlUtilization_t*)                     = nullptr;
    int  (*getClock)(nvmlDevice_t, int, unsigned int*)                    = nullptr;
    int  (*getPower)(nvmlDevice_t, unsigned int*)                         = nullptr;
    int  (*getMemory)(nvmlDevice_t, nvmlMemory_t*)                        = nullptr;
    int  (*getThrottle)(nvmlDevice_t, unsigned long long*)                = nullptr;
    int  (*getPowerLimit)(nvmlDevice_t, unsigned int*)                    = nullptr;
    int  (*getDefaultLimit)(nvmlDevice_t, unsigned int*)                  = nullptr;
    int  (*getLimitRange)(nvmlDevice_t, unsigned int*, unsigned int*)     = nullptr;
    int  (*getBar1)(nvmlDevice_t, nvmlBAR1Memory_t*)                      = nullptr;

    nvmlDevice_t device = nullptr;
    bool ready = false;
    std::string name;

    template <typename T>
    void bind(T& fn, const char* symbol) {
        fn = reinterpret_cast<T>(
            reinterpret_cast<void*>(GetProcAddress(dll, symbol)));
    }

    bool open() {
        // Modern suruculerde System32'de; eski kurulumlarda NVSMI klasorunde.
        dll = LoadLibraryA("nvml.dll");
        if (!dll) {
            char pf[MAX_PATH];
            if (GetEnvironmentVariableA("ProgramFiles", pf, MAX_PATH) > 0) {
                const std::string alt =
                    std::string(pf) + "\\NVIDIA Corporation\\NVSMI\\nvml.dll";
                dll = LoadLibraryA(alt.c_str());
            }
        }
        if (!dll) return false;

        bind(init,        "nvmlInit_v2");
        bind(shutdown,    "nvmlShutdown");
        bind(getCount,    "nvmlDeviceGetCount_v2");
        bind(getHandle,   "nvmlDeviceGetHandleByIndex_v2");
        bind(getName,     "nvmlDeviceGetName");
        bind(getTemp,     "nvmlDeviceGetTemperature");
        bind(getUtil,     "nvmlDeviceGetUtilizationRates");
        bind(getClock,    "nvmlDeviceGetClockInfo");
        bind(getPower,    "nvmlDeviceGetPowerUsage");
        bind(getMemory,   "nvmlDeviceGetMemoryInfo");
        bind(getThrottle, "nvmlDeviceGetCurrentClocksThrottleReasons");
        bind(getPowerLimit,   "nvmlDeviceGetPowerManagementLimit");
        bind(getDefaultLimit, "nvmlDeviceGetPowerManagementDefaultLimit");
        bind(getLimitRange,   "nvmlDeviceGetPowerManagementLimitConstraints");
        bind(getBar1,         "nvmlDeviceGetBAR1MemoryInfo");

        if (!init || !getHandle) { close(); return false; }
        if (init() != 0)         { close(); return false; }

        unsigned int count = 0;
        if (!getCount || getCount(&count) != 0 || count == 0) { close(); return false; }
        if (getHandle(0, &device) != 0)                       { close(); return false; }

        if (getName) {
            char buf[96] = {0};
            if (getName(device, buf, sizeof(buf)) == 0) name = buf;
        }
        ready = true;
        return true;
    }

    void close() {
        if (dll) {
            if (ready && shutdown) shutdown();
            FreeLibrary(dll);
            dll = nullptr;
        }
        ready = false;
        device = nullptr;
    }
};

// ============================================================================
//  PDH sayaclari
// ----------------------------------------------------------------------------
//  Sayac adlari yerellestirilmistir (Turkce Windows'ta "Islemci Bilgisi").
//  Bu yuzden ADA gore degil, INDEKSE gore aciyoruz: PdhAddCounter yerine
//  PdhAddEnglishCounter kullanmak tam olarak bu sorunu cozer.
// ============================================================================
struct Pdh {
    PDH_HQUERY   query   = nullptr;
    PDH_HCOUNTER perf    = nullptr;
    PDH_HCOUNTER parked  = nullptr;
    PDH_HCOUNTER usage   = nullptr;
    PDH_HCOUNTER diskIdle = nullptr;
    PDH_HCOUNTER diskLat  = nullptr;
    PDH_HCOUNTER diskBps  = nullptr;
    bool ready = false;

    bool open() {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return false;

        const bool a = PdhAddEnglishCounterW(query,
            L"\\Processor Information(_Total)\\% Processor Performance",
            0, &perf) == ERROR_SUCCESS;
        const bool b = PdhAddEnglishCounterW(query,
            L"\\Processor Information(_Total)\\Parking Status",
            0, &parked) == ERROR_SUCCESS;
        // Klasik islemci kullanimi. "% Processor Utility" degil bilerek:
        // Utility boost'u hesaba katip %100'un ustune cikabiliyor ve
        // kullanicinin Gorev Yoneticisi'nde gordugu sayiyla ortusmuyor.
        const bool c = PdhAddEnglishCounterW(query,
            L"\\Processor Information(_Total)\\% Processor Time",
            0, &usage) == ERROR_SUCCESS;

        PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\% Idle Time",
                              0, &diskIdle);
        PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\Avg. Disk sec/Transfer",
                              0, &diskLat);
        PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\Disk Bytes/sec",
                              0, &diskBps);

        if (!a && !b && !c) { close(); return false; }

        // Ilk toplama taban olusturur; oran sayaclari ikinci okumada anlamlanir.
        PdhCollectQueryData(query);
        ready = true;
        return true;
    }

    void close() {
        if (query) PdhCloseQuery(query);
        query = nullptr;
        ready = false;
    }

    double read(PDH_HCOUNTER c) {
        if (!c) return kUnknown;
        PDH_FMT_COUNTERVALUE v{};
        if (PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE, nullptr, &v)
                != ERROR_SUCCESS) return kUnknown;
        return v.doubleValue;
    }
};

// ============================================================================
//  ACPI termal bolge — WMI (root\WMI, MSAcpi_ThermalZoneTemperature)
// ----------------------------------------------------------------------------
//  DURUSTLUK NOTU: Bu deger islemci die (Tctl/Tdie) sicakligi DEGILDIR.
//  ACPI'nin bildirdigi termal bolgedir; dizustulerde genelde vardir ve
//  anlamlidir, masaustlerinde cogu zaman ya hic yoktur ya da kasa/anakart
//  sensorunu bildirir. Bu yuzden arayuzde "CPU sicakligi" diye degil
//  "sistem sicakligi (ACPI)" diye gosterilir.
//
//  Gercek Tctl/Tdie MSR okumasi ring 0 ister — HWiNFO bu yuzden kendi
//  cekirdek surucusunu kurar. Bizde v1.0'a ertelendi.
//
//  Baglanti ipliğe bagli: COM apartmanı ornekleyici ipliginde bir kez
//  kurulur ve sorgular hep o iplikten yapilir.
// ============================================================================
class AcpiThermal {
public:
    bool open() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return false;
        comReady_ = true;

        // Guvenlik seviyesi surec basina bir kez ayarlanir; baska bir bilesen
        // zaten ayarladiysa RPC_E_TOO_LATE doner ve bu bizim icin hata degil.
        const HRESULT sec = CoInitializeSecurity(
            nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr, EOAC_NONE, nullptr);
        if (FAILED(sec) && sec != RPC_E_TOO_LATE) { close(); return false; }

        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr,
                CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                reinterpret_cast<void**>(&locator_)))) { close(); return false; }

        BSTR ns = SysAllocString(L"ROOT\\WMI");
        const HRESULT hr = locator_->ConnectServer(ns, nullptr, nullptr, nullptr,
                                                   0, nullptr, nullptr, &services_);
        SysFreeString(ns);
        if (FAILED(hr) || !services_) { close(); return false; }

        CoSetProxyBlanket(services_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                          nullptr, RPC_C_AUTHN_LEVEL_CALL,
                          RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        return true;
    }

    void close() {
        if (services_) { services_->Release(); services_ = nullptr; }
        if (locator_)  { locator_->Release();  locator_  = nullptr; }
        if (comReady_) { CoUninitialize(); comReady_ = false; }
    }

    // Birden fazla termal bolge varsa EN YUKSEGI dondurulur: kullaniciyi
    // ilgilendiren sicak olan bolgedir, ortalamasi degil.
    double read() {
        if (!services_) return kUnknown;

        BSTR lang  = SysAllocString(L"WQL");
        BSTR query = SysAllocString(
            L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
        IEnumWbemClassObject* it = nullptr;
        const HRESULT hr = services_->ExecQuery(
            lang, query,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &it);
        SysFreeString(lang);
        SysFreeString(query);
        if (FAILED(hr) || !it) return kUnknown;

        double best = kUnknown;
        IWbemClassObject* obj = nullptr;
        ULONG got = 0;
        while (it->Next(WBEM_INFINITE, 1, &obj, &got) == S_OK && got == 1) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(obj->Get(L"CurrentTemperature", 0, &v, nullptr, nullptr))
                && (v.vt == VT_I4 || v.vt == VT_UI4)) {
                // Deger onda bir KELVIN cinsinden gelir.
                const double c = (v.vt == VT_I4 ? v.lVal : v.ulVal) / 10.0 - 273.15;
                // Anlamsiz degerleri ele: bazi firmware'ler 0 K ya da sabit
                // 2732 (=0 C) bildiriyor.
                if (c > 1.0 && c < 130.0 && c > best) best = c;
            }
            VariantClear(&v);
            obj->Release();
            obj = nullptr;
        }
        it->Release();
        return best;
    }

private:
    bool             comReady_ = false;
    IWbemLocator*    locator_  = nullptr;
    IWbemServices*   services_ = nullptr;
};

} // namespace

// ============================================================================
// ----------------------------------------------------------------------------
//  Ornekleyici cekirdegi
// ----------------------------------------------------------------------------
//  TASARIM: Tek bir kalici iplik saniyede bir TUM kaynaklari okur ve son
//  ornegi saklar. readNow() yalnizca bu onbellegi dondurur.
//
//  Neden: WMI/COM baglantisi ipliğe baglidir ve her cagrida yeniden kurmak
//  pahalidir. Ayrica arayuz ipligi ile kayit ipligi ayni sayaclari es zamanli
//  okursa PDH oran sayaclari bozulur. Tek okuyucu iplik ikisini de cozer.
//  start()/stop() yalnizca "kaydediliyor mu" bayragini degistirir.
struct Sampler::Impl {
    Nvml        nvml;
    Pdh         pdh;
    AcpiThermal acpi;
    bool        acpiOk = false;

    std::thread        worker;
    std::atomic<bool>  alive{false};
    std::atomic<bool>  recording{false};
    std::atomic<uint32_t> tick{0};

    mutable std::mutex  mutex;
    Sample              latest;
    std::vector<Sample> samples;
};

Sampler::Sampler() : impl_(new Impl()) {
    if (impl_->nvml.open()) {
        caps_.nvidiaGpu = true;
        caps_.gpuName   = impl_->nvml.name;
    }
    if (impl_->pdh.open()) caps_.cpuPerf = true;

    if (!caps_.nvidiaGpu)
        caps_.note = "GPU telemetrisi okunamıyor (NVIDIA sürücüsü bulunamadı; "
                     "AMD desteği henüz yok). ";

    // --- GPU sabitleri: guc limiti ve Resizable BAR ---
    if (impl_->nvml.ready) {
        Nvml& n = impl_->nvml;
        gpu_.known = true;
        gpu_.name  = n.name;

        unsigned int mw = 0;
        if (n.getPowerLimit && n.getPowerLimit(n.device, &mw) == 0)
            gpu_.powerLimitW = mw / 1000.0;
        if (n.getDefaultLimit && n.getDefaultLimit(n.device, &mw) == 0)
            gpu_.defaultPowerLimitW = mw / 1000.0;
        unsigned int lo = 0, hi = 0;
        if (n.getLimitRange && n.getLimitRange(n.device, &lo, &hi) == 0)
            gpu_.maxPowerLimitW = hi / 1000.0;

        nvmlMemory_t mem{};
        if (n.getMemory && n.getMemory(n.device, &mem) == 0)
            gpu_.vramTotalMb = mem.total / (1024ull * 1024ull);

        nvmlBAR1Memory_t bar{};
        if (n.getBar1 && n.getBar1(n.device, &bar) == 0)
            gpu_.bar1TotalMb = bar.bar1Total / (1024ull * 1024ull);

        // Resizable BAR acikken BAR1 penceresi VRAM'in tamamini kapsar.
        // Kapaliyken eski PCI standardi geregi 256 MB'ta kalir. Esigi
        // yarida tutuyoruz: bazi kartlar VRAM'in bir miktar altini bildirir.
        if (gpu_.vramTotalMb > 0 && gpu_.bar1TotalMb > 0) {
            const bool on = gpu_.bar1TotalMb >= gpu_.vramTotalMb / 2;
            gpu_.resizableBar = on ? GpuStatic::Tri::Yes : GpuStatic::Tri::No;
            gpu_.rebarNote =
                "BAR1 penceresi " + std::to_string(gpu_.bar1TotalMb) +
                " MB, VRAM " + std::to_string(gpu_.vramTotalMb) + " MB. " +
                (on ? "Resizable BAR açık."
                    : "Resizable BAR KAPALI — BIOS'ta 'Above 4G Decoding' ve "
                      "'Re-Size BAR Support' açılırsa bazı oyunlarda %5-10 "
                      "kazanç olur.");
        }
    }

    // Kalici okuyucu iplik. COM ve WMI baglantisi BU iplikte kurulur ve
    // yalnizca buradan kullanilir.
    impl_->alive.store(true);
    impl_->worker = std::thread([this]() {
        impl_->acpiOk = impl_->acpi.open();

        while (impl_->alive.load()) {
            Sample s = sampleOnce(impl_->tick.load());
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->latest = s;
                if (impl_->recording.load()) impl_->samples.push_back(s);
            }
            if (impl_->recording.load()) impl_->tick.fetch_add(1);

            for (int i = 0; i < 10 && impl_->alive.load(); ++i) Sleep(100);
        }
        impl_->acpi.close();
    });

    // Ilk ornegin gelmesini kisa sure bekle ki arayuz bos acilmasin.
    for (int i = 0; i < 20; ++i) {
        Sleep(25);
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->latest.tSec != 0 || known(impl_->latest.ramUsedPct)) break;
    }

    caps_.cpuTemp = known(readNow().cpuTempC);
    if (!caps_.cpuTemp)
        caps_.note += "İşlemci sıcaklığı okunamıyor: bu makinede ACPI termal "
                      "bölgesi yok. Gerçek die sıcaklığı için çekirdek "
                      "sürücüsü gerekir; uydurma değer gösterilmez.";
}

Sampler::~Sampler() {
    if (impl_) {
        impl_->recording.store(false);
        impl_->alive.store(false);
        if (impl_->worker.joinable()) impl_->worker.join();
        impl_->nvml.close();
        impl_->pdh.close();
        delete impl_;
    }
}

Sample Sampler::readNow(uint32_t /*tSec*/) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->latest;
}

Sample Sampler::sampleOnce(uint32_t tSec) {
    Sample s;
    s.tSec = tSec;

    if (impl_->nvml.ready) {
        Nvml& n = impl_->nvml;
        unsigned int u = 0;
        if (n.getTemp  && n.getTemp(n.device, NVML_TEMPERATURE_GPU, &u) == 0)
            s.gpuTempC = static_cast<double>(u);
        if (n.getClock && n.getClock(n.device, NVML_CLOCK_GRAPHICS, &u) == 0)
            s.gpuClockMhz = static_cast<double>(u);
        if (n.getPower && n.getPower(n.device, &u) == 0)
            s.gpuPowerW = static_cast<double>(u) / 1000.0;   // mW -> W

        nvmlUtilization_t util{};
        if (n.getUtil && n.getUtil(n.device, &util) == 0)
            s.gpuUtilPct = static_cast<double>(util.gpu);

        nvmlMemory_t mem{};
        if (n.getMemory && n.getMemory(n.device, &mem) == 0)
            s.gpuMemUsedMb = static_cast<double>(mem.used) / (1024.0 * 1024.0);

        unsigned long long reasons = 0;
        if (n.getThrottle && n.getThrottle(n.device, &reasons) == 0) {
            s.thermalThrottle = (reasons & (kThrottleSwThermalSlowdown |
                                            kThrottleHwThermalSlowdown)) != 0;
            s.powerCapThrottle = (reasons & kThrottleSwPowerCap) != 0;
            s.powerBrake       = (reasons & kThrottleHwPowerBrake) != 0;
        }
    }

    if (impl_->pdh.ready) {
        PdhCollectQueryData(impl_->pdh.query);
        s.cpuPerfPct    = impl_->pdh.read(impl_->pdh.perf);
        s.coreParkedPct = impl_->pdh.read(impl_->pdh.parked);
        s.cpuUsagePct   = impl_->pdh.read(impl_->pdh.usage);

        const double idle = impl_->pdh.read(impl_->pdh.diskIdle);
        if (known(idle)) {
            double active = 100.0 - idle;
            if (active < 0.0)   active = 0.0;
            if (active > 100.0) active = 100.0;
            s.diskActivePct = active;
        }
        const double lat = impl_->pdh.read(impl_->pdh.diskLat);
        if (known(lat)) s.diskLatencyMs = lat * 1000.0;   // saniye -> ms
        const double bps = impl_->pdh.read(impl_->pdh.diskBps);
        if (known(bps)) s.diskMbPerSec = bps / (1024.0 * 1024.0);
    }

    if (impl_->acpiOk) s.cpuTempC = impl_->acpi.read();

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        s.ramUsedPct   = static_cast<double>(ms.dwMemoryLoad);
        s.availPhysMb  = static_cast<double>(ms.ullAvailPhys) / (1024.0 * 1024.0);
    }

    // GetPerformanceInfo taahhut (commit charge) bilgisini verir. Bu, oyun
    // "16 GB RAM'e 17 GB istedi" durumunu goren TEK olcudur; dwMemoryLoad
    // %100'e dayanmadan da sistem sayfalamaya baslayabilir.
    PERFORMANCE_INFORMATION pi{};
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi)) && pi.CommitLimit > 0) {
        const double page   = static_cast<double>(pi.PageSize);
        const double commit = static_cast<double>(pi.CommitTotal) * page;
        const double limit  = static_cast<double>(pi.CommitLimit) * page;
        const double phys   = static_cast<double>(pi.PhysicalTotal) * page;

        s.commitUsedPct = commit * 100.0 / limit;
        s.commitOverRam = (phys > 0.0) && (commit > phys);
    }

    return s;
}

void Sampler::start() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->samples.clear();
    impl_->tick.store(0);
    impl_->recording.store(true);
}

void Sampler::stop() {
    if (impl_) impl_->recording.store(false);
}

std::vector<Sample> Sampler::samples() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->samples;
}

} // namespace sstelem

#endif // _WIN32
