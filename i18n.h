// ============================================================================
//  Syspect — dil katmani
//  ----------------------------------------------------------------------
//  TASARIM KARARI: anahtar icat edilmiyor, KAYNAK METNIN KENDISI anahtardir.
//
//      text(g2, T(L"Ölçüm süresi"), ...)
//
//  Neden boyle:
//    1) Turkce mod bugunkuyle BIT BIT AYNI kalir. Tablo bulunamazsa fonksiyon
//       kaynak metni oldugu gibi dondurur — yani ceviri eksikligi Turkce
//       arayuzde ASLA gorunmez. Anahtar tabanli bir sistemde eksik anahtar
//       "btn.copy" diye ekrana duserdi.
//    2) Goc kademeli yapilabilir. Bir dosyayi sarmalayip birakabilirsiniz;
//       arada bozuk bir ara surum olusmaz.
//    3) Ceviri dosyasi okunabilir: solda gercek Turkce cumle duruyor, ceviren
//       kisi baglami gormek icin koda bakmak zorunda kalmiyor.
//
//  Bedeli: Turkce metin degisirse cevirisi duser (ve kaynak metne geri doner,
//  yani bozulmaz). Kabul edilebilir — sessiz bozulma degil, sessiz geri donus.
//
//  ------------------------------------------------------------------------
//  DIL DOSYASI BICIMI  (lang/<kod>.lang, UTF-8)
//  ------------------------------------------------------------------------
//      # yorum satiri
//      @name    English            <- dil secicide gorunecek ad
//      @code    en
//
//      ~ Ölçüm süresi
//      = Measurement duration
//
//      ~ Takılmaların {1}'i {2} sürücüsüyle çakışıyor
//      = {1} of stutters coincide with the {2} driver
//
//  '~' kaynak (Turkce), '=' ceviri. Ayirac olarak '=' KULLANILMIYOR cunku
//  Turkce metinlerin icinde esittir gecebilir; satir basi isareti belirsizlik
//  birakmaz. Cok satirli metinlerde \n yazilir.
//
//  {1} {2} gibi isaretler yer tutuculardir ve ceviride YERI DEGISEBILIR —
//  Turkce'de sona gelen bir ek Ingilizce'de basa gecebilir. Bu yuzden
//  birlestirme degil, sablon kullaniyoruz.
//
//  Neden {1}, %1 degil: Turkce yuzdeyi sayidan ONCE yaziyor ("%40", "%5'i"),
//  yani metinlerin icinde bol bol % geciyor ve "disk islemlerinin %5'i"
//  ifadesindeki %5 yer tutucu saniliyordu. Kacis kurali eklemek yerine
//  cakismayan bir isaret secildi — ceviri yapan kisiye kural ogretmeyelim.
//
//  Yeni dil eklemek = lang/ klasorune bir dosya birakmak. Kod degismez,
//  yeniden derleme gerekmez.
// ============================================================================
#pragma once

#ifdef _WIN32

#include "report_html.h"     // ss::Translator

#include <string>
#include <vector>

namespace ss18 {

struct Language {
    std::string  code;       // "en", "de"
    std::string  name;       // secicide gorunen ad ("English")
    std::wstring path;       // dosya yolu; Turkce icin bos
};

// Kullanilabilir diller. Ilk sira DAIMA Turkce'dir (gomulu kaynak dil, dosya
// gerektirmez); ardindan lang/ klasorunde bulunan her dosya gelir.
const std::vector<Language>& available();

// Etkin dili degistirir. Bulunamazsa Turkce'ye doner ve false dondurur.
bool setLanguage(const std::string& code);
const std::string& currentCode();

// ---- Cevirme ----------------------------------------------------------------
//  Turkce modda (ya da ceviri bulunamadiginda) kaynak metni AYNEN dondurur.
const wchar_t* T(const wchar_t* src);
std::string    T(const std::string& src);

// Yer tutuculu surum. {1}..{9} sirayla args ile degistirilir.
//   Tf(L"Takılmaların {1}'i {2} ile çakışıyor", {L"%40", L"nvlddmkm.sys"})
std::wstring Tf(const wchar_t* src, const std::vector<std::wstring>& args);
std::string  Tf(const std::string& src, const std::vector<std::string>& args);

// ---- Tercih ----------------------------------------------------------------
//  HKCU altinda saklanir: Program Files'a yazma hakki gerektirmez ve
//  kullanici basina ayri tutulur.
std::string loadPreferredCode();          // kayitli tercih, yoksa ""
void        savePreferredCode(const std::string& code);

// Windows'un goruntu dilinden makul bir baslangic secer. Kayitli tercih
// yoksa ilk calistirmada bu kullanilir.
std::string systemDefaultCode();

// lang/ klasorunu yeniden tarar. Uygulama acikken dosya eklenirse ise yarar.
void refresh();

// ---- Sablon uretimi --------------------------------------------------------
//  Yeni dil eklemek isteyen birinin en buyuk sorunu "hangi metinler var?"
//  Kaynak dosyalari ayristirmak kirilgan bir cozum: metinler duz literal
//  olarak duruyor ve bir kismi calisma aninda kuruluyor.
//
//  Bunun yerine uygulama GOSTERDIGI her metni kendisi kaydediyor. Programi
//  calistirip sayfalarda gezmek, sonra sablonu disa aktarmak yeterli —
//  elde edilen dosyada gercekten ekrana cikan metinler bulunur, fazlasi ya da
//  eksigi degil.
//
//  Toplama Turkce modda da calisir ve olculebilir yuk uretmez (bir set'e
//  ekleme). Yalnizca arayuz derlemesinde etkin.
void noteSource(const wchar_t* s);
void noteSource(const std::string& s);

// Toplanan metinleri .lang sablonu olarak yazar: ceviri satirlari BOS birakilir.
// Bos ceviri "henuz cevrilmedi" demektir ve yukleyici onu atlar, yani yarim
// doldurulmus bir dosya bile guvenle kullanilabilir.
bool writeTemplate(const std::wstring& path, const std::string& code,
                   const std::string& name, std::string& error);

size_t collectedCount();

// ---- Tasinabilir katmanlara kanca ------------------------------------------
//  report_html.cpp Windows API'sine bagimli olamaz (tasarim kurali 5) ama
//  ciktisinin cevrilmesi gerekiyor. Cozum: rapor ureteci ceviriyi kendi
//  yapmaz, kendisine verilen islevleri cagirir. Bu fonksiyon o islevleri
//  dolduran hazir bir yapi dondurur.
//
//  Tek yerde durmasinin sebebi: arayuz ve komut satiri ayni kancayi
//  kullansin, ikisinde iki farkli davranis olusmasin.
ss::Translator translator();

} // namespace ss18

#endif // _WIN32
