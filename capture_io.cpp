// ============================================================================
//  Syspect — kayit dosyasi (.syscap)
// ============================================================================
#ifdef _WIN32

#include "capture_io.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sscap {
namespace {

constexpr const char* kMagic   = "SYSPECT-CAPTURE";
// 2: [eventlog] bolumu ve olcum penceresi eklendi. Surum 1 dosyalari hala
// okunur — o bolum yoksa evtlog.attempted false kalir ve arayuz "veri yok"
// diye davranir, "sorun yok" diye DEGIL.
constexpr int         kVersion = 2;

// Dosyaya yazilacak en fazla ornek olay. Ozet sayimlar zaten ayri satirda;
// bunlar yalnizca zaman cakismasi hesabi ve surucu adi icin. Ust sinir
// olmadan gurultulu bir makine dosyayi sisirir.
constexpr size_t      kMaxSavedEvents = 40;

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\r' || s[a] == '\t')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\r' || s[b-1] == '\t')) --b;
    return s.substr(a, b - a);
}

// Okunamayan deger -1 olarak yazilir; geri okurken kUnknown'a cevrilir.
// 0 KULLANILAMAZ: %0 kullanim ve 0 ms gecikme gecerli olculerdir.
double enc(double v) { return sstelem::known(v) ? v : -1.0; }
double dec(double v) { return (v <= -0.5) ? sstelem::kUnknown : v; }

std::string f(double v, int d = 2) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.*f", d, v);
    return b;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

double num(const std::vector<std::string>& v, size_t i) {
    if (i >= v.size()) return -1.0;
    return std::strtod(v[i].c_str(), nullptr);
}

std::string toUtf8Path(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                      nullptr, 0, nullptr, nullptr);
    std::string o(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                   o.data(), n, nullptr, nullptr);
    return o;
}

} // namespace

// ============================================================================
bool save(const std::wstring& path, const Capture& c, std::string& error) {
    std::ostringstream o;
    o << kMagic << ' ' << kVersion << '\n';
    o << "app="      << c.application    << '\n';
    o << "pid="      << c.processId      << '\n';
    o << "cpu="      << c.cpuName        << '\n';
    o << "gpu="      << c.gpuName        << '\n';
    o << "os="       << c.osBuild        << '\n';
    o << "ram_mb="   << c.ramInstalledMb << '\n';
    o << "vram_mb="  << c.vramTotalMb    << '\n';
    o << "created="  << c.createdAt      << '\n';
    o << "note="     << c.note           << '\n';
    o << "cap_start=" << c.captureStartFt << '\n';
    o << "cap_end="   << c.captureEndFt   << '\n';

    // ---------------- Olay gunlugu ozeti ----------------
    //  Bilerek OZET: tur basina uc sayim + en yeni zaman. Ham olay listesi
    //  yazilsaydi bu bolum tek basina dosyanin en buyuk parcasi olurdu.
    if (c.evtlog.attempted) {
        o << "[eventlog]\n";
        o << "# s,tur,24sa,7gun,30gun,ensonFILETIME  -> tur ozeti\n";
        o << "# e,tur,FILETIME,ayrinti               -> ornek olay\n";
        o << "s,_read," << (c.evtlog.ok ? 1 : 0) << ",0,0,0\n";
        o << "s,_live_kernel," << c.evtlog.liveKernelReports << ",0,0,0\n";

        for (size_t i = 0; i < static_cast<size_t>(sslog::Kind::Count_); ++i) {
            const sslog::Series& se = c.evtlog.series[i];
            if (se.empty()) continue;
            o << "s," << sslog::kindKey(se.kind) << ',' << se.last24h << ','
              << se.last7d << ',' << se.last30d << ',' << se.newestFileTime << '\n';
        }

        size_t written = 0;
        for (size_t i = 0; i < static_cast<size_t>(sslog::Kind::Count_) &&
                           written < kMaxSavedEvents; ++i) {
            for (const sslog::Event& e : c.evtlog.series[i].samples) {
                if (written >= kMaxSavedEvents) break;
                o << "e," << sslog::kindKey(e.kind) << ',' << e.fileTimeUtc
                  << ',' << e.detail << '\n';
                ++written;
            }
        }
    }

    o << "[frames]\n";
    o << "# kare suresi (ms), sirayla\n";
    for (const auto& fr : c.frames) o << f(fr.frameTimeMs, 3) << '\n';

    o << "[telemetry]\n";
    o << "# t,gpuUtil,gpuTemp,gpuClock,gpuPower,vramMb,cpuUsage,cpuPerf,"
         "parked,diskActive,diskMs,ramPct,availMb,commitPct,flags\n";
    o << "# -1 = o an okunamadi. flags: T=isinma P=guc-limiti B=power-brake "
         "R=commit>RAM\n";
    for (const auto& s : c.telemetry) {
        o << s.tSec << ','
          << f(enc(s.gpuUtilPct), 1)   << ',' << f(enc(s.gpuTempC), 1)    << ','
          << f(enc(s.gpuClockMhz), 0)  << ',' << f(enc(s.gpuPowerW), 1)   << ','
          << f(enc(s.gpuMemUsedMb), 0) << ',' << f(enc(s.cpuUsagePct), 1) << ','
          << f(enc(s.cpuPerfPct), 1)   << ',' << f(enc(s.coreParkedPct), 1) << ','
          << f(enc(s.diskActivePct), 1)<< ',' << f(enc(s.diskLatencyMs), 2) << ','
          << f(enc(s.ramUsedPct), 1)   << ',' << f(enc(s.availPhysMb), 0)  << ','
          << f(enc(s.commitUsedPct), 1)<< ','
          << (s.thermalThrottle  ? 'T' : '-')
          << (s.powerCapThrottle ? 'P' : '-')
          << (s.powerBrake       ? 'B' : '-')
          << (s.commitOverRam    ? 'R' : '-')
          << '\n';
    }

    const std::string text = o.str();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "Dosya oluşturulamadı.";
        return false;
    }
    DWORD w = 0;
    const bool ok = WriteFile(h, text.data(),
                              static_cast<DWORD>(text.size()), &w, nullptr) != 0;
    CloseHandle(h);
    if (!ok) error = "Dosyaya yazılamadı.";
    return ok;
}

// ============================================================================
bool load(const std::wstring& path, Capture& out, std::string& error) {
    std::ifstream in(toUtf8Path(path), std::ios::binary);
    if (!in) { error = "Dosya açılamadı."; return false; }

    std::string line;
    if (!std::getline(in, line) || line.rfind(kMagic, 0) != 0) {
        error = "Bu bir Syspect kayıt dosyası değil.";
        return false;
    }

    enum class Section { Header, Frames, Telemetry, EventLog } sec = Section::Header;
    size_t badLines = 0;

    while (std::getline(in, line)) {
        const std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;

        if (s == "[frames]")    { sec = Section::Frames;    continue; }
        if (s == "[telemetry]") { sec = Section::Telemetry; continue; }
        if (s == "[eventlog]")  { sec = Section::EventLog;  continue; }

        if (sec == Section::EventLog) {
            const auto v = split(s, ',');
            if (v.size() < 3) { ++badLines; continue; }

            // Bolum var demek tarama YAPILMIS demek. ok bayragi ayri satirda.
            out.evtlog.attempted = true;

            if (v[0] == "s") {
                if      (v[1] == "_read")        out.evtlog.ok = (v[2] == "1");
                else if (v[1] == "_live_kernel")
                    out.evtlog.liveKernelReports =
                        static_cast<uint32_t>(std::strtoul(v[2].c_str(), nullptr, 10));
                else if (v.size() >= 6) {
                    const sslog::Kind k = sslog::kindFromKey(v[1]);
                    if (k == sslog::Kind::Count_) { ++badLines; continue; }
                    sslog::Series& se = out.evtlog.series[static_cast<size_t>(k)];
                    se.kind    = k;
                    se.last24h = static_cast<uint32_t>(std::strtoul(v[2].c_str(), nullptr, 10));
                    se.last7d  = static_cast<uint32_t>(std::strtoul(v[3].c_str(), nullptr, 10));
                    se.last30d = static_cast<uint32_t>(std::strtoul(v[4].c_str(), nullptr, 10));
                    se.newestFileTime = std::strtoull(v[5].c_str(), nullptr, 10);
                } else ++badLines;
            } else if (v[0] == "e" && v.size() >= 3) {
                const sslog::Kind k = sslog::kindFromKey(v[1]);
                if (k == sslog::Kind::Count_) { ++badLines; continue; }
                sslog::Event e;
                e.kind        = k;
                e.fileTimeUtc = std::strtoull(v[2].c_str(), nullptr, 10);
                // Ayrinti alaninda virgul olabilir (bugcheck parametreleri);
                // kalan parcalari geri birlestir.
                for (size_t i = 3; i < v.size(); ++i) {
                    if (i > 3) e.detail += ',';
                    e.detail += v[i];
                }
                out.evtlog.series[static_cast<size_t>(k)].samples.push_back(e);
            } else ++badLines;
            continue;
        }

        if (sec == Section::Header) {
            const size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = s.substr(0, eq), v = s.substr(eq + 1);
            if      (k == "app")     out.application    = v;
            else if (k == "pid")     out.processId      = std::strtoul(v.c_str(), nullptr, 10);
            else if (k == "cpu")     out.cpuName        = v;
            else if (k == "gpu")     out.gpuName        = v;
            else if (k == "os")      out.osBuild        = v;
            else if (k == "ram_mb")  out.ramInstalledMb = std::strtoull(v.c_str(), nullptr, 10);
            else if (k == "vram_mb") out.vramTotalMb    = std::strtoull(v.c_str(), nullptr, 10);
            else if (k == "created") out.createdAt      = v;
            else if (k == "note")    out.note           = v;
            else if (k == "cap_start") out.captureStartFt = std::strtoull(v.c_str(), nullptr, 10);
            else if (k == "cap_end")   out.captureEndFt   = std::strtoull(v.c_str(), nullptr, 10);
            continue;
        }

        if (sec == Section::Frames) {
            const double ms = std::strtod(s.c_str(), nullptr);
            // CSV okuyucusuyla ayni eleme: negatif ya da 10 sn'den uzun kare olmaz
            if (ms <= 0.0 || ms > 10000.0) { ++badLines; continue; }
            ss::FrameSample fr;
            fr.frameTimeMs = ms;
            fr.timestampUs = out.frames.empty()
                ? static_cast<uint64_t>(ms * 1000.0)
                : out.frames.back().timestampUs + static_cast<uint64_t>(ms * 1000.0);
            out.frames.push_back(fr);
            continue;
        }

        // Telemetri
        const auto v = split(s, ',');
        if (v.size() < 15) { ++badLines; continue; }
        sstelem::Sample t;
        t.tSec          = static_cast<uint32_t>(num(v, 0));
        t.gpuUtilPct    = dec(num(v, 1));
        t.gpuTempC      = dec(num(v, 2));
        t.gpuClockMhz   = dec(num(v, 3));
        t.gpuPowerW     = dec(num(v, 4));
        t.gpuMemUsedMb  = dec(num(v, 5));
        t.cpuUsagePct   = dec(num(v, 6));
        t.cpuPerfPct    = dec(num(v, 7));
        t.coreParkedPct = dec(num(v, 8));
        t.diskActivePct = dec(num(v, 9));
        t.diskLatencyMs = dec(num(v, 10));
        t.ramUsedPct    = dec(num(v, 11));
        t.availPhysMb   = dec(num(v, 12));
        t.commitUsedPct = dec(num(v, 13));
        const std::string flags = v[14];
        t.thermalThrottle  = flags.find('T') != std::string::npos;
        t.powerCapThrottle = flags.find('P') != std::string::npos;
        t.powerBrake       = flags.find('B') != std::string::npos;
        t.commitOverRam    = flags.find('R') != std::string::npos;
        out.telemetry.push_back(t);
    }

    if (out.frames.empty()) {
        error = "Kayıt içinde geçerli kare bulunamadı.";
        return false;
    }
    if (badLines > 0) {
        out.note += (out.note.empty() ? "" : "  ") +
                    std::string("(") + std::to_string(badLines) +
                    " bozuk satır atlandı)";
    }
    return true;
}

} // namespace sscap

#endif // _WIN32
