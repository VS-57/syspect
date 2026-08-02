// ============================================================================
//  Syspect — kayit dosyasi (.syscap)
//  ----------------------------------------------------------------------
//  Bir olcumun HAM verisini diske yazar ve geri okur.
//
//  NEDEN GEREKLI:
//    1) Kullanici foruma "iste ham kaydim" diye dosya birakabilir; karsi
//       taraf ayni programda acip kendi gozuyle bakar. Ekran goruntusu
//       tartismasi biter.
//    2) Gelistirme sirasinda ayni kayit tekrar tekrar analiz edilebilir —
//       kural degistiginde eski bir vakanin hukmunun degisip degismedigi
//       gorulur. Regresyon testlerinin gercek veriyle beslenmesi bu sayede
//       mumkun.
//    3) Yonetici hakki olmayan bir makinede (ya da otomatik testte) arayuz
//       yine de gercek veriyle calisir.
//
//  BICIM: satir tabanli duz metin, UTF-8. Ikili bicim bilincli olarak
//  secilmedi — kullanici dosyayi Not Defteri'nde acip ne paylastigini
//  gorebilmeli. Gizlilik acisindan da onemli: icinde sadece olcum var,
//  surec adi disinda kisisel veri yok.
// ============================================================================
#pragma once

#ifdef _WIN32

#include "frame_source.h"
#include "telemetry.h"
#include "event_log.h"

#include <string>
#include <vector>

namespace sscap {

struct Capture {
    std::string application;
    uint32_t    processId = 0;
    std::string cpuName, gpuName, osBuild, note, createdAt;
    uint64_t    ramInstalledMb = 0;
    uint64_t    vramTotalMb    = 0;

    std::vector<ss::FrameSample>  frames;
    std::vector<sstelem::Sample>  telemetry;

    // ------------------------------------------------------------------
    //  Olcumu ALAN makinenin olay gunlugu ozeti
    // ------------------------------------------------------------------
    //  Neden dosyada: kaydi foruma birakan kisinin karsi tarafi, kare
    //  olcumunu goruyor ama makinenin gecmisini goremiyordu — oysa teshisin
    //  yarisi orada. Bu alan olmadan .syscap acan kisi ya hicbir sey gorur
    //  ya da (daha kotusu) KENDI makinesinin gecmisini gorur.
    //
    //  Neden OZET: ham olaylar degil, tur basina sayimlar saklanir. Gurultulu
    //  bir makinede 20.000 WHEA kaydi vardir; hepsi yazilsaydi dosya da
    //  paylasim metni de kullanilamaz hale gelirdi. Dosyaya eklenen yuk
    //  sabittir: en fazla ~12 ozet satiri + 40 ornek olay.
    sslog::Scan evtlog;

    // Olcum penceresi (FILETIME). Olay gunlugu kayitlarinin olcumle CAKISIP
    // cakismadigini karsi tarafta da hesaplayabilmek icin sart.
    uint64_t    captureStartFt = 0;
    uint64_t    captureEndFt   = 0;
};

bool save(const std::wstring& path, const Capture& c, std::string& error);
bool load(const std::wstring& path, Capture& out, std::string& error);

} // namespace sscap

#endif // _WIN32
