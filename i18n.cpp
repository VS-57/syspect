// ============================================================================
//  Syspect — dil katmani (uygulama)
//  Bicim ve gerekce icin bkz. i18n.h
// ============================================================================
#ifdef _WIN32

#include "i18n.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace ss18 {
namespace {

// Kaynak dil. Tablo bos oldugunda T() kaynagi aynen dondurur; yani Turkce
// mod hicbir arama yapmaz ve hicbir sekilde bozulamaz.
constexpr char kSourceCode[] = "tr";

struct State {
    std::string code = kSourceCode;

    // Ceviri tablosu. Anahtar UTF-8 kaynak metin.
    std::map<std::string, std::string>   utf8;
    // T(const wchar_t*) ham isaretci donduruyor; donen metnin omru tabloya
    // bagli olmali. Genis surumu ayri tutuyoruz ki her cagrida donusum
    // yapilmasin — cizim dongusu saniyede onlarca kez buradan geciyor.
    std::map<std::wstring, std::wstring> wide;

    std::vector<Language> langs;
    bool scanned = false;

    // Gosterilen metinlerin kaydi — sablon uretimi icin. std::set: siralı
    // kalsin ki uretilen sablon her calistirmada ayni sirada olsun ve
    // surum kontrolunde anlamsiz fark uretmesin.
    std::set<std::string> seen;
};

State& st() { static State s; return s; }

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                      static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    const size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

// "\n" kacisi cozulur; dosya bicimi tek satirlik oldugu icin cok satirli
// metinler boyle yaziliyor.
std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case 'n':  out += '\n'; ++i; continue;
                case 't':  out += '\t'; ++i; continue;
                case '\\': out += '\\'; ++i; continue;
                default: break;
            }
        }
        out += s[i];
    }
    return out;
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// UTF-8 BOM'u atla — Not Defteri'nde kaydedilen dosyalarda hep bulunur ve
// atlanmazsa ilk satirin basina gorunmez bir karakter yapisir.
void stripBom(std::string& line) {
    if (line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
        line.erase(0, 3);
}

// Dosyanin yalnizca BASLIGINI okur (ad ve kod). Dil listesini kurarken
// binlerce satirlik tablolari bosuna okumamak icin.
bool readHeader(const std::wstring& path, Language& out) {
    std::ifstream f(path);
    if (!f) return false;

    std::string line;
    int guard = 0;
    while (std::getline(f, line) && guard++ < 64) {
        stripBom(line);
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("@name", 0) == 0) out.name = trim(line.substr(5));
        else if (line.rfind("@code", 0) == 0) out.code = trim(line.substr(5));
        else if (line[0] == '~') break;   // basliktan cikti
    }
    if (out.code.empty()) return false;
    if (out.name.empty()) out.name = out.code;
    out.path = path;
    return true;
}

void scanLanguages() {
    State& s = st();
    s.langs.clear();

    // Kaynak dil daima ilk sirada ve dosyasiz.
    Language tr;
    tr.code = kSourceCode;
    tr.name = "Türkçe";
    s.langs.push_back(tr);

    const std::wstring dir = exeDir() + L"\\lang";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*.lang").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            Language L;
            if (!readHeader(dir + L"\\" + fd.cFileName, L)) continue;
            if (L.code == kSourceCode) continue;      // kaynak dil ezilemez
            bool dup = false;
            for (const auto& e : s.langs) if (e.code == L.code) dup = true;
            if (!dup) s.langs.push_back(std::move(L));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    s.scanned = true;
}

bool loadTable(const std::wstring& path) {
    State& s = st();
    s.utf8.clear();
    s.wide.clear();

    std::ifstream f(path);
    if (!f) return false;

    std::string line, src;
    bool haveSrc = false;
    while (std::getline(f, line)) {
        stripBom(line);
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == '@') continue;

        if (t[0] == '~') {
            src     = unescape(trim(t.substr(1)));
            haveSrc = true;
        } else if (t[0] == '=' && haveSrc) {
            const std::string dst = unescape(trim(t.substr(1)));
            // Bos ceviri = "henuz cevrilmedi". Tabloya KOYULMAZ; koyulsaydi
            // ekranda bos satir cikardi. Kaynak metne geri donmek dogru.
            if (!dst.empty() && !src.empty()) {
                s.utf8[src] = dst;
                s.wide[toWide(src)] = toWide(dst);
            }
            haveSrc = false;
        }
    }
    return true;
}

// Yer tutucu {1}..{9}. core.h'deki EvidencePart::format ile AYNI bicim —
// ceviri yapan kisi iki farkli kural ogrenmek zorunda kalmamali.
//
// Neden % degil: Turkce yuzdeyi sayidan once yaziyor ("%40", "%5'i"), yani
// metinlerin icinde bol bol % geciyor ve yer tutucuyla cakisiyordu.
template <typename S>
S formatWith(const S& tpl, const std::vector<S>& args) {
    S out;
    out.reserve(tpl.size() + 32);
    for (size_t i = 0; i < tpl.size(); ++i) {
        if (tpl[i] == '{' && i + 2 < tpl.size() &&
            tpl[i + 1] >= '1' && tpl[i + 1] <= '9' && tpl[i + 2] == '}') {
            const size_t idx = static_cast<size_t>(tpl[i + 1] - '1');
            if (idx < args.size()) { out += args[idx]; i += 2; continue; }
        }
        out += tpl[i];
    }
    return out;
}

constexpr wchar_t kRegPath[] = L"Software\\Syspect";
constexpr wchar_t kRegName[] = L"Language";

} // namespace

const std::vector<Language>& available() {
    if (!st().scanned) scanLanguages();
    return st().langs;
}

bool setLanguage(const std::string& code) {
    State& s = st();
    if (!s.scanned) scanLanguages();

    if (code == kSourceCode || code.empty()) {
        s.code = kSourceCode;
        s.utf8.clear();
        s.wide.clear();
        return true;
    }
    for (const auto& L : s.langs) {
        if (L.code != code) continue;
        if (loadTable(L.path)) { s.code = code; return true; }
        break;
    }
    // Bulunamadi ya da okunamadi: kaynak dile don. Sessizce yanlis dilde
    // kalmaktansa bilinen bir duruma donmek dogru.
    s.code = kSourceCode;
    s.utf8.clear();
    s.wide.clear();
    return false;
}

const std::string& currentCode() { return st().code; }

const wchar_t* T(const wchar_t* src) {
    const State& s = st();
    if (s.wide.empty() || !src) return src;
    const auto it = s.wide.find(src);
    return it == s.wide.end() ? src : it->second.c_str();
}

std::string T(const std::string& src) {
    const State& s = st();
    if (s.utf8.empty()) return src;
    const auto it = s.utf8.find(src);
    return it == s.utf8.end() ? src : it->second;
}

std::wstring Tf(const wchar_t* src, const std::vector<std::wstring>& args) {
    return formatWith(std::wstring(T(src)), args);
}

std::string Tf(const std::string& src, const std::vector<std::string>& args) {
    return formatWith(T(src), args);
}

std::string loadPreferredCode() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0, KEY_READ, &k)
            != ERROR_SUCCESS)
        return {};
    wchar_t buf[32] = L"";
    DWORD sz = sizeof(buf), type = 0;
    const bool ok = RegQueryValueExW(k, kRegName, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(buf), &sz)
                    == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(k);
    return ok ? toUtf8(buf) : std::string{};
}

void savePreferredCode(const std::string& code) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegPath, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    const std::wstring w = toWide(code);
    RegSetValueExW(k, kRegName, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(w.c_str()),
                   static_cast<DWORD>((w.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

std::string systemDefaultCode() {
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = L"";
    if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) == 0)
        return kSourceCode;

    // "tr-TR" -> "tr". Yalnizca dil kismi ilgilendiriyor.
    std::string full = toUtf8(buf);
    const size_t dash = full.find('-');
    const std::string lang = dash == std::string::npos ? full : full.substr(0, dash);

    if (!st().scanned) scanLanguages();
    for (const auto& L : st().langs) if (L.code == lang) return lang;

    // Sistem dili icin dosya yoksa: Turkce degilse Ingilizce'yi dene, o da
    // yoksa kaynak dilde kal.
    if (lang != kSourceCode)
        for (const auto& L : st().langs) if (L.code == "en") return "en";
    return kSourceCode;
}

void refresh() {
    scanLanguages();
    const std::string keep = st().code;
    setLanguage(keep);
}

// ---- Sablon uretimi ---------------------------------------------------------

namespace {

// Ters yon: dosyaya yazarken satir sonlarini kacirmak sart, yoksa cok satirli
// bir metin dosyayi bozar.
std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r':                break;   // CR atilir
            case '\\': out += "\\\\"; break;
            default:   out += c;      break;
        }
    }
    return out;
}

} // namespace

ss::Translator translator() {
    ss::Translator t;
    t.text = [](const std::string& s) { return T(s); };
    t.format = [](const std::string& tpl, const std::vector<std::string>& args) {
        return Tf(tpl, args);
    };
    return t;
}

namespace {

// Sablona GIRMEMESI gereken metinler.
//
// Toplayici ekrana cizilen her seyi kaydediyordu ve sablon soyle satirlarla
// doluyordu: "!", "%", "%0", "%15  BIOS / mikrokod surumu". Bunlar calisma
// aninda SAYIYLA kurulan degerler; ceviri tablosunda hicbir zaman
// bulunamazlar ve cevirmek isteyen kisiyi bogarlar. Gercek bir kullanicinin
// kaydettigi 328 satirlik sablonda bunlar cogunluktaydi.
//
// Olcut: en az iki HARF icermeli. Bu, "!" ve "%0" gibi salt isaret/sayi
// satirlarini eler ama "1 dakika" gibi mesru metinleri korur.
bool looksLikeRuntimeValue(const std::string& s) {
    if (s.size() < 3) return true;

    // UTF-8 icinde harf sayimi: ASCII harfler + cok baytli diziler (Turkce
    // karakterler) harf sayilir. Tam Unicode siniflandirmasi gerekmiyor,
    // amac yalnizca "hic harf yok" durumunu yakalamak.
    int letters = 0;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80) {
            if (++letters >= 2) return false;
        }
    }
    return true;
}

} // namespace

void noteSource(const std::string& s) {
    if (s.empty() || looksLikeRuntimeValue(s)) return;
    st().seen.insert(s);
}

void noteSource(const wchar_t* s) {
    if (!s || !*s) return;
    const std::string u = toUtf8(s);
    if (looksLikeRuntimeValue(u)) return;
    st().seen.insert(u);
}

size_t collectedCount() { return st().seen.size(); }

bool writeTemplate(const std::wstring& path, const std::string& code,
                   const std::string& name, std::string& error) {
    // Var olan bir dosya varsa cevirileri KORUNUR: sablon yeniden uretildiginde
    // yapilmis is silinmemeli. Yeni metinler bos ceviriyle eklenir.
    std::map<std::string, std::string> existing;
    {
        std::ifstream in(path);
        std::string line, src;
        bool haveSrc = false;
        while (in && std::getline(in, line)) {
            stripBom(line);
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#' || t[0] == '@') continue;
            if (t[0] == '~') { src = trim(t.substr(1)); haveSrc = true; }
            else if (t[0] == '=' && haveSrc) {
                existing[src] = trim(t.substr(1));
                haveSrc = false;
            }
        }
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { error = "Dosya yazılamadı."; return false; }

    f << "\xEF\xBB\xBF";   // UTF-8 BOM: Not Defteri'nde dogru acilsin
    f << "# Syspect dil dosyası\n"
         "#\n"
         "# '~' satırı kaynak metindir (Türkçe), DEĞİŞTİRMEYİN.\n"
         "# '=' satırına çevirisini yazın. Boş bırakılan satırlar çevrilmemiş\n"
         "# sayılır ve uygulamada Türkçe görünür — dosyayı parça parça\n"
         "# doldurabilirsiniz.\n"
         "#\n"
         "# %1 %2 gibi işaretler yer tutucudur; çeviride YERİ değişebilir ama\n"
         "# kaybolmamalıdır.\n"
         "#\n"
         "# Bu dosyayı uygulamanın yanındaki lang/ klasörüne koyun.\n"
         "\n";
    f << "@name " << (name.empty() ? code : name) << "\n";
    f << "@code " << code << "\n\n";

    size_t done = 0;
    for (const auto& src : st().seen) {
        const auto it = existing.find(escape(src));
        const std::string dst = (it == existing.end()) ? std::string{} : it->second;
        if (!dst.empty()) ++done;
        f << "~ " << escape(src) << "\n";
        f << "= " << dst << "\n\n";
    }

    f << "# " << done << " / " << st().seen.size() << " çevrildi\n";
    return true;
}

} // namespace ss18

#endif // _WIN32
