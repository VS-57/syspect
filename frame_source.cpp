// ============================================================================
//  StutterScope — Kare kaynagi uygulamalari
// ============================================================================
#include "frame_source.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ss {

// ============================================================================
//  CSV yardimcilari
// ============================================================================
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (std::isspace(static_cast<unsigned char>(s[a])) || s[a] == '"')) ++a;
    while (b > a && (std::isspace(static_cast<unsigned char>(s[b-1])) || s[b-1] == '"')) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Tirnak icindeki virgulleri koruyan basit CSV satir ayirici
std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') { inQuotes = !inQuotes; cur.push_back(c); }
        else if (c == ',' && !inQuotes) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    for (auto& s : out) s = trim(s);
    return out;
}

// Sutun adini bir aday listesinde arar, indeksini dondurur (-1 = yok)
int findColumn(const std::vector<std::string>& header,
               const std::vector<std::string>& candidates) {
    for (const auto& want : candidates) {
        std::string w = lower(want);
        for (size_t i = 0; i < header.size(); ++i)
            if (lower(header[i]) == w) return static_cast<int>(i);
    }
    return -1;
}

bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return false;
    if (std::isnan(v) || std::isinf(v)) return false;
    out = v;
    return true;
}

} // namespace

// ============================================================================
//  CsvFrameSource
// ============================================================================
CsvFrameSource::CsvFrameSource(const std::string& path) {
    info_.kind = "csv";

    std::ifstream f(path);
    if (!f) {
        error_ = "CSV dosyasi acilamadi: " + path;
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    parse(ss.str());
}

std::unique_ptr<CsvFrameSource> CsvFrameSource::fromString(const std::string& csvText) {
    std::unique_ptr<CsvFrameSource> p(new CsvFrameSource());
    p->info_.kind = "csv";
    p->parse(csvText);
    return p;
}

void CsvFrameSource::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line;

    // --- Baslik satirini bul ---
    std::vector<std::string> header;
    while (std::getline(in, line)) {
        if (trim(line).empty()) continue;
        header = splitCsvLine(line);
        break;
    }
    if (header.empty()) {
        error_ = "CSV bos veya baslik satiri yok.";
        return;
    }

    // --- Sutunlari ADA gore bul (siraya guvenmiyoruz) ---
    const int colTime = findColumn(header, {
        "TimeInSeconds",     // PresentMon 1.x
        "CPUStartTime",      // PresentMon 2.x
        "CPUStartTimeInSeconds",
    });
    const int colFrame = findColumn(header, {
        "MsBetweenPresents", // PresentMon 1.x
        "FrameTime",         // PresentMon 2.x
        "MsBetweenDisplayChange",
        "CPUFrameTime",
    });
    const int colApp     = findColumn(header, {"Application", "ProcessName"});
    const int colPid     = findColumn(header, {"ProcessID", "ProcessId"});
    const int colDropped = findColumn(header, {"Dropped"});

    if (colFrame < 0) {
        error_ = "CSV icinde kare suresi sutunu bulunamadi "
                 "(MsBetweenPresents / FrameTime bekleniyordu). "
                 "Bu bir PresentMon ciktisi mi?";
        return;
    }

    // --- Satirlari oku ---
    double  fallbackClockMs = 0.0;   // zaman sutunu yoksa kendimiz biriktiririz
    size_t  lineNo = 1;
    size_t  badRows = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        if (trim(line).empty()) continue;

        std::vector<std::string> f = splitCsvLine(line);
        if (static_cast<int>(f.size()) <= colFrame) { ++badRows; continue; }

        // Dusen kareler frame time istatistigini bozar, atlanir
        if (colDropped >= 0 && static_cast<int>(f.size()) > colDropped) {
            std::string d = lower(f[colDropped]);
            if (d == "1" || d == "true") { ++dropped_; continue; }
        }

        double frameMs = 0.0;
        if (!parseDouble(f[colFrame], frameMs)) { ++badRows; continue; }

        // Anlamsiz degerleri ele: negatif ya da 10 saniyeden uzun kare olmaz
        if (frameMs <= 0.0 || frameMs > 10000.0) { ++badRows; continue; }

        FrameSample s;
        s.frameTimeMs = frameMs;

        if (colTime >= 0 && static_cast<int>(f.size()) > colTime) {
            double sec = 0.0;
            if (parseDouble(f[colTime], sec)) {
                s.timestampUs = static_cast<uint64_t>(sec * 1'000'000.0);
            } else {
                fallbackClockMs += frameMs;
                s.timestampUs = static_cast<uint64_t>(fallbackClockMs * 1000.0);
            }
        } else {
            fallbackClockMs += frameMs;
            s.timestampUs = static_cast<uint64_t>(fallbackClockMs * 1000.0);
        }

        // Uygulama adi: ilk gecerli satirdan alinir
        if (info_.application.empty() && colApp >= 0 &&
            static_cast<int>(f.size()) > colApp && !f[colApp].empty()) {
            info_.application = f[colApp];
        }
        if (info_.processId == 0 && colPid >= 0 &&
            static_cast<int>(f.size()) > colPid) {
            double pid = 0.0;
            if (parseDouble(f[colPid], pid)) info_.processId = static_cast<uint32_t>(pid);
        }

        rows_.push_back(s);
    }

    if (rows_.empty()) {
        error_ = "CSV okundu ama gecerli kare bulunamadi.";
        return;
    }

    std::string note = std::to_string(rows_.size()) + " kare okundu";
    if (dropped_ > 0) note += ", " + std::to_string(dropped_) + " dusen kare atlandi";
    if (badRows > 0) note += ", " + std::to_string(badRows) + " bozuk satir atlandi";
    if (colTime < 0) note += " (zaman sutunu yok, kare surelerinden turetildi)";
    info_.note = note;
}

bool CsvFrameSource::next(FrameSample& out) {
    if (cursor_ >= rows_.size()) return false;
    out = rows_[cursor_++];
    return true;
}

// ============================================================================
//  Kaynak -> teshis
// ============================================================================
AnalysisResult analyzeSource(IFrameSource& src, const SystemInfo& sys,
                             const ISignalProvider* signals) {
    AnalysisResult r;

    StutterDetector det(120);
    FrameSample f;
    while (src.next(f)) {
        r.frames.push_back(f);
        // Takilma anindaki yan sinyaller saglayicidan alinir. Saglayici
        // yoksa (CSV modu) bos snapshot gider ve motor yalnizca kare deseni
        // kurallarini calistirir.
        const SignalSnapshot sig = signals ? signals->at(f.timestampUs)
                                           : SignalSnapshot{};
        if (auto e = det.push(f, sig)) r.events.push_back(*e);
    }

    r.stats     = analyzeSession(r.frames, r.events);
    r.diagnosis = diagnose(r.stats, r.events, sys);
    return r;
}

} // namespace ss
