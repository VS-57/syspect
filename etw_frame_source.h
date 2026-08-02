// ============================================================================
//  StutterScope — ETW kare kaynagi (Windows'a ozgu)
//  ----------------------------------------------------------------------
//  Microsoft-Windows-DxgKrnl saglayicisini gercek zamanli dinleyip Present
//  olaylarindan kare surelerini uretir. PresentMon.exe'ye ihtiyaci yoktur.
//
//  KAPSAM UYARISI (v0.1):
//  Bu toplayici Present olaylarinin ZAMANLARINI okur; PresentMon'un yaptigi
//  tam sunum-modeli takibini (flip zinciri, kuyruk, dropped/composed ayrimi)
//  YAPMAZ. Yani "kareler ne zaman sunuldu" sorusuna dogru, "bu kare ekranda
//  gercekten gorundu mu" sorusuna eksik cevap verir. Tam dogruluk icin
//  Intel PresentMon'un PresentData katmani entegre edilmelidir — CLAUDE.md
//  bunu oneriyor. Buradaki amac, boru hattinin ucundan ucuna calistigini
//  kanitlamak ve CSV bagimliligini kaldirmaktir.
//
//  YONETICI HAKKI SART. Gercek zamanli ETW oturumu acmak icin islem yukseltilmis
//  olmali; degilse startEtwCapture ERROR_ACCESS_DENIED ile doner ve hata
//  metninde bunu acikca soyler.
//
//  ANTI-CHEAT: Bu katman oyun surecine DOKUNMAZ. Hook yok, DLL yok, bellek
//  okuma yok — yalnizca isletim sisteminin yayinladigi olaylar pasif dinlenir.
// ============================================================================
#pragma once

#ifdef _WIN32

#include "frame_source.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ss {

struct EtwCaptureOptions {
    // 0 = tum surecler. Belirtilirse yalnizca o surecin kareleri toplanir.
    uint32_t processId = 0;

    // Yakalama suresi. 0 = stopEtwCapture cagrilana kadar.
    uint32_t seconds = 0;

    // Oturum adi. Ayni ada sahip yetim oturum varsa once kapatilir.
    std::string sessionName = "StutterScopeSession";
};

struct EtwCaptureResult {
    std::vector<FrameSample> frames;
    FrameSourceInfo          info;
    std::string              error;       // bos degilse yakalama basarisiz
    uint64_t                 rawEvents = 0;   // gelen toplam Present olayi
    uint64_t                 lostEvents = 0;  // ETW'nin dusurdugu olay sayisi

    // ------------------------------------------------------------------
    //  Zaman tabani — BASKA bir oturumla hizalamak icin sart
    // ------------------------------------------------------------------
    //  FrameSample::timestampUs bu oturumun ILK olayina goredir. DPC
    //  toplayicisi ayri bir oturumda calisiyor ve onun sifir noktasi baska.
    //  Iki tarafi hizalamadan "bu takilma sirasinda hangi DPC vardi" sorusu
    //  cevaplanamaz — hizalanmazsa surucu RASTGELE suclanir, ki bu urunun
    //  bir numarali riski.
    //
    //  Ikisi de EVENT_HEADER::TimeStamp okuyor, yani ayni QPC saati. Mutlak
    //  baslangici disari vermek hizalama icin yeterli.
    uint64_t                 firstQpc  = 0;   // ilk Present olayinin ham QPC'si
    uint64_t                 qpcFreq   = 0;   // tik/saniye

    bool ok() const { return error.empty(); }
};

// Yakalamayi calistirir ve BITENE KADAR bloklar (seconds kadar ya da
// stopEtwCapture cagrilana dek). Cagiran iplik bloke olur.
EtwCaptureResult runEtwCapture(const EtwCaptureOptions& opts);

// Suren yakalamayi disaridan durdurur (baska bir iplikten cagrilabilir).
void stopEtwCapture();

// Islem yukseltilmis mi? Kullaniciya anlamli hata vermek icin.
bool isElevated();

} // namespace ss

#endif // _WIN32
