// ============================================================================
//  Syspect — surum denetimi (uygulama)
//  Gerekce ve sinirlar icin bkz. update_check.h
// ============================================================================
#ifdef _WIN32

#include "update_check.h"
#include "version.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <cstdlib>
#include <vector>

namespace ssupd {
namespace {

constexpr wchar_t kRegPath[] = L"Software\\Syspect";
constexpr wchar_t kRegName[] = L"UpdateCheck";

// JSON icin kutuphane cekilmiyor: tek bir alan okunuyor ve bagimlilik
// eklemenin bedeli kazancindan buyuk. Aranan sey `"tag_name": "v0.3.0"`.
// Kacisli karakter beklenmiyor — surum etiketleri ASCII.
std::string jsonString(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return {};
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return {};

    // Degerin basindaki tirnagi bul
    p = body.find('"', p);
    if (p == std::string::npos) return {};
    const size_t start = p + 1;
    const size_t end   = body.find('"', start);
    if (end == std::string::npos) return {};
    return body.substr(start, end - start);
}

// "v0.3.0" -> {0,3,0}. Eksik bolumler sifir. Ayristirilamayan karakterde durur.
bool parseVersion(const std::string& s, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    size_t i = 0;
    if (i < s.size() && (s[i] == 'v' || s[i] == 'V')) ++i;
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i])))
        return false;

    for (int part = 0; part < 3 && i < s.size(); ++part) {
        int v = 0;
        bool any = false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            v = v * 10 + (s[i] - '0');
            ++i; any = true;
        }
        if (!any) break;
        out[part] = v;
        if (i < s.size() && s[i] == '.') ++i; else break;
    }
    return true;
}

} // namespace

bool isNewer(const std::string& tag, const std::string& current) {
    int a[3], b[3];
    // Ayristirilamayan etiket "yeni degil" sayilir: supheli veriyle
    // kullaniciya guncelleme onermek, hic onermemekten kotudur.
    if (!parseVersion(tag, a) || !parseVersion(current, b)) return false;
    for (int i = 0; i < 3; ++i) {
        if (a[i] > b[i]) return true;
        if (a[i] < b[i]) return false;
    }
    return false;
}

Result check(uint32_t timeoutMs) {
    Result r;

    // User-Agent GitHub API'si icin ZORUNLU (yoksa 403 doner). Icerigi
    // bilerek asgari: program adi ve surumu. Makineye ozgu hicbir sey yok.
    const std::wstring ua = L"Syspect/" L"" SS_VERSION_STRING;

    HINTERNET session = WinHttpOpen(ua.c_str(),
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { r.error = "Ağ oturumu açılamadı."; return r; }

    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET conn = WinHttpConnect(session, L"" SS_UPDATE_HOST,
                                    INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) {
        WinHttpCloseHandle(session);
        r.error = "Sunucuya bağlanılamadı.";
        return r;
    }

    const std::wstring path = L"" SS_UPDATE_PATH;

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        r.error = "İstek oluşturulamadı.";
        return r;
    }

    const wchar_t* hdr = L"Accept: application/json\r\n";
    bool ok = WinHttpSendRequest(req, hdr, -1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
           && WinHttpReceiveResponse(req, nullptr);

    DWORD status = 0, len = sizeof(status);
    if (ok) {
        WinHttpQueryHeaders(req,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    }

    std::string body;
    if (ok && status == 200) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
            // Ust sinir: bozuk ya da kotu niyetli bir cevap bellegi
            // sisirmesin. Release JSON'u tipik olarak birkac KB.
            if (body.size() > 512 * 1024) break;
            std::vector<char> buf(avail + 1, 0);
            DWORD read = 0;
            if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
            body.append(buf.data(), read);
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    if (!ok) { r.error = "Sürüm bilgisi alınamadı."; return r; }
    if (status != 200) {
        r.error = "Sunucu " + std::to_string(status) + " döndü.";
        return r;
    }

    r.checked   = true;

    // Sunucu hem "tag" (v0.2.0) hem "version" (0.2.0) donduruyor; ikisi de
    // ayristirilabilir. tag yoksa version'a dus — tek alana bagimli olmamak,
    // sunucu bicimi degisirse denetimin sessizce olmesini engelliyor.
    r.latestTag = jsonString(body, "tag");
    if (r.latestTag.empty()) r.latestTag = jsonString(body, "version");

    r.pageUrl = jsonString(body, "pageUrl");
    if (r.pageUrl.empty())
        r.pageUrl = "https://" SS_UPDATE_HOST "/syspect";

    r.available = isNewer(r.latestTag, SS_VERSION_STRING);
    return r;
}

bool enabled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0, KEY_READ, &k)
            != ERROR_SUCCESS)
        return true;                       // varsayilan: acik
    DWORD v = 1, sz = sizeof(v), type = 0;
    const bool got = RegQueryValueExW(k, kRegName, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(&v), &sz)
                     == ERROR_SUCCESS && type == REG_DWORD;
    RegCloseKey(k);
    return got ? (v != 0) : true;
}

void setEnabled(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegPath, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    DWORD v = on ? 1u : 0u;
    RegSetValueExW(k, kRegName, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(k);
}

} // namespace ssupd

#endif // _WIN32
