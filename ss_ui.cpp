// ============================================================================
//  StutterScope — masaustu uygulamasi (ss_ui.exe)
//  ----------------------------------------------------------------------
//  Win32 + GDI+. Stok kontrol kullanilmiyor: her sey elle ciziliyor.
//
//  NEDEN BOYLE:
//  - Ilk surum WebView2'ydi -> "app degil, pencereye konmus web sayfasi".
//  - Ikinci surum stok Win32'ydi (sekme kontrolu, menu, durum cubugu)
//    -> "cok cag disi". Hakliydi: o uc kontrol Windows 2000 estetigi tasiyor.
//  - Bu surum ozel cizim. Native, harici bagimliliksiz, ama gorunum bize ait.
//
//  KAYNAK TUKETIMI TASARIM KISITI:
//  Yeniden cizim YALNIZCA veri degistiginde ya da fare hareket ettiginde
//  yapilir. Cift tamponlama tek bir bellek bitmap'i uzerinden gider, her
//  karede yeniden ayrilmaz. OLCULDU: bos dururken ~40 MB calisma seti
//  (buyuk kismi GDI+ ve yazi tipi onbellegi), CPU yuku saniyede bir sayac
//  okumasi ve bir cizimden ibaret.
//
//  PALET: EnUcuzSistem'in koyu tema degiskenlerinden birebir turetildi.
//  Marka rengi turuncu (--brand-orange: #ee772a); yesil yalnizca "iyi durum"
//  anlaminda kullanilir (--chart-2).
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <psapi.h>

#include <objidl.h>
#include <gdiplus.h>

#include "etw_frame_source.h"
#include "report_html.h"
#include "telemetry.h"
#include "telemetry_signals.h"
#include "system_probe.h"
#include "event_log.h"
#include "dump_scan.h"
#include "report_share.h"
#include "capture_io.h"
#include "findings.h"
#include "storage_probe.h"
#include "i18n.h"
#include "update_check.h"
#include "dpc_source.h"
#include "version.h"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Gdiplus;

namespace {

// ----------------------------------------------------------------------------
//  Palet
// ----------------------------------------------------------------------------
// EnUcuzSistem'in tema degiskenlerinden birebir turetildi. Site renkleri
// oklch() ile tanimli (app/globals.css); asagidakiler o degerlerin sRGB
// karsiliklari. Goz karari yaklastirma DEGIL — ilk denemede vurgu rengini
// yesil sanmistim, marka rengi aslinda --brand-orange: #ee772a.
//
// ----------------------------------------------------------------------------
//  Neden sabit degil de degisken
// ----------------------------------------------------------------------------
//  Renkler `const Color` idi ve bu acik temayi IMKANSIZ kiliyordu: her cizim
//  cagrisi derleme aninda koyu degeri gomuyordu. Simdi tek bir Theme yapisi
//  var, k* isimleri ona bakan referanslar. Cizim kodunun tek satiri
//  degismedi — 200'den fazla kullanim yeri oldugu icin bu sart oldu.
//
//  Vurgu, tehlike ve uyari renkleri iki temada da AYNI kaliyor: marka rengi
//  temaya gore degismez, ve siddet renkleri bilgi tasiyor. Yalnizca zemin,
//  yuzey, cizgi ve metin renkleri degisiyor.
struct Theme {
    Color bg, card, cardHi, border, text, muted, faint;
    Color accent, accentDim, danger, warn, ok, info, violet;
};

const Theme kDarkTheme = {
    Color(255, 0x17, 0x17, 0x1A),   // bg      --background
    Color(255, 0x1E, 0x1E, 0x21),   // card    --card
    Color(255, 0x2D, 0x2D, 0x32),   // cardHi  --secondary
    Color(255, 0x32, 0x32, 0x37),   // border  --border
    Color(255, 0xE6, 0xE8, 0xEB),   // text    --foreground
    Color(255, 0xB6, 0xB6, 0xC1),   // muted   --muted-foreground
    Color(255, 0x52, 0x52, 0x5C),   // faint   --ring
    Color(255, 0xEE, 0x77, 0x2A),   // accent  --brand-orange
    Color(255, 0x8A, 0x45, 0x18),   // accentDim
    Color(255, 0xFB, 0x2C, 0x36),   // danger
    Color(255, 0xFE, 0x9A, 0x00),   // warn
    Color(255, 0x00, 0xBC, 0x7D),   // ok
    Color(255, 0x4B, 0x7B, 0xF5),   // info
    Color(255, 0xAD, 0x46, 0xFF),   // violet
};

// Acik tema. Koyu temanin tersi DEGIL — ayri ayri secildi:
//   - Zemin saf beyaz degil, hafif soguk gri; saf beyaz uzun okumada yoruyor.
//   - Kart beyaz ki zeminden yukselsin (koyuda tersi: kart zeminden acik).
//   - Vurgu ayni turuncu ama accentDim acik zeminde kisik degil ACIK ton
//     olmali, yoksa secili sekmenin uzerindeki metin okunmaz.
//   - ok/warn/info koyulastirildi: acik zeminde parlak yesil ve turuncu
//     kontrast esigini gecmiyor.
const Theme kLightTheme = {
    Color(255, 0xF6, 0xF7, 0xF9),   // bg
    Color(255, 0xFF, 0xFF, 0xFF),   // card
    Color(255, 0xEC, 0xEF, 0xF3),   // cardHi
    Color(255, 0xDD, 0xE2, 0xE8),   // border
    Color(255, 0x17, 0x1B, 0x21),   // text
    Color(255, 0x5C, 0x66, 0x73),   // muted
    Color(255, 0x9A, 0xA3, 0xAF),   // faint
    Color(255, 0xEE, 0x77, 0x2A),   // accent — marka rengi degismez
    Color(255, 0xFD, 0xE4, 0xD3),   // accentDim — acik zeminde ACIK ton
    Color(255, 0xC8, 0x38, 0x2F),   // danger
    Color(255, 0xB4, 0x70, 0x0A),   // warn
    Color(255, 0x0E, 0x8F, 0x60),   // ok
    Color(255, 0x2B, 0x5C, 0xD9),   // info
    Color(255, 0x8B, 0x2F, 0xD4),   // violet
};

Theme gTheme = kDarkTheme;

// Cizim kodu bu isimleri kullanmaya devam ediyor; artik referanslar.
const Color& kBg        = gTheme.bg;
const Color& kCard      = gTheme.card;
const Color& kCardHi    = gTheme.cardHi;
const Color& kBorder    = gTheme.border;
const Color& kText      = gTheme.text;
const Color& kMuted     = gTheme.muted;
const Color& kFaint     = gTheme.faint;
const Color& kAccent    = gTheme.accent;
const Color& kAccentDim = gTheme.accentDim;
const Color& kDanger    = gTheme.danger;
const Color& kWarn      = gTheme.warn;
const Color& kOk        = gTheme.ok;
const Color& kInfo      = gTheme.info;
const Color& kViolet    = gTheme.violet;

bool gDarkMode = true;

void applyTheme(bool dark) {
    gDarkMode = dark;
    gTheme = dark ? kDarkTheme : kLightTheme;
}

// Tema tercihi dil tercihiyle AYNI yerde: HKCU\Software\Syspect. Program
// Files'a yazma hakki gerektirmez ve kullanici basina ayri tutulur.
constexpr wchar_t kThemeRegPath[] = L"Software\\Syspect";
constexpr wchar_t kThemeRegName[] = L"DarkMode";

// Windows'un uygulama temasi tercihi. 0 = koyu, 1 = acik.
// Anahtar yoksa (eski Windows) koyu varsayilir.
bool systemPrefersDark() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &k) != ERROR_SUCCESS)
        return true;
    DWORD v = 0, sz = sizeof(v), type = 0;
    const bool got = RegQueryValueExW(k, L"AppsUseLightTheme", nullptr, &type,
                                      reinterpret_cast<LPBYTE>(&v), &sz)
                     == ERROR_SUCCESS && type == REG_DWORD;
    RegCloseKey(k);
    return got ? (v == 0) : true;
}

// Kullanici bir secim YAPTIYSA o gecerli; yapmadiysa SISTEM tercihi.
//
// Ayrim onemli: "koyu" degerini varsayilan olarak yazmak ile kullanicinin
// koyuyu SECMESI ayni sey degil. Ilkinde kullanici Windows'u acik temaya
// alinca program da onu izlemeli; ikincisinde izlememeli. Bu yuzden kayit
// defterinde deger YOKLUGU anlamli bir durum ve sifir ile karistirilmiyor.
bool loadThemePref() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kThemeRegPath, 0, KEY_READ, &k)
            != ERROR_SUCCESS)
        return systemPrefersDark();
    DWORD v = 0, sz = sizeof(v), type = 0;
    const bool got = RegQueryValueExW(k, kThemeRegName, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(&v), &sz)
                     == ERROR_SUCCESS && type == REG_DWORD;
    RegCloseKey(k);
    return got ? (v != 0) : systemPrefersDark();
}

void saveThemePref(bool dark) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kThemeRegPath, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    DWORD v = dark ? 1u : 0u;
    RegSetValueExW(k, kThemeRegName, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(k);
}

// ----------------------------------------------------------------------------
//  Etkilesim noktalari
// ----------------------------------------------------------------------------
enum : int {
    HS_NONE = 0,
    // DIKKAT — sekme sayisi artarsa buraya da bir HS_NAVn EKLENMELI.
    // Ayarlar sekmesi eklenince cizim dongusu HS_NAV0+5 uretiyordu ama enum'da
    // 5. giris yoktu; deger HS_RECORD ile cakisti ve Ayarlar'in ustune gelmek
    // "Kaydi Baslat" dugmesini de yesile boyadi. Sessiz ve bulmasi zor bir
    // hataydi: iki ayri ogenin ayni kimligi tasimasi derleyiciye gorunmuyor.
    HS_NAV0 = 1, HS_NAV1, HS_NAV2, HS_NAV3, HS_NAV4, HS_NAV5,
    HS_NAV_LAST = HS_NAV5,
    HS_RECORD,
    HS_DUR0, HS_DUR1, HS_DUR2, HS_DUR3,
    HS_SAVE, HS_COPY, HS_EXPORT, HS_ELEVATE, HS_OPEN,
    HS_LANGTPL,                 // dil sablonu disa aktar
    HS_UPDCHK, HS_UPDTOGGLE, HS_UPDOPEN,
    HS_THEME, HS_FEEDBACK,
    HS_HYP_BASE = 100,          // +index
    HS_LANG_BASE = 200,         // +index: dil secenegi
};

struct Hotspot {
    RectF r;
    int   id = HS_NONE;
};

constexpr UINT     WM_CAPTURE_DONE = WM_APP + 1;
constexpr UINT     WM_UPDATE_DONE  = WM_APP + 4;
// Olay gunlugu taramasi arka planda calisir: 30 gunluk WHEA gecmisi gurultulu
// makinelerde binlerce kayit demektir ve WM_CREATE icinde yapilirsa pencere
// acilmadan once takilir.
constexpr UINT     WM_EVTLOG_DONE  = WM_APP + 3;
constexpr UINT_PTR TIMER_LIVE      = 1;

const uint32_t kDurations[4] = {60, 180, 300, 0};   // 0 = sinirsiz
const wchar_t* kDurationText[4] = {L"1 dakika", L"3 dakika", L"5 dakika",
                                   L"Sınırsız"};

struct CaptureOutcome {
    bool                ok = false;
    std::string         error;
    ss::AnalysisResult  result;
    ss::FrameSourceInfo info;
    ss::SystemInfo      sys;
    std::vector<sstelem::Sample> telemetry;

    // Olcum BITTIKTEN sonra alinan olay gunlugu goruntusu. Baslangictaki
    // tarama olcum sirasinda olusan kayitlari goremez — TDR ve depolama
    // hatalarinin degeri tam olarak o cakismada oldugu icin yeniden taranir.
    sslog::Scan         evtlog;

    // DPC toplama sonucu ve kare oturumunun zaman tabani. Ikisi ayri ETW
    // oturumu; hizalama icin kare tarafinin mutlak baslangici da lazim.
    std::unique_ptr<ssdpc::Capture> dpc;
    uint64_t            frameFirstQpc = 0;
    uint64_t            frameQpcFreq  = 0;
    uint64_t            lostFrames    = 0;   // ETW'nin dusurdugu Present olayi
};

// ----------------------------------------------------------------------------
//  Uygulama durumu
// ----------------------------------------------------------------------------
struct App {
    HWND main = nullptr;
    int  dpi  = 96;
    int  page = 0;
    int  hover = HS_NONE;
    int  pressed = HS_NONE;
    int  durationIndex = 1;
    int  selectedHyp = 0;

    ULONG_PTR gdiToken = 0;

    // Cift tamponlama — her karede yeniden ayrilmaz
    HDC     memDc = nullptr;
    HBITMAP memBmp = nullptr;
    int     memW = 0, memH = 0;

    std::vector<Hotspot> hotspots;

    bool     capturing = false;
    uint32_t captureSeconds = 180;
    uint32_t elapsed = 0;

    bool                hasResult = false;

    // Gosterilen sonuc DOSYADAN mi geldi? Onemli: olay gunlugu, guc plani ve
    // aygit taramasi BU makineye aittir. Baskasinin .syscap dosyasini acan
    // kullaniciya, o kaydin yaninda kendi makinesinin gecmisini gostermek
    // duz bir yanlis teshis uretir — "11 kontrolsuz kapanma" satiri karsi
    // tarafin makinesindenmis gibi okunur.
    bool                resultFromFile = false;

    ss::AnalysisResult  result;
    ss::FrameSourceInfo info;
    ss::SystemInfo      sys;
    std::vector<sstelem::Sample> telemetry;

    sstelem::Sampler*   sampler = nullptr;
    sstelem::Sample     live;
    sstelem::Capabilities caps;

    ssprobe::PowerInfo  power;
    ssprobe::DeviceScan devices;
    ssprobe::MemorySpec memory;
    ssprobe::FirmwareInfo firmware;
    sslog::Scan         evtlog;      // BU makinenin gunlugu
    sslog::Scan         fileEvtlog;  // acilan .syscap dosyasindan gelen gunluk
    std::string         fileOsBuild; // kaydin alindigi makinenin Windows surumu
    ssdump::DumpScan    dumps;
    ssstore::StorageScan storage;   // disk envanteri + SMART

    // Surum denetimi. Ag cagrisi arka iplikte yapilir; acilisi geciktirmesi
    // kabul edilemez ve basarisiz olmasi da onemli degil.
    ssupd::Result       upd;
    bool                updBusy = false;

    // DPC toplama sonucu ve esikten gecen surucu (gecen yoksa bos).
    ssdpc::Capture      dpc;
    std::string         dpcSuspect;

    // Olcum penceresi (FILETIME). Olay gunlugundeki bir kaydin olcumle
    // CAKISIP cakismadigini bilmeden "sinyal mi baglam mi" ayrimi yapilamaz.
    uint64_t captureStartFt = 0;
    uint64_t captureEndFt   = 0;
    std::vector<ssfind::Finding> findings;
    size_t findingScroll = 0;
    size_t findingsFit   = 0;   // son cizimde kac bulgu sigdi
    sstelem::GpuStatic  gpuStatic;

    std::wstring status = L"Hazır";
    std::wstring appName = L"Syspect";
};

App g;

float S(float v) { return v * static_cast<float>(g.dpi) / 96.0f; }
int   Si(int v)  { return MulDiv(v, g.dpi, 96); }

// ----------------------------------------------------------------------------
//  Metin
// ----------------------------------------------------------------------------
std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring o(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        o.data(), n);
    return o;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                      static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string o(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        o.data(), n, nullptr, nullptr);
    return o;
}

std::wstring num(double v, int dec) {
    wchar_t b[64];
    swprintf(b, 64, L"%.*f", dec, v);
    return b;
}

// ----------------------------------------------------------------------------
//  Cizim yardimcilari
// ----------------------------------------------------------------------------
void roundPath(GraphicsPath& p, const RectF& r, float rad) {
    const float d = rad * 2.0f;
    p.Reset();
    p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

void card(Graphics& g2, const RectF& r, const Color& fill,
          const Color& edge, float rad = 10.0f) {
    GraphicsPath p;
    roundPath(p, r, S(rad));
    SolidBrush b(fill);
    g2.FillPath(&b, &p);
    if (edge.GetA() > 0) {
        Pen pen(edge, 1.0f);
        g2.DrawPath(&pen, &p);
    }
}

// ----------------------------------------------------------------------------
//  Yazi tipleri
// ----------------------------------------------------------------------------
//  DIKKAT — bunlar GDI+ nesneleridir ve GLOBAL omurlerinin sonu, GDI+'in
//  kapatilmasindan SONRAYA duser. Bir GDI+ nesnesini GdiplusShutdown'dan
//  sonra yikmak erisim ihlalidir (c0000005) ve program cikista coker.
//
//  Bu gercek bir hataydi: olay gunlugu okuyucusu devreye girince kendi
//  uygulamamizin 24 saatte 15 kez coktugu gorundu — hepsi cikis aninda,
//  hepsi ss_ui.exe icinde. Teshis araci ilk olarak kendini teshis etti.
//
//  Cozum: mesaj dongusu bitince, GdiplusShutdown'dan ONCE release() cagir.
struct Fonts {
    std::unique_ptr<FontFamily> family;
    std::unique_ptr<Font> h1, h2, body, sm, tiny, big;

    void release() {
        h1.reset(); h2.reset(); body.reset();
        sm.reset(); tiny.reset(); big.reset();
        family.reset();          // aile en son: fontlar ona bagli
    }
};
Fonts F;

// ----------------------------------------------------------------------------
//  Ekrana yazilan HER metin buradan gecer — ceviri de burada yapilir.
// ----------------------------------------------------------------------------
//  230 cagri yerini tek tek T(...) ile sarmalamak yerine tek nokta seciliyor.
//  Kazanci sadece emek degil: yeni bir metin eklendiginde onu sarmalamayi
//  UNUTMAK mumkun degil, cunku cizmenin baska yolu yok.
//
//  Turkce modda tablo bostur ve T() kaynagi aynen dondurur — yani bu satirin
//  Turkce arayuzde hicbir maliyeti ve hicbir riski yok.
//
//  Calisma aninda kurulan metinler (icinde sayi gecenler) tabloda bulunmaz ve
//  Turkce kalir. Onlar sablona cevrilecek; bkz. i18n.h, yer tutucular.
void text(Graphics& g2, const std::wstring& s, const Font* f,
          const RectF& r, const Color& c,
          StringAlignment h = StringAlignmentNear,
          StringAlignment v = StringAlignmentNear, bool wrap = false) {
    StringFormat sf;
    sf.SetAlignment(h);
    sf.SetLineAlignment(v);
    if (!wrap) sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    ss18::noteSource(s.c_str());
    g2.DrawString(ss18::T(s.c_str()), -1, f, r, &sf, &b);
}

void bar(Graphics& g2, const RectF& r, double pct, const Color& c) {
    card(g2, r, kBorder, Color(0, 0, 0, 0), r.Height / 2.0f);
    if (pct <= 0.0) return;
    if (pct > 100.0) pct = 100.0;
    RectF fill = r;
    fill.Width = r.Width * static_cast<float>(pct / 100.0);
    if (fill.Width < r.Height) fill.Width = r.Height;
    card(g2, fill, c, Color(0, 0, 0, 0), r.Height / 2.0f);
}

void ring(Graphics& g2, const RectF& box, double pct, const Color& c,
          const std::wstring& center, const std::wstring& caption) {
    const float w = S(9.0f);
    RectF a = box;
    a.Inflate(-w / 2.0f, -w / 2.0f);

    Pen bg(kBorder, w);
    bg.SetStartCap(LineCapRound);
    bg.SetEndCap(LineCapRound);
    g2.DrawArc(&bg, a, 0.0f, 360.0f);

    if (pct > 0.0) {
        Pen fg(c, w);
        fg.SetStartCap(LineCapRound);
        fg.SetEndCap(LineCapRound);
        g2.DrawArc(&fg, a, -90.0f, static_cast<REAL>(360.0 * pct / 100.0));
    }

    RectF t = box;
    t.Y -= S(7.0f);
    text(g2, center, F.h1.get(), t, c, StringAlignmentCenter, StringAlignmentCenter);
    RectF cp = box;
    cp.Y += S(19.0f);
    text(g2, caption, F.tiny.get(), cp, kFaint,
         StringAlignmentCenter, StringAlignmentCenter);
}

void addHotspot(const RectF& r, int id) { g.hotspots.push_back({r, id}); }

// ----------------------------------------------------------------------------
//  Logo
// ----------------------------------------------------------------------------
//  Vektorel ciziliyor, dosya olarak gomulmuyor: her DPI'da net kaliyor ve
//  tema rengiyle birlikte degisebiliyor. Uc parca:
//    - halka        : olcum aleti kadrani
//    - S harfi      : marka
//    - ibre + nabiz : turuncu, "olcuyor" fikri
void drawLogo(Graphics& g2, const RectF& box, const Color& body,
              const Color& accent) {
    const float d  = box.Width;
    const float cx = box.X + d / 2.0f;
    const float cy = box.Y + d / 2.0f;

    // --- Halka ---
    const float ringW = d * 0.055f;
    RectF ring(box.X + ringW / 2.0f, box.Y + ringW / 2.0f,
               d - ringW, d - ringW);
    Pen ringPen(body, ringW);
    g2.DrawEllipse(&ringPen, ring);

    // --- S harfi ---
    // Kendi egrisini cizmek yerine yazi tipinin S'i kullaniliyor: hem
    // Segoe UI ile ayni ailede duruyor hem de her boyutta duzgun.
    FontFamily fam(L"Segoe UI");
    Font sFont(&fam, d * 0.62f, FontStyleBold, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush sBrush(body);
    RectF sBox(box.X, box.Y - d * 0.04f, d, d);
    g2.DrawString(L"S", -1, &sFont, sBox, &sf, &sBrush);

    // --- Ibre: merkezden sag ust kose disina ---
    Pen needle(accent, d * 0.05f);
    needle.SetStartCap(LineCapRound);
    needle.SetEndCap(LineCapTriangle);
    g2.DrawLine(&needle, cx + d * 0.04f, cy - d * 0.02f,
                         cx + d * 0.44f, cy - d * 0.42f);
    SolidBrush hub(accent);
    const float hr = d * 0.055f;
    g2.FillEllipse(&hub, cx + d * 0.04f - hr, cy - d * 0.02f - hr, hr * 2, hr * 2);

    // --- Nabiz: sol alttan girip S'in altina baglanan EKG cizgisi ---
    Pen pulse(accent, d * 0.045f);
    pulse.SetLineJoin(LineJoinRound);
    pulse.SetStartCap(LineCapRound);
    const PointF pts[] = {
        PointF(box.X + d * 0.06f, cy + d * 0.18f),
        PointF(box.X + d * 0.22f, cy + d * 0.18f),
        PointF(box.X + d * 0.28f, cy + d * 0.03f),
        PointF(box.X + d * 0.36f, cy + d * 0.30f),
        PointF(box.X + d * 0.42f, cy + d * 0.18f),
        PointF(box.X + d * 0.50f, cy + d * 0.18f),
    };
    g2.DrawLines(&pulse, pts, 6);
    SolidBrush dot(accent);
    const float dr = d * 0.035f;
    g2.FillEllipse(&dot, box.X + d * 0.44f - dr, cy + d * 0.18f - dr,
                   dr * 2, dr * 2);
}

// ----------------------------------------------------------------------------
//  Donanim envanteri
// ----------------------------------------------------------------------------
std::string readCpuName() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &k) != ERROR_SUCCESS) return {};
    wchar_t buf[256];
    DWORD size = sizeof(buf);
    std::string out;
    if (RegQueryValueExW(k, L"ProcessorNameString", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
        out = toUtf8(buf);
        while (!out.empty() && out.back() == ' ') out.pop_back();
    }
    RegCloseKey(k);
    return out;
}

std::string readGpuName() {
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            return toUtf8(dd.DeviceString);
        dd.cb = sizeof(dd);
    }
    return {};
}

uint64_t readTotalRamMb() {
    // GetPhysicallyInstalledSystemMemory SMBIOS'un bildirdigi TAKILI bellegi
    // verir. GlobalMemoryStatusEx.ullTotalPhys donanima ayrilani dusuyor ve
    // 16 GB'lik makinede 15 GB gosteriyordu.
    ULONGLONG kb = 0;
    if (GetPhysicallyInstalledSystemMemory(&kb) && kb > 0) return kb / 1024ull;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    return GlobalMemoryStatusEx(&ms) ? ms.ullTotalPhys / (1024ull * 1024ull) : 0;
}

// PID -> calistirilabilir dosya adi. Olcumun hangi uygulamaya ait oldugunu
// gostermek ve "masaustunde de takiliyor mu" sorusunu KULLANICIYA SORMADAN
// cevaplamak icin gerekli.
std::wstring processName(uint32_t pid) {
    if (pid == 0) return L"";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t path[MAX_PATH] = L"";
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(h, 0, path, &size)) {
        const std::wstring full(path, size);
        const size_t slash = full.find_last_of(L'\\');
        name = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
    }
    CloseHandle(h);
    return name;
}

// Olculen surec bir oyun mu, yoksa masaustu/kabuk mu?
// Kullaniciya "oyun disinda da takiliyor mu?" diye sormak yerine olcumden
// cikariyoruz: takilma dwm/explorer/tarayici uzerinde olduysa sorun oyunda
// degil sistemdedir — CLAUDE.md'nin "en guclu ayirt edici" dedigi sinyal.
bool looksLikeDesktop(const std::wstring& exe) {
    static const wchar_t* kShell[] = {
        L"dwm.exe", L"explorer.exe", L"ShellExperienceHost.exe",
        L"chrome.exe", L"msedge.exe", L"firefox.exe", L"opera.exe",
        L"Discord.exe", L"Spotify.exe", L"Code.exe",
    };
    for (const wchar_t* s : kShell)
        if (_wcsicmp(exe.c_str(), s) == 0) return true;
    return false;
}

// ----------------------------------------------------------------------------
//  Ozet istatistik yardimcilari
// ----------------------------------------------------------------------------
double medianOfKnown(const std::vector<sstelem::Sample>& v,
                     double sstelem::Sample::* field) {
    std::vector<double> xs;
    xs.reserve(v.size());
    for (const auto& s : v) if (sstelem::known(s.*field)) xs.push_back(s.*field);
    if (xs.empty()) return ss::SystemInfo::kUnknownPct;
    std::sort(xs.begin(), xs.end());
    return xs[xs.size() / 2];
}

double p95OfKnown(const std::vector<sstelem::Sample>& v,
                  double sstelem::Sample::* field) {
    std::vector<double> xs;
    for (const auto& s : v) if (sstelem::known(s.*field)) xs.push_back(s.*field);
    if (xs.empty()) return ss::SystemInfo::kUnknownPct;
    std::sort(xs.begin(), xs.end());
    size_t i = static_cast<size_t>(xs.size() * 0.95);
    if (i >= xs.size()) i = xs.size() - 1;
    return xs[i];
}

} // namespace

// ============================================================================
//  Sayfalar
// ============================================================================
namespace {

// Yonetici uyarisi EN USTTE, tam genislikte bir serit — basligin da ustunde.
// Gerekce: yukseltilmemisken hicbir sey olculemiyor, gorulen ILK sey o olmali.
float bannerHeight() { return ss::isElevated() ? 0.0f : S(52.0f); }

float headerTop()  { return bannerHeight(); }
float contentTop() { return bannerHeight() + S(78.0f); }

// Icerik kartlariyla alt bilgi seridi arasindaki nefes payi.
float bottomGap()  { return S(30.0f); }

RectF contentRect(const RECT& client) {
    const float pad = S(20.0f);
    return RectF(pad, contentTop(),
                 client.right - pad * 2.0f - S(272.0f),
                 client.bottom - contentTop() - S(34.0f) - bottomGap());
}

// --- Sayfa 0: Olcum ---------------------------------------------------------
//  YERLESIM KURALI: kartlar ICERIGINE gore yukseklik alir, kalan bosluga gore
//  DEGIL. Onceki halde durum karti pencerenin dibine kadar uzuyordu; koyu
//  temada fark edilmiyordu ama acik temada ortada kocaman beyaz bir alan
//  kaliyordu ve "Kayit Ac" dugmesi metninden yuz piksel uzaga dusuyordu.
//
//  Metinler de kisaltildi: bu ekran OKUNMUYOR, TARANIYOR. Iki satirlik bir
//  aciklama yerine bir satir, hicbir bilgi kaybetmeden.
void drawMeasurePage(Graphics& g2, const RectF& area) {
    const float pad = S(20.0f);

    RectF c1(area.X, area.Y, area.Width, S(142.0f));
    card(g2, c1, kCard, kBorder);

    text(g2, L"Ölçüm süresi", F.h2.get(),
         RectF(c1.X + pad, c1.Y + S(16.0f), c1.Width - pad * 2, S(24.0f)), kText);
    text(g2, L"Oyunu açık bırakın, takıldığı anları bu süre içinde tekrarlayın.",
         F.sm.get(),
         RectF(c1.X + pad, c1.Y + S(40.0f), c1.Width - pad * 2, S(20.0f)), kMuted);

    // Sure secenekleri — pill
    const float pw = (c1.Width - pad * 2 - S(24.0f)) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        RectF p(c1.X + pad + i * (pw + S(8.0f)), c1.Y + S(70.0f), pw, S(44.0f));
        const bool on = (g.durationIndex == i);
        const bool hot = (g.hover == HS_DUR0 + i);
        card(g2, p, on ? kAccentDim : (hot ? kCardHi : kBg),
             on ? kAccent : kBorder, 8.0f);
        text(g2, kDurationText[i], on ? F.h2.get() : F.body.get(), p,
             on ? kText : kMuted, StringAlignmentCenter, StringAlignmentCenter);
        addHotspot(p, HS_DUR0 + i);
    }

    // Durum karti — yukseklik DURUMA gore.
    const float statusH = g.capturing   ? S(188.0f)
                        : g.hasResult   ? S(126.0f)
                                        : S(132.0f);
    RectF c2(area.X, c1.GetBottom() + S(14.0f), area.Width, statusH);
    card(g2, c2, kCard, kBorder);

    if (g.capturing) {
        const std::wstring big =
            (g.captureSeconds == 0)
                ? (num(g.elapsed / 60, 0) + L":" +
                   (g.elapsed % 60 < 10 ? L"0" : L"") + num(g.elapsed % 60, 0))
                : (num((g.captureSeconds - std::min(g.elapsed, g.captureSeconds)) / 60, 0) + L":" +
                   ((g.captureSeconds - std::min(g.elapsed, g.captureSeconds)) % 60 < 10 ? L"0" : L"") +
                   num((g.captureSeconds - std::min(g.elapsed, g.captureSeconds)) % 60, 0));

        text(g2, g.captureSeconds == 0 ? L"GEÇEN SÜRE" : L"KALAN SÜRE",
             F.tiny.get(),
             RectF(c2.X, c2.Y + S(22.0f), c2.Width, S(16.0f)), kFaint,
             StringAlignmentCenter);
        text(g2, big, F.big.get(),
             RectF(c2.X, c2.Y + S(40.0f), c2.Width, S(62.0f)), kAccent,
             StringAlignmentCenter);

        RectF pb(c2.X + S(40.0f), c2.Y + S(114.0f), c2.Width - S(80.0f), S(8.0f));
        const double pct = (g.captureSeconds == 0)
            ? 100.0
            : 100.0 * g.elapsed / static_cast<double>(g.captureSeconds);
        bar(g2, pb, pct, kAccent);

        text(g2, g.captureSeconds == 0
                     ? L"Yeterli veri toplayınca sağ üstten durdurun."
                     : L"Pencereyi kapatmayın.",
             F.sm.get(),
             RectF(c2.X, c2.Y + S(136.0f), c2.Width, S(20.0f)), kMuted,
             StringAlignmentCenter);
    } else if (g.hasResult) {
        text(g2, L"Son ölçüm tamamlandı", F.h2.get(),
             RectF(c2.X + pad, c2.Y + S(18.0f), c2.Width - pad * 2, S(24.0f)), kText);
        text(g2, toWide(g.result.diagnosis.headline), F.body.get(),
             RectF(c2.X + pad, c2.Y + S(46.0f), c2.Width - pad * 2, S(46.0f)),
             kMuted, StringAlignmentNear, StringAlignmentNear, true);
        text(g2, L"Ayrıntılar Sonuçlar sekmesinde.", F.sm.get(),
             RectF(c2.X + pad, c2.Y + S(96.0f), c2.Width - pad * 2, S(20.0f)),
             kFaint);
    } else {
        text(g2, L"Hazır", F.h1.get(),
             RectF(c2.X, c2.Y + S(34.0f), c2.Width, S(34.0f)), kText,
             StringAlignmentCenter);
        text(g2, L"Sağ üstteki düğmeye basın.", F.body.get(),
             RectF(c2.X, c2.Y + S(72.0f), c2.Width, S(24.0f)),
             kMuted, StringAlignmentCenter);
        text(g2, L"Kare süresi, sıcaklık, kullanım ve disk kaydedilir.",
             F.sm.get(),
             RectF(c2.X, c2.Y + S(96.0f), c2.Width, S(20.0f)),
             kFaint, StringAlignmentCenter);
    }

    // ------------------------------------------------------------------
    //  Baskasinin kaydini acma
    // ------------------------------------------------------------------
    //  Surukle-birak zaten calisiyordu ama KESFEDILEBILIR degildi: foruma
    //  birakilan .syscap dosyasini alan kisi uygulamayi acinca hicbir sey
    //  gormuyordu. Ozelligin butun amaci "karsi taraf acip baksin" oldugu
    //  icin gorunur bir dugme sart.
    //  Artik durum kartinin ICINDE degil, ALTINDA ayri bir satir: kart
    //  icerigine gore kuculdugu icin dugmeyi ona yapistirmak yerlesimi
    //  duruma bagimli kilardi.
    if (!g.capturing) {
        RectF bOpen(area.X, c2.GetBottom() + S(14.0f), S(196.0f), S(34.0f));
        card(g2, bOpen, g.hover == HS_OPEN ? kCardHi : kBg, kBorder, 7.0f);
        text(g2, L"Kayıt Aç (.syscap)", F.sm.get(), bOpen,
             g.hover == HS_OPEN ? kText : kMuted,
             StringAlignmentCenter, StringAlignmentCenter);
        addHotspot(bOpen, HS_OPEN);

        text(g2, L"Paylaşılan bir ölçümü açın. Sürükleyip bırakmak da olur.",
             F.sm.get(),
             RectF(bOpen.GetRight() + S(14.0f), bOpen.Y,
                   area.Width - bOpen.Width - S(14.0f), S(34.0f)),
             kFaint, StringAlignmentNear, StringAlignmentCenter, true);
    }
}

// --- Sayfa 1: Sonuclar ------------------------------------------------------
// --- Sayfa 1: Sonuclar -------------------------------------------------------
//  Iki katman: ustte motorun HUKMU, altta BULGU listesi.
//
//  Neden bulgu listesi: olasilik siralamasi ("%55 VRR / kare zamanlama")
//  dogru ama cogu kullanicinin okumak istedigi sey degil. Onlarin sorusu
//  "sistemimde ne yanlis, neye bakayim?" — bulgular tam bunu karsilar:
// Bulgulari yeniden toplar. Olcum OLMADAN da anlamlidir: guc modu, EXPO,
// ReBAR, surucu ve mavi ekran bulgulari olcum gerektirmez ve kullanici
// programi acar acmaz gormeli.
void refreshFindings() {
    ssfind::Input in;
    in.result    = g.hasResult ? &g.result : nullptr;
    in.source    = g.hasResult ? &g.info   : nullptr;
    in.firmware  = &g.firmware;
    in.sys       = &g.sys;
    in.telemetry = &g.telemetry;
    in.gpu       = &g.gpuStatic;
    in.caps      = &g.caps;
    in.power     = &g.power;
    in.memory    = &g.memory;
    in.devices   = &g.devices;
    in.dumps     = &g.dumps;
    in.storage   = &g.storage;
    in.evtlog    = &g.evtlog;

    // ------------------------------------------------------------------
    //  Dosyadan yuklenmis kayit: BU makinenin verisi gosterilmez
    // ------------------------------------------------------------------
    //  Guc plani, aygit taramasi, bellek yapilandirmasi, firmware ayarlari,
    //  mavi ekran kayitlari ve olay gunlugu — hepsi programin CALISTIGI
    //  makineye aittir. Baskasinin .syscap dosyasini acan kisiye bunlari
    //  gostermek, o satirlari kaydin sahibine aitmis gibi okutur ve duz bir
    //  yanlis teshise goturur. Kaydin kendi olay gunlugu varsa (surum 2+
    //  dosya) yalnizca o gosterilir.
    if (g.resultFromFile) {
        in.power    = nullptr;
        in.memory   = nullptr;
        in.devices  = nullptr;
        in.firmware = nullptr;
        in.dumps    = nullptr;
        in.caps     = nullptr;
        in.storage  = nullptr;   // disk envanteri de BU makineye ait
        in.evtlog   = g.fileEvtlog.attempted ? &g.fileEvtlog : nullptr;
        in.foreignCapture = true;
    }

    g.findings   = ssfind::collect(in);
    g.findingScroll = 0;
}

//  olculmus, ikili, tek satirda anlasilir tespitler.
void drawResultsPage(Graphics& g2, const RectF& area) {
    const float pad = S(20.0f);

    if (!g.hasResult && g.findings.empty()) {
        card(g2, area, kCard, kBorder);
        text(g2, L"Henüz ölçüm yapılmadı", F.h2.get(),
             RectF(area.X, area.Y + area.Height / 2 - S(20.0f), area.Width, S(30.0f)),
             kMuted, StringAlignmentCenter);
        return;
    }

    float y = area.Y;

    // ---- Hukum karti (yalnizca olcum varsa) ----
    if (g.hasResult) {
        const ss::Diagnosis& d = g.result.diagnosis;
        const ss::SessionStats& st = g.result.stats;

        RectF c1(area.X, y, area.Width, S(126.0f));
        card(g2, c1, kCard, kBorder);

        RectF ringBox(c1.GetRight() - S(108.0f), c1.Y + S(18.0f),
                      S(88.0f), S(88.0f));
        const Color rc = d.confidence >= 70 ? kOk
                       : d.confidence >= 45 ? kWarn : kFaint;
        ring(g2, ringBox, d.confidence, rc, L"%" + num(d.confidence, 0), L"GÜVEN");

        text(g2, toWide(d.headline), F.h2.get(),
             RectF(c1.X + pad, c1.Y + S(20.0f), c1.Width - S(146.0f), S(46.0f)),
             kText, StringAlignmentNear, StringAlignmentNear, true);

        struct { const wchar_t* k; std::wstring v; Color c; } quick[] = {
            {L"ORT. FPS", num(st.avgFps, 0), kText},
            {L"%1 DÜŞÜK", num(st.onePercentLowFps, 0),
             st.onePercentLowFps < st.avgFps * 0.5 ? kWarn : kText},
            {L"TAKILMA",  num(static_cast<double>(st.stutterCount), 0),
             st.stutterCount > 0 ? kWarn : kOk},
            {L"DONMA",    num(static_cast<double>(st.freezeCount), 0),
             st.freezeCount > 0 ? kDanger : kOk},
        };
        for (int i = 0; i < 4; ++i) {
            const float x = c1.X + pad + i * S(94.0f);
            text(g2, quick[i].k, F.tiny.get(),
                 RectF(x, c1.Y + S(80.0f), S(88.0f), S(14.0f)), kFaint);
            text(g2, quick[i].v, F.h2.get(),
                 RectF(x, c1.Y + S(94.0f), S(88.0f), S(24.0f)), quick[i].c);
        }
        y = c1.GetBottom() + S(16.0f);
    }

    // ---- Bulgular ----
    int bad = 0, warn = 0;
    for (const auto& fd : g.findings) {
        if (fd.severity == ssfind::Severity::Bad)  ++bad;
        if (fd.severity == ssfind::Severity::Warn) ++warn;
    }

    // Once KAC TANESI SIGIYOR hesaplaniyor, sonra baslik yaziliyor: "+N daha"
    // bilgisi baslikta duruyor. Listenin ALTINA yazilinca dugmelerin uzerine
    // biniyordu.
    const float listTop    = y + S(20.0f);
    const float listBottom = area.GetBottom() - S(48.0f);

    size_t fits = 0;
    {
        float probe = listTop;
        for (size_t i = g.findingScroll; i < g.findings.size(); ++i) {
            const float hh = g.findings[i].action.empty()
                           ? S(21.0f) + S(17.0f) + S(22.0f)
                           : S(21.0f) + S(17.0f) * 2.0f + S(22.0f);
            if (probe + hh > listBottom) break;
            probe += hh + S(6.0f);
            ++fits;
        }
    }

    std::wstring head = L"BULGULAR";
    if (bad || warn) {
        head += L"   ·   " + num(bad, 0) + L" sorun";
        if (warn) head += L", " + num(warn, 0) + L" uyarı";
    }
    text(g2, head, F.tiny.get(),
         RectF(area.X + S(2.0f), y, area.Width, S(14.0f)), kFaint);

    if (fits < g.findings.size()) {
        text(g2, L"Tekerlekle kaydırın   ·   " +
                 num(static_cast<double>(g.findingScroll + 1), 0) + L"–" +
                 num(static_cast<double>(g.findingScroll + fits), 0) + L" / " +
                 num(static_cast<double>(g.findings.size()), 0),
             F.tiny.get(),
             RectF(area.X, y, area.Width - S(2.0f), S(14.0f)), kFaint,
             StringAlignmentFar);
    }
    y = listTop;
    g.findingsFit = fits;

    for (size_t i = g.findingScroll; i < g.findings.size(); ++i) {
        const ssfind::Finding& fd = g.findings[i];
        const bool hasAction = !fd.action.empty();

        // Dikey ritim. Satirlar sabit yukseklikte kutulara oturuyor ve her
        // kutuda DIKEY ORTALANIYOR; onceki surumde ust kenardan sabit ofset
        // veriliyordu, farkli punto yuksekliklerinde satirlar kayik
        // gorunuyordu ve baslikla aciklama birbirine giriyordu.
        const float lineTitle  = S(21.0f);
        const float lineDetail = S(17.0f);
        const float content    = lineTitle + lineDetail +
                                 (hasAction ? lineDetail : 0.0f);
        const float hh         = content + S(22.0f);   // 11 ust + 11 alt
        if (y + hh > listBottom) break;

        RectF r(area.X, y, area.Width, hh);
        card(g2, r, kCard, kBorder, 9.0f);

        Color tone = kOk;
        const wchar_t* mark = L"✓";
        switch (fd.severity) {
            case ssfind::Severity::Bad:     tone = kDanger; mark = L"!"; break;
            case ssfind::Severity::Warn:    tone = kWarn;   mark = L"!"; break;
            case ssfind::Severity::Unknown: tone = kFaint;  mark = L"?"; break;
            default: break;
        }

        float ly = r.Y + S(11.0f);

        // Durum noktasi BASLIK satiriyla hizali, kartla degil.
        RectF dotBox(r.X + S(15.0f), ly + (lineTitle - S(18.0f)) / 2.0f,
                     S(18.0f), S(18.0f));
        card(g2, dotBox, tone, Color(0, 0, 0, 0), 9.0f);
        text(g2, mark, F.tiny.get(), dotBox, Color(255, 0x14, 0x14, 0x17),
             StringAlignmentCenter, StringAlignmentCenter);

        const float tx = r.X + S(46.0f);
        const float tw = r.Width - S(46.0f) - pad;

        text(g2, toWide(fd.title), F.h2.get(),
             RectF(tx, ly, tw, lineTitle),
             fd.severity == ssfind::Severity::Good ? kMuted : kText,
             StringAlignmentNear, StringAlignmentCenter);
        ly += lineTitle;

        text(g2, toWide(fd.detail), F.sm.get(),
             RectF(tx, ly, tw, lineDetail), kMuted,
             StringAlignmentNear, StringAlignmentCenter);
        ly += lineDetail;

        if (hasAction)
            text(g2, L"→ " + toWide(fd.action), F.sm.get(),
                 RectF(tx, ly, tw, lineDetail), tone,
                 StringAlignmentNear, StringAlignmentCenter);

        y += hh + S(6.0f);
    }

    // ---- Dugmeler ----
    RectF bSave(area.GetRight() - S(388.0f), area.GetBottom() - S(38.0f),
                S(158.0f), S(34.0f));
    RectF bCopy(area.GetRight() - S(222.0f), area.GetBottom() - S(38.0f),
                S(222.0f), S(34.0f));
    RectF bExport(area.X, area.GetBottom() - S(38.0f), S(156.0f), S(34.0f));

    card(g2, bExport, g.hover == HS_EXPORT ? kCardHi : kCard, kBorder, 7.0f);
    card(g2, bSave, g.hover == HS_SAVE ? kCardHi : kCard, kBorder, 7.0f);
    // Panoya kopyalama birincil eylem: kullanicinin bir sonraki adimi
    // genellikle bu metni bir foruma ya da dil modeline yapistirmak.
    card(g2, bCopy, g.hover == HS_COPY ? kAccent : kAccentDim, kAccent, 7.0f);

    text(g2, L"Kaydı Dışa Aktar", F.sm.get(), bExport, kMuted,
         StringAlignmentCenter, StringAlignmentCenter);
    text(g2, L"Rapor Kaydet (.md)", F.sm.get(), bSave, kText,
         StringAlignmentCenter, StringAlignmentCenter);
    text(g2, L"Panoya Kopyala — forum / yapay zekâ", F.sm.get(), bCopy,
         g.hover == HS_COPY ? Color(255, 0x1A, 0x0E, 0x05) : kText,
         StringAlignmentCenter, StringAlignmentCenter);

    addHotspot(bExport, HS_EXPORT);
    addHotspot(bSave, HS_SAVE);
    addHotspot(bCopy, HS_COPY);
}

// --- Sayfa 2: Grafik --------------------------------------------------------
struct Series {
    std::wstring name;
    Color        color;
    std::vector<double> v;
    double       vmax = 100.0;
    std::wstring unit;
};

std::vector<Series> buildSeries() {
    std::vector<Series> out;
    if (g.telemetry.empty()) return out;

    auto grab = [&](const wchar_t* name, const Color& c,
                    double sstelem::Sample::* f, double vmax,
                    const wchar_t* unit) {
        Series s;
        s.name = name; s.color = c; s.vmax = vmax; s.unit = unit;
        bool any = false;
        for (const auto& t : g.telemetry) {
            const double x = t.*f;
            s.v.push_back(sstelem::known(x) ? x : 0.0);
            if (sstelem::known(x)) any = true;
        }
        if (any) out.push_back(std::move(s));
    };

    // FPS serisi karelerden turetilir: her saniyeye dusen kare sayisi.
    // Telemetriden gelmez ama grafigin ASIL sorusu budur — digerleri onu
    // aciklamak icin var.
    if (g.hasResult && !g.result.frames.empty()) {
        const size_t seconds = g.telemetry.size();
        std::vector<double> fps(seconds, 0.0);
        for (const auto& f : g.result.frames) {
            const size_t s = static_cast<size_t>(f.timestampUs / 1'000'000ull);
            if (s < seconds) fps[s] += 1.0;
        }
        double peak = 0.0;
        for (const double v : fps) if (v > peak) peak = v;
        if (peak > 0.0) {
            Series s;
            s.name = L"FPS";
            s.color = kAccent;          // marka rengi: baslica seri
            s.unit = L"";
            // Olcek 25'in katina yuvarlanir ki eksen okunabilir kalsin.
            s.vmax = std::ceil(peak / 25.0) * 25.0;
            s.v = std::move(fps);
            out.push_back(std::move(s));
        }
    }

    grab(L"GPU kullanım", kOk, &sstelem::Sample::gpuUtilPct,  100.0, L"%");
    // Sicaklik kirmizi: FPS marka turuncusunu kullaniyor, ikisi ayni renkte
    // olunca grafikte ayirt edilemiyordu. Kirmizi ayrica sicaklik icin
    // dogal okuma.
    grab(L"GPU sıcaklık", kDanger, &sstelem::Sample::gpuTempC,    100.0, L"°C");
    grab(L"CPU kullanım", kInfo,   &sstelem::Sample::cpuUsagePct, 100.0, L"%");
    grab(L"Disk aktif",   kViolet, &sstelem::Sample::diskActivePct, 100.0, L"%");
    return out;
}

void drawGraphPage(Graphics& g2, const RectF& area) {
    const float pad = S(20.0f);
    card(g2, area, kCard, kBorder);

    const std::vector<Series> series = buildSeries();
    if (series.empty()) {
        text(g2, g.hasResult ? L"Telemetri kaydı yok"
                             : L"Önce bir ölçüm alın",
             F.h2.get(),
             RectF(area.X, area.Y + area.Height / 2 - S(16.0f), area.Width, S(28.0f)),
             kMuted, StringAlignmentCenter);
        return;
    }

    text(g2, L"Ölçüm boyunca", F.h2.get(),
         RectF(area.X + pad, area.Y + S(14.0f), area.Width - pad * 2, S(24.0f)), kText);

    // Efsane
    float lx = area.X + pad;
    for (const Series& s : series) {
        RectF dot(lx, area.Y + S(46.0f), S(9.0f), S(9.0f));
        card(g2, dot, s.color, Color(0, 0, 0, 0), 4.5f);
        text(g2, s.name, F.tiny.get(),
             RectF(lx + S(14.0f), area.Y + S(42.0f), S(110.0f), S(16.0f)), kMuted);
        lx += S(130.0f);
    }

    // Sagda FPS ekseni icin yer birakiliyor.
    RectF plot(area.X + pad + S(30.0f), area.Y + S(70.0f),
               area.Width - pad * 2 - S(74.0f),
               area.Height - S(70.0f) - S(34.0f));

    // Izgara. Sol eksen yuzde/derece (0-100), sag eksen FPS — FPS'i ayni
    // eksene koymak digerlerini ezerdi.
    double fpsMax = 0.0;
    for (const Series& s : series) if (s.name == L"FPS") fpsMax = s.vmax;

    Pen grid(kBorder, 1.0f);
    for (int i = 0; i <= 4; ++i) {
        const float y = plot.Y + plot.Height * i / 4.0f;
        g2.DrawLine(&grid, plot.X, y, plot.GetRight(), y);
        text(g2, num(100 - i * 25, 0), F.tiny.get(),
             RectF(area.X + pad - S(6.0f), y - S(7.0f), S(30.0f), S(14.0f)),
             kFaint, StringAlignmentFar);
        if (fpsMax > 0.0)
            text(g2, num(fpsMax * (4 - i) / 4.0, 0), F.tiny.get(),
                 RectF(plot.GetRight() + S(4.0f), y - S(7.0f), S(34.0f), S(14.0f)),
                 kAccent);
    }

    const size_t n = series.front().v.size();
    if (n < 2) return;

    for (const Series& s : series) {
        std::vector<PointF> pts;
        pts.reserve(s.v.size());
        for (size_t i = 0; i < s.v.size(); ++i) {
            const float x = plot.X + plot.Width * i / static_cast<float>(n - 1);
            double val = s.v[i] / s.vmax;
            if (val < 0.0) val = 0.0;
            if (val > 1.0) val = 1.0;
            pts.push_back(PointF(x, plot.GetBottom() -
                                    plot.Height * static_cast<float>(val)));
        }
        Pen pen(s.color, S(1.8f));
        pen.SetLineJoin(LineJoinRound);
        g2.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
    }

    // Zaman ekseni
    text(g2, L"0 sn", F.tiny.get(),
         RectF(plot.X, plot.GetBottom() + S(8.0f), S(60.0f), S(16.0f)), kFaint);
    text(g2, num(static_cast<double>(n - 1), 0) + L" sn", F.tiny.get(),
         RectF(plot.GetRight() - S(60.0f), plot.GetBottom() + S(8.0f),
               S(60.0f), S(16.0f)), kFaint, StringAlignmentFar);
}

// --- Sayfa 3: Sistem --------------------------------------------------------
// --- Sayfa 5: Ayarlar -------------------------------------------------------
//  Su an tek ayar var: dil. Sayfanin varlik sebebi de bu — bir ayar icin
//  sayfa acmak fazla gorunebilir ama alternatifi menu cubugu eklemekti ve o,
//  tamamen ozel cizilen bu arayuze yamanmis dururdu.
// ----------------------------------------------------------------------------
//  Ayarlar — IKI SUTUN
// ----------------------------------------------------------------------------
//  Bes ayar vardi ve alt alta dizilince tam sayfa kaplıyordu; oysa hicbiri
//  genis degil. Sol sutun "gorunum" (tema + dil), sag sutun "program"
//  (surum + geri bildirim). Gruplama keyfi degil: soldakiler nasil
//  gorundugunu, sagdakiler programin kendisini ilgilendiriyor.
//
//  Aciklama metinleri de kisaldi. Ayar ekrani okunmaz, bakilir; her satirin
//  altina paragraf koymak ayari degil aciklamayi one cikariyordu.
void drawSettingsPage(Graphics& g2, const RectF& area) {
    const float pad = S(18.0f);
    const float gap = S(14.0f);
    const float colW = (area.Width - gap) / 2.0f;
    const float rightX = area.X + colW + gap;

    // ===== SOL SUTUN =====
    // --- Gorunum ---
    RectF c0(area.X, area.Y, colW, S(96.0f));
    card(g2, c0, kCard, kBorder);
    text(g2, L"Görünüm", F.h2.get(),
         RectF(c0.X + pad, c0.Y + S(14.0f), c0.Width - pad * 2, S(22.0f)), kText);
    text(g2, L"Varsayılan Windows ayarınızı izler.", F.sm.get(),
         RectF(c0.X + pad, c0.Y + S(36.0f), c0.Width - pad * 2, S(18.0f)), kFaint);

    {
        const float bw = (c0.Width - pad * 2 - S(8.0f)) / 2.0f;
        for (int i = 0; i < 2; ++i) {
            const bool dark = (i == 1);
            RectF r(c0.X + pad + i * (bw + S(8.0f)), c0.GetBottom() - S(42.0f),
                    bw, S(30.0f));
            const bool on  = (dark == gDarkMode);
            const bool hot = (g.hover == HS_THEME);
            if (on)       card(g2, r, kAccentDim, kAccent, 7.0f);
            else if (hot) card(g2, r, kCardHi, kBorder, 7.0f);
            else          card(g2, r, kBg, kBorder, 7.0f);
            text(g2, dark ? L"Koyu" : L"Açık", on ? F.h2.get() : F.body.get(), r,
                 on ? kText : kMuted, StringAlignmentCenter, StringAlignmentCenter);
            addHotspot(r, HS_THEME);
        }
    }

    // --- Dil ---
    const auto& langs = ss18::available();
    const float langH = S(60.0f) + std::min<size_t>(langs.size(), 6) * S(34.0f);
    RectF c1(area.X, c0.GetBottom() + gap, colW, langH);
    card(g2, c1, kCard, kBorder);

    text(g2, L"Dil", F.h2.get(),
         RectF(c1.X + pad, c1.Y + S(14.0f), c1.Width - pad * 2, S(22.0f)), kText);

    float ly = c1.Y + S(46.0f);
    for (size_t i = 0; i < langs.size() && i < 6; ++i) {
        RectF r(c1.X + pad, ly, c1.Width - pad * 2, S(30.0f));
        const bool on  = (langs[i].code == ss18::currentCode());
        const bool hot = (g.hover == static_cast<int>(HS_LANG_BASE + i));
        if (on)       card(g2, r, kAccentDim, kAccent, 7.0f);
        else if (hot) card(g2, r, kCardHi, kBorder, 7.0f);

        text(g2, toWide(langs[i].name), on ? F.h2.get() : F.body.get(),
             RectF(r.X + S(12.0f), r.Y, r.Width - S(24.0f), r.Height),
             on ? kText : kMuted, StringAlignmentNear, StringAlignmentCenter);
        text(g2, toWide(langs[i].code), F.sm.get(),
             RectF(r.X, r.Y, r.Width - S(12.0f), r.Height), kFaint,
             StringAlignmentFar, StringAlignmentCenter);

        addHotspot(r, static_cast<int>(HS_LANG_BASE + i));
        ly += S(34.0f);
    }

    // ÇEVİRİ ŞABLONU DÜĞMESİ KALDIRILDI.
    //
    //  Amaci "yeni dil ekleyecek kisi hangi metinlerin cevrilecegini gorsun"
    //  idi. Iki sebeple kaldirildi:
    //
    //  1) Cikti kullanilamiyordu. Toplayici EKRANA CIKAN her metni
    //     kaydediyor, oysa metinlerin cogu calisma aninda kuruluyor:
    //     islemci adi, olcum sonuclari, saat, sayac. Gercek bir kayitta 328
    //     satirin 141'i sayacin kendi etiketiydi ("12 metin", "13 metin"...)
    //     — sayaci yazmak sayaci artiriyor, her cizimde yeni bir satir
    //     doguyordu.
    //
    //  2) Son kullanicinin isi degil. Ayarlar sayfasi zaten kalabalikti ve
    //     bu dugme ceviri yapmayan herkes icin gurultuydu.
    //
    //  Dil eklemek hala mumkun ve kod degisikligi gerektirmiyor: lang/
    //  klasorune bir .lang dosyasi birakmak yeterli. Cevrilecek metin listesi
    //  kaynaktan uretiliyor (lang/en.lang buyuk olcude tam bir ornektir).

    // ------------------------------------------------------------------
    //  Surum ve guncelleme
    // ------------------------------------------------------------------
    //  Program kendini GUNCELLEMEZ, yalnizca yeni surum olup olmadigini
    //  soyler. Sebebi update_check.h'de yazili: imzasiz binary + yonetici
    //  hakki = otomatik guncelleyici yazmak icin en kotu bilesim.
    // ===== SAG SUTUN =====
    //  YERLESIM: her sey USTTEN konumlaniyor. Onceki halde baslik ustten,
    //  dugmeler alttan hesaplaniyordu ve ikisi kartin ortasinda UST USTE
    //  BINIYORDU — "hicbir bilgi gonderilmez" satirinin uzerine dugmeler
    //  cizilmisti. Tek yonden hesaplamak bu sinifi hatayi imkansiz kiliyor.
    const bool hasUpdate = g.upd.available && !g.upd.pageUrl.empty();
    RectF c3(rightX, area.Y, colW, hasUpdate ? S(190.0f) : S(152.0f));
    card(g2, c3, kCard, kBorder);

    text(g2, L"Sürüm", F.h2.get(),
         RectF(c3.X + pad, c3.Y + S(14.0f), c3.Width - pad * 2, S(22.0f)), kText);
    text(g2, g.appName + L" " + toWide(SS_VERSION_STRING), F.body.get(),
         RectF(c3.X + pad, c3.Y + S(40.0f), c3.Width - pad * 2, S(22.0f)), kMuted);

    // Denetim durumu
    std::wstring updLine;
    Color updColor = kFaint;
    if (g.updBusy) {
        updLine = L"Denetleniyor…";
    } else if (!g.upd.error.empty()) {
        updLine = L"Denetlenemedi: " + toWide(g.upd.error);
    } else if (g.upd.available) {
        updLine = L"Yeni sürüm var: " + toWide(g.upd.latestTag);
        updColor = kAccent;
    } else if (g.upd.checked) {
        updLine = L"En güncel sürümü kullanıyorsunuz.";
    } else {
        updLine = L"Henüz denetlenmedi.";
    }
    text(g2, updLine, F.sm.get(),
         RectF(c3.X + pad, c3.Y + S(62.0f), c3.Width - pad * 2, S(20.0f)),
         updColor);
    text(g2, L"Hiçbir bilgi gönderilmez; program kendini güncellemez.",
         F.sm.get(),
         RectF(c3.X + pad, c3.Y + S(82.0f), c3.Width - pad * 2, S(18.0f)),
         kFaint);

    const float bw2 = (c3.Width - pad * 2 - S(8.0f)) / 2.0f;
    RectF bChk(c3.X + pad, c3.Y + S(108.0f), bw2, S(30.0f));
    card(g2, bChk, g.hover == HS_UPDCHK ? kCardHi : kBg, kBorder, 7.0f);
    text(g2, L"Şimdi Denetle", F.sm.get(), bChk,
         g.hover == HS_UPDCHK ? kText : kMuted,
         StringAlignmentCenter, StringAlignmentCenter);
    addHotspot(bChk, HS_UPDCHK);

    // Acilista denetleme anahtari
    RectF bTgl(bChk.GetRight() + S(8.0f), bChk.Y, bw2, S(30.0f));
    const bool on = ssupd::enabled();
    card(g2, bTgl, g.hover == HS_UPDTOGGLE ? kCardHi : kBg,
         on ? kAccent : kBorder, 7.0f);
    text(g2, on ? L"Açılışta: AÇIK" : L"Açılışta: KAPALI",
         F.sm.get(), bTgl, on ? kText : kMuted,
         StringAlignmentCenter, StringAlignmentCenter);
    addHotspot(bTgl, HS_UPDTOGGLE);

    // Yeni surum varsa indirme dugmesi tam genislikte alta gelir; nadir bir
    // durum oldugu icin yerlesimi daraltmasin.
    if (hasUpdate) {
        RectF bOpn(c3.X + pad, bChk.GetBottom() + S(8.0f),
                   c3.Width - pad * 2, S(30.0f));
        card(g2, bOpn, g.hover == HS_UPDOPEN ? kAccent : kAccentDim, kAccent, 7.0f);
        text(g2, L"İndirme sayfasını aç", F.sm.get(), bOpn,
             g.hover == HS_UPDOPEN ? Color(255, 0xFF, 0xFF, 0xFF) : kText,
             StringAlignmentCenter, StringAlignmentCenter);
        addHotspot(bOpn, HS_UPDOPEN);
    }

    // ------------------------------------------------------------------
    //  Geri bildirim
    // ------------------------------------------------------------------
    //  Dugme formu TARAYICIDA aciyor; program dogrudan bir sunucuya YAZMIYOR.
    //  Sebep teknik degil, sozle ilgili: Syspect "veri toplamaz" diyor.
    //  Icerden sessizce POST atmak o sozu cignerdi. Kullanici formu bilerek
    //  acip bilerek dolduruyor — ve ne gonderildigini sayfada goruyor.
    //  Sag sutunun ikinci karti. Yine USTTEN hesap: dugmeyi alttan
    //  konumlandirmak aciklama metninin uzerine bindiriyordu.
    RectF c4(rightX, c3.GetBottom() + gap, colW, S(128.0f));
    card(g2, c4, kCard, kBorder);

    text(g2, L"Geri bildirim", F.h2.get(),
         RectF(c4.X + pad, c4.Y + S(14.0f), c4.Width - pad * 2, S(22.0f)), kText);
    text(g2, L"Yanlış teşhis ya da eksik bulduğunuz bir şey. Form tarayıcıda "
             L"açılır; program hiçbir şey göndermez.",
         F.sm.get(),
         RectF(c4.X + pad, c4.Y + S(38.0f), c4.Width - pad * 2, S(38.0f)),
         kMuted, StringAlignmentNear, StringAlignmentNear, true);

    RectF bFb(c4.X + pad, c4.Y + S(84.0f), S(196.0f), S(30.0f));
    card(g2, bFb, g.hover == HS_FEEDBACK ? kCardHi : kBg, kBorder, 7.0f);
    text(g2, L"Geri Bildirim Gönder", F.sm.get(), bFb,
         g.hover == HS_FEEDBACK ? kText : kMuted,
         StringAlignmentCenter, StringAlignmentCenter);
    addHotspot(bFb, HS_FEEDBACK);
}

void drawSystemPage(Graphics& g2, const RectF& area) {
    const float pad = S(20.0f);
    float y = area.Y;

    // Uyarilar once
    auto warnCard = [&](const std::wstring& title, const std::wstring& body,
                        const Color& accent) {
        RectF r(area.X, y, area.Width, S(78.0f));
        if (r.GetBottom() > area.GetBottom()) return;
        card(g2, r, kCard, kBorder);
        RectF stripe(r.X, r.Y + S(10.0f), S(3.0f), r.Height - S(20.0f));
        card(g2, stripe, accent, Color(0, 0, 0, 0), 1.5f);
        text(g2, title, F.h2.get(),
             RectF(r.X + pad, r.Y + S(12.0f), r.Width - pad * 2, S(22.0f)), accent);
        text(g2, body, F.sm.get(),
             RectF(r.X + pad, r.Y + S(34.0f), r.Width - pad * 2, S(38.0f)),
             kMuted, StringAlignmentNear, StringAlignmentNear, true);
        y += S(86.0f);
    };

    if (g.power.shouldWarn)
        warnCard(L"Güç ayarı", toWide(g.power.warning), kWarn);

    if (!g.devices.problems.empty()) {
        std::wstring body;
        for (size_t i = 0; i < g.devices.problems.size() && i < 3; ++i) {
            if (i) body += L"   ·   ";
            body += toWide(g.devices.problems[i].name);
        }
        warnCard(L"Sürücü sorunu — " + toWide(g.devices.note), body, kDanger);
    }

    if (g.memory.mixedModules) {
        warnCard(L"Bellek — modüller birbirinin aynısı değil",
                 toWide(g.memory.mixedModulesNote), kDanger);
    } else if (g.memory.profile == ssprobe::MemorySpec::Profile::Off ||
               g.memory.singleChannelRisk) {
        warnCard(L"Bellek — " + toWide(g.memory.profileLabel) +
                 (g.memory.profile == ssprobe::MemorySpec::Profile::Off
                      ? L" kapalı" : L" açık"),
                 toWide(g.memory.profileNote), kWarn);
    }

    if (g.gpuStatic.resizableBar == sstelem::GpuStatic::Tri::No)
        warnCard(L"Resizable BAR kapalı", toWide(g.gpuStatic.rebarNote), kWarn);

    // Envanter
    RectF r(area.X, y, area.Width, area.GetBottom() - y);
    if (r.Height < S(60.0f)) return;
    card(g2, r, kCard, kBorder);

    const uint64_t ram = readTotalRamMb();
    struct Row { std::wstring k, v; };
    std::vector<Row> rows = {
        {L"İşlemci",      toWide(readCpuName())},
        {L"Ekran kartı",  toWide(readGpuName())},
        {L"Bellek",       ram ? num(static_cast<double>(ram) / 1024.0, 0) + L" GB"
                              : L"Bilinmiyor"},
        {L"Güç planı",    toWide(g.power.friendlyName) +
                          (g.power.onBattery ? L"  ·  PİLDEN çalışıyor" : L"")},
        {L"Yetki",        ss::isElevated() ? L"Yönetici" : L"Standart kullanıcı"},
        {L"Ölçüm yöntemi",L"ETW · DxgKrnl (pasif dinleme, oyuna dokunulmaz)"},
        {L"Aygıt taraması", num(g.devices.totalDevices, 0) + L" aygıt · " +
                            toWide(g.devices.note)},
    };

    if (!g.memory.modules.empty()) {
        rows.insert(rows.begin() + 3,
            {L"Bellek hızı", toWide(g.memory.typeName) + L" · " +
                             num(g.memory.configuredMTs, 0) + L" MT/s (modüller " +
                             num(g.memory.maxSpeedMTs, 0) + L" MT/s destekliyor)"});
        rows.insert(rows.begin() + 4,
            {toWide(g.memory.profileLabel) + L" profili",
             g.memory.profile == ssprobe::MemorySpec::Profile::On  ? L"AÇIK"
           : g.memory.profile == ssprobe::MemorySpec::Profile::Off ? L"KAPALI"
                                                                   : L"Belirlenemedi"});
        std::wstring mods;
        for (const auto& m : g.memory.modules) {
            if (!mods.empty()) mods += L"  ·  ";
            mods += toWide(m.locator) + L" " + toWide(m.manufacturer) + L" " +
                    toWide(m.partNumber);
        }
        rows.insert(rows.begin() + 5, {L"Modüller", mods});
    }

    if (g.gpuStatic.known) {
        if (sstelem::known(g.gpuStatic.powerLimitW)) {
            std::wstring pw = num(g.gpuStatic.powerLimitW, 0) + L" W";
            if (sstelem::known(g.gpuStatic.maxPowerLimitW))
                pw += L"  (tavan " + num(g.gpuStatic.maxPowerLimitW, 0) + L" W)";
            rows.push_back({L"GPU güç limiti", pw});
        }
        rows.push_back({L"Resizable BAR",
            g.gpuStatic.resizableBar == sstelem::GpuStatic::Tri::Yes ? L"AÇIK"
          : g.gpuStatic.resizableBar == sstelem::GpuStatic::Tri::No  ? L"KAPALI"
                                                                     : L"Belirlenemedi"});
        if (g.gpuStatic.vramTotalMb)
            rows.push_back({L"VRAM / BAR1",
                num(static_cast<double>(g.gpuStatic.vramTotalMb), 0) + L" MB / " +
                num(static_cast<double>(g.gpuStatic.bar1TotalMb), 0) + L" MB"});
    }
    if (g.hasResult) {
        rows.push_back({L"Son ölçüm", toWide(g.info.note)});
        rows.push_back({L"Süre", num(g.result.stats.durationSec, 0) + L" sn"});
        rows.push_back({L"Medyan FPS", num(g.result.stats.medianFps, 1)});
    }

    float ry = r.Y + S(14.0f);
    for (const Row& row : rows) {
        if (ry + S(26.0f) > r.GetBottom()) break;
        text(g2, row.k, F.sm.get(),
             RectF(r.X + pad, ry, S(150.0f), S(20.0f)), kFaint);
        text(g2, row.v, F.sm.get(),
             RectF(r.X + pad + S(156.0f), ry, r.Width - pad * 2 - S(156.0f), S(20.0f)),
             kText);
        ry += S(26.0f);
    }
}

// --- Sayfa 4: Mavi Ekran ----------------------------------------------------
//  Ayri sekme olmasinin sebebi: bu urun uc ayri sikayeti kapsiyor —
//  takilma, dusuk FPS ve mavi ekran. Ucu de esit agirlikta olmali; mavi
//  ekran kaydini sistem envanterinin dibine gomerek onemsizlestirmemek
//  gerekiyor.
void drawBsodPage(Graphics& g2, const RectF& area) {
    const float pad = S(20.0f);

    // Ozet serit. UC AYRI DURUM ve ucu de farkli renkte:
    //   kirmizi — kayit bulundu
    //   turuncu — OKUNAMADI (yonetici yok) ya da dump kaydi kapali
    //   yesil   — gercekten kayit yok
    // Ilk surumde "okunamadi" durumu yesil "iyi haber" olarak
    // gosteriliyordu; makinede mavi ekranlar dururken yanlis guven veriyordu.
    RectF top(area.X, area.Y, area.Width, S(72.0f));
    const bool denied = g.dumps.accessDenied;
    const bool bad    = !g.dumps.findings.empty();
    const bool off    = !g.dumps.dumpsEnabled;
    const Color tone  = denied ? kWarn : (bad ? kDanger : (off ? kWarn : kOk));

    card(g2, top, kCard, kBorder);
    RectF stripe(top.X, top.Y + S(10.0f), S(3.0f), top.Height - S(20.0f));
    card(g2, stripe, tone, Color(0, 0, 0, 0), 1.5f);

    text(g2, denied ? L"Mavi ekran kayıtları okunamadı"
                    : (bad ? L"Mavi ekran kaydı bulundu"
                           : (off ? L"Mavi ekran kaydı KAPALI"
                                  : L"Mavi ekran kaydı yok")),
         F.h2.get(),
         RectF(top.X + pad, top.Y + S(14.0f), top.Width - pad * 2, S(24.0f)),
         tone);
    // Aciklama iki satir surebiliyor; sabit 24 px kutuda kirpiliyordu.
    text(g2, toWide(g.dumps.note), F.sm.get(),
         RectF(top.X + pad, top.Y + S(37.0f), top.Width - pad * 2, S(40.0f)),
         kMuted, StringAlignmentNear, StringAlignmentNear, true);

    float y = top.GetBottom() + S(12.0f);

    if (g.dumps.findings.empty()) {
        RectF r(area.X, y, area.Width, area.GetBottom() - y);
        if (r.Height < S(60.0f)) return;
        card(g2, r, kCard, kBorder);

        // DIKKAT: "okunamadi" ile "kayit yok" ayni metni PAYLASAMAZ.
        // Ilk surumde bu dal accessDenied'i bilmiyordu ve baslikta
        // "okunamadi" yazarken govdede "sistem temiz gorunuyor" diyordu.
        const wchar_t* body =
            denied ? L"Bu soruya cevap veremiyoruz.\n"
                     L"Kayıtların bulunduğu klasör yönetici hakkı istiyor;\n"
                     L"programı yönetici olarak açtığınızda burada görünecek."
          : off    ? L"Dump kaydı açılmadan mavi ekranların sebebi\n"
                     L"tespit edilemez. Windows bu ayar kapalıyken\n"
                     L"çökme anındaki bellek görüntüsünü diske yazmaz."
                   : L"Bu makinede kayıtlı mavi ekran yok.\n"
                     L"Sistem bu açıdan temiz görünüyor.";

        text(g2, body, F.body.get(),
             RectF(r.X + pad, r.Y + r.Height / 2 - S(46.0f),
                   r.Width - pad * 2, S(76.0f)),
             kMuted, StringAlignmentCenter, StringAlignmentNear, true);

        if (denied) {
            RectF btn(r.X + r.Width / 2 - S(87.0f),
                      r.Y + r.Height / 2 + S(36.0f), S(174.0f), S(32.0f));
            card(g2, btn, g.hover == HS_ELEVATE ? kAccent : kAccentDim,
                 kAccent, 7.0f);
            text(g2, L"Yönetici olarak aç", F.h2.get(), btn,
                 g.hover == HS_ELEVATE ? Color(255, 0x1A, 0x0E, 0x05) : kText,
                 StringAlignmentCenter, StringAlignmentCenter);
            addHotspot(btn, HS_ELEVATE);
        }
        return;
    }

    // Dump listesi — en yeniden eskiye
    for (const auto& f : g.dumps.findings) {
        const float hh = f.ranked.empty() ? S(84.0f) : S(112.0f);
        if (y + hh > area.GetBottom()) break;

        RectF r(area.X, y, area.Width, hh);
        card(g2, r, kCard, kBorder);

        text(g2, toWide(f.parsed ? f.bugcheckName : f.fileName), F.h2.get(),
             RectF(r.X + pad, r.Y + S(12.0f), r.Width - pad * 2 - S(120.0f), S(22.0f)),
             kDanger);
        text(g2, toWide(f.fileName), F.tiny.get(),
             RectF(r.GetRight() - pad - S(180.0f), r.Y + S(15.0f), S(180.0f), S(16.0f)),
             kFaint, StringAlignmentFar);

        if (!f.parsed) {
            text(g2, L"Okunamadı: " + toWide(f.error), F.sm.get(),
                 RectF(r.X + pad, r.Y + S(38.0f), r.Width - pad * 2, S(20.0f)),
                 kMuted);
        } else {
            text(g2, toWide(f.bugcheckMeaning), F.sm.get(),
                 RectF(r.X + pad, r.Y + S(36.0f), r.Width - pad * 2, S(36.0f)),
                 kMuted, StringAlignmentNear, StringAlignmentNear, true);

            if (!f.suspectDriver.empty()) {
                text(g2, L"Şüpheli sürücü: " + toWide(f.suspectDriver),
                     F.sm.get(),
                     RectF(r.X + pad, r.Y + S(64.0f), r.Width - pad * 2, S(20.0f)),
                     kAccent);
            }

            if (!f.ranked.empty()) {
                float x = r.X + pad;
                for (size_t i = 0; i < f.ranked.size() && i < 3; ++i) {
                    const auto& c = f.ranked[i];
                    const std::wstring tag =
                        L"%" + num(c.percent, 0) + L"  " + toWide(c.label);
                    RectF chip(x, r.Y + S(84.0f), S(190.0f), S(20.0f));
                    if (chip.GetRight() > r.GetRight() - pad) break;
                    card(g2, chip, kCardHi, Color(0, 0, 0, 0), 6.0f);
                    text(g2, tag, F.tiny.get(), chip, kMuted,
                         StringAlignmentCenter, StringAlignmentCenter);
                    x += S(196.0f);
                }
            }
        }
        y += hh + S(8.0f);
    }
}

// ----------------------------------------------------------------------------
//  Yan panel — canli degerler
// ----------------------------------------------------------------------------
void drawSidebar(Graphics& g2, const RECT& client) {
    RectF side(client.right - S(252.0f), contentTop(), S(232.0f),
               client.bottom - contentTop() - S(34.0f) - bottomGap());

    text(g2, L"CANLI", F.tiny.get(),
         RectF(side.X + S(2.0f), side.Y, side.Width, S(16.0f)), kFaint);

    float y = side.Y + S(22.0f);

    auto metric = [&](const wchar_t* label, double value, const wchar_t* unit,
                      double pct, const Color& c) {
        RectF r(side.X, y, side.Width, S(62.0f));
        card(g2, r, kCard, kBorder);
        text(g2, label, F.tiny.get(),
             RectF(r.X + S(14.0f), r.Y + S(10.0f), r.Width - S(28.0f), S(14.0f)),
             kFaint);
        if (sstelem::known(value)) {
            // Sayi ve birim ayri kademede: sayi one cikar, birim sessiz kalir.
            const std::wstring v = num(value, 0);
            text(g2, v, F.h1.get(),
                 RectF(r.X + S(14.0f), r.Y + S(21.0f), r.Width - S(28.0f), S(28.0f)),
                 c);
            // Birimi sayinin hemen sagina koymak icin sayinin GERCEK
            // genisligi olculur. Karakter basina sabit genislik varsaymak
            // ("11 px") oransal yazi tipinde tutmuyordu: '1' ile '0' ayni
            // genislikte degil, birim sayinin ustune biniyordu.
            RectF measured;
            g2.MeasureString(v.c_str(), -1, F.h1.get(),
                             PointF(0.0f, 0.0f), &measured);
            RectF unitBox(r.X + S(14.0f) + measured.Width + S(3.0f),
                          r.Y + S(29.0f), S(44.0f), S(18.0f));
            text(g2, unit, F.sm.get(), unitBox, kFaint);

            RectF pb(r.X + S(14.0f), r.Y + S(50.0f), r.Width - S(28.0f), S(3.0f));
            bar(g2, pb, pct, c);
        } else {
            text(g2, L"Ölçülemiyor", F.sm.get(),
                 RectF(r.X + S(14.0f), r.Y + S(26.0f), r.Width - S(28.0f), S(20.0f)),
                 kFaint);
        }
        y += S(70.0f);
    };

    const sstelem::Sample& s = g.live;

    // ------------------------------------------------------------------
    //  CPU sicakligi — HENUZ OKUNAMIYOR, ama yerini bos birakmiyoruz
    // ------------------------------------------------------------------
    //  Cekirdek sicakligi (Tctl/Tdie) ring 0 gerektiriyor; kullanici-mod bir
    //  programa Windows bu degeri vermiyor. Kutucugu hic koymamak "bu program
    //  islemci sicakligina bakmiyor" diye okunuyordu — oysa BAKAMIYOR.
    //  Farki soylemek, sessiz kalmaktan iyi.
    //
    //  ACIK sicaklik degeri UYDURULMUYOR: kutuda sayi degil "yakında" yaziyor.
    {
        RectF r(side.X, y, side.Width, S(62.0f));
        card(g2, r, kCard, kBorder);
        text(g2, L"CPU SICAKLIK", F.tiny.get(),
             RectF(r.X + S(14.0f), r.Y + S(10.0f), r.Width - S(28.0f), S(14.0f)),
             kFaint);
        text(g2, L"Yakında", F.body.get(),
             RectF(r.X + S(14.0f), r.Y + S(26.0f), r.Width - S(28.0f), S(20.0f)),
             kFaint);
        y += S(70.0f);
    }

    metric(L"GPU SICAKLIK", s.gpuTempC, L"°C",
           sstelem::known(s.gpuTempC) ? s.gpuTempC : 0.0,
           s.gpuTempC >= 83.0 ? kDanger : s.gpuTempC >= 75.0 ? kWarn : kOk);
    metric(L"GPU KULLANIM", s.gpuUtilPct, L"%",
           sstelem::known(s.gpuUtilPct) ? s.gpuUtilPct : 0.0, kAccent);
    metric(L"CPU KULLANIM", s.cpuUsagePct, L"%",
           sstelem::known(s.cpuUsagePct) ? s.cpuUsagePct : 0.0,
           s.cpuUsagePct >= 90.0 ? kWarn : kInfo);
    metric(L"DİSK AKTİF", s.diskActivePct, L"%",
           sstelem::known(s.diskActivePct) ? s.diskActivePct : 0.0,
           s.diskActivePct >= 90.0 ? kDanger : kViolet);
    metric(L"BELLEK", s.ramUsedPct, L"%",
           sstelem::known(s.ramUsedPct) ? s.ramUsedPct : 0.0,
           s.ramUsedPct >= 90.0 ? kWarn : kMuted);

    // Guc modu rozeti
    RectF pw(side.X, y, side.Width, S(58.0f));
    if (pw.GetBottom() <= side.GetBottom()) {
        const Color pc = g.power.shouldWarn ? kWarn : kOk;
        card(g2, pw, kCard, kBorder);
        text(g2, L"GÜÇ MODU", F.tiny.get(),
             RectF(pw.X + S(14.0f), pw.Y + S(10.0f), pw.Width - S(28.0f), S(14.0f)),
             kFaint);
        text(g2, g.power.friendlyName.empty() ? L"Bilinmiyor"
                                              : toWide(g.power.friendlyName),
             F.body.get(),
             RectF(pw.X + S(14.0f), pw.Y + S(26.0f), pw.Width - S(28.0f), S(22.0f)),
             pc);
    }
}

} // namespace

// ============================================================================
//  Yakalama
// ============================================================================
namespace {

void relaunchElevated(HWND owner) {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return;
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow  = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) PostMessageW(owner, WM_CLOSE, 0, 0);
}

void startCapture(HWND hwnd) {
    if (g.capturing) {
        g.status = L"Durduruluyor, son kareler işleniyor…";
        ss::stopEtwCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (!ss::isElevated()) {
        if (MessageBoxW(hwnd,
                L"Ölçüm için yönetici hakkı gerekiyor.\n\n"
                L"Windows'un kare olaylarını dinleyebilmek için programın "
                L"yükseltilmiş çalışması gerekir. Oyununuza dokunulmaz — "
                L"yalnızca işletim sisteminin yayınladığı ölçüm olayları "
                L"pasif olarak okunur.\n\n"
                L"Yönetici olarak yeniden başlatılsın mı?",
                L"Syspect", MB_ICONINFORMATION | MB_YESNO) == IDYES)
            relaunchElevated(hwnd);
        return;
    }

    ss::SystemInfo sys;
    sys.cpuName = readCpuName();
    sys.gpuName = readGpuName();

    // ------------------------------------------------------------------
    //  SMBIOS bellek yoklamasi -> kural motoru
    // ------------------------------------------------------------------
    //  Bu veri acilista okunuyordu (g.memory) ve Bulgular sayfasinda
    //  gosteriliyordu ama diagnose()'a HIC ulasmiyordu; sonucta KURAL 2'nin
    //  hiz ve profil bacaklari arayuzde olu duruyordu. Vaka kulliyatinin
    //  yarisi o kurala cikiyor.
    //
    //  DIKKAT: burasi CANLI olcum yolu. .syscap acma yolunda bu alanlar
    //  DOLDURULMAZ — baskasinin kaydini kendi makinemizin bellek hiziyla
    //  yorumlamak, dosyayi acan herkese kendi RAM'ini teshis ettirirdi.
    if (!g.memory.modules.empty()) {
        sys.ramConfiguredMTs = g.memory.configuredMTs;
        sys.ramModuleCount   = static_cast<uint32_t>(g.memory.modules.size());
        sys.isAM5            = g.memory.likelyAM5;
        sys.isIntelDdr5      = g.memory.likelyIntelDdr5;
        sys.expoActive       =
            (g.memory.profile == ssprobe::MemorySpec::Profile::On);
        sys.ramModulesMismatched = g.memory.mixedModules;
    }

    g.captureSeconds = kDurations[g.durationIndex];
    g.capturing = true;
    g.elapsed   = 0;
    g.sys       = sys;
    g.status    = (g.captureSeconds == 0)
                ? L"Ölçüm sürüyor — siz durdurana kadar"
                : L"Ölçüm sürüyor";

    if (g.sampler) g.sampler->start();
    SetTimer(hwnd, TIMER_LIVE, 1000, nullptr);

    g.captureStartFt = sslog::nowFileTime();
    g.captureEndFt   = 0;

    const uint32_t seconds = g.captureSeconds;
    std::thread([sys, seconds]() {
        auto* out = new CaptureOutcome();
        out->sys = sys;

        // ------------------------------------------------------------------
        //  DPC toplama — kare yakalamasiyla ES ZAMANLI
        // ------------------------------------------------------------------
        //  Ayri oturum, ayri iplik. Iki oturum ayni QPC saatini kullaniyor,
        //  bu yuzden sonradan hizalanabiliyorlar (bkz. LongDpc::qpc).
        //
        //  Suresiz olcumde (seconds == 0) DPC oturumuna yine de bir tavan
        //  koymak gerekiyor: sinirsiz calisirsa ham olay listesi bellekte
        //  buyumeye devam eder. 10 dakika, hem tipik bir oyun seansini
        //  kapsayacak hem de bellegi tasirmayacak kadar.
        auto* dpc = new ssdpc::Capture();
        std::thread dpcThread([dpc, seconds]() {
            ssdpc::Options o;
            o.seconds = seconds > 0 ? seconds : 600;
            *dpc = ssdpc::run(o);
        });

        ss::EtwCaptureOptions opts;
        opts.seconds = seconds;

        const ss::EtwCaptureResult cap = ss::runEtwCapture(opts);

        // Kare yakalamasi bitti — DPC oturumunu da kapat ve bekle.
        ssdpc::stop();
        if (dpcThread.joinable()) dpcThread.join();

        if (!cap.ok()) {
            out->error = cap.error;
        } else {
            out->info = cap.info;
            out->ok   = true;
            // Kareler VectorFrameSource'a burada verilmiyor; SystemInfo
            // telemetriyle zenginlestirildikten SONRA analiz edilecek.
            out->result.frames = cap.frames;
            out->frameFirstQpc = cap.firstQpc;
            out->frameQpcFreq  = cap.qpcFreq;
            out->lostFrames    = cap.lostEvents;
        }
        out->dpc.reset(dpc);

        // Olay gunlugunu olcum BITTIKTEN sonra tara. Baslangictaki tarama
        // olcum sirasinda dusen kayitlari goremez ve o kayitlar en degerli
        // olanlardir. Bu is zaten arayuz disi bir is parcaciginda.
        out->evtlog = sslog::scan(30);

        PostMessageW(g.main, WM_CAPTURE_DONE, 0, reinterpret_cast<LPARAM>(out));
    }).detach();
}

void onCaptureDone(HWND hwnd, CaptureOutcome* raw) {
    std::unique_ptr<CaptureOutcome> out(raw);

    KillTimer(hwnd, TIMER_LIVE);
    g.capturing = false;
    if (g.sampler) g.sampler->stop();

    if (!out) return;

    if (!out->ok) {
        g.status = L"Ölçüm tamamlanamadı";
        MessageBoxW(hwnd, toWide(out->error).c_str(), L"StutterScope",
                    MB_ICONWARNING);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    g.telemetry = g.sampler ? g.sampler->samples()
                            : std::vector<sstelem::Sample>{};

    g.captureEndFt = sslog::nowFileTime();
    if (out->evtlog.ok) g.evtlog = out->evtlog;

    // Telemetri ozetini SystemInfo'ya aktar — kural motoru bunlari bekliyor.
    ss::SystemInfo sys = out->sys;
    sslog::applyTo(sys, out->evtlog, g.captureStartFt, g.captureEndFt);
    sys.medianGpuUtilPct    = medianOfKnown(g.telemetry, &sstelem::Sample::gpuUtilPct);
    sys.medianCpuUsagePct   = medianOfKnown(g.telemetry, &sstelem::Sample::cpuUsagePct);
    sys.medianDiskActivePct = medianOfKnown(g.telemetry, &sstelem::Sample::diskActivePct);
    sys.p95DiskLatencyMs    = p95OfKnown(g.telemetry, &sstelem::Sample::diskLatencyMs);
    sys.ramTotalMb          = static_cast<uint32_t>(readTotalRamMb());

    for (const auto& t : g.telemetry) {
        if (t.powerBrake)       sys.gpuPowerBrakeSeen      = true;
        if (t.thermalThrottle)  sys.gpuThermalThrottleSeen = true;
        if (t.powerCapThrottle) sys.gpuPowerCapSeen        = true;
        if (t.commitOverRam)    sys.commitExceededRam      = true;

        // Bellek icin ORTALAMA degil EN KOTU an onemli: takilma zaten o anda
        // olusuyor. Medyan alsak 3 dakikalik oturumdaki 10 saniyelik
        // sayfalama krizi tamamen kaybolurdu.
        if (sstelem::known(t.commitUsedPct) &&
            (!ss::SystemInfo::pctKnown(sys.maxCommitUsedPct) ||
             t.commitUsedPct > sys.maxCommitUsedPct))
            sys.maxCommitUsedPct = t.commitUsedPct;

        if (sstelem::known(t.availPhysMb) &&
            (!ss::SystemInfo::pctKnown(sys.minAvailPhysMb) ||
             t.availPhysMb < sys.minAvailPhysMb))
            sys.minAvailPhysMb = t.availPhysMb;
    }

    // "Oyun disinda da takiliyor mu?" sorusunu KULLANICIYA SORMUYORUZ:
    // olcumun hangi surece ait oldugundan cikariyoruz.
    const std::wstring exe = processName(out->info.processId);
    if (!exe.empty() && looksLikeDesktop(exe)) sys.desktopStutterObserved = true;

    // Yan sinyal kanali: takilma anlarindaki GPU/CPU/disk durumunu motora
    // ver. Bu olmadan DPC/VRAM/isinma/guc kurallari hicbir zaman
    // atesleyemiyordu.
    const sstelem::TelemetrySignals provider(g.telemetry,
                                             g.gpuStatic.vramTotalMb);

    ss::VectorFrameSource src(out->result.frames, out->info);
    g.result    = ss::analyzeSource(src, sys, &provider);

    // ------------------------------------------------------------------
    //  DPC suclusu — IKI GECISLI, cunku baska turlu yapilamaz
    // ------------------------------------------------------------------
    //  Kontrol grubu takilma pencerelerini gerektiriyor; pencereler ise ancak
    //  dedektor calistiktan sonra belli oluyor. Yani once analiz, sonra
    //  suclu tespiti, sonra analiz TEKRAR. Ikinci gecis pahali degil —
    //  toplanmis veri uzerinde saf hesap, milisaniyeler surer.
    //
    //  Hizalama: iki ETW oturumu ayri sifir noktasi kullaniyor ama ayni QPC
    //  saatini. Kare zaman damgalari mutlak QPC'ye cevrilip DPC olaylariyla
    //  ayni eksene tasiniyor. Bu adim atlanirsa surucu RASTGELE suclanir.
    if (out->dpc && out->dpc->ok && out->frameQpcFreq > 0) {
        ssdpc::Capture& dc = *out->dpc;

        std::vector<ssdpc::Window> windows;
        windows.reserve(g.result.events.size());
        for (const auto& e : g.result.events) {
            // Takilmaya sebep olan DPC, karenin SUNULDUGU anda degil o kare
            // uretilirken calisti. Pencere karenin kendi suresi kadar geriye
            // uzatiliyor.
            const uint64_t backUs =
                static_cast<uint64_t>(e.frameTimeMs * 1000.0) + 1000;
            const uint64_t startUs = e.timestampUs > backUs
                                   ? e.timestampUs - backUs : 0;

            ssdpc::Window w;
            w.startQpc = out->frameFirstQpc +
                         startUs * out->frameQpcFreq / 1000000ull;
            w.endQpc   = out->frameFirstQpc +
                         e.timestampUs * out->frameQpcFreq / 1000000ull;
            windows.push_back(w);
        }

        ssdpc::summarize(dc, windows);
        const ssdpc::DriverStats* suspect = ssdpc::primeSuspect(dc);

        if (suspect) {
            // Esikten gecti. Olaylara YALNIZCA bu surucunun adi yaziliyor;
            // esigi gecemeyen suruculer icin ad bos birakiliyor ve KURAL 1
            // onlar icin hic atesleyemiyor.
            const std::string name = suspect->name;
            const uint32_t idx = static_cast<uint32_t>(
                std::find(dc.driverNames.begin(), dc.driverNames.end(), name)
                - dc.driverNames.begin());

            for (size_t i = 0; i < g.result.events.size() && i < windows.size(); ++i) {
                double worst = 0.0;
                for (const auto& d : dc.events) {
                    if (d.driverIndex != idx) continue;
                    if (d.qpc < windows[i].startQpc || d.qpc > windows[i].endQpc)
                        continue;
                    if (d.ms > worst) worst = d.ms;
                }
                if (worst > 0.0) {
                    g.result.events[i].signals.dpcMaxMs = worst;
                    g.result.events[i].signals.dpcDriver = name;
                }
            }

            // Hukum yeniden veriliyor: artik olaylarda DPC bilgisi var.
            g.result.diagnosis =
                ss::diagnose(g.result.stats, g.result.events, sys);
        }

        g.dpc = std::move(dc);
        g.dpcSuspect = suspect ? suspect->name : std::string{};
    }
    g.info      = out->info;
    if (!exe.empty()) g.info.application = toUtf8(exe);
    g.sys       = sys;
    g.hasResult = true;
    g.resultFromFile = false;
    refreshFindings();
    g.selectedHyp = 0;
    g.page      = 1;

    g.status = L"Ölçüm tamamlandı · " +
               num(static_cast<double>(g.result.stats.frameCount), 0) + L" kare";
    if (!exe.empty()) g.status += L" · " + exe;

    InvalidateRect(hwnd, nullptr, FALSE);
}

std::string osBuildString() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            0, KEY_READ, &k) != ERROR_SUCCESS) return {};
    wchar_t name[128] = L"", build[64] = L"", disp[64] = L"";
    DWORD sz = sizeof(name);
    RegQueryValueExW(k, L"ProductName", nullptr, nullptr,
                     reinterpret_cast<LPBYTE>(name), &sz);
    sz = sizeof(build);
    RegQueryValueExW(k, L"CurrentBuild", nullptr, nullptr,
                     reinterpret_cast<LPBYTE>(build), &sz);
    sz = sizeof(disp);
    RegQueryValueExW(k, L"DisplayVersion", nullptr, nullptr,
                     reinterpret_cast<LPBYTE>(disp), &sz);
    RegCloseKey(k);

    std::string out = toUtf8(name);

    // ProductName Windows 11'de HALA "Windows 10 Pro" yaziyor — Microsoft
    // uyumluluk icin bilerek boyle birakti. Yapi numarasi 22000 ve ustu
    // Windows 11 demektir; duzeltmezsek rapor "Windows 10 Pro 25H2 (build
    // 26200)" gibi kendi icinde celiskili bir satir uretiyor.
    const unsigned long b = wcstoul(build, nullptr, 10);
    if (b >= 22000) {
        const size_t at = out.find("Windows 10");
        if (at != std::string::npos) out.replace(at, 10, "Windows 11");
    }

    if (disp[0])  out += " " + toUtf8(disp);
    if (build[0]) out += " (build " + toUtf8(build) + ")";
    return out;
}

std::string nowString() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[64];
    std::snprintf(b, sizeof(b), "%04u-%02u-%02u %02u:%02u",
                  t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    return b;
}

// Rapor ureticilerinin ihtiyac duydugu her seyi tek yerde toplar.
ssreport::Bundle makeBundle() {
    ssreport::Bundle b;
    b.result    = g.hasResult ? &g.result : nullptr;
    b.source    = g.hasResult ? &g.info   : nullptr;
    b.sys       = &g.sys;
    b.telemetry = &g.telemetry;
    b.gpu       = &g.gpuStatic;
    b.power     = &g.power;
    b.memory    = &g.memory;
    b.devices   = &g.devices;
    b.dumps     = &g.dumps;
    b.evtlog    = &g.evtlog;

    // Bulgular sayfasiyla ayni kural: dosyadan gelen kaydin raporuna bu
    // makinenin hicbir yerel yoklamasi eklenmez. Paylasilan metin en cok
    // yaniltma potansiyeli olan yer — karsi taraf her satiri kaydin sahibine
    // ait sanir.
    b.cpuName   = readCpuName();
    b.gpuName   = readGpuName();
    b.ramInstalledMb = readTotalRamMb();
    b.osBuild   = osBuildString();
    b.timestamp = nowString();

    if (g.resultFromFile) {
        b.power   = nullptr;
        b.memory  = nullptr;
        b.devices = nullptr;
        b.dumps   = nullptr;
        b.evtlog  = g.fileEvtlog.attempted ? &g.fileEvtlog : nullptr;

        // Donanim adlari da kaydin sahibine ait olmali; yoksa rapor baskasinin
        // olcumunu bu makinenin islemcisiyle etiketler.
        if (!g.sys.cpuName.empty())   b.cpuName        = g.sys.cpuName;
        if (!g.sys.gpuName.empty())   b.gpuName        = g.sys.gpuName;
        if (g.sys.ramTotalMb > 0)     b.ramInstalledMb = g.sys.ramTotalMb;
        if (!g.fileOsBuild.empty())   b.osBuild        = g.fileOsBuild;
    }
    return b;
}

bool writeFileUtf8(const wchar_t* path, const std::string& data) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    const bool ok = WriteFile(f, data.data(),
                              static_cast<DWORD>(data.size()), &w, nullptr) != 0;
    CloseHandle(f);
    return ok;
}

// ----------------------------------------------------------------------------
//  Kayit dosyasi: disa aktar / ac
// ----------------------------------------------------------------------------
void exportCapture(HWND owner) {
    if (!g.hasResult) return;
    wchar_t path[MAX_PATH] = L"syspect-kayit.syscap";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"Syspect kaydı\0*.syscap\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"syscap";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    sscap::Capture c;
    c.application    = g.info.application;
    c.processId      = g.info.processId;
    c.cpuName        = readCpuName();
    c.gpuName        = readGpuName();
    c.osBuild        = osBuildString();
    c.ramInstalledMb = readTotalRamMb();
    c.vramTotalMb    = g.gpuStatic.vramTotalMb;
    c.createdAt      = nowString();
    c.note           = g.info.note;
    c.frames         = g.result.frames;
    c.telemetry      = g.telemetry;

    // Olay gunlugu ve olcum penceresi de gider: karsi taraf ayni hukmu
    // uretebilsin diye. Dosyadan yuklenmis bir kaydi yeniden disa
    // aktariyorsak o kaydin KENDI gunlugu tasinir, bu makineninki degil.
    c.evtlog         = g.resultFromFile ? g.fileEvtlog : g.evtlog;
    c.captureStartFt = g.captureStartFt;
    c.captureEndFt   = g.captureEndFt;

    std::string err;
    g.status = sscap::save(path, c, err)
        ? L"Kayıt dışa aktarıldı — bu dosyayı foruma bırakabilirsiniz"
        : (L"Kayıt yazılamadı: " + toWide(err));
}

// Dosya secme penceresi. Surukle-birak zaten vardi ama kesfedilemiyordu.
void browseAndOpenCapture(HWND owner);

// Kaydi diskten yukleyip yeniden analiz eder. Kural motoru degistiginde
// eski bir vakanin hukmunun degisip degismedigi boyle gorulur.
void openCapture(HWND hwnd, const std::wstring& path) {
    sscap::Capture c;
    std::string err;
    if (!sscap::load(path, c, err)) {
        MessageBoxW(hwnd, toWide(err).c_str(), L"Syspect", MB_ICONWARNING);
        return;
    }

    ss::SystemInfo sys;
    sys.cpuName    = c.cpuName;
    sys.gpuName    = c.gpuName;
    sys.ramTotalMb = static_cast<uint32_t>(c.ramInstalledMb);

    // Kaydin KENDI olay gunlugu — bu makinenin degil. Olcum penceresi de
    // dosyadan geldigi icin zaman cakismasi hesabi karsi tarafta da aynen
    // calisir; yani TDR "olcum sirasinda oldu" hukmu tasinabilir.
    g.fileEvtlog  = c.evtlog;
    g.fileOsBuild = c.osBuild;
    sslog::applyTo(sys, c.evtlog, c.captureStartFt, c.captureEndFt);

    g.telemetry = c.telemetry;
    sys.medianGpuUtilPct    = medianOfKnown(g.telemetry, &sstelem::Sample::gpuUtilPct);
    sys.medianCpuUsagePct   = medianOfKnown(g.telemetry, &sstelem::Sample::cpuUsagePct);
    sys.medianDiskActivePct = medianOfKnown(g.telemetry, &sstelem::Sample::diskActivePct);
    sys.p95DiskLatencyMs    = p95OfKnown(g.telemetry, &sstelem::Sample::diskLatencyMs);
    for (const auto& t : g.telemetry) {
        if (t.powerBrake)       sys.gpuPowerBrakeSeen      = true;
        if (t.thermalThrottle)  sys.gpuThermalThrottleSeen = true;
        if (t.powerCapThrottle) sys.gpuPowerCapSeen        = true;
        if (t.commitOverRam)    sys.commitExceededRam      = true;
        if (sstelem::known(t.commitUsedPct) &&
            (!ss::SystemInfo::pctKnown(sys.maxCommitUsedPct) ||
             t.commitUsedPct > sys.maxCommitUsedPct))
            sys.maxCommitUsedPct = t.commitUsedPct;
        if (sstelem::known(t.availPhysMb) &&
            (!ss::SystemInfo::pctKnown(sys.minAvailPhysMb) ||
             t.availPhysMb < sys.minAvailPhysMb))
            sys.minAvailPhysMb = t.availPhysMb;
    }

    ss::FrameSourceInfo info;
    info.kind        = "syscap";
    info.application = c.application;
    info.processId   = c.processId;
    info.note        = c.note.empty()
                     ? (std::to_string(c.frames.size()) + " kare, dosyadan okundu")
                     : c.note;

    const sstelem::TelemetrySignals provider(g.telemetry, c.vramTotalMb);
    ss::VectorFrameSource src(c.frames, info);

    g.result    = ss::analyzeSource(src, sys, &provider);
    g.info      = info;
    g.sys       = sys;
    g.gpuStatic.vramTotalMb = c.vramTotalMb;
    g.hasResult = true;
    g.resultFromFile = true;
    refreshFindings();
    g.selectedHyp = 0;
    g.page      = 1;
    g.status    = L"Kayıt yüklendi · " + toWide(c.application) + L" · " +
                  num(static_cast<double>(c.frames.size()), 0) + L" kare";
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Surum denetimini arka iplikte baslatir. Sonuc pencereye mesajla doner —
// ag cagrisi cizim ipligini ASLA bloklamamali.
void startUpdateCheck(HWND hwnd) {
    if (g.updBusy) return;
    g.updBusy = true;
    g.upd = ssupd::Result{};

    std::thread([hwnd]() {
        auto* r = new ssupd::Result(ssupd::check());
        if (!PostMessageW(hwnd, WM_UPDATE_DONE, 0,
                          reinterpret_cast<LPARAM>(r)))
            delete r;
    }).detach();
}

// Toplanan metinleri .lang sablonu olarak yazar. Ayni dosya varsa icindeki
// cevirileri KORUR — sablonu yeniden uretmek yapilmis isi silmemeli.
void exportLangTemplate(HWND owner) {
    wchar_t path[MAX_PATH] = L"en.lang";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"Syspect dil dosyası\0*.lang\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"lang";
    ofn.lpstrTitle  = L"Çeviri şablonunu kaydet";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    // Dosya adindan dil kodunu cikar: "en.lang" -> "en". Kullanici sonradan
    // dosyanin icinden de degistirebilir.
    std::wstring base(path);
    const size_t slash = base.find_last_of(L'\\');
    if (slash != std::wstring::npos) base = base.substr(slash + 1);
    const size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);

    std::string err;
    const std::string code = toUtf8(base);
    if (ss18::writeTemplate(path, code, code, err)) {
        g.status = L"Şablon kaydedildi · " +
                   num(static_cast<double>(ss18::collectedCount()), 0) +
                   L" metin. Çevirip lang klasörüne koyun.";
    } else {
        g.status = L"Şablon yazılamadı: " + toWide(err);
    }
}

void browseAndOpenCapture(HWND owner) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"Syspect kaydı\0*.syscap\0Tüm dosyalar\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Paylaşılan bir ölçüm kaydı açın";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) openCapture(owner, path);
}

void saveReport(HWND owner) {
    wchar_t path[MAX_PATH] = L"syspect-rapor.md";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    // Varsayilan markdown: foruma ve dil modeline yapistirmaya en uygun
    // bicim. HTML gozle bakmak icin ikinci secenek.
    ofn.lpstrFilter  = L"Markdown — forum / yapay zekâ\0*.md\0"
                       L"HTML — tarayıcıda görüntülemek için\0*.html\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrDefExt  = L"md";
    ofn.nFilterIndex = 1;
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    const bool html = (ofn.nFilterIndex == 2);
    const std::string data = html
        ? (g.hasResult ? ss::renderHtmlReport(g.result, g.info, g.sys,
                                              ss18::translator())
                       : std::string("<p>Henüz ölçüm yapılmadı.</p>"))
        : ssreport::buildDiagnosticLog(makeBundle());

    g.status = writeFileUtf8(path, data) ? L"Rapor kaydedildi"
                                         : L"Rapor yazılamadı";
}

void copyReport(HWND owner) {
    // Olcum yapilmamis olsa bile kopyalanabilir: donanim envanteri, guc
    // plani, EXPO/ReBAR durumu ve mavi ekran kayitlari tek baslarina bile
    // foruma sorulacak bir soruyu tasiyor.
    const std::wstring t = toWide(ssreport::buildShareText(makeBundle()));
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (t.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(m)) {
            memcpy(p, t.c_str(), bytes);
            GlobalUnlock(m);
            SetClipboardData(CF_UNICODETEXT, m);
        }
    }
    CloseClipboard();
    g.status = L"Rapor panoya kopyalandı — foruma veya yapay zekâya yapıştırabilirsiniz";
}

// ----------------------------------------------------------------------------
//  Ana cizim
// ----------------------------------------------------------------------------
void paintAll(HDC dc, const RECT& client) {
    Graphics g2(dc);
    g2.SetSmoothingMode(SmoothingModeAntiAlias);
    g2.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    SolidBrush bg(kBg);
    g2.FillRectangle(&bg, 0, 0, client.right, client.bottom);

    g.hotspots.clear();

    const float pad = S(20.0f);

    // --- Yonetici uyarisi: en ustte, tam genislikte ---
    if (!ss::isElevated()) {
        RectF b(0.0f, 0.0f, static_cast<float>(client.right), bannerHeight());
        // Serit zemini TEMAYA bagli. Sabit koyu kahve yaziliydi ve acik temada
        // sayfanin ortasinda kara bir bant gibi duruyordu. Iki temada da
        // "turuncunun cok kisik hali" olmali, ayni renk degil.
        SolidBrush bb(gDarkMode ? Color(255, 0x36, 0x24, 0x12)
                                : Color(255, 0xFD, 0xF0, 0xE6));
        g2.FillRectangle(&bb, b);
        Pen edge(kAccentDim, 1.0f);
        g2.DrawLine(&edge, 0.0f, b.GetBottom() - 0.5f,
                    static_cast<float>(client.right), b.GetBottom() - 0.5f);

        RectF icon(pad, b.Height / 2 - S(9.0f), S(18.0f), S(18.0f));
        card(g2, icon, kAccent, Color(0, 0, 0, 0), 9.0f);
        text(g2, L"!", F.h2.get(), icon, Color(255, 0x1A, 0x0E, 0x05),
             StringAlignmentCenter, StringAlignmentCenter);

        // Serit metni kendi zeminine gore secilir; kMuted acik seritte
        // okunmuyordu.
        const Color bannerStrong = gDarkMode ? kAccent : Color(255, 0x9A, 0x45, 0x0C);
        const Color bannerSoft   = gDarkMode ? kMuted  : Color(255, 0x7A, 0x5A, 0x42);

        text(g2, L"Yönetici olarak çalışmıyor — ölçüm alınamaz.", F.h2.get(),
             RectF(pad + S(28.0f), S(8.0f), b.Width - S(260.0f), S(19.0f)),
             bannerStrong);
        text(g2, L"Windows kare olaylarını yalnızca yükseltilmiş süreçlere verir. "
                 L"Oyununuza dokunulmaz.", F.sm.get(),
             RectF(pad + S(28.0f), S(27.0f), b.Width - S(260.0f), S(18.0f)),
             bannerSoft);

        RectF btn(b.Width - pad - S(174.0f), S(10.0f), S(174.0f), S(32.0f));
        card(g2, btn, kAccent, kAccent, 7.0f);
        text(g2, L"Yönetici olarak aç", F.h2.get(), btn,
             Color(255, 0xFF, 0xFF, 0xFF),
             StringAlignmentCenter, StringAlignmentCenter);
        addHotspot(btn, HS_ELEVATE);
    }

    // --- Ust serit ---
    //  Simge kaldirildi: 34 px'lik bir marka isareti serit yuksekligine gore
    //  buyuk kaliyor ve yaninda duran kelime markasiyla yarisiyordu. Ad tek
    //  basina zaten kimligi tasiyor. Simge dosyasi (syspect.ico) duruyor —
    //  pencere ve gorev cubugu onu kullanmaya devam ediyor.
    const float hy = headerTop();
    text(g2, g.appName, F.h1.get(),
         RectF(pad, hy + S(20.0f), S(200.0f), S(34.0f)), kText);

    const wchar_t* navText[6] = {L"Ölçüm", L"Sonuçlar", L"Grafik",
                                 L"Mavi Ekran", L"Sistem", L"Ayarlar"};
    float nx = pad + S(152.0f);   // simge kalkinca sekmeler sola kaydi
    for (int i = 0; i < 6; ++i) {
        const float nw = (i == 3) ? S(102.0f) : S(84.0f);
        RectF r(nx, hy + S(22.0f), nw, S(32.0f));
        const bool on  = (g.page == i);
        const bool hot = (g.hover == HS_NAV0 + i);
        if (on)       card(g2, r, kCardHi, kAccentDim, 8.0f);
        else if (hot) card(g2, r, kCard, kBorder, 8.0f);
        text(g2, navText[i], on ? F.h2.get() : F.body.get(), r,
             on ? kText : kMuted, StringAlignmentCenter, StringAlignmentCenter);
        // Mavi ekran kaydi varsa sekmede kirmizi nokta
        if (i == 3 && !g.dumps.findings.empty()) {
            RectF d(r.GetRight() - S(14.0f), r.Y + S(7.0f), S(7.0f), S(7.0f));
            card(g2, d, kDanger, Color(0, 0, 0, 0), 3.5f);
        }
        addHotspot(r, HS_NAV0 + i);
        nx += nw + S(4.0f);
    }

    // Kayit dugmesi — sag ust
    RectF rec(client.right - pad - S(168.0f), hy + S(20.0f), S(168.0f), S(38.0f));
    const Color rbg = g.capturing
        ? (g.hover == HS_RECORD ? Color(255, 0xF8, 0x5C, 0x5C) : kDanger)
        : (g.hover == HS_RECORD ? Color(255, 0x2E, 0xD4, 0x6D) : kAccent);
    card(g2, rec, rbg, Color(0, 0, 0, 0), 9.0f);
    RectF dot(rec.X + S(16.0f), rec.Y + rec.Height / 2 - S(5.0f), S(10.0f), S(10.0f));
    card(g2, dot, Color(255, 0x0B, 0x14, 0x0E), Color(0, 0, 0, 0), 5.0f);
    text(g2, g.capturing ? L"Durdur" : L"Kaydı Başlat", F.h2.get(),
         RectF(rec.X + S(32.0f), rec.Y, rec.Width - S(40.0f), rec.Height),
         Color(255, 0x08, 0x14, 0x0C),
         StringAlignmentCenter, StringAlignmentCenter);
    addHotspot(rec, HS_RECORD);

    // --- Icerik ---
    const RectF area = contentRect(client);
    switch (g.page) {
        case 0: drawMeasurePage(g2, area); break;
        case 1: drawResultsPage(g2, area); break;
        case 2: drawGraphPage(g2, area);   break;
        case 3: drawBsodPage(g2, area);    break;
        case 5: drawSettingsPage(g2, area); break;
        default: drawSystemPage(g2, area); break;
    }

    drawSidebar(g2, client);

    // --- Alt serit ---
    Pen sep(kBorder, 1.0f);
    const float fy = static_cast<float>(client.bottom) - S(34.0f);
    g2.DrawLine(&sep, 0.0f, fy, static_cast<float>(client.right), fy);
    text(g2, g.status, F.sm.get(),
         RectF(pad, fy + S(9.0f), client.right - pad * 2 - S(140.0f), S(20.0f)),
         kMuted);
    text(g2, ss::isElevated() ? L"Yönetici" : L"Standart kullanıcı",
         F.sm.get(),
         RectF(client.right - pad - S(160.0f), fy + S(9.0f), S(160.0f), S(20.0f)),
         ss::isElevated() ? kOk : kFaint, StringAlignmentFar);
}

int hitTest(int x, int y) {
    const PointF p(static_cast<float>(x), static_cast<float>(y));
    for (const Hotspot& h : g.hotspots)
        if (h.r.Contains(p)) return h.id;
    return HS_NONE;
}

void onClick(HWND hwnd, int id) {
    if (id >= HS_NAV0 && id <= HS_NAV_LAST) { g.page = id - HS_NAV0; return; }

    // Dil secimi. Tercih HEMEN kaydediliyor — "Uygula" dugmesi yok, cunku
    // secim aninda ekranda etkisini gorursunuz; ayrica bir onay adimi
    // eklemek tek secimlik bir ayar icin gereksiz.
    if (id >= HS_LANG_BASE) {
        const auto& langs = ss18::available();
        const size_t idx = static_cast<size_t>(id - HS_LANG_BASE);
        if (idx < langs.size()) {
            ss18::setLanguage(langs[idx].code);
            ss18::savePreferredCode(langs[idx].code);
            refreshFindings();   // bulgu metinleri yeni dilde uretilsin
        }
        return;
    }
    if (id >= HS_DUR0 && id <= HS_DUR3) { g.durationIndex = id - HS_DUR0; return; }
    if (id >= HS_HYP_BASE)              { g.selectedHyp = id - HS_HYP_BASE; return; }
    switch (id) {
        case HS_RECORD:  startCapture(hwnd);    break;
        case HS_SAVE:    saveReport(hwnd);      break;
        case HS_COPY:    copyReport(hwnd);      break;
        case HS_ELEVATE: relaunchElevated(hwnd);break;
        case HS_EXPORT:  exportCapture(hwnd);   break;
        case HS_OPEN:    browseAndOpenCapture(hwnd); break;
        case HS_LANGTPL: exportLangTemplate(hwnd); break;
        case HS_UPDCHK:  startUpdateCheck(hwnd);   break;
        case HS_UPDOPEN:
            if (!g.upd.pageUrl.empty())
                ShellExecuteW(hwnd, L"open", toWide(g.upd.pageUrl).c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case HS_UPDTOGGLE: ssupd::setEnabled(!ssupd::enabled()); break;
        case HS_THEME:     applyTheme(!gDarkMode); saveThemePref(gDarkMode); break;
        case HS_FEEDBACK: {
            // Surum URL'e ekleniyor ki hangi surumden geldigi belli olsun.
            // Sayfa bunu kullaniciya GOSTERIYOR — gizlice eklenen bir alan
            // degil.
            const std::wstring url =
                L"https://" L"" SS_UPDATE_HOST L"/syspect/geri-bildirim?v="
                L"" SS_VERSION_STRING;
            ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
            break;
        }
        default: break;
    }
}

// ----------------------------------------------------------------------------
//  Pencere yordami
// ----------------------------------------------------------------------------
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.main = hwnd;
            g.dpi  = static_cast<int>(GetDpiForWindow(hwnd));

            F.family = std::make_unique<FontFamily>(L"Segoe UI");
            // Tipografi olcegi. Ilk surumde h2 ile body ayni boyuttaydi
            // (ikisi de 14 px) — sadece kalinlik degisiyordu ve bu hiyerarsi
            // yaratmiyordu. Simdi her kademe bir oncekinden belirgin farkli.
            F.big    = std::make_unique<Font>(F.family.get(), S(52.0f), FontStyleBold,   UnitPixel);
            F.h1     = std::make_unique<Font>(F.family.get(), S(23.0f), FontStyleBold,   UnitPixel);
            F.h2     = std::make_unique<Font>(F.family.get(), S(15.5f), FontStyleBold,   UnitPixel);
            F.body   = std::make_unique<Font>(F.family.get(), S(13.5f), FontStyleRegular,UnitPixel);
            F.sm     = std::make_unique<Font>(F.family.get(), S(12.0f), FontStyleRegular,UnitPixel);
            F.tiny   = std::make_unique<Font>(F.family.get(), S(10.0f), FontStyleBold,   UnitPixel);

            g.sampler = new sstelem::Sampler();
            g.caps    = g.sampler->capabilities();
            g.live    = g.sampler->readNow(0);
            g.gpuStatic = g.sampler->gpuStatic();
            g.power     = ssprobe::readPowerPlan();
            g.devices   = ssprobe::scanProblemDevices();
            g.memory    = ssprobe::readMemorySpec();
            g.firmware  = ssprobe::readFirmwareInfo();
            g.storage   = ssstore::scan();

            // Tema, ilk cizimden ONCE uygulanmali; sonra uygulanirsa acilis
            // karesi koyu, ikincisi acik cizilir ve goze carpar.
            applyTheme(loadThemePref());

            // Dil: kayitli tercih varsa o, yoksa Windows'un goruntu dilinden
            // makul bir baslangic. Turkce icin dosya gerekmez ve tercih
            // bulunamazsa da guvenle Turkce'de kalir.
            {
                std::string code = ss18::loadPreferredCode();
                if (code.empty()) code = ss18::systemDefaultCode();
                ss18::setLanguage(code);
            }

            // Surum denetimi — kullanici kapatmadiysa. Arka iplikte, acilisi
            // geciktirmeden; basarisiz olmasi da onemli degil.
            if (ssupd::enabled()) startUpdateCheck(hwnd);
            g.dumps     = ssdump::scanSystemDumps();
            refreshFindings();

            // Olay gunlugu arka planda taranir. Gurultulu bir makinede 30
            // gunluk WHEA gecmisi binlerce kayittir; burada beklenirse
            // pencere acilmadan once donar.
            std::thread([]() {
                auto* s = new sslog::Scan(sslog::scan(30));
                if (!PostMessageW(g.main, WM_EVTLOG_DONE, 0,
                                  reinterpret_cast<LPARAM>(s)))
                    delete s;
            }).detach();

            // Canli panel saniyede bir tazelenir. Olcum yokken de calisir ama
            // tek yaptigi birkac sayac okumak — olculebilir yuk uretmez.
            SetTimer(hwnd, TIMER_LIVE, 1000, nullptr);
            return 0;
        }

        case WM_ERASEBKGND: return 1;

        // Kayit dosyasini pencereye surukleyip birakma. Foruma .syscap
        // birakan kullanicinin karsi tarafi icin en kisa yol bu.
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            wchar_t dropped[MAX_PATH] = L"";
            if (DragQueryFileW(drop, 0, dropped, MAX_PATH) > 0)
                openCapture(hwnd, dropped);
            DragFinish(drop);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);

            // Cift tamponlama: bitmap yalnizca boyut degisince yeniden ayrilir
            if (!g.memDc || g.memW != rc.right || g.memH != rc.bottom) {
                if (g.memBmp) DeleteObject(g.memBmp);
                if (g.memDc)  DeleteDC(g.memDc);
                g.memDc  = CreateCompatibleDC(dc);
                g.memBmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
                SelectObject(g.memDc, g.memBmp);
                g.memW = rc.right;
                g.memH = rc.bottom;
            }

            paintAll(g.memDc, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, g.memDc, 0, 0, SRCCOPY);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_GETMINMAXINFO: {
            auto* m = reinterpret_cast<MINMAXINFO*>(lp);
            m->ptMinTrackSize.x = Si(860);
            m->ptMinTrackSize.y = Si(760);   // bulgu listesine nefes payi
            return 0;
        }

        case WM_MOUSEMOVE: {
            const int id = hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (id != g.hover) {
                g.hover = id;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }

        // Bulgu listesi fare tekerlegiyle kayar. Yalnizca Sonuclar
        // sayfasinda ve liste tasiyorsa is yapar.
        case WM_MOUSEWHEEL: {
            if (g.page != 1 || g.findings.empty()) return 0;
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            const size_t maxScroll = (g.findings.size() > g.findingsFit)
                                   ? g.findings.size() - g.findingsFit : 0;
            const size_t before = g.findingScroll;
            if (delta < 0 && g.findingScroll < maxScroll)      ++g.findingScroll;
            else if (delta > 0 && g.findingScroll > 0)         --g.findingScroll;
            if (before != g.findingScroll) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSELEAVE:
            if (g.hover != HS_NONE) {
                g.hover = HS_NONE;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && g.hover != HS_NONE) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;

        // Komut satirindan gelen kayit dosyasi. WM_CREATE bittikten sonra
        // islensin diye mesajla geciktiriliyor; isaretci _wcsdup ile ayrildi.
        case WM_APP + 2: {
            wchar_t* p = reinterpret_cast<wchar_t*>(lp);
            if (p) { openCapture(hwnd, p); free(p); }
            return 0;
        }

        case WM_UPDATE_DONE: {
            auto* r = reinterpret_cast<ssupd::Result*>(lp);
            g.updBusy = false;
            if (r) {
                g.upd = *r;
                delete r;
                // Yeni surum varsa alt seritte de soyle — kullanici Ayarlar
                // sayfasina hic girmeyebilir.
                if (g.upd.available)
                    g.status = L"Yeni sürüm var: " + toWide(g.upd.latestTag) +
                               L" — Ayarlar sayfasından indirebilirsiniz";
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN:
            g.pressed = hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONUP: {
            const int id = hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (id != HS_NONE && id == g.pressed) onClick(hwnd, id);
            g.pressed = HS_NONE;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_TIMER:
            if (wp == TIMER_LIVE) {
                if (g.sampler) g.live = g.sampler->readNow(g.elapsed);
                if (g.capturing) ++g.elapsed;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_CAPTURE_DONE:
            onCaptureDone(hwnd, reinterpret_cast<CaptureOutcome*>(lp));
            return 0;

        case WM_EVTLOG_DONE: {
            std::unique_ptr<sslog::Scan> s(reinterpret_cast<sslog::Scan*>(lp));
            // Olcum bittiyse elimizde daha YENI bir tarama var; acilistaki
            // gec kalan sonuc onun uzerine yazmamali.
            if (s && g.captureEndFt == 0) {
                g.evtlog = *s;
                refreshFindings();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_CLOSE:
            if (g.capturing &&
                MessageBoxW(hwnd, L"Ölçüm sürüyor. Şimdi çıkarsanız sonuç "
                                  L"kaydedilmez.\n\nYine de çıkılsın mı?",
                            L"Syspect", MB_ICONQUESTION | MB_YESNO) != IDYES)
                return 0;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            ss::stopEtwCapture();
            if (g.sampler) { g.sampler->stop(); delete g.sampler; g.sampler = nullptr; }
            if (g.memBmp) DeleteObject(g.memBmp);
            if (g.memDc)  DeleteDC(g.memDc);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

// ============================================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    GdiplusStartupInput gdiInput;
    if (GdiplusStartup(&g.gdiToken, &gdiInput, nullptr) != Ok) return 1;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"SyspectWindow";

    // Simge kaynagi (ss_ui.rc'de ID 1). Bunu atamazsak exe'nin icinde simge
    // GOMULU OLSA BILE pencere basligi ve Alt+Tab varsayilan simgeyi
    // gosterir; Explorer'daki simge ile pencerenin simgesi ayri yollardan
    // gelir. hIconSm ayrica veriliyor ki 16 px'te dogru varyant kullanilsin.
    wc.hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(1),
        IMAGE_ICON, GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(1),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"SyspectWindow", L"Syspect",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1020, 720,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    // Baslik cubugunu koyu tema yap (Win10 20H1+). Basarisiz olursa acik
    // baslik cubuguyla devam eder, sorun degil.
    BOOL dark = TRUE;
    using DwmSetAttr = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        if (auto fn = reinterpret_cast<DwmSetAttr>(
                reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute"))))
            fn(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
        FreeLibrary(dwm);
    }

    DragAcceptFiles(hwnd, TRUE);

    // Komut satirinda kayit dosyasi verildiyse ac: cift tiklamayla .syscap
    // acilabilsin diye.
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            if (argc > 1 && argv[1][0] != L'-')
                PostMessageW(hwnd, WM_APP + 2, 0,
                             reinterpret_cast<LPARAM>(_wcsdup(argv[1])));
            LocalFree(argv);
        }
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // GDI+ nesneleri GDI+'tan ONCE olmeli. Bu satir olmadan yazi tipleri
    // statik yikimda, yani GdiplusShutdown'dan sonra yikiliyor ve program
    // cikista erisim ihlaliyle cokuyor.
    F.release();

    GdiplusShutdown(g.gdiToken);
    return static_cast<int>(msg.wParam);
}
