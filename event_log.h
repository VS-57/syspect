// ============================================================================
//  Syspect — Windows olay gunlugu okuyucu (Event Viewer'in okudugu veri)
//  ----------------------------------------------------------------------
//  Kare sureleri bize "ne zaman takildi"yi soyler; olay gunlugu "sistem o
//  sirada neyi sikayet etti"yi soyler. Ikisinin kesisimi teshisin kendisidir.
//
//  Neden ETW degil de olay gunlugu:
//    - Yonetici hakki GEREKMEZ (System kanali herkese acik; yalnizca Security
//      kanali yukseltme ister). Program yonetici olmadan aciksa bile bu veri
//      okunabilir.
//    - Gecmise bakar. ETW yalnizca dinlerken olani gorur; olay gunlugu 30 gun
//      once yasanan mavi ekrani da verir.
//    - Ucuzdur. Birkac XPath sorgusu, olculebilir yuk yok.
//
//  Neyi VERMEZ (yanlis beklenti kurmamak icin):
//    - DPC suclusu (.sys). Olay gunlugune hic yazilmaz, ETW sistem logger'i
//      gerekir. Tek istisna DPC_WATCHDOG_VIOLATION mavi ekranidir, o da
//      dump'tan okunur.
//    - Golgelendirici derlemesi. Hicbir kanalda karsiligi yok.
//    - Kare seviyesinde hicbir sey.
//
//  ------------------------------------------------------------------------
//  GURULTU UYARISI — tasarim kurali 3'un dogrudan konusu
//  ------------------------------------------------------------------------
//  Olay gunlugu saglikli makinede bile doludur. WHEA ID 17 bircok AMD
//  sisteminde semptomsuz binlerce kez uretilir; tek bir "Disk 153" hicbir sey
//  ifade etmez. Bu yuzden okuyucu ham sayi degil UC pencere birden verir
//  (24 saat / 7 gun / 30 gun) ve her olayin zaman damgasini saklar.
//
//  Kural motoru bunlari ikiye ayirmali:
//    - Olcum penceresiyle CAKISAN olay  -> sinyal
//    - Yalnizca gecmiste duran olay     -> baglam
//
//  Sayilar tek basina asla hukum olmamalidir.
// ============================================================================
#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <vector>

#include "core.h"     // SystemInfo — tasinabilir motorun bekledigi yapi

namespace sslog {

// ----------------------------------------------------------------------------
//  Olay turleri
// ----------------------------------------------------------------------------
//  Ham (saglayici, kimlik) ciftleri yerine anlam tasiyan kategoriler. Motorun
//  "nvlddmkm mi amdkmdag mi" bilmesine gerek yok; "ekran surucusu sifirlandi"
//  bilmesi yeterli.
enum class Kind {
    KernelPower41,      // kontrolsuz kapanma — kayit tutulamadan guc kesildi
    WheaFatal,          // ID 18 — donanim hatasi, duzeltilemedi
    WheaCorrected,      // ID 17/19/46/47 — duzeltildi, cogu zaman zararsiz
    BugCheck,           // ID 1001 — mavi ekran; kod metnin icinde
    Tdr,                // ID 4101 — ekran surucusu yanit vermedi, sifirlandi
    StorageReset,       // ID 129 — denetleyiciye sifirlama gonderildi
    StorageRetry,       // ID 153 — G/C yeniden denendi
    StorageBadBlock,    // ID 7/11/51 — hatali blok / denetleyici hatasi
    NtfsCorruption,     // ID 55 — dosya sistemi bozulmasi
    UnexpectedShutdown, // ID 6008 — beklenmedik kapanma
    AppCrash,           // Application kanali ID 1000 — uygulama cokmesi
    Count_
};

const char* kindLabel(Kind k);       // kullaniciya donuk Turkce etiket
bool        kindIsSevere(Kind k);    // tek basina bile dikkate deger mi

// Makine tarafindan okunan sabit anahtar ("tdr", "kernel_power_41").
// core.h'deki causeKey ile ayni gerekce: kindLabel kullanici metnidir ve
// degisebilir; .syscap dosyasi bu anahtara baglanir.
const char* kindKey(Kind k);
Kind        kindFromKey(const std::string& key);   // bulunamazsa Kind::Count_

// ----------------------------------------------------------------------------
//  Tek bir olay
// ----------------------------------------------------------------------------
struct Event {
    Kind        kind = Kind::Count_;
    uint32_t    eventId = 0;
    std::string provider;
    uint64_t    fileTimeUtc = 0;   // FILETIME (100 ns birim, 1601 baslangicli)
    std::string detail;            // TDR'de surucu adi, BugCheck'te kod...
};

// ----------------------------------------------------------------------------
//  Bir tur icin ozet
// ----------------------------------------------------------------------------
//  Uc pencere birden tutuluyor cunku "30 gunde 400 tane" ile "son 24 saatte
//  400 tane" tamamen farkli iki durumdur. Ilki taban gurultusu, ikincisi
//  yeni baslamis bir sorun.
struct Series {
    Kind     kind = Kind::Count_;
    uint32_t last24h = 0;
    uint32_t last7d  = 0;
    uint32_t last30d = 0;

    uint64_t newestFileTime = 0;   // 0 = hic gorulmedi
    uint64_t oldestFileTime = 0;

    // Ornek olaylar — rapora yazmak icin. Tamami degil, en yeni birkaci.
    std::vector<Event> samples;

    bool empty() const { return last30d == 0; }

    // Gunluk ortalamaya gore son 24 saat anormal mi?
    // Taban cizgisi karsilastirmasi: 30 gunluk ortalamanin uc kati ve en az
    // 3 olay. Esik keyfi degil — ID 17 gibi surekli gurultu uretenlerde
    // "her gun 40 tane" normaldir, "bugun 400 tane" degildir.
    bool spikingToday() const {
        if (last24h < 3) return false;
        const double perDay = last30d / 30.0;
        return perDay <= 0.0 ? true : (last24h > perDay * 3.0);
    }
};

// ----------------------------------------------------------------------------
//  Tarama sonucu
// ----------------------------------------------------------------------------
struct Scan {
    // DIKKAT — bu ikisi ayni sey degildir:
    //   attempted : tarama calisti mi (arayuz acilista arka planda tariyor)
    //   ok        : tarama basarili oldu mu
    // Ayrimi olmadan, tarama daha bitmeden Bulgular sayfasi "olay gunlugu
    // okunamadi" diye yaziyordu. Henuz denenmemis olmak hata degildir.
    bool        attempted = false;
    bool        ok = false;
    std::string error;          // okunamadiysa sebep (kullaniciya gosterilir)

    Series series[static_cast<size_t>(Kind::Count_)];

    // LiveKernelReports: mavi ekran vermeden cokup toparlanan surucu kaydi.
    // Olay gunlugunde degil, dosya sisteminde durur. En sik uretici ekran
    // surucusudur ve TDR ile birlikte gorulur.
    uint32_t liveKernelReports = 0;
    std::string newestLiveKernelReport;

    uint32_t totalRead = 0;     // taranan olay sayisi (tani amacli)

    const Series& at(Kind k) const {
        return series[static_cast<size_t>(k)];
    }
};

// ----------------------------------------------------------------------------
//  Tarama
// ----------------------------------------------------------------------------
//  days: geriye kac gun bakilacak. 30 varsayilan; daha uzun pencere yavaslatir
//  ve taban cizgisini bulanistirir.
Scan scan(uint32_t days = 30);

// ----------------------------------------------------------------------------
//  Zaman cakismasi
// ----------------------------------------------------------------------------
//  Bir turun olcum penceresiyle cakisip cakismadigini soyler. Kural motorunun
//  "sinyal mi baglam mi" ayrimini yapabilmesi icin sart olan fonksiyon.
//
//  windowStartUtc / windowEndUtc: FILETIME. slackSec olcumun oncesine ve
//  sonrasina eklenen pay — surucu sifirlanmasi kaydin bitiminden birkac saniye
//  sonra gunluge dusebilir.
uint32_t countInWindow(const Series& s,
                       uint64_t windowStartUtc,
                       uint64_t windowEndUtc,
                       uint32_t slackSec = 30);

// Su anin FILETIME degeri — cagiran tarafin windows.h'a bulasmamasi icin.
uint64_t nowFileTime();

// ----------------------------------------------------------------------------
//  Taramayi kural motorunun anladigi yapiya cevir
// ----------------------------------------------------------------------------
//  Arayuz ve komut satiri ayni esleme mantigini kullansin diye burada;
//  iki yerde ayri yazilsaydi biri guncellenip oteki unutulurdu.
//
//  winStart/winEnd olcum penceresi (FILETIME). Verilirse TDR ve depolama
//  olaylarinin olcumle CAKISIP cakismadigi hesaplanir — kural motorunun
//  "sinyal mi baglam mi" ayrimi buna dayanir. Sifir birakilirsa yalnizca
//  30 gunluk sayimlar tasinir.
void applyTo(ss::SystemInfo& sys, const Scan& log,
             uint64_t winStartUtc = 0, uint64_t winEndUtc = 0);

} // namespace sslog

#endif // _WIN32
