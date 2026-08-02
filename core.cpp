// ============================================================================
//  StutterScope — Cekirdek analiz motoru (uygulama)
// ============================================================================
#include "core.h"

#include <algorithm>
#include <numeric>
#include <cmath>

namespace ss {

// ============================================================================
//  Yardimci istatistik
// ============================================================================
static double medianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    double m = v[mid];
    if (v.size() % 2 == 0) {
        // cift sayida eleman: iki ortancanin ortalamasi
        double lower = *std::max_element(v.begin(), v.begin() + mid);
        m = (m + lower) / 2.0;
    }
    return m;
}

static double percentileOf(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double idx = p * (static_cast<double>(v.size()) - 1);
    size_t lo = static_cast<size_t>(idx);
    size_t hi = std::min(lo + 1, v.size() - 1);
    double frac = idx - static_cast<double>(lo);
    return v[lo] * (1.0 - frac) + v[hi] * frac;
}

// ============================================================================
//  StutterDetector
// ============================================================================
StutterDetector::StutterDetector(size_t windowSize)
    : window_(windowSize == 0 ? 1 : windowSize) {}

double StutterDetector::thresholdFor(double baselineMs) {
    return std::max(2.0 * baselineMs, baselineMs + 8.0);
}

bool StutterDetector::warmedUp() const {
    // Pencerenin en az dortte biri dolmadan hukum vermeyiz
    return ring_.size() >= std::max<size_t>(20, window_ / 4);
}

double StutterDetector::baselineMs() const {
    if (ring_.empty()) return 0.0;
    return medianOf(std::vector<double>(ring_.begin(), ring_.end()));
}

std::optional<StutterEvent> StutterDetector::push(const FrameSample& f,
                                                  const SignalSnapshot& sig) {
    ++totalSamples_;

    std::optional<StutterEvent> result;

    if (warmedUp()) {
        const double base = baselineMs();
        const double thr  = thresholdFor(base);

        if (base > 0.0 && f.frameTimeMs > thr) {
            StutterEvent e;
            e.timestampUs = f.timestampUs;
            e.frameTimeMs = f.frameTimeMs;
            e.baselineMs  = base;
            e.severity    = f.frameTimeMs / base;
            e.signals     = sig;

            if (f.frameTimeMs >= 500.0)      e.kind = StutterKind::Freeze;
            else if (f.frameTimeMs >= 50.0)  e.kind = StutterKind::Hitch;
            else                             e.kind = StutterKind::MicroStutter;

            result = e;
        }
    }

    // Takilan kareyi de pencereye ekliyoruz ama medyan kullandigimiz icin
    // taban cizgisini bozmuyor. Boylece kalici bir yavaslama olursa taban
    // dogal olarak kayar ve surekli yanlis alarm uretmeyiz.
    ring_.push_back(f.frameTimeMs);
    if (ring_.size() > window_) ring_.pop_front();

    return result;
}

// ============================================================================
//  Oturum analizi
// ============================================================================
SessionStats analyzeSession(const std::vector<FrameSample>& frames,
                            const std::vector<StutterEvent>& events) {
    SessionStats s;
    s.frameCount = frames.size();
    if (frames.empty()) return s;

    std::vector<double> ft;
    ft.reserve(frames.size());
    for (const auto& f : frames) ft.push_back(f.frameTimeMs);

    s.medianFrameTimeMs = medianOf(ft);
    s.p99FrameTimeMs    = percentileOf(ft, 0.99);
    if (s.medianFrameTimeMs > 0.0) s.medianFps = 1000.0 / s.medianFrameTimeMs;
    if (s.p99FrameTimeMs   > 0.0) s.onePercentLowFps = 1000.0 / s.p99FrameTimeMs;

    // Gercek ortalama: toplam kare / toplam sure. Medyandan turetilen deger
    // ortalama DEGILDIR — uzun takilmalar medyani hic etkilemez ama ortalamayi
    // dusurur, ve kullanicinin "ortalama FPS" derken kastettigi budur.
    double totalMs = 0.0;
    for (const double v : ft) totalMs += v;
    s.durationSec = totalMs / 1000.0;
    if (totalMs > 0.0) s.avgFps = (static_cast<double>(ft.size()) * 1000.0) / totalMs;

    s.stutterCount = events.size();
    for (const auto& e : events) {
        switch (e.kind) {
            case StutterKind::MicroStutter: ++s.microStutterCount; break;
            case StutterKind::Hitch:        ++s.hitchCount;        break;
            case StutterKind::Freeze:       ++s.freezeCount;       break;
        }
    }

    // --- Mikro-takilma duzenli mi? --------------------------------------
    // VRR / frame pacing sorunlarinin parmak izi: olaylar arasindaki sure
    // neredeyse sabittir. Rastgele hitch'lerde ise degisken olur.
    if (s.microStutterCount >= 5) {
        std::vector<double> gaps;
        const StutterEvent* prev = nullptr;
        for (const auto& e : events) {
            if (e.kind != StutterKind::MicroStutter) continue;
            if (prev) gaps.push_back((e.timestampUs - prev->timestampUs) / 1000.0);
            prev = &e;
        }
        if (gaps.size() >= 4) {
            double mean = std::accumulate(gaps.begin(), gaps.end(), 0.0) / gaps.size();
            double var  = 0.0;
            for (double g : gaps) var += (g - mean) * (g - mean);
            var /= gaps.size();
            double cv = (mean > 0.0) ? std::sqrt(var) / mean : 1.0; // degisim katsayisi
            if (cv < 0.25) {
                s.periodicMicroStutter = true;
                s.microStutterPeriodMs = mean;
            }
        }
    }

    // --- Takilma mi, yoksa sadece yavas mi? ------------------------------
    // Takilma yok denecek kadar az olay + dusuk FPS + dar dagilim
    // => bu bir kararlilik sorunu degil, yetersiz donanimdir.
    const double stutterRate = static_cast<double>(s.stutterCount) / s.frameCount;
    const bool   tightSpread = (s.medianFrameTimeMs > 0.0) &&
                               (s.p99FrameTimeMs / s.medianFrameTimeMs < 1.5);
    if (stutterRate < 0.002 && s.avgFps < 45.0 && tightSpread)
        s.lowFpsNotStutter = true;

    return s;
}

// ============================================================================
//  Etiketler
// ============================================================================
const char* stutterKindName(StutterKind k) {
    switch (k) {
        case StutterKind::MicroStutter: return "Mikro-takılma";
        case StutterKind::Hitch:        return "Hitch";
        case StutterKind::Freeze:       return "Donma";
    }
    return "Bilinmiyor";
}

const char* causeLabel(Cause c) {
    switch (c) {
        case Cause::DRIVER_DPC:     return "Sürücü sistemi blokluyor (DPC)";
        case Cause::GPU_DRIVER:     return "Ekran kartı sürücüsü";
        case Cause::MEMORY_EXPO:    return "Bellek / EXPO-XMP kararsızlığı";
        case Cause::OVERCLOCK_CO:   return "Undervolt / Curve Optimizer";
        case Cause::VRAM:           return "Ekran kartı belleği (VRAM) yetmiyor";
        case Cause::SHADER_COMPILE: return "Gölgelendirici derlemesi (normal)";
        case Cause::VRR_PACING:     return "VRR / kare zamanlama uyumsuzluğu";
        case Cause::PAGEFILE_RAM:   return "RAM yetersiz, sayfalama yapılıyor";
        case Cause::STORAGE:        return "Depolama (SSD/HDD) gecikmesi";
        case Cause::THERMAL:        return "Isınma sebebiyle hız düşürme";
        case Cause::POWER_PLAN:     return "Güç planı / çekirdek park";
        case Cause::BACKGROUND_APP: return "Arka planda çalışan program";
        case Cause::PSU:            return "Güç kaynağı yetersizliği";
        case Cause::UNBALANCED_HW:  return "Donanım bu ayarlar için yetersiz";
        case Cause::RECENT_CHANGE:  return "Yakın zamandaki bir değişiklik";
        case Cause::CPU_BOTTLENECK: return "İşlemci darboğazı";
        case Cause::GPU_UNDERUSED:  return "Donanım tam kullanılmıyor";
        case Cause::UNKNOWN_OTHER:  return "Ölçülmemiş diğer sebepler";
    }
    return "Bilinmiyor";
}

const char* causeKey(Cause c) {
    switch (c) {
        case Cause::DRIVER_DPC:     return "DRIVER_DPC";
        case Cause::GPU_DRIVER:     return "GPU_DRIVER";
        case Cause::MEMORY_EXPO:    return "MEMORY_EXPO";
        case Cause::OVERCLOCK_CO:   return "OVERCLOCK_CO";
        case Cause::VRAM:           return "VRAM";
        case Cause::SHADER_COMPILE: return "SHADER_COMPILE";
        case Cause::VRR_PACING:     return "VRR_PACING";
        case Cause::PAGEFILE_RAM:   return "PAGEFILE_RAM";
        case Cause::STORAGE:        return "STORAGE";
        case Cause::THERMAL:        return "THERMAL";
        case Cause::POWER_PLAN:     return "POWER_PLAN";
        case Cause::BACKGROUND_APP: return "BACKGROUND_APP";
        case Cause::PSU:            return "PSU";
        case Cause::UNBALANCED_HW:  return "UNBALANCED_HW";
        case Cause::RECENT_CHANGE:  return "RECENT_CHANGE";
        case Cause::CPU_BOTTLENECK: return "CPU_BOTTLENECK";
        case Cause::GPU_UNDERUSED:  return "GPU_UNDERUSED";
        case Cause::UNKNOWN_OTHER:  return "UNKNOWN_OTHER";
    }
    return "UNKNOWN";
}

const char* causeAction(Cause c) {
    switch (c) {
        case Cause::DRIVER_DPC:
            return "Suçlanan sürücüyü üretici sitesinden güncelleyin.";
        case Cause::GPU_DRIVER:
            return "DDU ile temiz kaldırıp güncel sürücüyü kurun.";
        case Cause::MEMORY_EXPO:
            return "BIOS’ta EXPO/XMP’yi KAPATIP bir gün kullanın. Düzelirse hızı bir kademe düşürüp tekrar açın.";
        case Cause::OVERCLOCK_CO:
            return "Curve Optimizer / PBO / undervolt ayarlarını varsayılana döndürün.";
        case Cause::VRAM:
            return "Doku (texture) kalitesini bir kademe düşürün.";
        case Cause::SHADER_COMPILE:
            return "Bir şey yapmanıza gerek yok, birkaç dakika içinde geçer.";
        case Cause::VRR_PACING:
            return "G-Sync/FreeSync ile V-Sync ayarlarını kontrol edin, FPS sınırını yenileme hızının 3 altına alın.";
        case Cause::PAGEFILE_RAM:
            return "Arka plandaki programları kapatın veya RAM yükseltmeyi değerlendirin.";
        case Cause::STORAGE:
            return "SSD firmware’ini güncelleyin, SMART değerlerine bakın.";
        case Cause::THERMAL:
            return "Kasa içi hava akışını ve tozu kontrol edin.";
        case Cause::POWER_PLAN:
            return "Windows güç planını Yüksek Performans yapın, BIOS’ta C-State’i kapatmayı deneyin.";
        case Cause::BACKGROUND_APP:
            return "Belirtilen programı oyun sırasında kapatın.";
        case Cause::PSU:
            return "Başka bir güç kaynağı ile test edin; GPU kablolarını ayrı hatlardan çekin.";
        case Cause::UNBALANCED_HW:
            return "Oyun ayarlarını düşürün. Bu bir arıza değil, performans sınırı.";
        case Cause::RECENT_CHANGE:
            return "Belirtilen değişikliği geri alıp test edin.";
        case Cause::CPU_BOTTLENECK:
            return "Çözünürlüğü YÜKSELTİN (işlemci yükü değişmez, ekran kartı "
                   "devreye girer) ve işlemciye yük bindiren ayarları düşürün: "
                   "kalabalık/NPC yoğunluğu, görüş mesafesi, gölge detayı.";
        case Cause::GPU_UNDERUSED:
            return "Önce FPS sınırı ve dikey eşitlemeyi kontrol edin. Yoksa güç "
                   "planını Yüksek Performans yapın ve ekran kartının güç "
                   "limitine takılıp takılmadığına bakın.";
        case Cause::UNKNOWN_OTHER:
            return "Bu paya bakarak bir şey değiştirmeyin. Daha uzun bir kayıt alın; "
                   "DPC ve VRAM sinyalleri için ETW toplayıcısı gerekir.";
    }
    return "";
}

// ============================================================================
//  Platform bellek hizi tavani
// ----------------------------------------------------------------------------
//  AM5'te bellek denetleyicisi (IMC) belirli hizlarin ustunde 1:2 moduna
//  gecer ve kararsizlik olasiligi ciddi sekilde artar. Modul sayisi arttikca
//  guvenli tavan duser.
//
//  Intel DDR5'te ayni olgunun adi Gear 1 -> Gear 2 gecisidir ve 4 DIMM'de
//  kararli tavan yine belirgin duser. Tavan AM5'inkinden YUKSEK tutuluyor:
//  Intel'in bellek denetleyicisi pratikte daha yuksek hizlari tasiyor ve
//  ayni esigi kullanmak saglikli Intel makinelerinde sahte bellek suphesi
//  uretirdi — tasarim kurali 3'un dogrudan konusu.
//
//  Platform bilinmiyorsa 0 doner ve kural HIC uygulanmaz. "Bilmiyorum"
//  gecerli bir cevaptir; tahminle esik uydurmak degildir.
// ============================================================================
uint32_t safeMemorySpeedFor(const SystemInfo& sys) {
    if (sys.isAM5) {
        if (sys.ramModuleCount >= 4) return 5600;
        if (sys.ramModuleCount == 1) return 6000; // tek cubuk da sorunlu olabilir
        return 6000;                              // 2 cubuk icin tatli nokta
    }
    if (sys.isIntelDdr5) {
        if (sys.ramModuleCount >= 4) return 5600; // 4 DIMM'de tavan sert duser
        return 6400;                              // 1-2 DIMM
    }
    return 0; // bilinmiyor / kural uygulanmaz
}

// ============================================================================
//  Kanit parcasi
// ============================================================================
//  Yer tutucu bicimi {1}..{9}.
//
//  Ilk denemede %1 kullanilmisti ve BOZULDU: Turkce yuzdeyi sayidan ONCE
//  yaziyor ("%40", "%5'i"), yani metinlerin kendisinde bol bol % geciyor.
//  "disk islemlerinin %5'i" ifadesindeki %5, bir yer tutucu sanildi. Kaçış
//  eklemek yerine cakismayan bir isaret secmek dogru cozum — ceviri yapan
//  kisi de kacis kurali ogrenmek zorunda kalmiyor.
std::string EvidencePart::format() const {
    std::string out;
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

// ============================================================================
//  Teshis motoru
// ============================================================================
namespace {

struct Acc {
    std::map<Cause, int>                       score;
    std::map<Cause, std::vector<EvidencePart>> parts;

    // Sablonlu surum — tercih edilen. Sablon ceviri tablosunda aranabilir,
    // degerler ayri durur.
    void add(Cause c, int w, EvidencePart p) {
        score[c] += w;
        if (!p.tpl.empty()) parts[c].push_back(std::move(p));
    }

    // Argumansiz kisayol: metnin tamami sabitse sablonun kendisidir.
    void add(Cause c, int w, const std::string& reason) {
        add(c, w, EvidencePart{reason, {}});
    }
};

// Kanit parcalarini tek bir Turkce cumleye birlestirir. Ceviri yapmayan
// cagiranlar (komut satiri, testler, .syscap) bu metni okumaya devam ediyor.
std::string joinParts(const std::vector<EvidencePart>& ps) {
    std::string out;
    for (const auto& p : ps) {
        if (!out.empty()) out += "; ";
        out += p.format();
    }
    return out;
}

// olaylarin yuzde kaci kosulu saglıyor
template <typename Pred>
double fractionOf(const std::vector<StutterEvent>& ev, Pred p) {
    if (ev.empty()) return 0.0;
    size_t n = 0;
    for (const auto& e : ev) if (p(e)) ++n;
    return static_cast<double>(n) / static_cast<double>(ev.size());
}

std::string pct(double f) {
    return "%" + std::to_string(static_cast<int>(f * 100.0 + 0.5));
}

} // namespace

Diagnosis diagnose(const SessionStats& stats,
                   const std::vector<StutterEvent>& events,
                   const SystemInfo& sys) {
    Diagnosis d;
    Acc acc;

    // ---------------------------------------------------------------------
    // KURAL 0 — Takilma yok, sadece dusuk FPS
    // ---------------------------------------------------------------------
    if (stats.lowFpsNotStutter) {
        // "Donanim yetersiz" hukmu YALNIZCA ekran karti gercekten doluyken
        // verilebilir. Ekran karti %70'te kalirken FPS dusukse donanim
        // yetersiz DEGILDIR — kullanilamiyordur, ki bu bambaska bir sorun.
        // (Gercek vaka: RTX 5070 + 16 GB RAM, GPU %70, CPU %99, sebep RAM.)
        const bool gpuStarved =
            SystemInfo::pctKnown(sys.medianGpuUtilPct) &&
            sys.medianGpuUtilPct < kGpuSaturatedPct;

        if (!gpuStarved) {
            acc.add(Cause::UNBALANCED_HW, 100,
                    "takilma tespit edilmedi; kare suresi dusuk ama duzenli ("
                    + std::to_string(static_cast<int>(stats.avgFps)) + " FPS) "
                    "ve ekran karti dolu");
            d.headline = "Sisteminizde takılma yok. FPS bu ayarlar için düşük.";
        } else {
            d.headline = "Takılma yok ama FPS düşük ve ekran kartı boşta "
                         "bekliyor — darboğaz başka yerde.";
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 1 — Surucu DPC
    // Tek bir .sys olaylarin buyuk kisminda uzun DPC yapiyorsa guclu kanit.
    // ---------------------------------------------------------------------
    if (!events.empty()) {
        std::map<std::string, size_t> drvHits;
        for (const auto& e : events)
            if (e.signals.dpcMaxMs > 1.0 && !e.signals.dpcDriver.empty())
                ++drvHits[e.signals.dpcDriver];

        if (!drvHits.empty()) {
            auto best = std::max_element(
                drvHits.begin(), drvHits.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            double frac = static_cast<double>(best->second) / events.size();
            if (frac >= 0.40) {
                acc.add(Cause::DRIVER_DPC, static_cast<int>(frac * 90),
                        EvidencePart{"takilmalarin {1}'i {2} surucusunun uzun "
                                     "DPC'leri ile cakisiyor",
                                     {pct(frac), best->first}});
            } else if (frac >= 0.20) {
                acc.add(Cause::DRIVER_DPC, static_cast<int>(frac * 60),
                        EvidencePart{"takilmalarin {1}'i {2} ile cakisiyor",
                                     {pct(frac), best->first}});
            }
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 2 — Bellek / EXPO kararsizligi
    // En degerli kural: vaka kulliyatinin yarisi buraya cikiyor.
    // ---------------------------------------------------------------------
    {
        int w = 0;
        std::vector<EvidencePart> why;

        uint32_t safe = safeMemorySpeedFor(sys);
        if (safe > 0 && sys.ramConfiguredMTs > safe) {
            w += 30;
            why.push_back({"{1} MT/s, bu platformda guvenli kabul edilen "
                           "{2} MT/s ustunde",
                           {std::to_string(sys.ramConfiguredMTs),
                            std::to_string(safe)}});
        }
        // Karisik modul takimi. Hiz tavani kuralindan BAGIMSIZ ve platformdan
        // bagimsiz: DDR4 bir Intel makinesinde de gecerlidir. Agirligi hiz
        // asimiyla ayni seviyede tutuluyor cunku ikisi de "olculmus olgu",
        // cikarim degil.
        if (sys.ramModulesMismatched) {
            w += 30;
            why.push_back({"takili bellek modulleri birbirinin aynisi degil — "
                           "modul basina test bu durumu elemez", {}});
        }
        // Duzeltilmis WHEA olaylari TEK BASINA sinyal DEGILDIR: ID 17 bircok
        // AM5 sisteminde semptomsuz binlerce kez uretilir ve tek basina
        // agirlik verilirse bellek kurali her AM5 makinesinde sahte bir
        // yuzdeye oturur. Iki kosuldan biri sart:
        //   (a) bellek yonunde baska bir kanit zaten var, ya da
        //   (b) sayim taban cizgisinin cok ustune cikmis (spiking).
        // Bkz. CLAUDE.md, WHEA notu ve tasarim kurali 3.
        //
        // DIKKAT — asagidaki bayrak "w > 0" ile YAZILAMAZ: kanitlarin bir
        // kismi bu noktadan SONRA toplaniyor, o yuzden sira bagimliligi
        // olusur ve dump uretmeden kapanan makinede WHEA sessizce duserdi.
        const bool otherMemoryEvidence =
            (safe > 0 && sys.ramConfiguredMTs > safe) ||
            sys.ramModulesMismatched ||
            sys.wheaFatal > 0 ||
            (sys.kernelPower41 > 0 && sys.bugcheckCount == 0) ||
            (sys.expoActive && sys.desktopStutterObserved);

        if (sys.wheaCorrected >= 100 &&
            (otherMemoryEvidence || sys.wheaCorrectedSpiking)) {
            w += sys.wheaCorrectedSpiking ? 30 : 25;
            // Iki ayri sablon: "artis var" eki cumlenin sonuna yamanmiyor,
            // cunku cevirilerde o ek cumlenin basina gecebilir.
            why.push_back(sys.wheaCorrectedSpiking
                ? EvidencePart{"{1} adet duzeltilmis donanim hatasi (WHEA) ve "
                               "son 24 saatte belirgin artis var",
                               {std::to_string(sys.wheaCorrected)}}
                : EvidencePart{"{1} adet duzeltilmis donanim hatasi (WHEA)",
                               {std::to_string(sys.wheaCorrected)}});
        }
        if (sys.wheaFatal > 0) {
            w += 35;
            why.push_back({"{1} adet OLUMCUL donanim hatasi",
                           {std::to_string(sys.wheaFatal)}});
        }
        // Dump uretmeyen sert donmalar bellek kararsizliginin klasik imzasi
        if (sys.kernelPower41 > 0 && sys.bugcheckCount == 0) {
            w += 20;
            why.push_back({"sistem {1} kez dump uretmeden kapandi",
                           {std::to_string(sys.kernelPower41)}});
        }
        if (sys.expoActive && sys.desktopStutterObserved) {
            w += 15;
            why.push_back({"EXPO acik ve masaustunde de takilma var", {}});
        }
        if (w > 0) {
            acc.score[Cause::MEMORY_EXPO] += w;
            auto& dst = acc.parts[Cause::MEMORY_EXPO];
            dst.insert(dst.end(), why.begin(), why.end());
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 3 — Undervolt / Curve Optimizer
    // Ayirt edici: sorun oyunda degil, SISTEM genelinde.
    // ---------------------------------------------------------------------
    if (sys.curveOptimizerActive) {
        int w = 25;
        acc.add(Cause::OVERCLOCK_CO, w, "Curve Optimizer / undervolt etkin");
        if (sys.desktopStutterObserved) {
            acc.add(Cause::OVERCLOCK_CO, 30,
                    "takilmalar oyun disinda da (masaustu, tarayici) goruluyor");
        }
        if (sys.wheaCorrected >= 50) {
            acc.add(Cause::OVERCLOCK_CO, 20,
                    "duzeltilmis donanim hatalari mevcut");
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 4 — VRAM
    // ---------------------------------------------------------------------
    {
        double f = fractionOf(events, [](const StutterEvent& e) {
            return e.signals.vramOverBudget;
        });
        if (f >= 0.30)
            acc.add(Cause::VRAM, static_cast<int>(f * 80),
                    EvidencePart{"takilmalarin {1}'inde VRAM butcesi asilmis",
                                 {pct(f)}});
    }

    // ---------------------------------------------------------------------
    // KURAL 5 — Shader derlemesi (normal davranis)
    // Ilk dakikalarda yogun, sonra kayboluyor.
    // ---------------------------------------------------------------------
    //  Uc kosul birden gerekir:
    //    1) Oyun baslangic zamani BILINIYOR olmali (aksi halde cikarim yapilamaz)
    //    2) Oturum 10 dakikadan uzun surmus olmali — yoksa "ilk 10 dakikada
    //       yogunlasti" demek anlamsizdir, cunku her sey ilk 10 dakikadadir
    //    3) Olaylarin buyuk cogunlugu ilk 10 dakikada olmali
    if (events.size() >= 5) {
        bool     allKnown = true;
        uint32_t maxMinute = 0;
        for (const auto& e : events) {
            if (!e.signals.hasGameTime()) { allKnown = false; break; }
            maxMinute = std::max(maxMinute, e.signals.minutesSinceGameStart);
        }

        if (allKnown && maxMinute > 10) {
            double early = fractionOf(events, [](const StutterEvent& e) {
                return e.signals.minutesSinceGameStart <= 10;
            });
            if (early >= 0.80)
                acc.add(Cause::SHADER_COMPILE, 55,
                        EvidencePart{"takilmalarin {1}'i oyunun ilk 10 "
                                     "dakikasinda, sonrasinda kayboluyor",
                                     {pct(early)}});
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 6 — VRR / frame pacing
    // ---------------------------------------------------------------------
    if (stats.periodicMicroStutter) {
        acc.add(Cause::VRR_PACING, 55,
                EvidencePart{"mikro-takilmalar duzenli araliklarla tekrarliyor "
                             "({1} ms)",
                             {std::to_string(
                                 static_cast<int>(stats.microStutterPeriodMs))}});
        if (sys.monitorRecentlyChanged)
            acc.add(Cause::VRR_PACING, 25,
                    "monitor yakin zamanda degistirilmis");
    }

    // ---------------------------------------------------------------------
    // KURAL 7 — Hard fault / RAM yetersiz
    // ---------------------------------------------------------------------
    {
        double f = fractionOf(events, [](const StutterEvent& e) {
            return e.signals.hardFaults > 100;
        });
        if (f >= 0.30)
            acc.add(Cause::PAGEFILE_RAM, static_cast<int>(f * 70),
                    EvidencePart{"takilmalarin {1}'inde yogun sayfalama var",
                                 {pct(f)}});
    }

    // ---------------------------------------------------------------------
    // KURAL 8 — Depolama
    // ---------------------------------------------------------------------
    {
        double f = fractionOf(events, [](const StutterEvent& e) {
            return e.signals.diskWaitMs > 100.0;
        });
        if (f >= 0.30)
            acc.add(Cause::STORAGE, static_cast<int>(f * 70),
                    EvidencePart{"takilmalarin {1}'inde disk beklemesi "
                                 "100 ms ustu", {pct(f)}});
    }

    // ---------------------------------------------------------------------
    // KURAL 9 — Termal
    // ---------------------------------------------------------------------
    {
        double f = fractionOf(events, [](const StutterEvent& e) {
            return e.signals.gpuThermalThrottle || e.signals.cpuPerfPercent < 80.0;
        });
        if (f >= 0.30)
            acc.add(Cause::THERMAL, static_cast<int>(f * 75),
                    EvidencePart{"takilmalarin {1}'inde isi sebebiyle hiz "
                                 "dusurulmus", {pct(f)}});
    }

    // ---------------------------------------------------------------------
    // KURAL 10 — Guc kaynagi
    // Power Brake = PSU'nun GPU'ya "yavasla" demesi. En yakin dogrudan kanit.
    // ---------------------------------------------------------------------
    {
        double f = fractionOf(events, [](const StutterEvent& e) {
            return e.signals.gpuPowerBrake;
        });
        if (f >= 0.15) {
            acc.add(Cause::PSU, static_cast<int>(f * 90),
                    EvidencePart{"takilmalarin {1}'inde GPU harici guc freni "
                                 "(Power Brake) tetiklenmis", {pct(f)}});
        }
        // Dump uretmeyen ani kapanma + zayif PSU
        if (sys.kernelPower41 > 0 && sys.bugcheckCount == 0) {
            int add = 25;
            if (sys.psuQualityTier == 1) add += 20;
            acc.add(Cause::PSU, add,
                    EvidencePart{"sistem {1} kez dump uretmeden kapandi",
                                 {std::to_string(sys.kernelPower41)}});
        }
        if (sys.psuWatts > 0 && sys.estimatedPeakWatts > 0 &&
            sys.estimatedPeakWatts > sys.psuWatts * 85 / 100) {
            acc.add(Cause::PSU, 20,
                    EvidencePart{"tahmini tepe cekis ({1}W) guc kaynagi "
                                 "kapasitesine ({2}W) cok yakin",
                                 {std::to_string(sys.estimatedPeakWatts),
                                  std::to_string(sys.psuWatts)}});
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 11 — Arka plan programi
    // ---------------------------------------------------------------------
    if (!events.empty()) {
        std::map<std::string, size_t> procHits;
        for (const auto& e : events)
            if (e.signals.backgroundCpuPercent > 25.0 &&
                !e.signals.topBackgroundProcess.empty())
                ++procHits[e.signals.topBackgroundProcess];

        if (!procHits.empty()) {
            auto best = std::max_element(
                procHits.begin(), procHits.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            double frac = static_cast<double>(best->second) / events.size();
            if (frac >= 0.30)
                acc.add(Cause::BACKGROUND_APP, static_cast<int>(frac * 65),
                        EvidencePart{"{1} takilmalarin {2}'i ile cakisiyor",
                                     {best->first, pct(frac)}});
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 12 — Guc plani / cekirdek park
    // ---------------------------------------------------------------------
    {
        double f = fractionOf(events, [](const StutterEvent& e) {
            return e.signals.coreParkedRatio > 0.4 && !e.signals.gpuThermalThrottle;
        });
        if (f >= 0.35)
            acc.add(Cause::POWER_PLAN, static_cast<int>(f * 60),
                    EvidencePart{"takilmalarin {1}'inde cekirdeklerin buyuk "
                                 "kismi park halinde", {pct(f)}});
    }

    // ---------------------------------------------------------------------
    // KURAL 13 — Yakin zamandaki degisiklik
    // ---------------------------------------------------------------------
    if (sys.driverRecentlyUpdated && !sys.recentChangeNote.empty())
        acc.add(Cause::RECENT_CHANGE, 30, sys.recentChangeNote);

    // ---------------------------------------------------------------------
    // KURAL 14 — GPU surucusu / TDR
    // ---------------------------------------------------------------------
    if (sys.liveKernelReports > 0) {
        int w = 30 + static_cast<int>(std::min<uint32_t>(sys.liveKernelReports, 5)) * 8;
        acc.add(Cause::GPU_DRIVER, w,
                EvidencePart{"{1} adet mavi ekransiz GPU cokmesi kaydi bulundu",
                             {std::to_string(sys.liveKernelReports)}});
        if (sys.gpuOverclocked)
            acc.add(Cause::OVERCLOCK_CO, 20, "ekran karti hiz asirtmali");
    }

    // ---------------------------------------------------------------------
    // KURAL 14b — TDR: ekran surucusu yanit vermeyip sifirlandi (olay 4101)
    // ---------------------------------------------------------------------
    //  Kullanicinin "ekran bir saniye dondu, sonra kendine geldi" tarifinin
    //  makinedeki tam karsiligi. Ama zamanlama her seydir:
    //
    //    olcum SIRASINDA gorulduyse -> takilmanin dogrudan sebebi
    //    yalnizca gecmiste duruyorsa -> baglam, sebep degil
    //
    //  Bu ayrim olmadan iki ay once bir kez sifirlanmis saglikli bir makine
    //  bugunku takilmasi icin ekran surucusunu suclar.
    // ---------------------------------------------------------------------
    if (sys.tdrCount > 0) {
        int w = 0;
        EvidencePart p;

        // Surucu adi VARSA ayri bir sablon kullaniliyor; cumlenin sonuna
        // yamanmiyor. Cevirilerde o ek cumlenin ortasina ya da basina
        // gecebilir ve yamama bunu imkansiz kilardi.
        const bool named = !sys.tdrDriver.empty();

        if (sys.tdrDuringCapture > 0) {
            w = 70;
            p = named
                ? EvidencePart{"olcum sirasinda ekran surucusu {1} kez yanit "
                               "vermeyip sifirlandi (TDR), surucu: {2}",
                               {std::to_string(sys.tdrDuringCapture),
                                sys.tdrDriver}}
                : EvidencePart{"olcum sirasinda ekran surucusu {1} kez yanit "
                               "vermeyip sifirlandi (TDR)",
                               {std::to_string(sys.tdrDuringCapture)}};
        } else {
            w = 20 + static_cast<int>(std::min<uint32_t>(sys.tdrCount, 5)) * 5;
            p = named
                ? EvidencePart{"son 30 gunde {1} kez ekran surucusu sifirlandi "
                               "(TDR) — olcum sirasinda tekrarlamadi, "
                               "surucu: {2}",
                               {std::to_string(sys.tdrCount), sys.tdrDriver}}
                : EvidencePart{"son 30 gunde {1} kez ekran surucusu sifirlandi "
                               "(TDR) — olcum sirasinda tekrarlamadi",
                               {std::to_string(sys.tdrCount)}};
        }
        acc.add(Cause::GPU_DRIVER, w, std::move(p));
    }

    // ---------------------------------------------------------------------
    // KURAL 14c — Depolama aygitinin KENDI bildirdigi hatalar
    // ---------------------------------------------------------------------
    //  PDH gecikme sayaclari diskin yavas oldugunu soyler; bu olaylar
    //  diskin HATA verdigini soyler. Ikincisi cok daha agirdir.
    //
    //  Yine de dikkat: bu sayimlar SSD asinmasi gibi "sagligi %56" turu
    //  cikarimlara malzeme yapilmamali. Gecmiste kalan bir sifirlama
    //  bugunku takilmanin sebebi degildir; olcumle cakisan sifirlama ise
    //  dogrudan sebeptir. Rapor bu ikisini ayri gostermek zorunda.
    // ---------------------------------------------------------------------
    if (sys.storageResetCount > 0 || sys.storageErrorCount > 0 ||
        sys.storageRetryCount >= 10) {
        int w = 0;
        std::vector<EvidencePart> why;

        if (sys.storageDuringCapture > 0) {
            w = 65;
            why.push_back({"olcum sirasinda depolama aygiti {1} kez hata bildirdi",
                           {std::to_string(sys.storageDuringCapture)}});
        } else {
            // "(olcum sirasinda tekrarlamadi)" eki her sablonun ICINDE, sonuna
            // yamanmis degil: ceviride o niteleme cumlenin basina gecebilir.
            if (sys.storageResetCount > 0) {
                w += 25 + static_cast<int>(
                         std::min<uint32_t>(sys.storageResetCount, 5)) * 5;
                why.push_back({"{1} kez depolama aygitina sifirlama gonderildi "
                               "(olcum sirasinda tekrarlamadi)",
                               {std::to_string(sys.storageResetCount)}});
            }
            if (sys.storageErrorCount > 0) {
                w += 20;
                why.push_back({"{1} adet disk hatasi kaydi "
                               "(olcum sirasinda tekrarlamadi)",
                               {std::to_string(sys.storageErrorCount)}});
            }
            if (sys.storageRetryCount >= 10) {
                w += 10;
                why.push_back({"{1} kez G/C yeniden denendi "
                               "(olcum sirasinda tekrarlamadi)",
                               {std::to_string(sys.storageRetryCount)}});
            }
        }

        if (sys.ntfsCorruption > 0) {
            w += 10;
            why.push_back({"dosya sistemi bozulmasi kaydi var", {}});
        }
        if (w > 0) {
            acc.score[Cause::STORAGE] += w;
            auto& dst = acc.parts[Cause::STORAGE];
            dst.insert(dst.end(), why.begin(), why.end());
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 15 — Kullanim profili: darbogaz nerede?
    // ----------------------------------------------------------------------
    //  Forum sikayetlerinin en sik deseni: "ekran karti %70'te kaliyor,
    //  islemci %99". Kullanici bunu gorup ekran kartini sucluyor; oysa
    //  ekran karti bosta bekliyor demektir. Bu kural o okumayi tersine
    //  cevirir.
    // ---------------------------------------------------------------------
    if (SystemInfo::pctKnown(sys.medianGpuUtilPct)) {
        const int  gpu      = static_cast<int>(sys.medianGpuUtilPct + 0.5);
        // Doygunluk esigi %90. GPU-bound bir oyun %97-99'da oturur; %70-80
        // "yuksek" gibi gorunse de ekran kartinin bekledigi anlamina gelir.
        // Ilk surumde esik %70'ti ve tam da tarif edilen forum vakasini
        // (GPU %72, CPU %95) kaciriyordu.
        const bool gpuIdle  = sys.medianGpuUtilPct < kGpuSaturatedPct;
        const bool cpuKnown = SystemInfo::pctKnown(sys.medianCpuUsagePct);
        const int  cpu      = cpuKnown
                            ? static_cast<int>(sys.medianCpuUsagePct + 0.5) : 0;

        if (gpuIdle && cpuKnown && sys.medianCpuUsagePct >= kCpuBusyPct) {
            // Iki kademe. Zayif bir islemcinin guclu bir karti %100
            // besleyememesi ARIZA DEGILDIR, sistemin dogal dengesidir; %80-90
            // araligi bu yuzden yalnizca bilgi notu agirligindadir. %80'in
            // altina dusmek ise gercekten dikkat isteyen bir tablodur.
            const bool severe = sys.medianGpuUtilPct < 80.0;
            acc.add(Cause::CPU_BOTTLENECK, severe ? 70 : 30,
                    severe
                    ? EvidencePart{"ekran karti %{1} kullanimda kalirken islemci "
                                   "%{2}'e dayaniyor — darbogaz islemcide",
                                   {std::to_string(gpu), std::to_string(cpu)}}
                    : EvidencePart{"ekran karti %{1} kullanimda kalirken islemci "
                                   "%{2}'e dayaniyor — darbogaz islemcide "
                                   "(bu aralik islemci sinifina gore normal "
                                   "olabilir, ariza belirtisi degildir)",
                                   {std::to_string(gpu), std::to_string(cpu)}});
        } else if (gpuIdle && cpuKnown && sys.medianCpuUsagePct < 70.0 &&
                   !stats.periodicMicroStutter) {
            acc.add(Cause::GPU_UNDERUSED, 55,
                    EvidencePart{"ne ekran karti (%{1}) ne islemci (%{2}) "
                                 "doluyor; bir yerde tavan var",
                                 {std::to_string(gpu), std::to_string(cpu)}});
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 15b — Bellek baskisi / sayfalama
    // ----------------------------------------------------------------------
    //  Gercek vaka: RTX 5070 + Ryzen 5 7600 + 16 GB. Oyun 17 GB taahhut
    //  ediyor, islemci %99'a dayaniyor, ekran karti %70'te bekliyor.
    //  Kullanici ekran kartini sucluyor; sebep RAM.
    //
    //  Neden dwMemoryLoad yetmez: sistem "dolu %85" gorunurken de sayfa
    //  dosyasina yazmaya baslamis olabilir. Belirleyici olcu TAAHHUTtur
    //  (commit charge) — istenmis bellegin toplami.
    // ---------------------------------------------------------------------
    {
        const bool commitKnown = SystemInfo::pctKnown(sys.maxCommitUsedPct);
        const bool availKnown  = SystemInfo::pctKnown(sys.minAvailPhysMb);

        int weight = 0;
        std::vector<EvidencePart> why;

        if (sys.commitExceededRam) {
            weight = 70;
            why.push_back(sys.ramTotalMb > 0
                ? EvidencePart{"sistem takili RAM'den fazlasini taahhut etti "
                               "({1} GB kurulu) — fark sayfa dosyasina, yani "
                               "diske gidiyor",
                               {std::to_string(sys.ramTotalMb / 1024)}}
                : EvidencePart{"sistem takili RAM'den fazlasini taahhut etti — "
                               "fark sayfa dosyasina, yani diske gidiyor", {}});
        } else if (commitKnown && sys.maxCommitUsedPct >= 90.0) {
            weight = 55;
            why.push_back({"taahhut edilen bellek sinirin %{1}'ine ulasti",
                           {std::to_string(
                               static_cast<int>(sys.maxCommitUsedPct + 0.5))}});
        } else if (availKnown && sys.minAvailPhysMb < 600.0) {
            weight = 45;
            why.push_back({"bos fiziksel bellek {1} MB'a kadar dustu",
                           {std::to_string(
                               static_cast<int>(sys.minAvailPhysMb))}});
        }

        if (weight > 0) {
            // Ekran karti da bosta bekliyorsa tablo tamamlanir: darbogaz
            // bellekte, ve bu CPU_BOTTLENECK'ten daha derin bir sebeptir.
            if (SystemInfo::pctKnown(sys.medianGpuUtilPct) &&
                sys.medianGpuUtilPct < kGpuSaturatedPct) {
                weight += 15;
                why.push_back({"ekran karti ayni anda %{1} kullanimda bekliyor",
                               {std::to_string(static_cast<int>(
                                   sys.medianGpuUtilPct + 0.5))}});
            }
            acc.score[Cause::PAGEFILE_RAM] += weight;
            auto& dst = acc.parts[Cause::PAGEFILE_RAM];
            dst.insert(dst.end(), why.begin(), why.end());
        }
    }

    // ---------------------------------------------------------------------
    // KURAL 16 — Depolama
    // ---------------------------------------------------------------------
    if (SystemInfo::pctKnown(sys.medianDiskActivePct) &&
        sys.medianDiskActivePct >= 90.0) {
        acc.add(Cause::STORAGE, 60,
                EvidencePart{"disk olcum boyunca %{1} aktif kaldi — surekli "
                             "doygun",
                             {std::to_string(static_cast<int>(
                                 sys.medianDiskActivePct + 0.5))}});
    }
    if (sys.p95DiskLatencyMs >= 20.0) {
        acc.add(Cause::STORAGE, 50,
                EvidencePart{"disk islemlerinin %5'i {1} ms'den uzun surdu",
                             {std::to_string(static_cast<int>(
                                 sys.p95DiskLatencyMs + 0.5))}});
    }

    // ---------------------------------------------------------------------
    // KURAL 17 — Ekran kartinin KENDI bildirdigi kisitlamalar
    // ----------------------------------------------------------------------
    //  Bunlar sicaklik OKUMASI degil, donanimin "su an kisitliyorum" diye
    //  bildirdigi bayraklardir. Tasarim kurali 1 sicaklik OLCUSUNE dayali
    //  kural yasakliyor; olculmus kisitlama olgusu bunun disindadir.
    // ---------------------------------------------------------------------
    if (sys.gpuPowerBrakeSeen) {
        acc.add(Cause::PSU, 55,
                "ekran karti donanimsal frenleme (power brake) bildirdi — bu "
                "sinyali guc kaynagi tetikler");
    }
    if (sys.gpuThermalThrottleSeen) {
        acc.add(Cause::THERMAL, 45,
                "ekran karti isinma sebebiyle hiz dusurdugunu bildirdi");
    }
    if (sys.gpuPowerCapSeen && !sys.gpuPowerBrakeSeen) {
        acc.add(Cause::POWER_PLAN, 25,
                "ekran karti guc limitine takildigini bildirdi");
    }

    // ---------------------------------------------------------------------
    //  Normalizasyon ve siralama
    // ---------------------------------------------------------------------
    int total = 0;
    for (const auto& kv : acc.score) total += kv.second;

    // ---------------------------------------------------------------------
    //  Olcum yeterli mi?
    // ----------------------------------------------------------------------
    //  Bu kontrol normalizasyondan ONCE gelir: kisa bir kayitta hipotez
    //  uretmek, sonra "ama guven dusuk" demekten daha dogru degildir.
    //  Kullanici listeyi okur, uyariyi okumaz.
    // ---------------------------------------------------------------------
    if (stats.frameCount > 0 &&
        (stats.durationSec < kMinSessionSec ||
         stats.frameCount  < kMinSessionFrames)) {
        d.inconclusive = true;
        d.confidence   = 0;
        d.headline     = "Ölçüm hüküm vermek için çok kısa (" +
                         std::to_string(static_cast<int>(stats.durationSec)) +
                         " sn, " + std::to_string(stats.frameCount) +
                         " kare). En az " +
                         std::to_string(static_cast<int>(kMinSessionSec)) +
                         " saniye oyun içinde kayıt alın.";
        return d;
    }

    if (total <= 0) {
        d.inconclusive = true;
        d.confidence   = 0;
        d.headline     = "Belirgin bir sebep bulunamadı. Daha uzun bir oturum kaydedin.";
        return d;
    }

    for (const auto& kv : acc.score) {
        Hypothesis h;
        h.cause    = kv.first;
        h.percent  = static_cast<int>((kv.second * 100.0) / total + 0.5);
        h.label    = causeLabel(kv.first);
        h.action   = causeAction(kv.first);
        auto it = acc.parts.find(kv.first);
        if (it != acc.parts.end()) {
            h.evidenceParts = it->second;
            h.evidence      = joinParts(it->second);
        }
        if (h.percent > 0) d.ranked.push_back(h);
    }

    std::sort(d.ranked.begin(), d.ranked.end(),
              [](const Hypothesis& a, const Hypothesis& b) {
                  return a.percent > b.percent;
              });

    int sum = 0;
    for (const auto& h : d.ranked) sum += h.percent;
    if (!d.ranked.empty() && sum != 100) d.ranked.front().percent += (100 - sum);

    // ---------------------------------------------------------------------
    //  Tasarim kurali 4: asla "kesin sebep bu" deme.
    // ---------------------------------------------------------------------
    //  Tek kural atesledigi zaman normalizasyon o hipoteze %100 veriyordu.
    //  Bu, kullanicinin calisan bir surucuyu kaldirmasina ya da gereksiz
    //  donanim almasina yol acabilecek bir kesinlik iddiasidir.
    //
    //  Fark UNKNOWN_OTHER'a aktarilir. Bu uydurma bir alternatif DEGILDIR:
    //  CSV modunda DPC, VRAM ve disk sinyalleri gercekten olculmemistir, yani
    //  motorun gormedigi bir alan fiilen vardir.
    int residual = 0;
    if (!d.ranked.empty() && d.ranked.front().percent > kMaxSingleCausePercent) {
        residual = d.ranked.front().percent - kMaxSingleCausePercent;
        d.ranked.front().percent = kMaxSingleCausePercent;

        Hypothesis u;
        u.cause    = Cause::UNKNOWN_OTHER;
        u.percent  = residual;
        u.label    = causeLabel(Cause::UNKNOWN_OTHER);
        u.action   = causeAction(Cause::UNKNOWN_OTHER);
        u.evidence = "tek bir kural atesledi; bu oturumda DPC, VRAM ve disk "
                     "sinyalleri olculmedi";
        d.ranked.push_back(u);

        std::sort(d.ranked.begin(), d.ranked.end(),
                  [](const Hypothesis& a, const Hypothesis& b) {
                      return a.percent > b.percent;
                  });
    }

    // Guven: birinci ile ikinci arasindaki acikliga bagli
    if (!d.ranked.empty()) {
        int top    = d.ranked.front().percent;
        int second = (d.ranked.size() > 1) ? d.ranked[1].percent : 0;
        d.confidence = std::min(95, top + (top - second) / 2);

        // Olculmemis pay ne kadar buyukse guven o kadar dusuk olmali.
        // %20'lik bir bilinmezle %95 guven iddia etmek kendi icinde celiskili.
        if (residual > 0) d.confidence = std::min(d.confidence, 100 - residual);

        // Az veri = dusuk guven
        if (events.size() < 5 && !stats.lowFpsNotStutter)
            d.confidence = std::min(d.confidence, 45);

        if (d.confidence < 35) d.inconclusive = true;

        if (d.headline.empty()) {
            d.headline = std::string("Ana şüphe: ") + d.ranked.front().label
                       + " (%" + std::to_string(d.ranked.front().percent) + ")";
        }
    }

    return d;
}

} // namespace ss
