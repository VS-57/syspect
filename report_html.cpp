// ============================================================================
//  StutterScope — HTML rapor ureteci
// ============================================================================
#include "report_html.h"

#include <cstdio>
#include <sstream>

namespace ss {
namespace {

std::string esc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (const char c : s) {
        switch (c) {
            case '&':  o += "&amp;";  break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            case '"':  o += "&quot;"; break;
            case '\'': o += "&#39;";  break;
            default:   o += c;
        }
    }
    return o;
}

std::string num(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

// Guven yuzdesine gore renk: dusuk guven sari, yuksek guven yesil.
// Kirmizi KULLANILMAZ — kirmizi "sorun agir" demektir, "eminiz" demek degil.
const char* confidenceColor(int pct) {
    if (pct >= 75) return "#3ddc97";
    if (pct >= 50) return "#ffc857";
    return "#8a94a6";
}

// Tek bir sayi karti
void statCard(std::ostringstream& h, const std::string& label,
              const std::string& value, const std::string& unit = "") {
    h << "<div class=\"stat\">"
      << "<div class=\"stat-label\">" << esc(label) << "</div>"
      << "<div class=\"stat-value\">" << esc(value);
    if (!unit.empty()) h << "<span class=\"stat-unit\">" << esc(unit) << "</span>";
    h << "</div></div>\n";
}

} // namespace

// ============================================================================
std::string renderHtmlReport(const AnalysisResult& r,
                             const FrameSourceInfo& src,
                             const SystemInfo& sys,
                             const Translator& tr) {
    const SessionStats& s = r.stats;
    const Diagnosis&    d = r.diagnosis;

    // Ceviri kancasi verilmemisse metinler oldugu gibi kalir. Bu yol Turkce
    // ciktiyi bugunkuyle bit bit ayni tutuyor ve katmanin tasinabilirligini
    // bozmuyor (tasarim kurali 5).
    auto T = [&tr](const std::string& v) -> std::string {
        return tr.valid() ? tr.text(v) : v;
    };

    // Kanit cumleleri: hazir metin ceviri tablosunda bulunamaz cunku icinde
    // olculmus sayilar var. Sablon aranir, degerler sonra yerlesir. Parcalar
    // yoksa (eski kayit ya da cevrilmemis kural) hazir metne geri donulur.
    auto evidenceOf = [&tr, &T](const Hypothesis& hy) -> std::string {
        if (!tr.valid() || !tr.format || hy.evidenceParts.empty())
            return T(hy.evidence);
        std::string out;
        for (const auto& p : hy.evidenceParts) {
            if (!out.empty()) out += "; ";
            out += tr.format(p.tpl, p.args);
        }
        return out;
    };

    // Belgenin dil etiketi de degismeli: ekran okuyucular ve tarayici ceviri
    // onerisi bu alani okuyor. Kanca varsa dil kodunu ondan sor.
    const std::string lang = tr.valid() ? T("tr") : std::string("tr");

    std::ostringstream h;

    h << "<!doctype html>\n<html lang=\"" << esc(lang) << "\">\n<head>\n"
      << "<meta charset=\"utf-8\">\n"
      << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
      << "<title>" << esc(T("Syspect — Teşhis Raporu")) << "</title>\n"
      << "<style>\n"
         "*{box-sizing:border-box;margin:0;padding:0}\n"
         "body{background:#0e1116;color:#e6e9ef;font:15px/1.6 -apple-system,"
         "'Segoe UI',Roboto,sans-serif;padding:32px 20px;min-height:100vh}\n"
         ".wrap{max-width:860px;margin:0 auto}\n"
         "header{display:flex;align-items:baseline;gap:12px;flex-wrap:wrap;"
         "padding-bottom:18px;border-bottom:1px solid #232936;margin-bottom:28px}\n"
         ".brand{font-size:19px;font-weight:650;letter-spacing:-.01em}\n"
         ".brand span{color:#5b6478;font-weight:400}\n"
         ".meta{margin-left:auto;color:#8a94a6;font-size:13px;text-align:right}\n"
         ".meta b{color:#c3cad6;font-weight:600}\n"

         ".verdict{background:#151a22;border:1px solid #232936;border-radius:14px;"
         "padding:26px 28px;margin-bottom:24px;display:flex;gap:26px;align-items:center}\n"
         ".verdict-text{flex:1;min-width:0}\n"
         ".verdict h1{font-size:21px;line-height:1.35;font-weight:620;"
         "letter-spacing:-.01em;margin-bottom:8px}\n"
         ".verdict p{color:#8a94a6;font-size:13.5px}\n"
         ".ring{flex-shrink:0;position:relative;width:104px;height:104px}\n"
         ".ring svg{transform:rotate(-90deg)}\n"
         ".ring-label{position:absolute;inset:0;display:flex;flex-direction:column;"
         "align-items:center;justify-content:center}\n"
         ".ring-pct{font-size:25px;font-weight:670;letter-spacing:-.02em}\n"
         ".ring-cap{font-size:10.5px;color:#5b6478;text-transform:uppercase;"
         "letter-spacing:.09em;margin-top:1px}\n"

         "h2{font-size:12px;text-transform:uppercase;letter-spacing:.1em;"
         "color:#5b6478;margin:30px 0 13px;font-weight:640}\n"

         ".hyp{background:#151a22;border:1px solid #232936;border-radius:11px;"
         "padding:17px 19px;margin-bottom:11px}\n"
         ".hyp-top{display:flex;align-items:center;gap:12px;margin-bottom:11px}\n"
         ".hyp-pct{font-size:17px;font-weight:670;min-width:52px;letter-spacing:-.01em}\n"
         ".hyp-name{font-weight:570;flex:1;font-size:15px}\n"
         ".bar{height:5px;background:#232936;border-radius:3px;overflow:hidden;"
         "margin-bottom:13px}\n"
         ".bar i{display:block;height:100%;border-radius:3px}\n"
         ".row{display:flex;gap:9px;font-size:13.5px;margin-top:7px;"
         "align-items:flex-start}\n"
         ".row .k{color:#5b6478;min-width:74px;flex-shrink:0;font-size:12.5px;"
         "padding-top:1px}\n"
         ".row .v{color:#c3cad6}\n"
         ".row.act .v{color:#3ddc97}\n"

         ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(128px,1fr));"
         "gap:10px}\n"
         ".stat{background:#151a22;border:1px solid #232936;border-radius:10px;"
         "padding:14px 16px}\n"
         ".stat-label{color:#5b6478;font-size:11.5px;text-transform:uppercase;"
         "letter-spacing:.07em;margin-bottom:6px}\n"
         ".stat-value{font-size:21px;font-weight:640;letter-spacing:-.02em}\n"
         ".stat-unit{font-size:12px;color:#5b6478;font-weight:400;margin-left:3px}\n"

         // Olay gunlugu tablosu. Kart izgarasi bu veriye uymuyor: satir
         // sayisi degisken ve her satirin bir aciklamasi var.
         "table{width:100%;border-collapse:collapse;font-size:13.5px;"
         "background:#151a22;border:1px solid #232936;border-radius:10px;"
         "overflow:hidden}\n"
         "th{text-align:left;color:#5b6478;font-size:11.5px;font-weight:600;"
         "text-transform:uppercase;letter-spacing:.07em;padding:11px 14px;"
         "border-bottom:1px solid #232936}\n"
         "td{padding:10px 14px;color:#c3cad6;border-bottom:1px solid #1c222c;"
         "vertical-align:top}\n"
         "tr:last-child td{border-bottom:none}\n"
         "td:nth-child(2){text-align:right;font-weight:640;color:#e6ebf2;"
         "white-space:nowrap}\n"
         "td b{color:#ffc857}\n"

         ".flag{background:#1a1f28;border-left:3px solid #ffc857;border-radius:0 8px 8px 0;"
         "padding:13px 17px;margin:14px 0;font-size:13.5px;color:#c3cad6}\n"
         ".flag b{color:#ffc857;font-weight:600}\n"

         "footer{margin-top:34px;padding-top:18px;border-top:1px solid #232936;"
         "color:#5b6478;font-size:12.5px;line-height:1.65}\n"
         "@media(max-width:560px){.verdict{flex-direction:column;text-align:center}}\n"
      << "</style>\n</head>\n<body>\n<div class=\"wrap\">\n";

    // ---- Baslik ----
    h << "<header>\n"
      << "<div class=\"brand\">StutterScope <span>v0.1</span></div>\n"
      << "<div class=\"meta\">";
    if (!src.application.empty()) h << "<b>" << esc(src.application) << "</b><br>";
    h << s.frameCount << " kare analiz edildi";
    if (!sys.cpuName.empty() || !sys.gpuName.empty()) {
        h << "<br>" << esc(sys.cpuName);
        if (!sys.cpuName.empty() && !sys.gpuName.empty()) h << " &middot; ";
        h << esc(sys.gpuName);
    }
    h << "</div>\n</header>\n";

    // ---- Hüküm ----
    const char* col = confidenceColor(d.confidence);
    const double circ = 2.0 * 3.14159265 * 46.0;
    const double fill = circ * (d.confidence / 100.0);

    h << "<div class=\"verdict\">\n<div class=\"verdict-text\">\n"
      << "<h1>" << esc(T(d.headline)) << "</h1>\n";
    if (d.inconclusive) {
        h << "<p>" << esc(T("Motor hüküm vermeyi reddediyor. Yanlış teşhis, "
             "teşhissizlikten pahalıdır — daha uzun bir kayıt alın veya eksik "
             "donanım bilgisini girin.")) << "</p>\n";
    } else {
        h << "<p>" << esc(T("Aşağıdaki olasılıklar ölçülen kare desenine ve "
             "verdiğiniz donanım bilgisine dayanır.")) << "</p>\n";
    }
    h << "</div>\n"
      << "<div class=\"ring\">\n"
      << "<svg width=\"104\" height=\"104\">\n"
      << "<circle cx=\"52\" cy=\"52\" r=\"46\" fill=\"none\" stroke=\"#232936\" "
         "stroke-width=\"8\"/>\n"
      << "<circle cx=\"52\" cy=\"52\" r=\"46\" fill=\"none\" stroke=\"" << col
      << "\" stroke-width=\"8\" stroke-linecap=\"round\" stroke-dasharray=\""
      << num(fill, 1) << " " << num(circ, 1) << "\"/>\n"
      << "</svg>\n"
      << "<div class=\"ring-label\"><div class=\"ring-pct\" style=\"color:" << col
      << "\">%" << d.confidence << "</div><div class=\"ring-cap\">"
      << esc(T("güven")) << "</div></div>\n"
      << "</div>\n</div>\n";

    // ---- Hipotezler ----
    if (!d.ranked.empty()) {
        h << "<h2>" << esc(T("Olası sebepler")) << "</h2>\n";
        for (const Hypothesis& hy : d.ranked) {
            const char* hcol = confidenceColor(hy.percent);
            h << "<div class=\"hyp\">\n"
              << "<div class=\"hyp-top\">"
              << "<div class=\"hyp-pct\" style=\"color:" << hcol << "\">%"
              << hy.percent << "</div>"
              << "<div class=\"hyp-name\">" << esc(T(hy.label)) << "</div></div>\n"
              << "<div class=\"bar\"><i style=\"width:" << hy.percent
              << "%;background:" << hcol << "\"></i></div>\n";
            const std::string ev = evidenceOf(hy);
            if (!ev.empty())
                h << "<div class=\"row\"><div class=\"k\">" << esc(T("Kanıt"))
                  << "</div><div class=\"v\">" << esc(ev) << "</div></div>\n";
            if (!hy.action.empty())
                h << "<div class=\"row act\"><div class=\"k\">"
                  << esc(T("Yapılacak")) << "</div><div class=\"v\">"
                  << esc(T(hy.action)) << "</div></div>\n";
            h << "</div>\n";
        }
    }

    // ---- Takılmalar ----
    h << "<h2>" << esc(T("Takılmalar")) << "</h2>\n<div class=\"grid\">\n";
    statCard(h, "Toplam",        std::to_string(s.stutterCount));
    statCard(h, "Mikro",         std::to_string(s.microStutterCount));
    statCard(h, "Sıçrama",       std::to_string(s.hitchCount));
    statCard(h, "Donma",         std::to_string(s.freezeCount));
    h << "</div>\n";

    if (s.periodicMicroStutter) {
        h << "<div class=\"flag\"><b>Düzenli desen.</b> Mikro-takılmalar rastgele "
             "değil, yaklaşık " << num(s.microStutterPeriodMs, 0)
          << " ms aralıklarla tekrarlıyor. Bu, kare zamanlaması veya değişken "
             "yenileme hızı (VRR) parmak izidir; donanım arızası değildir.</div>\n";
    }
    if (s.lowFpsNotStutter) {
        h << "<div class=\"flag\"><b>Takılma yok.</b> Kare süreleri dar ve düzenli "
             "dağılmış. Sorun kararlılık değil, ham performans — ayarları düşürmek "
             "veya donanım yükseltmek dışında yapılacak bir şey yok.</div>\n";
    }

    // ---- Kare zamanları ----
    h << "<h2>" << esc(T("Kare zamanları")) << "</h2>\n<div class=\"grid\">\n";
    statCard(h, "Ortalama FPS",  num(s.avgFps, 1));
    statCard(h, "%1 düşük FPS",  num(s.onePercentLowFps, 1));
    statCard(h, "Medyan",        num(s.medianFrameTimeMs, 2), "ms");
    statCard(h, "P99",           num(s.p99FrameTimeMs, 2), "ms");
    h << "</div>\n";

    // ---- Sistemin kendi sikayetleri (olay gunlugu) ----
    //  Bu bolum tasinabilir katmanda oldugu icin sslog'u GORMEZ; veriyi
    //  SystemInfo uzerinden alir. Boylece core.h'nin Windows'a bagimsiz
    //  kalma kurali (tasarim kurali 5) bozulmaz ve sentetik SystemInfo ile
    //  test edilebilir.
    //
    //  Sayilar hukum degildir; bolum basligi ve kapanis cumlesi bunu soyler.
    if (sys.eventLogRead) {
        const bool anything =
            sys.kernelPower41 || sys.wheaFatal || sys.bugcheckCount ||
            sys.tdrCount || sys.storageResetCount || sys.storageErrorCount ||
            sys.ntfsCorruption || sys.liveKernelReports ||
            sys.wheaCorrected >= 100;

        h << "<h2>" << esc(T("Sistemin kendi kayıtları (son 30 gün)")) << "</h2>\n";
        if (!anything) {
            h << "<div class=\"flag\">Olay günlüğü tarandı: kontrolsüz kapanma, "
                 "donanım hatası, ekran sürücüsü sıfırlaması ve disk hatası "
                 "bulunamadı.</div>\n";
        } else {
            h << "<table>\n<tr><th>Kayıt</th><th>Adet</th><th>Not</th></tr>\n";

            auto row = [&h](const char* name, uint32_t n, const char* note) {
                if (n == 0) return;
                h << "<tr><td>" << name << "</td><td>" << n << "</td><td>"
                  << note << "</td></tr>\n";
            };

            row("Kontrolsüz kapanma (Kernel-Power 41)", sys.kernelPower41,
                sys.bugcheckCount == 0
                    ? "Mavi ekran kaydı yok — bellek/EXPO ya da güç kaynağı"
                    : "Mavi ekran kayıtlarıyla birlikte");
            row("Ölümcül donanım hatası (WHEA 18)", sys.wheaFatal,
                "Sağlıklı makinede hiç oluşmaz");
            row("Mavi ekran kaydı", sys.bugcheckCount, "&mdash;");
            row("Ekran sürücüsü sıfırlandı (TDR)", sys.tdrCount,
                sys.tdrDuringCapture > 0
                    ? "<b>Ölçüm sırasında da oldu</b>"
                    : "Ölçüm sırasında tekrarlamadı");
            row("Depolama aygıtı sıfırlandı", sys.storageResetCount,
                "Sıfırlama sırasında diske giden her işlem durur");
            row("Disk hatası kaydı", sys.storageErrorCount, "&mdash;");
            row("Dosya sistemi bozulması", sys.ntfsCorruption,
                "Genelde sebep değil sonuç");
            row("Mavi ekransız sürücü çökmesi", sys.liveKernelReports,
                "Fark edilmemiş olabilir");
            if (sys.wheaCorrected >= 100)
                row("Düzeltilmiş donanım hatası (WHEA)", sys.wheaCorrected,
                    sys.wheaCorrectedSpiking
                        ? "<b>Son 24 saatte artış var</b>"
                        : "Birçok sistemde belirtisiz birikir; tek başına "
                          "sorun sayılmaz");

            h << "</table>\n";
            h << "<div class=\"flag\">Bu sayımlar tek başına teşhis değildir. "
                 "Sağlıklı makinelerde de birikirler; belirleyici olan, "
                 "olayların takılma anlarıyla zaman olarak çakışmasıdır.</div>\n";
        }
    }

    // ---- Altbilgi ----
    h << "<footer>\n";
    if (src.kind == "csv") {
        h << "<b>Sınır:</b> Bu rapor yalnızca kare deseni üzerine kuruludur. "
             "Sürücü DPC gecikmeleri, VRAM taşması ve disk beklemeleri ölçülmedi — "
             "bunlar için ETW toplayıcısı gerekir.<br>\n";
    }
    h << "Kaynak: " << esc(src.kind);
    if (!src.note.empty()) h << " &middot; " << esc(src.note);
    h << "<br>StutterScope hiçbir zaman kesin hüküm vermez. Bir donanımı "
         "değiştirmeden önce en ucuz ve geri alınabilir adımı deneyin.\n"
      << "</footer>\n";

    h << "</div>\n</body>\n</html>\n";
    return h.str();
}

} // namespace ss
