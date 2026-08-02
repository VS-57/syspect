// ============================================================================
//  StutterScope — sistem yoklamasi (Windows'a ozgu)
//  ----------------------------------------------------------------------
//  Kullaniciya sormadan, oyun performansini dogrudan etkileyen iki seyi
//  olcer:
//
//    1) Guc plani — "Dengeli" bile oyunlarda cekirdek park etme ve frekans
//       dalgalanmasi uretebilir. "Guc tasarrufu" ise felakettir.
//    2) Surucusu eksik/sorunlu aygitlar — Aygit Yoneticisi'ndeki sari unlem.
//       Ozellikle eksik yonga seti surucusu, guc yonetimini bozdugu icin
//       takilmanin dogrudan sebebi olabilir.
//
//  Ikisi de pasif okuma; hicbir ayar DEGISTIRILMEZ. Kullaniciya ne
//  yapacagini soyleriz, onun yerine yapmayiz.
// ============================================================================
#pragma once

#ifdef _WIN32

#include <string>
#include <vector>

namespace ssprobe {

enum class PowerPlan {
    Unknown,
    PowerSaver,        // Guc tasarrufu — oyun icin en kotusu
    Balanced,          // Dengeli — Windows varsayilani, park/frekans dalgalanmasi
    HighPerformance,   // Yuksek performans
    Ultimate,          // Nihai performans
    Custom,            // ureticiye ozel plan (Armoury Crate, Lenovo Vantage...)
};

struct PowerInfo {
    PowerPlan   plan = PowerPlan::Unknown;
    std::string friendlyName;    // planin gercek adi
    bool        onBattery = false;

    // Sistemde PIL VAR MI — "su an pilde mi" ile ayni sey degil. Prize takili
    // bir dizustu de dizustudur ve guc limiti kurallari onun icin de gecerli.
    // Masaustunde ekran karti limiti fabrika degerinin altindaysa sebep
    // farklidir (kullanici dusurmustur); dizustunde ureticinin TGP ayaridir.
    bool        hasBattery = false;

    // Oyun icin uyari gerekiyor mu ve neden
    bool        shouldWarn = false;
    std::string warning;
    std::string action;
};

struct ProblemDevice {
    std::string name;        // "NVIDIA GeForce RTX 4070 Ti"
    std::string cls;         // aygit sinifi ("Display", "System"...)
    uint32_t    problemCode = 0;
    std::string problemText; // kullaniciya donuk aciklama
    bool        driverMissing = false;   // hic surucu yuklu degil mi
};

struct DeviceScan {
    std::vector<ProblemDevice> problems;
    uint32_t totalDevices = 0;
    std::string note;
};

// ----------------------------------------------------------------------------
//  Bellek modulleri — SMBIOS Type 17
// ----------------------------------------------------------------------------
//  "EXPO/XMP acik mi?" sorusuna en saglam cevap SPD'yi okumak DEGIL (o SMBus
//  ve ring 0 ister), SMBIOS'un iki ayri alanini karsilastirmaktir:
//
//     Speed                 -> modulun DESTEKLEDIGI en yuksek hiz
//     ConfiguredMemorySpeed -> su an GERCEKTEN calistigi hiz
//
//  Ikisi esitse profil uygulanmis; yapilandirilmis hiz dusukse modul
//  yavas calisiyor demektir — yani profil KAPALI. Bu cikarim tahmin degil,
//  iki olculmus degerin karsilastirmasidir.
struct MemoryModule {
    std::string manufacturer;
    std::string partNumber;
    std::string locator;        // "DIMM_A1" gibi
    uint32_t    sizeMb = 0;
    uint32_t    maxSpeedMTs = 0;        // Speed
    uint32_t    configuredMTs = 0;      // ConfiguredMemorySpeed
    uint8_t     memoryType = 0;         // 0x1A=DDR4, 0x22=DDR5
};

struct MemorySpec {
    std::vector<MemoryModule> modules;
    uint32_t totalMb = 0;
    uint32_t configuredMTs = 0;   // modullerin ortak calisma hizi
    uint32_t maxSpeedMTs   = 0;   // modullerin destekledigi en yuksek hiz
    std::string typeName;         // "DDR4" / "DDR5"

    enum class Profile { Unknown, Off, On };
    Profile profile = Profile::Unknown;
    std::string profileNote;      // kullaniciya gosterilecek aciklama
    std::string profileLabel;     // "EXPO" (AMD) ya da "XMP" (Intel)

    // Islemci ureticisi. Etiketin (EXPO/XMP) kaynagi budur ama asil onemi
    // ACIKLAMA metinlerinde: "ozellikle Ryzen sistemlerde" cumlesi bir Intel
    // makinesinde yanlistir ve kullanicinin raporun kendisine ait olduguna
    // olan guvenini bozar. Ureticiyi BILMIYORSAK hicbir marka adi gecmemeli.
    enum class Cpu { Unknown, Amd, Intel };
    Cpu cpuVendor = Cpu::Unknown;

    bool singleChannelRisk = false;  // tek modul: her platformda FPS kaybi

    // ------------------------------------------------------------------
    //  Karisik modul takimi
    // ------------------------------------------------------------------
    //  Farkli hizda / boyutta / tipte modullerin bir arada takilmasi. Bellek
    //  denetleyicisi en yavas modulun hizina duser ve zamanlamalar iki takim
    //  arasinda uyusmadigi icin kararsizlik olasiligi belirgin artar. Modul
    //  BASINA Memtest gecmesi bu durumu ELEMEZ — sorun modullerin kendisinde
    //  degil, birlikte calismalarindadir.
    //
    //  Bu bayrak olmadan profil hukmu TEHLIKELI hale gelir: 2133 + 3600 karisik
    //  takimda "moduller 3600 destekliyor ama 2133'te calisiyor, XMP'yi acin"
    //  denirdi. O tavsiye sistemi calisir halden cikarir.
    bool mixedModules = false;
    std::string mixedModulesNote;

    // AM5 platformu mu (Ryzen 7000/8000/9000 masaustu). SMBIOS bunu dogrudan
    // sylemez; AMD + DDR5 birlesiminden cikarilir. Kural motorunun bellek
    // hizi tavani bu bilgiye dayanir.
    bool likelyAM5 = false;
    // Intel + DDR5. Gear 2 gecisi ve 4 DIMM tavani icin ayni islevi gorur.
    bool likelyIntelDdr5 = false;
};

// ----------------------------------------------------------------------------
//  Ureti yazilimi ve guvenlik ayarlari
// ----------------------------------------------------------------------------
//  Hepsi kayit defterinden okunur; yonetici hakki GEREKMEZ. Buradaki
//  ayarlarin ikisi oyun performansini dogrudan etkiler:
//
//  - Bellek Butunlugu (HVCI / "izole cekirdek"): sanallastirma tabanli kod
//    dogrulama. Windows 11'de cogu makinede VARSAYILAN ACIK gelir ve
//    oyunlarda tipik %5-15 kayip uretir. Kullanicilarin buyuk kismi boyle
//    bir ayarin varligindan haberdar bile degil.
//  - Donanim Hizlandirmali GPU Zamanlama (HAGS): kapali oldugunda bazi
//    sistemlerde kare zamanlamasi bozulur, acik oldugunda bazilarinda
//    bozulur — tek dogrusu yok, ama DEGERI bilinmeden tartisilamaz.
//
//  Secure Boot ve UEFI/Legacy dogrudan performans ayari degildir; vaka
//  paylasirken karsi tarafin ilk sordugu seyler oldugu icin raporda yer alir.
struct FirmwareInfo {
    enum class Tri { Unknown, Off, On };

    Tri  secureBoot     = Tri::Unknown;
    Tri  memoryIntegrity= Tri::Unknown;   // HVCI — "izole cekirdek"
    Tri  vbs            = Tri::Unknown;   // sanallastirma tabanli guvenlik
    Tri  gpuScheduling  = Tri::Unknown;   // HAGS
    Tri  gameMode       = Tri::Unknown;
    bool uefi           = false;
    bool firmwareKnown  = false;
};

// ----------------------------------------------------------------------------
//  Platforma gore niteleme cumleleri
// ----------------------------------------------------------------------------
//  Bellek hizinin ve kanal sayisinin oyun performansina etkisi platforma gore
//  degisir; metin de degismeli. Ikisi de CUMLE SONUNA eklenecek bir ek dondurur
//  ve uretici BILINMIYORSA bos string verir — yanlis marka adi vermektense hic
//  vermemek dogrudur. Arayuz ile system_probe ayni cumleyi iki yerde ayri
//  yazmasin diye burada.
const char* memorySpeedImpactNote(MemorySpec::Cpu v);
const char* singleChannelImpactNote(MemorySpec::Cpu v);

PowerInfo    readPowerPlan();
DeviceScan   scanProblemDevices();
MemorySpec   readMemorySpec();
FirmwareInfo readFirmwareInfo();

} // namespace ssprobe

#endif // _WIN32
