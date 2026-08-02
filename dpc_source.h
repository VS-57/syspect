// ============================================================================
//  Syspect — DPC suclusu tespiti
//  ----------------------------------------------------------------------
//  Kullanicinin tarifi: "oyunda anlik takiliyor, tam o anda kulakliktan pit
//  pit ses geliyor." Bu, bir surucunun DPC seviyesinde islemciyi cok uzun
//  tutmasinin klasik imzasidir — ayni anda hem kare hem ses tamponu kaciyor.
//
//  Olay gunlugu bu veriyi VERMEZ. Tek kaynak ETW cekirdek oturumudur.
//
//  ------------------------------------------------------------------------
//  BU DOSYANIN EN ONEMLI KISMI: KONTROL GRUBU
//  ------------------------------------------------------------------------
//  Her sistemde saniyede binlerce DPC calisir. "Takilma aninda nvlddmkm'nin
//  DPC'si vardi" cumlesi TEK BASINA hicbir sey ifade etmez — nvlddmkm'nin
//  DPC'si her an vardir.
//
//  CLAUDE.md tasarim kurali 3'un dogrudan uyarisi bu: kontrol grubu olmadan
//  her takilmaya bir .sys yapistirilir ve KURAL 1 sahte bir %80'e oturur.
//  Kullanici calisan bir surucuyu kaldirir.
//
//  Bu yuzden her surucu icin IKI oran tutuluyor:
//
//      pencere ici : takilma anlarinda saniyede kac uzun DPC
//      taban       : takilma DISINDA saniyede kac uzun DPC
//
//  Bir surucu ancak pencere ici orani tabanin belirgin ustundeyse suclanir.
//  Surekli uzun DPC yapan ama takilmalarla ilgisi olmayan bir surucu
//  (tipik olarak depolama ve ag surucularinde gorulur) boylece elenir.
//
//  ------------------------------------------------------------------------
//  Riskler — bilerek kayda geciyorum
//  ------------------------------------------------------------------------
//  1) NT Kernel Logger sistemde TEK olabilir. xperf, WPR ya da bazi guvenlik
//     yazilimlari aciksa oturum acilmaz. Hata SESSIZ gecilmez, bildirilir.
//  2) DPC olay hacmi yuksektir. Present oturumunu bogmamasi icin ayri bir
//     oturum ve ayri tamponlar kullaniliyor; yine de es zamanli calistirmadan
//     once olcum kalitesi dogrulanmali.
//  3) Yonetici hakki sart.
// ============================================================================
#pragma once

#ifdef _WIN32

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ssdpc {

// Tek bir surucunun DPC davranisi
struct DriverStats {
    std::string name;              // "nvlddmkm.sys"

    uint64_t longCount     = 0;    // esigi asan DPC sayisi (toplam)
    uint64_t longInWindow  = 0;    // takilma pencerelerinde olanlar
    double   maxMs         = 0.0;  // gorulen en uzun DPC
    double   totalMs       = 0.0;  // esigi asanlarin toplam suresi

    // Oranlar. Payda SANIYEDIR, olay sayisi degil: pencereler toplam sureden
    // cok daha kisa oldugu icin ham sayilari karsilastirmak yanlis olurdu.
    double   inWindowRate  = 0.0;  // pencere ici: uzun DPC / saniye
    double   baselineRate  = 0.0;  // pencere disi: uzun DPC / saniye

    // Kontrol grubu hukmu: pencere ici oran tabanin kac kati.
    // 1.0 = fark yok. Taban sifirsa ve pencere ici varsa buyuk bir sayi doner.
    double   liftOverBaseline() const {
        if (baselineRate <= 0.0) return inWindowRate > 0.0 ? 99.0 : 0.0;
        return inWindowRate / baselineRate;
    }
};

// Esigi asan tek bir DPC. HAM kayit tutuluyor, ozet degil.
// ----------------------------------------------------------------------------
//  Ilk tasarimda siniflandirma olay geldigi anda yapiliyordu ve bu YANLISTI:
//  takilma pencereleri ancak olcum BITTIKTEN sonra, kare verisi cozulunce
//  belli oluyor. Yani toplama sirasinda "bu olay pencere icinde mi" sorusu
//  cevaplanamaz. Ham kayit tutup sonradan siniflandirmak sart.
//
//  Hacim sorun degil: yalnizca esigi asanlar saklaniyor ve saglikli bir
//  makinede bu sayi sifira yakin — dogrulama kosusunda 18.666 DPC'nin
//  hicbiri esigi asmadi.
struct LongDpc {
    // HAM QPC — kendi oturumumuza gore degil, MUTLAK. Kare toplayicisi ayri
    // bir oturumda calisiyor ve onun sifir noktasi baska; goreli zaman
    // tutulursa iki taraf hizalanamaz ve surucu RASTGELE suclanir.
    // Ikisi de EVENT_HEADER::TimeStamp okuyor, yani ayni saat.
    uint64_t qpc          = 0;
    double   ms           = 0.0;
    uint32_t driverIndex  = 0;   // Capture::driverNames icine indis
};

struct Capture {
    bool        ok = false;
    std::string error;

    // Oturum gercekten veri gordu mu. Sifir olay "surucu sorunu yok" DEMEK
    // DEGILDIR — cogu zaman oturumun hic calismadigi anlamina gelir ve bu
    // ikisi karistirilirsa motor yanlis negatif uretir.
    uint64_t    totalDpcEvents = 0;
    uint64_t    unresolvedAddresses = 0;   // modul tablosunda bulunamayanlar

    double      durationSec      = 0.0;
    double      windowSecTotal   = 0.0;    // takilma pencerelerinin toplam suresi

    // Zaman tabani. Olaylarin qpc alani MUTLAK oldugu icin frekans disari
    // veriliyor; cagiran taraf kare zaman damgalarini bu birime cevirir.
    uint64_t    qpcFreq          = 0;

    std::vector<DriverStats> drivers;      // uzun DPC sayisina gore sirali

    // Esigi asan DPC'lerin toplam suresi / olcum suresi. Sistem genelinde
    // "DPC baskisi" gostergesi; tek surucuye bakmadan once buna bakilir.
    double      longDpcTimePct = 0.0;

    // Ham kayitlar — pencereler sonradan verilip yeniden ozetlenebilsin diye.
    std::vector<LongDpc>     events;
    std::vector<std::string> driverNames;
};

// ----------------------------------------------------------------------------
//  Kontrol grubu hesabi
// ----------------------------------------------------------------------------
//  Takilma pencereleri belli olduktan SONRA cagrilir; drivers[] alanini
//  pencere ici / taban oranlariyla yeniden kurar. run() zaten bir kez
//  cagiriyor (pencere verilmediyse bos listeyle), analiz katmani gercek
//  pencerelerle tekrar cagirir.
//
//  Pencereler MUTLAK QPC cinsindendir — bkz. LongDpc::qpc.
struct Window { uint64_t startQpc; uint64_t endQpc; };
void summarize(Capture& cap, const std::vector<Window>& windows);

// ----------------------------------------------------------------------------
//  Suclama esigi — TEK YER
// ----------------------------------------------------------------------------
//  Bir surucunun "suclu" sayilabilmesi icin gereken en az kanit. Esikler tek
//  bir yerde duruyor cunku bunlar teknik ayar degil, URUN KARARI: gevsetilirse
//  motor her takilmaya bir .sys yapistirir ve kullanici calisan bir surucuyu
//  kaldirir (tasarim kurali 3).
//
//    kMinLift        pencere ici oran, tabanin en az bu kati olmali.
//                    3x keyfi degil: 2x'te normal dalgalanma esigi asiyor,
//                    5x'te gercek vakalar eleniyor.
//    kMinInWindow    tek bir rastlanti suclama uretmesin.
//    kMinWindowSec   pencereler toplami bu kadar kisaysa oran anlamsiz.
constexpr double kMinLift      = 3.0;
constexpr uint64_t kMinInWindow = 3;
constexpr double kMinWindowSec = 0.05;

// Esikleri gecen surucuyu dondurur; yoksa nullptr.
// nullptr "surucu sorunu yok" DEMEK DEGILDIR — "suclayacak kadar kanit yok"
// demektir. Iki sey farklidir ve rapor da farkli soylemelidir.
const DriverStats* primeSuspect(const Capture& cap);

struct Options {
    uint32_t seconds = 30;

    // "Uzun" DPC esigi. 1 ms zaten ses tamponu ve kare zamanlamasi icin
    // sorunlu kabul edilen sinirdir; core.cpp KURAL 1 de ayni degeri
    // kullaniyor.
    double   longDpcMs = 1.0;

};

// Oturumu acar, verilen sure boyunca dinler, ozet dondurur.
// Yonetici hakki gerektirir.
Capture run(const Options& opts);

// Calisan bir toplamayi erkenden durdurur (baska bir iplikten cagrilabilir).
void stop();

// ---- Modul tablosu ---------------------------------------------------------
//  Cekirdek adresini surucu adina cevirir. ETW olayi bize DPC rutininin
//  ADRESINI verir; hangi .sys'e ait oldugunu bu tablo soyler.
//  Ayri acildi cunku yonetici hakki GEREKTIRMEZ ve tek basina test edilebilir.
class ModuleMap {
public:
    bool build();                                  // basarisizsa false
    std::string resolve(uint64_t address) const;   // bulunamazsa bos
    size_t size() const { return sorted_.size(); }

private:
    struct Entry { uint64_t base; std::string name; };
    std::vector<Entry> sorted_;                    // base'e gore artan
};

} // namespace ssdpc

#endif // _WIN32
