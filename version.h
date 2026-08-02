// ============================================================================
//  Syspect — surum bilgisi
//  ----------------------------------------------------------------------
//  TEK KAYNAK. Surum numarasi daha once ss_cli.cpp icinde metin olarak
//  gomuluydu ("StutterScope v0.1") ve arayuzde hic gorunmuyordu; iki yerde
//  ayri yazilan bir sey er ya da gec ayrisir.
//
//  Buradan besleniyor:
//    - komut satiri basligi
//    - arayuzun Ayarlar sayfasi
//    - ss_ui.rc icindeki VERSIONINFO (Explorer > Ozellikler > Ayrintilar)
//    - guncelleme denetimi karsilastirmasi
//
//  .rc dosyasi C++ derleyicisinden gecmez; bu yuzden makrolar sade tutuldu
//  (kaynak derleyicisi yalnizca #define ve basit sabitleri anlar).
// ============================================================================
#pragma once

#define SS_VERSION_MAJOR 0
#define SS_VERSION_MINOR 2
#define SS_VERSION_PATCH 0

#define SS_VERSION_STRING "0.2.0"

// GitHub Releases etiketi bu bicimde olmali: v0.2.0
#define SS_VERSION_TAG "v" SS_VERSION_STRING

#define SS_APP_NAME    "Syspect"
#define SS_REPO_OWNER  "VS-57"
#define SS_REPO_NAME   "syspect"

// ----------------------------------------------------------------------------
//  Surum denetimi sunucusu
// ----------------------------------------------------------------------------
//  GitHub API yerine kendi sunucumuz. Uc sebep:
//    1) GitHub'in kimlik dogrulamasiz istek siniri 60/saat/IP. Ortak IP
//       arkasindaki kullanicilar (kurumsal ag, mobil operator NAT) birbirinin
//       kotasini yiyor ve denetim SESSIZCE basarisiz oluyor.
//    2) Surumu biz belirliyoruz. Etiket yanlis atilirsa ya da bir on-surum
//       yayinlanirsa GitHub yolu uygulamaya yanlis surum verir.
//    3) Istekleri kendi tarafimizda sayabiliyoruz: kac aktif kurulum var,
//       hangi surumler dolasimda. Kalici kimlik gerektirmeyen tek analitik.
#define SS_UPDATE_HOST "enucuzsistem.com"
#define SS_UPDATE_PATH "/api/syspect/version"
