// ============================================================================
//  Syspect — surum denetimi
//  ----------------------------------------------------------------------
//  NE YAPAR: GitHub Releases'e tek bir GET atar, en son etiketi okur, kendi
//  surumuyle karsilastirir.
//
//  NE YAPMAZ — ve bu bilincli:
//
//    * Dosya INDIRMEZ. Kendini GUNCELLEMEZ.
//      Sebep: binary imzasiz dagitiliyor ve uygulama olcum icin yonetici
//      hakkiyla calisiyor. Yonetici sureste imzasiz bir exe indirip
//      calistirmak, kelimenin tam anlamiyla ayricalik yukseltme
//      zafiyetidir — ve zararli yazilimlarin birebir ayni deseni. Antivirus
//      de bu deseni isaretler. Yeni surum varsa kullaniciya SOYLENIR,
//      indirmeyi o yapar.
//
//    * Hicbir sey GONDERMEZ. Kimlik uretmez, saklamaz, yollamaz.
//      Istek duz bir GET; govdesi yok, sorgu parametresi yok. Sunucunun
//      gorebilecegi tek sey IP ve User-Agent — ki User-Agent'ta yalnizca
//      program adi ve surumu var, makineye ozgu hicbir sey yok.
//
//      Bu ayni zamanda ihtiyac duyulan tek "analitik": istekleri sunucu
//      tarafinda saymak kac kurulum oldugunu ve hangi surumlerin dolasimda
//      olduğunu verir. Kalici kimlik gerektirmez.
//
//  Kendi sunucumuz YOK: GitHub Releases API'si bu isi zaten yapiyor.
//  Kimlik dogrulamasiz istek siniri saatte 60/IP — acilista bir kez
//  denetleyen bir program icin fazlasiyla yeterli.
// ============================================================================
#pragma once

#ifdef _WIN32

#include <string>

namespace ssupd {

struct Result {
    bool        checked   = false;   // istek tamamlandi mi
    bool        available = false;   // daha yeni bir surum var mi
    std::string latestTag;           // "v0.3.0"
    std::string pageUrl;             // release sayfasi
    std::string error;               // bos degilse denetim basarisiz
};

// Aglayan cagri — arayuzde ARKA IPLIKTE calistirilmali.
// timeoutMs kucuk tutuluyor: guncelleme denetimi programin acilmasini
// geciktirmemeli, basarisiz olmasi da onemli degil.
Result check(uint32_t timeoutMs = 4000);

// "v0.3.0" > "0.2.0" ise true. Etiketteki bas harf 'v' goz ardi edilir,
// eksik bolumler sifir sayilir. Ayristirilamayan bir etiket "yeni degil"
// sayilir — supheli veriyle kullaniciya guncelleme onermek yanlis olur.
bool isNewer(const std::string& tag, const std::string& current);

// Tercih: denetim acik mi. Dil tercihiyle ayni yerde (HKCU) saklaniyor.
bool enabled();
void setEnabled(bool on);

} // namespace ssupd

#endif // _WIN32
