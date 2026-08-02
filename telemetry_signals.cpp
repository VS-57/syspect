// ============================================================================
//  Syspect — telemetriden yan sinyal saglayicisi
// ============================================================================
#ifdef _WIN32

#include "telemetry_signals.h"

namespace sstelem {

ss::SignalSnapshot TelemetrySignals::at(uint64_t timestampUs) const {
    ss::SignalSnapshot s;
    if (samples_.empty()) return s;

    // Kare zaman damgasi olcumun basindan itibaren mikrosaniye; telemetri
    // ornekleri saniye. En yakin ornegi seciyoruz.
    const uint64_t sec = timestampUs / 1'000'000ull;
    size_t index = static_cast<size_t>(sec);
    if (index >= samples_.size()) index = samples_.size() - 1;
    const Sample& t = samples_[index];

    // --- Oyun zamani ---
    // Bu alan varsayilan olarak kUnknownMinute'tur ve golgelendirici derleme
    // kurali buna bakar. Artik gercekten biliyoruz: olcumun basindan bu yana
    // gecen sure. DIKKAT — bu "oyunun acilmasindan beri" degil, "olcumun
    // basindan beri". Kullanici oyunu acip 40 dakika sonra kayda baslarsa
    // ilk dakikalar golgelendirici derlemesi SANILMAMALI. Bu yuzden yalnizca
    // olcum oyunun ilk dakikalarini kapsiyorsa anlamli; su an bunu bilmenin
    // bir yolu olmadigindan alan BILEREK doldurulmuyor.
    //   s.minutesSinceGameStart = static_cast<uint32_t>(sec / 60);
    // Yanlis pozitif uretmemek icin sentinel korunuyor.

    // --- GPU ---
    if (known(t.gpuUtilPct)) s.gpuUtilPercent = t.gpuUtilPct;
    s.gpuThermalThrottle = t.thermalThrottle;
    s.gpuPowerBrake      = t.powerBrake;

    // VRAM doygunlugu: kartin bellegi dolduysa surucu sistem bellegine
    // tasmaya baslar ve bu takilma uretir. Esik %92 — ustunde kalan bosluk
    // surucunun kendi tamponlarina bile yetmez.
    if (vramTotalMb_ > 0 && known(t.gpuMemUsedMb)) {
        const double ratio = t.gpuMemUsedMb / static_cast<double>(vramTotalMb_);
        s.vramOverBudget = (ratio >= 0.92);
    }

    // --- CPU ---
    if (known(t.cpuPerfPct))    s.cpuPerfPercent  = t.cpuPerfPct;
    if (known(t.coreParkedPct)) s.coreParkedRatio = t.coreParkedPct / 100.0;

    // --- Disk ---
    // PDH "Avg. Disk sec/Transfer" ORTALAMA bekleme suresidir; ETW'nin
    // verecegi tekil en uzun bekleme degil. Motorun disk kurali esik
    // karsilastirmasi yaptigi icin ortalama da kullanilabilir, ama daha
    // muhafazakar davranir: kisa tekil sivriler gorunmez.
    if (known(t.diskLatencyMs)) s.diskWaitMs = t.diskLatencyMs;

    s.inGame = true;
    return s;
}

} // namespace sstelem

#endif // _WIN32
