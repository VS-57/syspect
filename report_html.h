// ============================================================================
//  StutterScope — HTML rapor ureteci
//  ----------------------------------------------------------------------
//  Teshis sonucunu kendi kendine yeten (harici dosya, font, script YOK) bir
//  HTML sayfasina cevirir.
//
//  NEDEN AYRI KATMAN: Ayni HTML iki yerde kullanilacak —
//    1) ss_cli --html rapor.html   (bugun: tarayicida acilir)
//    2) ss_ui.exe WebView2 penceresi (v0.2: ayni govde, native pencere icinde)
//  Bu yuzden burada hicbir Windows API'si yok; sadece string uretimi.
// ============================================================================
#pragma once

#include "frame_source.h"

#include <functional>
#include <vector>

namespace ss {

// ----------------------------------------------------------------------------
//  Ceviri kancasi
// ----------------------------------------------------------------------------
//  Tasarim kurali 5: bu katman Windows API'sine bagimli olamaz. Dil katmani
//  (i18n) ise kayit defteri ve dosya sistemi kullaniyor, yani Windows'a ozgu.
//  Ikisini dogrudan baglamak kurali kirardi.
//
//  Cozum: rapor ureteci ceviriyi KENDI yapmaz, kendisine verilen bir islevi
//  cagirir. Islev verilmezse metinler oldugu gibi kalir — yani Turkce cikti
//  bugunkuyle bit bit ayni ve tasinabilirlik bozulmuyor.
//
//  Sablon + arguman surumu ayri: kanit cumleleri calisma aninda kuruluyor ve
//  hazir metin hicbir ceviri tablosunda bulunamaz (bkz. core.h, EvidencePart).
struct Translator {
    // Sabit metin. Ceviri yoksa kaynagi dondurmeli.
    std::function<std::string(const std::string&)> text;

    // Sablonlu metin: sablon cevrilir, {1}..{9} degerlerle doldurulur.
    std::function<std::string(const std::string&,
                              const std::vector<std::string>&)> format;

    bool valid() const { return static_cast<bool>(text); }
};

// Teshis sonucunu tam bir HTML belgesi olarak dondurur (UTF-8).
// tr verilmezse hicbir ceviri yapilmaz.
std::string renderHtmlReport(const AnalysisResult& result,
                             const FrameSourceInfo& source,
                             const SystemInfo& sys,
                             const Translator& tr = Translator{});

} // namespace ss
