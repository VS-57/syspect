// ============================================================================
//  Syspect — bulgular
// ============================================================================
#ifdef _WIN32

#include "findings.h"
#include "i18n.h"

#include <algorithm>
#include <cstdio>

namespace ssfind {
namespace {

// Bulgu metinleri de tek noktadan geciyor: add() cagrisi. Cizim katmanindaki
// text() ile ayni gerekce — sarmalamayi unutmak mumkun olmasin.
using ss18::T;

std::string n0(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.0f", v);
    return b;
}

std::string n1(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

// Bir alanin oturum boyunca en yuksek / medyan degeri
bool statOf(const std::vector<sstelem::Sample>& v,
            double sstelem::Sample::* field,
            double& median, double& max) {
    std::vector<double> xs;
    for (const auto& s : v) if (sstelem::known(s.*field)) xs.push_back(s.*field);
    if (xs.empty()) return false;
    std::sort(xs.begin(), xs.end());
    median = xs[xs.size() / 2];
    max    = xs.back();
    return true;
}

void add(std::vector<Finding>& out, Severity s, std::string title,
         std::string detail, std::string action = {}) {
    // Ceviri burada. Turkce modda T() kaynagi aynen dondurur; calisma aninda
    // sayi ile kurulan metinler tabloda bulunmaz ve Turkce kalir.
    ss18::noteSource(title);
    ss18::noteSource(detail);
    ss18::noteSource(action);
    out.push_back({s, T(title), T(detail), T(action)});
}

} // namespace

// ============================================================================
std::vector<Finding> collect(const Input& in) {
    std::vector<Finding> f;

    // Dosyadan yuklenen kayitta liste kisalir; sebebini SOYLEMEK zorundayiz.
    // Aksi halde kullanici "guc planim iyi demek ki" diye okur — oysa hic
    // bakilmadi. Bkz. tasarim kurali 3: bilmedigimizi bilmedigimizi soyle.
    if (in.foreignCapture)
        add(f, Severity::Unknown, "Bu kayıt başka bir bilgisayardan yüklendi",
            "Güç planı, aygıt durumu, bellek yapılandırması ve mavi ekran "
            "kayıtları sizin makinenize aittir; bu kaydın sahibine değil. "
            "Yanıltmamak için gösterilmiyorlar.",
            "Kendi sisteminizi görmek için yeni bir ölçüm alın.");

    // ------------------------------------------------------------------
    //  1. Guc modu
    // ------------------------------------------------------------------
    if (in.power && !in.power->friendlyName.empty()) {
        if (in.power->onBattery) {
            add(f, Severity::Bad, "Bilgisayar pilden çalışıyor",
                "Pil modunda işlemci ve ekran kartı güç limitleri ciddi "
                "biçimde düşürülür. Bu tek başına yarı yarıya performans "
                "kaybı demek olabilir.",
                "Şarj adaptörünü takıp ölçümü tekrarlayın.");
        } else if (in.power->plan == ssprobe::PowerPlan::PowerSaver) {
            add(f, Severity::Bad, "Güç modu \"Güç tasarrufu\"",
                "Bu plan işlemci frekansını bilerek düşük tutar ve "
                "çekirdekleri park eder.",
                "Güç Seçenekleri'nden \"Yüksek performans\" planına geçin.");
        } else if (in.power->plan == ssprobe::PowerPlan::Balanced) {
            add(f, Severity::Warn, "Güç modu \"Dengeli\"",
                "Windows varsayılanı. Çoğu makinede sorun çıkarmaz ama "
                "çekirdek park etme ve frekans dalgalanması açıktır.",
                "\"Yüksek performans\" planına geçip bir gün kullanın; "
                "fark görmezseniz geri alın.");
        } else if (in.power->plan == ssprobe::PowerPlan::Custom) {
            add(f, Severity::Warn,
                "Üreticiye özel güç planı: " + in.power->friendlyName,
                "Bu planlar genelde sessizlik veya pil ömrü için "
                "performansı kısar.",
                "Windows'un \"Yüksek performans\" planına geçip farkı ölçün.");
        } else {
            add(f, Severity::Good,
                "Güç modu \"" + in.power->friendlyName + "\"", "Sorun yok.");
        }
    }

    // ------------------------------------------------------------------
    //  2. Bellek profili (EXPO / XMP) ve kanal sayisi
    // ------------------------------------------------------------------
    if (in.memory && !in.memory->modules.empty()) {
        const auto& m = *in.memory;

        // Karisik takim once gelir: profil hukmunden ONCE okunmasi gerekiyor,
        // cunku o hukum bu durumda zaten "belirlenemedi"ye duser ve kullanici
        // sebebini bilmeden okur.
        if (m.mixedModules) {
            add(f, Severity::Bad, "Bellek modülleri birbirinin aynısı değil",
                m.mixedModulesNote,
                "Tek takım halinde satılmış, aynı modellerden oluşan bir kit "
                "kullanın. Elinizde varsa yalnızca aynı olan iki modülle test "
                "edin.");
        }

        if (m.profile == ssprobe::MemorySpec::Profile::Off) {
            add(f, Severity::Bad,
                m.profileLabel + " kapalı — RAM tam hızında çalışmıyor",
                "Modüller " + std::to_string(m.maxSpeedMTs) +
                " MT/s destekliyor ama " + std::to_string(m.configuredMTs) +
                " MT/s'de çalışıyor. Bu fark oyunlarda doğrudan FPS kaybı "
                "üretir." + ssprobe::memorySpeedImpactNote(m.cpuVendor),
                "BIOS'a girip " + m.profileLabel + " profilini açın.");
        } else if (m.profile == ssprobe::MemorySpec::Profile::On) {
            add(f, Severity::Good,
                m.profileLabel + " açık — RAM " +
                std::to_string(m.configuredMTs) + " MT/s",
                "Modüller destekledikleri hızda çalışıyor.");
        } else {
            add(f, Severity::Unknown, "Bellek profili belirlenemedi",
                "Bellek hızı SMBIOS'tan okunamadı.");
        }

        if (m.singleChannelRisk) {
            add(f, Severity::Bad, "Tek bellek modülü takılı (tek kanal)",
                "Tek kanal bellek oyunlarda ciddi FPS kaybına yol açar" +
                std::string(ssprobe::singleChannelImpactNote(m.cpuVendor)) + ".",
                "İkinci bir modül eklemek en ucuz iyileştirmedir.");
        }
    }

    // ------------------------------------------------------------------
    //  2b. Windows guvenlik ayarlari — performansa dokunanlar
    // ------------------------------------------------------------------
    if (in.firmware) {
        using Tri = ssprobe::FirmwareInfo::Tri;
        const auto& fw = *in.firmware;

        // Bellek Butunlugu (HVCI) — kullanicilarin cogu varligindan habersiz
        if (fw.memoryIntegrity == Tri::On) {
            add(f, Severity::Warn,
                "Bellek Bütünlüğü (izole çekirdek) açık",
                "Sanallaştırma tabanlı kod doğrulama. Windows 11'de çoğu "
                "makinede varsayılan açık gelir ve oyunlarda tipik %5-15 "
                "performans kaybı üretir.",
                "Windows Güvenliği > Cihaz güvenliği > Çekirdek yalıtımı'ndan "
                "kapatıp farkı ölçün. Güvenlik özelliği olduğunu unutmayın.");
        } else if (fw.memoryIntegrity == Tri::Off) {
            add(f, Severity::Good, "Bellek Bütünlüğü kapalı",
                "Oyun performansını kısan bu ayar devre dışı.");
        }

        if (fw.gpuScheduling == Tri::Off) {
            add(f, Severity::Warn,
                "Donanım hızlandırmalı GPU zamanlama kapalı",
                "Bazı sistemlerde açık olması kare zamanlamasını düzeltir, "
                "bazılarında bozar — tek doğrusu yok ama denenmeye değer.",
                "Ayarlar > Sistem > Ekran > Grafikler'den açıp farkı ölçün.");
        }

        // Secure Boot performansi ETKILEMEZ; burada olmasinin sebebi bazi
        // oyunlarin hile korumasinin (Valorant, Fortnite) acik olmasini sart
        // kosmasi ve vaka paylasirken ilk sorulan seylerden olmasi.
        if (fw.secureBoot == Tri::Off) {
            add(f, Severity::Warn, "Secure Boot kapalı",
                "Performansı doğrudan etkilemez ama bazı oyunların hile "
                "koruması açık olmasını şart koşar (Valorant, Fortnite).",
                "Oyun açılmıyorsa BIOS'ta Secure Boot'u açın.");
        } else if (fw.secureBoot == Tri::On) {
            add(f, Severity::Good, "Secure Boot açık", "Sorun yok.");
        }
    }

    // ------------------------------------------------------------------
    //  3. Resizable BAR
    // ------------------------------------------------------------------
    if (in.gpu && in.gpu->known) {
        if (in.gpu->resizableBar == sstelem::GpuStatic::Tri::No) {
            add(f, Severity::Warn, "Resizable BAR kapalı",
                "Açıldığında bazı oyunlarda %5-10 kazanç sağlar.",
                "BIOS'ta \"Above 4G Decoding\" ve \"Re-Size BAR Support\" "
                "seçeneklerini açın.");
        } else if (in.gpu->resizableBar == sstelem::GpuStatic::Tri::Yes) {
            add(f, Severity::Good, "Resizable BAR açık", "Sorun yok.");
        }
    }

    // ------------------------------------------------------------------
    //  3b. Ekran kartinin guc limiti — "140 W kart neden 40 W cekiyor?"
    // ------------------------------------------------------------------
    //  Dizustu kullanicilarinin en sik ve en cevapsiz sorusu. Ayni model kart
    //  bir dizustude 140 W, otekinde 60 W calisir; fark ureticinin sectigi
    //  TGP'dir ve hicbir yerde yazmaz. NVML uygulanan limiti VE fabrika
    //  limitini ayri ayri verdigi icin bu soru artik cevaplanabiliyor.
    //
    //  Masaustunde ayni tablo baska bir sey anlatir: orada limiti kullanici
    //  dusurmustur. Ayni sayi, iki farkli tavsiye — bu yuzden pil varligina
    //  bakiliyor.
    if (in.gpu && in.gpu->known && sstelem::known(in.gpu->powerLimitW)) {
        const double lim = in.gpu->powerLimitW;
        const double def = in.gpu->defaultPowerLimitW;
        const bool laptop = in.power && in.power->hasBattery;

        // Uygulanan limit fabrika limitinin belirgin altinda mi?
        if (sstelem::known(def) && def > 0.0 && lim < def * 0.9) {
            add(f, Severity::Warn,
                "Ekran kartı " + n0(lim) + " W ile sınırlı (fabrika ayarı " +
                n0(def) + " W)",
                laptop
                    ? "Dizüstülerde üretici kartın güç bütçesini düşürebilir. "
                      "Aynı model kart başka bir dizüstüde çok daha hızlı "
                      "olabilir; fark buradan gelir."
                    : "Kartın güç limiti fabrika değerinin altına çekilmiş. "
                      "Genellikle bir hız aşırtma programının profili bunu yapar.",
                laptop
                    ? "Üreticinin uygulamasında (Armoury Crate, Legion Vantage, "
                      "MSI Center…) performans profiline geçin ve prize takılı "
                      "olduğunuzdan emin olun."
                    : "Afterburner benzeri programlarda güç limitini %100'e "
                      "geri alın.");
        } else {
            add(f, Severity::Good,
                "Ekran kartı güç limiti " + n0(lim) + " W",
                sstelem::known(def) && def > 0.0
                    ? "Fabrika ayarıyla aynı; kart kısıtlanmamış."
                    : "Kartın uyguladığı güç bütçesi.");
        }

        // Kart limitine hic yaklasmiyor mu? Bu, guc limitinin sinirlayici
        // OLMADIGINI soyler — daraltici baska yerdedir. Yalnizca kart
        // gercekten calisiyorken anlamli: %20 kullanimda az watt cekmesi
        // normaldir, bulgu degildir.
        if (in.telemetry && !in.telemetry->empty() && lim > 0.0) {
            double pMed = 0, pMax = 0, uMed = 0, uMax = 0;
            const bool haveP = statOf(*in.telemetry,
                                      &sstelem::Sample::gpuPowerW, pMed, pMax);
            const bool haveU = statOf(*in.telemetry,
                                      &sstelem::Sample::gpuUtilPct, uMed, uMax);
            if (haveP && haveU && uMed >= 80.0 && pMax < lim * 0.6) {
                add(f, Severity::Warn,
                    "Ekran kartı yüklüyken bile gücünü kullanmıyor",
                    "Kart %" + n0(uMed) + " kullanımdayken en fazla " +
                    n0(pMax) + " W çekti; limiti " + n0(lim) + " W. "
                    "Güç limiti sınırlayıcı değil — daraltan başka bir şey var.",
                    "Kare hızı sınırı, dikey eşitleme ve sürücüdeki güç "
                    "yönetimi ayarını kontrol edin.");
            }
        }
    }

    // ------------------------------------------------------------------
    //  4. Isinma ve guc kisitlamalari — kartin KENDI bildirimi
    // ------------------------------------------------------------------
    if (in.telemetry && !in.telemetry->empty()) {
        bool thermal = false, cap = false, brake = false;
        for (const auto& s : *in.telemetry) {
            thermal |= s.thermalThrottle;
            cap     |= s.powerCapThrottle;
            brake   |= s.powerBrake;
        }
        double med = 0, mx = 0;
        const bool haveTemp = statOf(*in.telemetry,
                                     &sstelem::Sample::gpuTempC, med, mx);

        if (thermal) {
            add(f, Severity::Bad, "Ekran kartı ısınma yüzünden hız düşürüyor",
                haveTemp ? ("Kart en yüksek " + n0(mx) +
                            " °C gördü ve kısıtlamaya girdiğini bildirdi.")
                         : "Kart ısınma kısıtlamasına girdiğini bildirdi.",
                "Kasa içi hava akışını ve tozu kontrol edin; dizüstüyse "
                "havalandırmayı kapatmayın.");
        } else if (haveTemp && mx >= 85.0) {
            // Kart henuz kisitlamaya girmemis olabilir ama bu sicaklikta
            // kisitlama esiginin hemen altindadir; yaz aylarinda ya da tozlu
            // bir kasada gecer. Kullanicinin BILMESI gereken bir sey.
            add(f, Severity::Warn,
                "Ekran kartı sıcak — en yüksek " + n0(mx) + " °C",
                "Kart henüz hız düşürdüğünü bildirmedi ama kısıtlama eşiğine "
                "yakın. Kasa ısındıkça ya da toz biriktikçe bu sınır aşılır.",
                "Kasa içi hava akışını ve toz durumunu kontrol edin.");
        } else if (haveTemp) {
            add(f, Severity::Good,
                "Ekran kartı sıcaklığı en yüksek " + n0(mx) + " °C",
                mx >= 78.0
                    ? "Yüksek ama normal aralıkta; ısınma kısıtlaması "
                      "bildirilmedi."
                    : "Isınma kısıtlaması bildirilmedi.");
        }

        // Islemci sicakligi — OKUNABILDIYSE. Cogu masaustunde bu sensor yok;
        // olan makinelerde de verdigi sey cekirdek sicakligi (Tctl/Tdie) DEGIL,
        // ACPI termal bolgesidir. Sayiyi gostermek dogru, ama ne oldugunu
        // sylemeden gostermek kullaniciyi yanilgiya goturur.
        double cpuMed = 0, cpuMax = 0;
        if (statOf(*in.telemetry, &sstelem::Sample::cpuTempC, cpuMed, cpuMax)) {
            const bool hot = cpuMax >= 90.0;
            add(f, hot ? Severity::Warn : Severity::Good,
                "Sistem sıcaklığı en yüksek " + n0(cpuMax) + " °C",
                hot ? "Bu değer anakartın ACPI sensöründen geliyor; işlemci "
                      "çekirdek sıcaklığı (Tctl/Tdie) değil, genellikle ondan "
                      "düşüktür. Yani gerçek çekirdek sıcaklığı daha yüksek "
                      "olabilir."
                    : "Anakartın ACPI sensöründen okundu. İşlemci çekirdek "
                      "sıcaklığı değil, sistem sıcaklığıdır.",
                hot ? "İşlemci soğutucusunun oturduğunu ve fanının döndüğünü "
                      "kontrol edin."
                    : "");
        }

        if (brake) {
            add(f, Severity::Bad, "Donanımsal frenleme (power brake) bildirildi",
                "Bu sinyali güç kaynağı tetikler; kart istediği akımı "
                "alamıyor demektir.",
                "Başka bir güç kaynağıyla test edin, GPU kablolarını ayrı "
                "hatlardan çekin.");
        } else if (cap) {
            add(f, Severity::Warn, "Ekran kartı güç limitine takılıyor",
                "Kart daha hızlı çalışabilir ama güç sınırı izin vermiyor.",
                "Güç planını Yüksek Performans yapın; dizüstüyse üreticinin "
                "performans profiline geçin.");
        }
    }

    // ------------------------------------------------------------------
    //  5. Kullanim profili — darbogaz nerede?
    // ------------------------------------------------------------------
    if (in.sys) {
        const auto& s = *in.sys;
        if (ss::SystemInfo::pctKnown(s.medianGpuUtilPct)) {
            const int gpu = static_cast<int>(s.medianGpuUtilPct + 0.5);
            const bool cpuKnown =
                ss::SystemInfo::pctKnown(s.medianCpuUsagePct);
            const int cpu = cpuKnown
                          ? static_cast<int>(s.medianCpuUsagePct + 0.5) : 0;

            if (s.medianGpuUtilPct >= ss::kGpuSaturatedPct) {
                add(f, Severity::Good,
                    "Ekran kartı tam kullanılıyor (%" + std::to_string(gpu) + ")",
                    "Darboğaz ekran kartında; bu beklenen ve sağlıklı durum.");
            } else if (cpuKnown && s.medianCpuUsagePct >= ss::kCpuBusyPct) {
                add(f, Severity::Warn,
                    "İşlemci darboğazı — GPU %" + std::to_string(gpu) +
                    ", CPU %" + std::to_string(cpu),
                    "Ekran kartı boşta beklerken işlemci doluyor. Zayıf bir "
                    "işlemcide bu normal olabilir, arıza belirtisi değildir.",
                    "Çözünürlüğü YÜKSELTİN ve işlemciye yük bindiren ayarları "
                    "düşürün: kalabalık, görüş mesafesi, gölge detayı.");
            } else if (cpuKnown) {
                add(f, Severity::Warn,
                    "Ne ekran kartı ne işlemci doluyor (GPU %" +
                    std::to_string(gpu) + ", CPU %" + std::to_string(cpu) + ")",
                    "Bir yerde tavan var: FPS sınırı, dikey eşitleme, güç "
                    "limiti ya da oyunun kendisi.",
                    "Önce oyun içi FPS sınırını ve V-Sync'i kontrol edin.");
            }
        }

        // Bellek baskisi
        if (s.commitExceededRam) {
            add(f, Severity::Bad, "RAM yetmiyor — sistem diske taşıyor",
                std::string("Sistem takılı RAM'den fazlasını taahhüt etti") +
                (s.ramTotalMb ? (" (" + std::to_string(s.ramTotalMb / 1024) +
                                 " GB kurulu)") : "") +
                ". Fark sayfa dosyasına, yani diske gidiyor ve takılma üretir.",
                "Arka plandaki programları kapatın; kalıcı çözüm RAM "
                "yükseltmek.");
        } else if (ss::SystemInfo::pctKnown(s.maxCommitUsedPct) &&
                   s.maxCommitUsedPct >= 90.0) {
            add(f, Severity::Warn, "Bellek sınırına yaklaşıldı",
                "Taahhüt edilen bellek sınırın %" +
                n0(s.maxCommitUsedPct) + "'ine ulaştı.",
                "Arka planda çalışan programları azaltın.");
        }

        // Depolama
        if (ss::SystemInfo::pctKnown(s.medianDiskActivePct) &&
            s.medianDiskActivePct >= 90.0) {
            add(f, Severity::Bad, "Disk sürekli doygun",
                "Disk ölçüm boyunca %" + n0(s.medianDiskActivePct) +
                " aktif kaldı. Oyun veri beklerken takılır.",
                "Arka plan indirme/tarama var mı bakın; SSD'nin boş alanını "
                "kontrol edin.");
        } else if (ss::SystemInfo::pctKnown(s.p95DiskLatencyMs) &&
                   s.p95DiskLatencyMs >= 20.0) {
            add(f, Severity::Warn, "Disk yanıt süresi yüksek",
                "İşlemlerin %5'i " + n0(s.p95DiskLatencyMs) +
                " ms'den uzun sürdü.",
                "SSD firmware'ini güncelleyin, SMART değerlerine bakın.");
        }
    }

    // ------------------------------------------------------------------
    //  6. Kare olcumleri — takilma ve FPS
    // ------------------------------------------------------------------
    if (in.result && in.result->stats.frameCount > 0) {
        const auto& st = in.result->stats;

        // --- Once: bu olcum hukum verilebilir mi? ---
        const bool tooShort = st.durationSec < ss::kMinSessionSec ||
                              st.frameCount  < ss::kMinSessionFrames;

        // Kareler bir oyundan mi geldi, masaustunden mi?
        // Masaustunde DWM hicbir sey degismezken seyrek, bir animasyon
        // olunca sik kare uretir; bu dogal dalgalanma dedektorun esigini
        // asar ve takilma gibi gorunur. Gercek vaka: 6 saniyelik masaustu
        // kaydinda 7 "takilma" raporlandi — hicbiri gercek degildi.
        bool desktopSource = false;
        std::string appName;
        if (in.source && !in.source->application.empty()) {
            appName = in.source->application;
            static const char* kShell[] = {
                "dwm.exe", "explorer.exe", "ShellExperienceHost.exe",
                "chrome.exe", "msedge.exe", "firefox.exe", "opera.exe",
                "Discord.exe", "Spotify.exe", "Code.exe", "SearchHost.exe",
                "StartMenuExperienceHost.exe", "TextInputHost.exe",
            };
            for (const char* s : kShell)
                if (_stricmp(appName.c_str(), s) == 0) { desktopSource = true; break; }
        }

        if (desktopSource) {
            add(f, Severity::Unknown,
                "Ölçüm bir oyundan değil, masaüstünden alındı",
                "Kareler " + appName + " sürecinden geldi. Masaüstü düzensiz "
                "kare üretir ve bu doğal dalgalanma takılma gibi görünür; "
                "aşağıdaki kare sayıları oyun performansı hakkında bir şey "
                "söylemez.",
                "Oyunu açıp tam ekranda oynarken kayıt alın.");
        }
        if (tooShort) {
            add(f, Severity::Unknown, "Ölçüm çok kısa",
                n0(st.durationSec) + " saniye, " +
                std::to_string(st.frameCount) + " kare kaydedildi. Dedektör "
                "taban çizgisini oturtmak için daha fazlasına ihtiyaç duyar.",
                "En az " + n0(ss::kMinSessionSec) + " saniye kayıt alın.");
        }

        // Kisa ya da masaustu kaydinda takilma SAYISI raporlanmaz: sayiyi
        // gostermek, uyariyi okumayan kullanicinin onu gercek sanmasina yol
        // acar. Yalnizca FPS ozeti verilir.
        if (tooShort || desktopSource) {
            add(f, Severity::Unknown, "Takılma değerlendirmesi yapılmadı",
                "Ortalama " + n1(st.avgFps) + " FPS ölçüldü ama bu kayıt "
                "takılma hükmü vermek için uygun değil.");
        } else if (st.freezeCount > 0) {
            add(f, Severity::Bad,
                std::to_string(st.freezeCount) + " donma yaşandı",
                "Yarım saniyeden uzun süren duraklamalar ölçüldü. Bu, "
                "mikro-takılmadan farklı ve daha ciddi bir belirtidir.");
        }
        if (st.periodicMicroStutter) {
            add(f, Severity::Warn, "Düzenli mikro-takılma deseni",
                "Takılmalar rastgele değil, yaklaşık " +
                n0(st.microStutterPeriodMs) +
                " ms aralıklarla tekrarlıyor. Bu kare zamanlaması / VRR "
                "parmak izidir, donanım arızası değildir.",
                "G-Sync/FreeSync ve V-Sync ayarlarını kontrol edin; FPS "
                "sınırını yenileme hızının 3 altına alın.");
        } else if (st.stutterCount > 0) {
            add(f, Severity::Warn,
                std::to_string(st.stutterCount) + " takılma tespit edildi",
                "Ortalama " + n1(st.avgFps) + " FPS, %1 düşük " +
                n1(st.onePercentLowFps) + " FPS.");
        } else {
            add(f, Severity::Good, "Takılma tespit edilmedi",
                "Ortalama " + n1(st.avgFps) + " FPS, kare süreleri düzenli.");
        }

        if (st.lowFpsNotStutter) {
            add(f, Severity::Warn, "FPS düşük ama kararlı",
                "Kare süreleri dar ve düzenli dağılmış — bu bir kararlılık "
                "sorunu değil.");
        }
    }

    // ------------------------------------------------------------------
    //  7. Surucusu eksik aygitlar
    // ------------------------------------------------------------------
    if (in.devices) {
        size_t missing = 0;
        for (const auto& d : in.devices->problems) if (d.driverMissing) ++missing;

        if (missing > 0) {
            std::string names;
            for (const auto& d : in.devices->problems) {
                if (!d.driverMissing) continue;
                if (!names.empty()) names += ", ";
                names += d.name;
            }
            add(f, Severity::Bad,
                std::to_string(missing) + " aygıtın sürücüsü yüklü değil",
                names + ". Eksik yonga seti sürücüsü güç yönetimini bozar ve "
                "doğrudan takılma sebebi olabilir.",
                "Anakart üreticisinin sitesinden yonga seti sürücülerini "
                "kurun.");
        } else if (!in.devices->problems.empty()) {
            add(f, Severity::Warn,
                std::to_string(in.devices->problems.size()) +
                " aygıt sorun bildiriyor",
                "Aygıt Yöneticisi'nde uyarı işaretli cihazlar var.",
                "Aygıt Yöneticisi'ni açıp sarı ünlem işaretli cihazlara "
                "bakın.");
        } else if (in.devices->totalDevices > 0) {
            add(f, Severity::Good, "Tüm aygıt sürücüleri yerinde",
                std::to_string(in.devices->totalDevices) +
                " aygıt tarandı, sorun bildiren yok.");
        }
    }

    // ------------------------------------------------------------------
    //  8. Mavi ekran
    // ------------------------------------------------------------------
    if (in.dumps) {
        if (in.dumps->accessDenied) {
            add(f, Severity::Unknown, "Mavi ekran kayıtları okunamadı",
                "Windows bu klasörü yalnızca yönetici haklarıyla açtırıyor. "
                "Kayıt olup olmadığını bilmiyoruz.",
                "Programı yönetici olarak çalıştırın.");
        } else if (!in.dumps->findings.empty()) {
            const auto& d0 = in.dumps->findings.front();
            add(f, Severity::Bad,
                std::to_string(in.dumps->findings.size()) +
                " mavi ekran kaydı var",
                "En yenisi: " + (d0.parsed ? d0.bugcheckName : d0.fileName) +
                (d0.suspectDriver.empty() ? ""
                                          : " — şüpheli: " + d0.suspectDriver),
                "Mavi Ekran sekmesinde ayrıntılara bakın.");
        } else if (!in.dumps->dumpsEnabled) {
            add(f, Severity::Warn, "Mavi ekran kaydı kapalı",
                "Mavi ekran yaşarsanız sebebi tespit edilemez; Windows "
                "çökme anındaki bellek görüntüsünü diske yazmıyor.",
                "Sistem Özellikleri > Gelişmiş > Başlangıç ve Kurtarma'dan "
                "küçük bellek dökümünü açın.");
        } else {
            add(f, Severity::Good, "Mavi ekran kaydı yok",
                "Sistem bu açıdan temiz.");
        }
    }

    // ------------------------------------------------------------------
    //  8b. Olay gunlugu — sistemin kendi sikayetleri
    // ------------------------------------------------------------------
    //  Buradaki satirlar TESHIS DEGIL, olgudur: "sistem su kadar kez sunu
    //  bildirdi". Sebep hukmu ancak olcumle zaman cakismasi varsa verilir ve
    //  onu kural motoru yapar. Bu sayfa kullanicinin makinesinde neyin
    //  birikmis oldugunu gostermekle yetinir.
    //
    //  Esikler taban gurultusunu elemek icin: saglikli bir makinede de tek
    //  tuk uygulama cokmesi ve seyrek disk yeniden denemesi olur.
    if (in.evtlog && in.evtlog->ok) {
        const sslog::Scan& L = *in.evtlog;

        const uint32_t kp41 = L.at(sslog::Kind::KernelPower41).last30d;
        const uint32_t bugs = L.at(sslog::Kind::BugCheck).last30d;
        const uint32_t tdr  = L.at(sslog::Kind::Tdr).last30d;
        const uint32_t rst  = L.at(sslog::Kind::StorageReset).last30d;
        const uint32_t bad  = L.at(sslog::Kind::StorageBadBlock).last30d;
        const uint32_t rty  = L.at(sslog::Kind::StorageRetry).last30d;
        const uint32_t ntfs = L.at(sslog::Kind::NtfsCorruption).last30d;
        const uint32_t fatal= L.at(sslog::Kind::WheaFatal).last30d;
        const sslog::Series& whea = L.at(sslog::Kind::WheaCorrected);

        // Dump uretmeden kapanma — vaka kulliyatindaki en guclu tek imza.
        if (kp41 > 0 && bugs == 0) {
            add(f, Severity::Bad,
                std::to_string(kp41) + " kez kontrolsüz kapanma, mavi ekran kaydı yok",
                "Sistem son 30 günde bu kadar kez kaydını tutamadan kapandı. "
                "Mavi ekran kaydı hiç yok. Bu ikisi bir aradayken en sık "
                "sebep bellek/EXPO kararsızlığı ya da güç kaynağıdır.",
                "BIOS'ta EXPO/XMP'yi kapatıp bir gün kullanın.");
        } else if (kp41 > 0) {
            add(f, Severity::Warn,
                std::to_string(kp41) + " kez kontrolsüz kapanma",
                "Sistem düzgün kapanmadan gitmiş. Mavi ekran kayıtları da "
                "olduğu için sebep Mavi Ekran sekmesinden izlenebilir.",
                "Mavi Ekran sekmesine bakın.");
        }

        if (fatal > 0)
            add(f, Severity::Bad,
                std::to_string(fatal) + " ölümcül donanım hatası (WHEA 18)",
                "İşlemci ya da yonga seti düzeltilemeyen bir hata bildirdi. "
                "Bu kayıt sağlıklı bir makinede hiç oluşmaz.",
                "Bellek hızını düşürüp tekrar deneyin; sürerse donanım "
                "servisi gerekir.");

        // Duzeltilmis WHEA: TEK BASINA sorun degil. Yalnizca taban cizgisinin
        // ustune ciktiginda bildiriliyor — aksi halde bircok AM5 makinesinde
        // binlerce kayit "kirmizi" gorunur ve kullanici saglam sistemi
        // sokmeye baslar.
        if (whea.last30d >= 100) {
            if (whea.spikingToday())
                add(f, Severity::Warn,
                    "Düzeltilmiş donanım hatalarında son 24 saatte artış",
                    "Son 30 günde " + std::to_string(whea.last30d) + ", son 24 "
                    "saatte " + std::to_string(whea.last24h) + " tane. Bu tür "
                    "kayıtlar tek başına zararsızdır; dikkat çeken şey sayının "
                    "birden yükselmesi.",
                    "Yakın zamanda değiştirdiğiniz BIOS/hız ayarını geri alın.");
            else
                add(f, Severity::Good,
                    "Düzeltilmiş donanım hataları normal seyrinde",
                    "Son 30 günde " + std::to_string(whea.last30d) + " kayıt var "
                    "ama artış yok. Bu kayıtlar birçok sistemde belirtisiz "
                    "birikir; tek başına sorun sayılmaz.");
        }

        if (tdr > 0) {
            std::string driver;
            for (const sslog::Event& e : L.at(sslog::Kind::Tdr).samples)
                if (!e.detail.empty()) { driver = e.detail; break; }
            add(f, Severity::Bad,
                std::to_string(tdr) + " kez ekran sürücüsü sıfırlandı (TDR)",
                "Ekran kartı sürücüsü yanıt vermeyi bırakıp kendini "
                "toparladı. Ekranın bir anlığına donup düzelmesi budur." +
                (driver.empty() ? "" : " Kayıtta geçen sürücü: " + driver + "."),
                "DDU ile temiz kaldırıp güncel sürücüyü kurun.");
        }

        if (rst > 0 || bad > 0) {
            add(f, Severity::Bad,
                "Depolama aygıtı hata bildirdi",
                (rst > 0 ? std::to_string(rst) + " kez aygıta sıfırlama "
                           "gönderildi" : std::string()) +
                (rst > 0 && bad > 0 ? ", " : "") +
                (bad > 0 ? std::to_string(bad) + " disk hatası kaydı" : "") +
                ". Sıfırlama sırasında diske giden her işlem durur; bu "
                "saniyeler süren donmalar üretir.",
                "SSD firmware'ini güncelleyin, SMART değerlerine bakın, "
                "kabloyu kontrol edin.");
        } else if (rty >= 10) {
            add(f, Severity::Warn,
                std::to_string(rty) + " kez disk işlemi yeniden denendi",
                "Aygıt hata vermedi ama okuma/yazma ilk seferde tamamlanmadı. "
                "Tek başına hüküm değil; disk yavaşlığıyla birlikte anlam "
                "kazanır.");
        }

        if (ntfs > 0)
            add(f, Severity::Warn, "Dosya sistemi bozulması kaydı var",
                "Windows NTFS üzerinde tutarsızlık bildirdi. Bu genelde "
                "sebep değil sonuçtur, ama diskin durumu hakkında somut bir "
                "bilgidir.",
                "Yönetici komut isteminde: chkdsk C: /scan");

        if (L.liveKernelReports > 0)
            add(f, Severity::Warn,
                std::to_string(L.liveKernelReports) +
                " mavi ekransız sürücü çökmesi kaydı",
                "Bir sürücü çöküp Windows tarafından toparlandı. Mavi ekran "
                "olmadığı için fark etmemiş olabilirsiniz. En sık üreteni "
                "ekran kartı sürücüsüdür.",
                "Ekran kartı sürücüsünü temiz kurulumla yenileyin.");

        // Hicbiri yoksa bunu da SOYLE. "Bakildi, temiz" bilgisi, sessizlikten
        // cok daha degerlidir — kullanici neyin elendigini bilmeli.
        if (kp41 == 0 && fatal == 0 && tdr == 0 && rst == 0 && bad == 0 &&
            ntfs == 0 && L.liveKernelReports == 0)
            add(f, Severity::Good, "Olay günlüğünde donanım şikâyeti yok",
                "Son 30 gün tarandı: kontrolsüz kapanma, donanım hatası, "
                "ekran sürücüsü sıfırlaması ve disk hatası bulunmadı.");
    } else if (in.evtlog && in.evtlog->attempted) {
        add(f, Severity::Unknown, "Olay günlüğü okunamadı",
            in.evtlog->error.empty()
                ? "Windows olay günlüğü açılamadı; sistemin geçmişi hakkında "
                  "bir şey söyleyemiyoruz."
                : in.evtlog->error);
    }

    // ------------------------------------------------------------------
    //  8c. Depolama envanteri
    // ------------------------------------------------------------------
    //  PDH sayaclari diskin YAVAS oldugunu soyler; burasi NE OLDUGUNU soyler.
    //
    //  Yorum siniri: asinma yuzdesi SEBEP degil BAGLAM'dir. "Sagligi %56,
    //  takilmanin sebebi bu" cikarimi supheli ve tam da yanlis parca aldiracak
    //  turden. Yalnizca donen disk, dolu disk ve ureticinin ariza tahmini
    //  dogrudan bulgu sayilir.
    if (in.storage && in.storage->ok) {
        for (const auto& d : in.storage->drives) {
            if (d.bus == ssstore::Bus::Usb) continue;   // harici disk konumuz degil

            const std::string ad = d.model.empty()
                ? ("Disk " + std::to_string(d.index)) : d.model;

            // --- SEBEP olabilecekler ---
            if (d.failurePredicted == ssstore::Drive::Tri::Yes) {
                add(f, Severity::Bad, ad + " arıza uyarısı veriyor",
                    "Diskin kendi izleme sistemi (SMART) eşik aşımı bildiriyor. "
                    "Bu, üreticinin \"bu disk arızalanmak üzere\" demesidir.",
                    "Verilerinizi hemen yedekleyin ve diski değiştirin.");
            }
            if (d.criticalWarning) {
                add(f, Severity::Bad, ad + " kritik uyarı bayrağı kaldırdı",
                    "NVMe diskin kendisi kritik bir durum bildiriyor "
                    "(aşırı ısınma, yedek blok tükenmesi ya da salt okunur moda "
                    "geçme).",
                    "Verilerinizi yedekleyin, diski değiştirmeyi planlayın.");
            }

            if (d.kind == ssstore::Kind::Hdd && d.hasSystemVolume()) {
                add(f, Severity::Bad, "Windows dönen disk (HDD) üzerinde",
                    ad + " mekanik bir disk. İşletim sistemi ve oyunlar burada "
                    "olduğunda yükleme takılmaları ve donmalar doğrudan bunun "
                    "sonucudur.",
                    "Bir SSD takıp Windows'u ve oyunları oraya taşıyın — bu "
                    "sistemdeki en büyük tek iyileştirmedir.");
            } else if (d.kind == ssstore::Kind::Hdd) {
                add(f, Severity::Warn, ad + " dönen disk (HDD)",
                    "Bu diskte oyun varsa yükleme ekranlarında ve açık dünya "
                    "oyunlarında takılma üretir.",
                    "Oyunlarınızı SSD'ye taşıyın.");
            }

            for (const auto& v : d.volumes) {
                if (v.totalMb == 0) continue;
                const double freePct = v.freePct();
                const std::string vad = v.letter;

                if (freePct < 5.0) {
                    add(f, Severity::Bad, vad + " sürücüsü neredeyse dolu",
                        "Boş alan %" + n0(freePct) + " (" +
                        n0(static_cast<double>(v.freeMb) / 1024.0) + " GB). "
                        + (d.kind == ssstore::Kind::Ssd
                            ? "SSD'ler doldukça yazma hızları belirgin düşer; "
                              "sayfa dosyası da bu diskteyse takılma üretir."
                            : "Sayfa dosyası bu diskteyse takılma üretir.") ,
                        "En az %10 boş alan kalacak şekilde yer açın.");
                } else if (freePct < 10.0) {
                    add(f, Severity::Warn, vad + " sürücüsünde boş alan az",
                        "Boş alan %" + n0(freePct) + " (" +
                        n0(static_cast<double>(v.freeMb) / 1024.0) + " GB).",
                        "Biraz yer açmak yazma performansını toparlar.");
                }
            }

            // --- Yalnizca BAGLAM ---
            //  Bu satirlar hicbir teshise agirlik vermez ve asla kirmizi
            //  olmaz. Amac kullanicinin diskinin durumunu BILMESI, sucu
            //  diske atmak degil.
            if (d.smartRead && ssstore::known(d.wearPct)) {
                const std::string extra =
                    ssstore::known(d.powerOnHours)
                        ? (" · " + std::to_string(d.powerOnHours / 24) + " gün açık kalmış")
                        : std::string{};
                add(f, d.wearPct >= 90 ? Severity::Warn : Severity::Good,
                    ad + " ömrünün %" + std::to_string(d.wearPct) + "'ini kullanmış",
                    (d.wearPct >= 90
                        ? "Ömrünün sonuna yaklaşıyor; yedekleme planı yapın."
                        : "Bu bir aşınma sayacıdır, hız göstergesi değildir — "
                          "aşınmış bir disk ilk günkü hızında çalışıyor olabilir. "
                          "Takılma sebebi olarak sayılmıyor, bilgi olarak "
                          "veriliyor.") + extra,
                    d.wearPct >= 90 ? "Verilerinizi düzenli yedekleyin." : "");
            }
        }

        if (!in.storage->elevated) {
            add(f, Severity::Unknown, "Disk sağlık verisi okunamadı",
                "SMART kayıtlarını okumak için programın yönetici olarak "
                "çalışması gerekiyor. Disk modelleri ve tipleri okundu, sağlık "
                "durumu okunmadı.");
        }
    }

    // ------------------------------------------------------------------
    //  9. Olculemeyenler — sessizce atlamiyoruz
    // ------------------------------------------------------------------
    if (in.caps) {
        if (!in.caps->nvidiaGpu)
            add(f, Severity::Unknown, "Ekran kartı telemetrisi okunamıyor",
                "NVIDIA sürücüsü bulunamadı; AMD kartlar için destek henüz "
                "yazılmadı. Sıcaklık, kullanım ve kısıtlama bayrakları bu "
                "ölçümde yok.");
        if (!in.caps->cpuTemp)
            add(f, Severity::Unknown, "İşlemci sıcaklığı okunamıyor",
                "Gerçek çekirdek sıcaklığı (Tctl/Tdie) için sistem sürücüsü "
                "gerekiyor; Windows bu değeri normal programlara vermiyor. "
                "Ölçülemeyen alan boş bırakılır.");
    }

    // Onem sirasi: kirmizi, turuncu, olculemeyen, sorun yok.
    // std::stable_sort: ayni seviyedekiler eklendikleri sirada kalir ki
    // konu butunlugu (guc -> bellek -> GPU -> disk) bozulmasin.
    std::stable_sort(f.begin(), f.end(),
                     [](const Finding& a, const Finding& b) {
                         return static_cast<int>(a.severity) <
                                static_cast<int>(b.severity);
                     });
    return f;
}

} // namespace ssfind

#endif // _WIN32
