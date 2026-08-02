// ============================================================================
//  Syspect — telemetriden yan sinyal saglayicisi
//  ----------------------------------------------------------------------
//  Takilma dedektoru bir olay urettiginde motorun sordugu "o anda ne
//  oluyordu?" sorusunu, saniyede bir toplanan telemetri orneklerinden
//  cevaplar.
//
//  KAPSAM — ne veriyor, ne VERMIYOR:
//    veriyor : GPU kullanimi, GPU kisitlama bayraklari (isinma / guc limiti
//              / donanimsal frenleme), VRAM doygunlugu, CPU performans
//              orani, cekirdek park orani, disk bekleme suresi, oyunun
//              basindan bu yana gecen dakika
//    VERMIYOR: DPC suresi ve suclu .sys adi, hard page fault sayisi,
//              arka plan surec adi. Bunlar ETW sistem logger'i ister ve
//              henuz yazilmadi — ilgili alanlar SIFIR degil, "olculmedi"
//              olarak birakilir (bkz. dpcMeasured / memoryMeasured).
//
//  ORNEKLEME FARKI: Telemetri 1 Hz, kareler ~100-500 Hz. Bir takilma anina
//  en yakin ornek secilir. Bu, milisaniyelik bir DPC sivrisini yakalamaya
//  yetmez — ama zaten DPC'yi buradan olcmuyoruz. Isinma, guc limiti ve disk
//  doygunlugu saniyeler suren olgulardir; 1 Hz onlar icin yeterlidir.
// ============================================================================
#pragma once

#ifdef _WIN32

#include "frame_source.h"
#include "telemetry.h"

#include <vector>

namespace sstelem {

class TelemetrySignals : public ss::ISignalProvider {
public:
    // vramTotalMb 0 ise VRAM doygunluk sinyali uretilmez.
    TelemetrySignals(std::vector<Sample> samples, uint64_t vramTotalMb)
        : samples_(std::move(samples)), vramTotalMb_(vramTotalMb) {}

    ss::SignalSnapshot at(uint64_t timestampUs) const override;

private:
    std::vector<Sample> samples_;
    uint64_t            vramTotalMb_ = 0;
};

} // namespace sstelem

#endif // _WIN32
