// ============================================================================
//  Syspect — paylasilabilir rapor
// ============================================================================
#ifdef _WIN32

#include "report_share.h"
#include "i18n.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace ssreport {
namespace {

// ----------------------------------------------------------------------------
//  Kanit cumlelerinin cevrilmis hali
// ----------------------------------------------------------------------------
//  Motor kaniti SABLON + DEGER olarak veriyor (bkz. core.h, EvidencePart).
//  Hazir birlestirilmis h.evidence metnini kullanmak KOLAY olurdu ama o metin
//  ceviri tablosunda bulunamaz — icinde olculmus sayilar var. Sablon aranir,
//  degerler sonra yerlesir.
//
//  Eski alan yine de duruyor: parcalar bos gelirse (eski bir .syscap ya da
//  cevrilmemis bir kural) hazir metne geri donuluyor.
std::string translatedEvidence(const ss::Hypothesis& h) {
    if (h.evidenceParts.empty()) return ss18::T(h.evidence);

    std::string out;
    for (const auto& p : h.evidenceParts) {
        if (!out.empty()) out += "; ";
        out += ss18::Tf(p.tpl, p.args);
    }
    return out;
}

std::string f2(double v, int dec = 1) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.*f", dec, v);
    return b;
}

const char* triText(sstelem::GpuStatic::Tri t) {
    switch (t) {
        case sstelem::GpuStatic::Tri::Yes: return "AÇIK";
        case sstelem::GpuStatic::Tri::No:  return "KAPALI";
        default:                           return "belirlenemedi";
    }
}

// Bir alanin oturum boyunca medyan / en yuksek degeri. Okunamayan ornekler
// atlanir; hic okunamadiysa ok=false doner ve rapor "ölçülemedi" yazar.
struct Stat { bool ok = false; double median = 0, max = 0, min = 0; };

Stat statOf(const std::vector<sstelem::Sample>& v,
            double sstelem::Sample::* field) {
    std::vector<double> xs;
    for (const auto& s : v) if (sstelem::known(s.*field)) xs.push_back(s.*field);
    Stat st;
    if (xs.empty()) return st;
    std::sort(xs.begin(), xs.end());
    st.ok     = true;
    st.min    = xs.front();
    st.max    = xs.back();
    st.median = xs[xs.size() / 2];
    return st;
}

void metricRow(std::ostringstream& o, const char* label,
               const Stat& s, const char* unit) {
    o << "| " << label << " | ";
    if (s.ok) o << f2(s.median, 0) << unit << " | " << f2(s.max, 0) << unit
                << " | " << f2(s.min, 0) << unit << " |\n";
    else      o << "ölçülemedi | — | — |\n";
}

} // namespace

// ============================================================================
std::string buildShareText(const Bundle& b) {
    std::ostringstream o;

    o << "# Syspect teşhis raporu\n\n";
    o << "*Bu rapor Syspect v" << b.appVersion
      << " tarafından otomatik üretildi. Ölçüm yöntemi: Windows ETW "
         "(Microsoft-Windows-DxgKrnl) pasif dinleme — oyun sürecine "
         "bağlanılmaz, kod enjekte edilmez.*\n\n";
    if (!b.timestamp.empty()) o << "Tarih: " << b.timestamp << "\n\n";

    // ---------------- Hüküm ----------------
    if (b.result) {
        const ss::Diagnosis& d = b.result->diagnosis;
        o << "## Sonuç\n\n";
        o << "**" << d.headline << "**\n\n";
        o << "Güven: %" << d.confidence;
        if (d.inconclusive) o << " — motor hüküm vermeyi reddediyor, veri yetersiz";
        o << "\n\n";

        if (!d.ranked.empty()) {
            o << "| Olasılık | Sebep | Kanıt | Yapılacak |\n";
            o << "|---|---|---|---|\n";
            for (const ss::Hypothesis& h : d.ranked) {
                o << "| %" << h.percent << " | " << ss18::T(h.label) << " | "
                  << translatedEvidence(h) << " | "
                  << ss18::T(h.action) << " |\n";
            }
            o << "\n";
        }
    }

    // ---------------- Kare ölçümleri ----------------
    if (b.result) {
        const ss::SessionStats& s = b.result->stats;
        o << "## Kare ölçümleri\n\n";
        if (b.source && !b.source->application.empty())
            o << "Ölçülen uygulama: `" << b.source->application << "`\n\n";
        o << "| Ölçüm | Değer |\n|---|---|\n";
        o << "| Süre | " << f2(s.durationSec, 0) << " sn |\n";
        o << "| Kare sayısı | " << s.frameCount << " |\n";
        o << "| Ortalama FPS | " << f2(s.avgFps) << " |\n";
        o << "| Medyan FPS | " << f2(s.medianFps) << " |\n";
        o << "| %1 düşük FPS | " << f2(s.onePercentLowFps) << " |\n";
        o << "| Medyan kare süresi | " << f2(s.medianFrameTimeMs, 2) << " ms |\n";
        o << "| P99 kare süresi | " << f2(s.p99FrameTimeMs, 2) << " ms |\n";
        o << "| Takılma (toplam) | " << s.stutterCount << " |\n";
        o << "| — mikro takılma | " << s.microStutterCount;
        if (s.periodicMicroStutter)
            o << " (DÜZENLİ, ~" << f2(s.microStutterPeriodMs, 0) << " ms aralıkla)";
        o << " |\n";
        o << "| — sıçrama | " << s.hitchCount << " |\n";
        o << "| — donma | " << s.freezeCount << " |\n\n";

        if (s.lowFpsNotStutter)
            o << "> Takılma tespit edilmedi; kare süreleri dar ve düzenli "
                 "dağılmış. Sorun kararlılık değil.\n\n";
    }

    // ---------------- Ölçüm boyunca telemetri ----------------
    if (b.telemetry && !b.telemetry->empty()) {
        const auto& t = *b.telemetry;
        o << "## Ölçüm boyunca\n\n";
        o << "| Değer | Medyan | En yüksek | En düşük |\n|---|---|---|---|\n";
        metricRow(o, "GPU kullanım",  statOf(t, &sstelem::Sample::gpuUtilPct),   "%");
        metricRow(o, "GPU sıcaklık",  statOf(t, &sstelem::Sample::gpuTempC),     " °C");
        metricRow(o, "GPU frekans",   statOf(t, &sstelem::Sample::gpuClockMhz),  " MHz");
        metricRow(o, "GPU güç",       statOf(t, &sstelem::Sample::gpuPowerW),    " W");
        metricRow(o, "VRAM kullanım", statOf(t, &sstelem::Sample::gpuMemUsedMb), " MB");
        metricRow(o, "CPU kullanım",  statOf(t, &sstelem::Sample::cpuUsagePct),  "%");
        metricRow(o, "CPU performans",statOf(t, &sstelem::Sample::cpuPerfPct),   "%");
        metricRow(o, "Çekirdek park", statOf(t, &sstelem::Sample::coreParkedPct),"%");
        metricRow(o, "Sistem sıcaklık (ACPI)", statOf(t, &sstelem::Sample::cpuTempC), " °C");
        metricRow(o, "Disk aktif",    statOf(t, &sstelem::Sample::diskActivePct),"%");
        metricRow(o, "Disk gecikme",  statOf(t, &sstelem::Sample::diskLatencyMs)," ms");
        metricRow(o, "Bellek dolu",   statOf(t, &sstelem::Sample::ramUsedPct),   "%");
        o << "\n";

        bool thermal = false, cap = false, brake = false;
        for (const auto& s : t) {
            thermal |= s.thermalThrottle;
            cap     |= s.powerCapThrottle;
            brake   |= s.powerBrake;
        }
        o << "Ekran kartının bildirdiği kısıtlamalar: ";
        if (!thermal && !cap && !brake) o << "yok.\n\n";
        else {
            if (thermal) o << "**ısınma kısıtlaması** ";
            if (cap)     o << "**güç limiti** ";
            if (brake)   o << "**donanımsal frenleme (power brake)** ";
            o << "\n\n";
        }
    }

    // ---------------- Donanım ----------------
    o << "## Donanım ve ayarlar\n\n";
    o << "| | |\n|---|---|\n";
    if (!b.cpuName.empty()) o << "| İşlemci | " << b.cpuName << " |\n";
    if (!b.gpuName.empty()) o << "| Ekran kartı | " << b.gpuName << " |\n";
    if (b.ramInstalledMb)
        o << "| Takılı bellek | " << (b.ramInstalledMb / 1024) << " GB |\n";
    if (!b.osBuild.empty()) o << "| Windows | " << b.osBuild << " |\n";

    if (b.memory && !b.memory->modules.empty()) {
        const auto& m = *b.memory;
        o << "| Bellek | " << m.typeName << " · "
          << m.configuredMTs << " MT/s (modüller " << m.maxSpeedMTs
          << " MT/s destekliyor) |\n";
        o << "| " << m.profileLabel << " profili | "
          << (m.profile == ssprobe::MemorySpec::Profile::On  ? "**AÇIK**"
            : m.profile == ssprobe::MemorySpec::Profile::Off ? "**KAPALI**"
                                                             : "belirlenemedi")
          << " |\n";
        o << "| Kanal | " << m.modules.size() << " modül"
          << (m.singleChannelRisk ? " — **tek kanal**" : "") << " |\n";
    }

    if (b.gpu && b.gpu->known) {
        const auto& gp = *b.gpu;
        if (sstelem::known(gp.powerLimitW)) {
            o << "| GPU güç limiti | " << f2(gp.powerLimitW, 0) << " W";
            if (sstelem::known(gp.maxPowerLimitW))
                o << " (tavan " << f2(gp.maxPowerLimitW, 0) << " W)";
            o << " |\n";
        }
        o << "| Resizable BAR | **" << triText(gp.resizableBar) << "**";
        if (gp.vramTotalMb && gp.bar1TotalMb)
            o << " (BAR1 " << gp.bar1TotalMb << " MB / VRAM "
              << gp.vramTotalMb << " MB)";
        o << " |\n";
    }

    if (b.power && !b.power->friendlyName.empty()) {
        o << "| Güç planı | " << b.power->friendlyName;
        if (b.power->onBattery) o << " — **PİLDEN çalışıyor**";
        o << " |\n";
    }
    o << "\n";

    if (b.power && b.power->shouldWarn)
        o << "> **Güç uyarısı:** " << b.power->warning << "\n>\n> "
          << b.power->action << "\n\n";

    if (b.memory && b.memory->profile == ssprobe::MemorySpec::Profile::Off)
        o << "> **Bellek uyarısı:** " << b.memory->profileNote << "\n\n";

    if (b.gpu && b.gpu->resizableBar == sstelem::GpuStatic::Tri::No)
        o << "> **Resizable BAR:** " << b.gpu->rebarNote << "\n\n";

    // ---------------- Sürücü sorunları ----------------
    if (b.devices && !b.devices->problems.empty()) {
        o << "## Sürücü sorunları\n\n";
        o << b.devices->note << "\n\n";
        o << "| Aygıt | Sınıf | Sorun |\n|---|---|---|\n";
        for (const auto& d : b.devices->problems)
            o << "| " << d.name << " | " << d.cls << " | ("
              << d.problemCode << ") " << d.problemText << " |\n";
        o << "\n";
    }

    // ---------------- Mavi ekran ----------------
    if (b.dumps) {
        o << "## Mavi ekran kayıtları\n\n" << b.dumps->note << "\n\n";
        if (!b.dumps->findings.empty()) {
            o << "| Dosya | Durdurma kodu | Şüpheli sürücü |\n|---|---|---|\n";
            for (const auto& f : b.dumps->findings) {
                o << "| " << f.fileName << " | "
                  << (f.parsed ? f.bugcheckName : std::string("okunamadı")) << " | "
                  << (f.suspectDriver.empty() ? "—" : f.suspectDriver) << " |\n";
            }
            o << "\n";
            const auto& first = b.dumps->findings.front();
            if (first.parsed && !first.bugcheckMeaning.empty())
                o << "En yeni kaydın anlamı: " << first.bugcheckMeaning << "\n\n";
        }
    }

    // ---------------- Olay günlüğü ----------------
    //  Karşı tarafın (forumdaki kişi ya da dil modeli) en çok işine yarayan
    //  bölüm bu olabilir: takılma ölçümü tek bir oturumu anlatır, olay
    //  günlüğü ise makinenin 30 günlük geçmişini.
    //
    //  Sayımlar HÜKÜM DEĞİLDİR ve rapor bunu açıkça yazar; aksi halde karşı
    //  taraf "11 kapanma varmış, bozukmuş" deyip yanlış yönlendirir.
    if (b.evtlog && b.evtlog->attempted) {
        o << "## Olay günlüğü (son 30 gün)\n\n";
        if (!b.evtlog->ok) {
            o << "Okunamadı: " << b.evtlog->error << "\n\n";
        } else {
            bool any = false;
            o << "| Olay | 24 saat | 7 gün | 30 gün |\n|---|---|---|---|\n";
            for (size_t i = 0; i < static_cast<size_t>(sslog::Kind::Count_); ++i) {
                const sslog::Series& s = b.evtlog->series[i];
                if (s.empty()) continue;
                any = true;
                o << "| " << sslog::kindLabel(s.kind) << " | " << s.last24h
                  << " | " << s.last7d << " | " << s.last30d << " |\n";
            }
            if (b.evtlog->liveKernelReports > 0) {
                any = true;
                o << "| Mavi ekransız sürücü çökmesi | — | — | "
                  << b.evtlog->liveKernelReports << " |\n";
            }
            if (!any)
                o << "| _kayda değer olay yok_ | 0 | 0 | 0 |\n";
            o << "\nBu sayımlar tek başına teşhis değildir; sağlıklı "
                 "makinelerde de birikir. Belirleyici olan, olayların takılma "
                 "anlarıyla zaman olarak çakışıp çakışmadığıdır.\n\n";
        }
    }

    // ---------------- Kapanış ----------------
    o << "---\n\n";
    o << "**Sorum:** Yukarıdaki ölçümlere göre takılmanın/düşük FPS'in sebebi "
         "ne olabilir? Hangi adımı önce denemeliyim?\n\n";
    o << "*Not: Syspect kesin hüküm vermez, olasılık sunar. Bir donanımı "
         "değiştirmeden önce en ucuz ve geri alınabilir adımı deneyin.*\n";

    return o.str();
}

// ============================================================================
std::string buildDiagnosticLog(const Bundle& b) {
    std::ostringstream o;
    o << buildShareText(b);

    if (b.telemetry && !b.telemetry->empty()) {
        o << "\n---\n\n## Ham telemetri (saniyede bir örnek)\n\n";
        o << "```\n";
        o << "t(sn)  gpu%   gpuC  gpuMHz  gpuW   vramMB  cpu%  cpuPerf%  "
             "disk%  diskMs  ram%  throttle\n";
        for (const auto& s : *b.telemetry) {
            char line[256];
            auto v = [](double x) { return sstelem::known(x) ? x : -1.0; };
            std::snprintf(line, sizeof(line),
                "%5u  %5.0f  %5.0f  %6.0f  %5.1f  %6.0f  %4.0f  %8.0f  "
                "%5.0f  %6.1f  %4.0f  %c%c%c\n",
                s.tSec, v(s.gpuUtilPct), v(s.gpuTempC), v(s.gpuClockMhz),
                v(s.gpuPowerW), v(s.gpuMemUsedMb), v(s.cpuUsagePct),
                v(s.cpuPerfPct), v(s.diskActivePct), v(s.diskLatencyMs),
                v(s.ramUsedPct),
                s.thermalThrottle ? 'T' : '-',
                s.powerCapThrottle ? 'P' : '-',
                s.powerBrake ? 'B' : '-');
            o << line;
        }
        o << "```\n\n";
        o << "*-1 = o an okunamayan değer. throttle sütunu: T=ısınma, "
             "P=güç limiti, B=donanımsal frenleme.*\n";
    }

    if (b.result && !b.result->frames.empty()) {
        const auto& fr = b.result->frames;
        o << "\n## Kare süreleri (ilk 200 kare, ms)\n\n```\n";
        for (size_t i = 0; i < fr.size() && i < 200; ++i) {
            o << f2(fr[i].frameTimeMs, 2) << (((i + 1) % 12) ? "  " : "\n");
        }
        o << "\n```\n";
    }

    return o.str();
}

} // namespace ssreport

#endif // _WIN32
