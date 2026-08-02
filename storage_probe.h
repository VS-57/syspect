// ============================================================================
//  Syspect — depolama envanteri ve saglik durumu (Windows'a ozgu)
//  ----------------------------------------------------------------------
//  PDH sayaclari diskin o an YAVAS oldugunu soyler. Bu modul diskin NE
//  OLDUGUNU soyler: donen disk mi, ne kadar dolu, uretici arizasini onceden
//  bildiriyor mu, NVMe ise ne kadar asinmis.
//
//  Ikisi ayri sorulardir ve karistirilmamalidir.
//
//  ------------------------------------------------------------------------
//  YORUM SINIRI — tasarim kurali 3'un dogrudan konusu
//  ------------------------------------------------------------------------
//  "SSD sagligi %56, takilmanin sebebi bu" cikarimi SUPHELIDIR. Asinma
//  yuzdesi diskin omrunun ne kadarini harcadigini soyler; yavaslayacagini ya
//  da takilma uretecegini SOYLEMEZ. %56 asinmis bir NVMe ilk gunku hiziyla
//  calisiyor olabilir.
//
//  Bu yuzden alanlar iki gruba ayrilmistir:
//
//    SEBEP olabilir : donen disk (HDD) uzerinde oyun, disk neredeyse dolu,
//                     ureticinin arizayi onceden bildirmesi (SMART esigi asti)
//    BAGLAM         : asinma yuzdesi, yazilan toplam veri, calisma saati,
//                     disk sicakligi
//
//  Ikinci grup rapora girer ama hicbir teshis kuralina agirlik VERMEZ.
//
//  ------------------------------------------------------------------------
//  Yonetici hakki
//  ------------------------------------------------------------------------
//  Aygit ozellikleri (model, veri yolu, donen disk mi) yonetici hakki
//  GEREKTIRMEZ. NVMe SMART kayitlari ve SMART ariza tahmini surucuye dogrudan
//  IOCTL ister; yukseltilmemis calisirken bu alanlar bos kalir ve "okunamadi"
//  diye isaretlenir — sifir yazilmaz.
// ============================================================================
#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <vector>

namespace ssstore {

// Okunamayan sayisal alan icin sentinel. 0 KULLANILMAZ: "%0 asinmis" ve
// "0 saat calismis" gecerli olculerdir. (core.h ve telemetry.h ile ayni gerekce.)
constexpr int32_t kUnknown = -1;

inline bool known(int32_t v) { return v >= 0; }

enum class Bus  { Unknown, Sata, Nvme, Usb, RaidOrVirtual };
enum class Kind { Unknown, Ssd, Hdd };

struct Volume {
    // Surucu harfi her zaman ASCII; genis karakter tutmanin anlami yok ve
    // rapor katmani UTF-8 calisiyor.
    std::string  letter;        // "C:"
    uint64_t     totalMb = 0;
    uint64_t     freeMb  = 0;
    bool         hasSystem = false;   // Windows bu bolumde mi

    double freePct() const {
        return totalMb ? (100.0 * static_cast<double>(freeMb) /
                                  static_cast<double>(totalMb)) : 0.0;
    }
};

struct Drive {
    uint32_t     index = 0;      // \\.\PhysicalDriveN
    std::string  model;          // "Samsung SSD 990 PRO 2TB"
    Bus          bus  = Bus::Unknown;
    Kind         kind = Kind::Unknown;
    uint64_t     sizeMb = 0;

    std::vector<Volume> volumes;  // bu diskteki bolumler

    // ---- Saglik: SEBEP olabilecek alan ----
    // Uretici "bu disk arizalanmak uzere" diyor mu. Tek bit; SMART esiklerinin
    // asilmasi demek. Okunamadiysa Unknown kalir.
    enum class Tri { Unknown, No, Yes };
    Tri failurePredicted = Tri::Unknown;

    // ---- Saglik: yalnizca BAGLAM ----
    // NVMe SMART (log sayfasi 0x02). SATA'da genellikle okunamaz.
    int32_t wearPct        = kUnknown;  // omrun harcanan yuzdesi
    int32_t spareLeftPct   = kUnknown;  // kalan yedek blok yuzdesi
    int32_t tempC          = kUnknown;
    int32_t powerOnHours   = kUnknown;
    int64_t writtenGb      = kUnknown;  // toplam yazilan veri
    bool    criticalWarning = false;    // NVMe kritik uyari bayragi

    bool smartRead = false;   // SMART verisi gercekten okunabildi mi

    bool hasSystemVolume() const {
        for (const auto& v : volumes) if (v.hasSystem) return true;
        return false;
    }
};

struct StorageScan {
    bool attempted = false;
    bool ok        = false;
    std::string error;

    // Yukseltilmemis calisirken model/tip okunur ama SMART okunmaz. Rapor
    // "SSD sagligi bilinmiyor" ile "SSD saglikli" arasindaki farki gostermek
    // zorunda; bu bayrak o ayrimi tasir.
    bool elevated = false;

    std::vector<Drive> drives;
};

StorageScan scan();

// Kullaniciya donuk etiketler
const char* busName(Bus b);
const char* kindName(Kind k);

} // namespace ssstore

#endif // _WIN32
