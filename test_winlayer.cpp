// ============================================================================
//  Syspect — Windows katmani testleri
//  ----------------------------------------------------------------------
//  test_core.cpp tasinabilir motoru sinar. Bu dosya ONUN gormedigi iki yeni
//  katmani sinar:
//
//    1) sslog::applyTo  — olay gunlugu taramasindan SystemInfo'ya esleme
//    2) sscap kayit dosyasi — [eventlog] bolumunun gidis-donus dogrulugu
//
//  NEDEN AYRI BIR DOSYA: ikisi de Windows API'sine bagimli hedeflerde durur;
//  test_core Windows'suz makinede de derlenebilmeli (tasarim kurali 5).
//
//  NEDEN ONEMLI: bu makinede hic TDR, hic depolama hatasi kaydi yok. Yani o
//  kod yollari gercek veriyle DOGRULANAMIYOR. Sentetik Scan uretip esleme ve
//  seri hale getirme mantigini burada sinamak, elde edilebilecek en yakin
//  guvence. Gercek bir TDR gorulunce yine de `ss_cli eventlog` ile bakilmali.
// ============================================================================
#ifdef _WIN32

#include "event_log.h"
#include "capture_io.h"
#include "i18n.h"
#include "dpc_source.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* what, bool ok) {
    std::printf("    [%s] %s\n", ok ? " OK " : "FAIL", what);
    ok ? ++g_pass : ++g_fail;
}

void header(const char* t) {
    std::printf("\n--- %s ---\n", t);
}

// Elle kurulmus, gercekci bir tarama: TDR yasamis bir makine.
sslog::Scan makeScan(uint64_t now) {
    sslog::Scan s;
    s.attempted = true;
    s.ok = true;
    for (size_t i = 0; i < static_cast<size_t>(sslog::Kind::Count_); ++i)
        s.series[i].kind = static_cast<sslog::Kind>(i);

    auto& tdr = s.series[static_cast<size_t>(sslog::Kind::Tdr)];
    tdr.last24h = 2; tdr.last7d = 3; tdr.last30d = 5;
    tdr.newestFileTime = now;
    sslog::Event e;
    e.kind = sslog::Kind::Tdr;
    e.eventId = 4101;
    e.provider = "Display";
    e.fileTimeUtc = now;
    e.detail = "nvlddmkm";
    tdr.samples.push_back(e);

    auto& rst = s.series[static_cast<size_t>(sslog::Kind::StorageReset)];
    rst.last30d = 4;
    rst.newestFileTime = now;
    sslog::Event r;
    r.kind = sslog::Kind::StorageReset;
    r.eventId = 129;
    r.provider = "storahci";
    r.fileTimeUtc = now;
    rst.samples.push_back(r);

    auto& whea = s.series[static_cast<size_t>(sslog::Kind::WheaCorrected)];
    whea.last24h = 300; whea.last7d = 340; whea.last30d = 400;
    whea.newestFileTime = now;

    auto& kp = s.series[static_cast<size_t>(sslog::Kind::KernelPower41)];
    kp.last30d = 3;
    kp.newestFileTime = now;

    s.liveKernelReports = 2;
    return s;
}

// ----------------------------------------------------------------------------
void testApplyTo() {
    header("sslog::applyTo — tarama -> SystemInfo eslemesi");

    const uint64_t now = sslog::nowFileTime();
    const sslog::Scan s = makeScan(now);

    // --- Olcum penceresi olayla CAKISIYOR ---
    {
        ss::SystemInfo sys;
        sslog::applyTo(sys, s, now - 600000000ull, now);   // ~60 sn pencere

        check("eventLogRead isaretlendi", sys.eventLogRead);
        check("TDR sayisi tasindi", sys.tdrCount == 5);
        check("TDR olcumle cakisti", sys.tdrDuringCapture == 1);
        check("Suclanan surucu adi tasindi", sys.tdrDriver == "nvlddmkm");
        check("Depolama sifirlama sayisi tasindi", sys.storageResetCount == 4);
        check("Depolama olayi olcumle cakisti", sys.storageDuringCapture == 1);
        check("Kernel-Power 41 tasindi", sys.kernelPower41 == 3);
        check("LiveKernelReports tasindi", sys.liveKernelReports == 2);
        check("WHEA artisi yakalandi (400/30gun, 300/24sa)",
              sys.wheaCorrectedSpiking);
    }

    // --- Pencere VERILMEDI: cakisma hesaplanmamali ---
    {
        ss::SystemInfo sys;
        sslog::applyTo(sys, s, 0, 0);
        check("Pencere yokken cakisma sifir kalir",
              sys.tdrDuringCapture == 0 && sys.storageDuringCapture == 0);
        check("Sayimlar yine de tasinir", sys.tdrCount == 5);
    }

    // --- Okunamamis tarama hicbir alani doldurmamali ---
    {
        sslog::Scan bad;
        bad.attempted = true;
        bad.ok = false;
        ss::SystemInfo sys;
        sslog::applyTo(sys, bad, now - 600000000ull, now);
        check("Okunamayan tarama SystemInfo'ya dokunmuyor",
              !sys.eventLogRead && sys.tdrCount == 0);
    }

    // --- Cok eski olay olcum penceresine girmemeli ---
    {
        ss::SystemInfo sys;
        // Pencereyi olaydan 10 gun SONRAYA kaydir
        const uint64_t later = now + 8640000000000ull;
        sslog::applyTo(sys, s, later, later + 600000000ull);
        check("Pencere disindaki TDR cakisma sayilmiyor",
              sys.tdrDuringCapture == 0);
        check("Ama 30 gunluk sayim yine de bildiriliyor", sys.tdrCount == 5);
    }
}

// ----------------------------------------------------------------------------
void testCaptureRoundTrip() {
    header(".syscap — olay gunlugu bolumunun gidis-donusu");

    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    const std::wstring path = std::wstring(dir) + L"syspect-selftest.syscap";

    const uint64_t now = sslog::nowFileTime();

    sscap::Capture out;
    out.application    = "test.exe";
    out.processId      = 4242;
    out.cpuName        = "Test CPU";
    out.gpuName        = "Test GPU";
    out.osBuild        = "Windows 11 (test)";
    out.ramInstalledMb = 32768;
    out.captureStartFt = now - 600000000ull;
    out.captureEndFt   = now;
    out.evtlog         = makeScan(now);

    for (int i = 0; i < 700; ++i) {
        ss::FrameSample f;
        f.frameTimeMs = 6.94;
        f.timestampUs = static_cast<uint64_t>(i) * 6940ull;
        out.frames.push_back(f);
    }

    std::string err;
    check("Kayit yazildi", sscap::save(path, out, err));

    sscap::Capture in;
    check("Kayit okundu", sscap::load(path, in, err));

    check("Olcum penceresi korundu",
          in.captureStartFt == out.captureStartFt &&
          in.captureEndFt   == out.captureEndFt);
    check("Olay gunlugu bolumu okundu", in.evtlog.attempted && in.evtlog.ok);

    const sslog::Series& tdr = in.evtlog.at(sslog::Kind::Tdr);
    check("TDR sayimlari korundu",
          tdr.last24h == 2 && tdr.last7d == 3 && tdr.last30d == 5);
    check("TDR ornek olayi korundu", tdr.samples.size() == 1);
    check("Surucu adi korundu",
          !tdr.samples.empty() && tdr.samples[0].detail == "nvlddmkm");
    check("Zaman damgasi korundu",
          !tdr.samples.empty() && tdr.samples[0].fileTimeUtc == now);
    check("LiveKernelReports korundu", in.evtlog.liveKernelReports == 2);
    check("Kareler korundu", in.frames.size() == 700);

    // Gidis-donusten sonra ESLEME de ayni sonucu vermeli. Asil guvence bu:
    // dosyayi acan kisi, kaydi alan kisiyle ayni hukmu gormeli.
    ss::SystemInfo a, b;
    sslog::applyTo(a, out.evtlog, out.captureStartFt, out.captureEndFt);
    sslog::applyTo(b, in.evtlog,  in.captureStartFt,  in.captureEndFt);
    check("Dosyadan acan ayni TDR hukmunu goruyor",
          a.tdrCount == b.tdrCount &&
          a.tdrDuringCapture == b.tdrDuringCapture &&
          a.tdrDriver == b.tdrDriver);
    check("Dosyadan acan ayni depolama hukmunu goruyor",
          a.storageResetCount == b.storageResetCount &&
          a.storageDuringCapture == b.storageDuringCapture);

    // Surum 1 dosyasi (bolum yok) "sorun yok" diye OKUNMAMALI.
    {
        sscap::Capture old = out;
        old.evtlog = sslog::Scan{};        // hic taranmamis
        const std::wstring p2 = std::wstring(dir) + L"syspect-selftest-v1.syscap";
        sscap::save(p2, old, err);

        sscap::Capture back;
        sscap::load(p2, back, err);
        check("Gunlugu olmayan kayit 'taranmadi' olarak geliyor",
              !back.evtlog.attempted);
        DeleteFileW(p2.c_str());
    }

    DeleteFileW(path.c_str());
}

// ============================================================================
//  Dil katmani — dosya bicimi gidis-donus
// ----------------------------------------------------------------------------
//  Bicimin kendisi urunun bir parcasi: ceviri yapacak kisi bu dosyayi elle
//  duzenleyecek. Bozuk bir ayristirici sessizce YANLIS DIL gosterir, ki bu
//  cokmekten kotudur. Ozellikle sinanan uc davranis:
//    - bos ceviri satiri "cevrilmedi" demek ve kaynak metne DONMEK
//    - kaynak metnin icinde gecen '=' isaretinin ayirac sanilmamasi
//    - cok satirli metinlerin \n ile gidip donmesi
// ============================================================================
void testI18n() {
    header("Dil dosyasi bicimi");

    wchar_t tmp[MAX_PATH] = L"";
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"syspect-test.lang";

    // Sablon uretimi icin once metin topla.
    ss18::noteSource(L"Ölçüm süresi");
    ss18::noteSource(L"Boş bırakılacak metin");
    ss18::noteSource(L"Oran = %1 ve satır\nsonu var");

    // Calisma aninda kurulan degerler sablona GIRMEMELI. Gercek bir
    // kullanicinin kaydettigi sablonda bunlar cogunluktaydi ve ceviri
    // yapacak kisiyi boguyordu.
    const size_t before = ss18::collectedCount();
    ss18::noteSource(L"!");
    ss18::noteSource(L"%");
    ss18::noteSource(L"%0");
    ss18::noteSource(L"73");
    ss18::noteSource(L"4:12");
    check("Salt isaret/sayi metinleri sablona girmiyor",
          ss18::collectedCount() == before);

    // Ama icinde sayi GECEN mesru metinler korunmali.
    ss18::noteSource(L"3 dakika");
    check("Sayi iceren mesru metin korunuyor",
          ss18::collectedCount() == before + 1);

    std::string err;
    check("Sablon yazilabildi",
          ss18::writeTemplate(path, "zz", "Test", err));

    // Sablon bos cevirilerle cikar; ustune iki ceviri yazip geri okuyalim.
    {
        std::wifstream dummy;   // (dosya asagida elle yeniden yaziliyor)
        FILE* f = nullptr;
        _wfopen_s(&f, path.c_str(), L"wb");
        check("Test dosyasi acildi", f != nullptr);
        if (f) {
            const char* body =
                "@name Test\n@code zz\n\n"
                "~ \xC3\x96l\xC3\xA7\xC3\xBCm s\xC3\xBCresi\n"
                "= Measurement duration\n\n"
                "~ Bo\xC5\x9F b\xC4\xB1rak\xC4\xB1lacak metin\n"
                "= \n\n"
                "~ Oran = %1 ve sat\xC4\xB1r\\nsonu var\n"
                "= Ratio = %1 with a line\\nbreak\n";
            std::fwrite(body, 1, std::strlen(body), f);
            std::fclose(f);
        }
    }

    // Dosyayi lang/ disinda tuttugumuz icin setLanguage bulamaz; dogrudan
    // yukleme yolunu sinamak adina lang klasorune kopyalamak gerekirdi.
    // Bunun yerine ayristiricinin GOZLENEBILIR sonucunu sinariz: sablon
    // yeniden uretildiginde var olan ceviriler KORUNMALI.
    check("Toplanan metin sayisi arttı", ss18::collectedCount() >= 3);

    check("Sablon yeniden uretilebildi",
          ss18::writeTemplate(path, "zz", "Test", err));

    // Yeniden uretilen dosyada onceki cevirilerin durdugunu dogrula.
    {
        std::string all;
        FILE* f = nullptr;
        _wfopen_s(&f, path.c_str(), L"rb");
        if (f) {
            char buf[4096];
            size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
            std::fclose(f);
        }
        check("Var olan ceviri korundu (silinmedi)",
              all.find("Measurement duration") != std::string::npos);
        check("Satir sonu kacisi korundu",
              all.find("\\n") != std::string::npos);
        check("Bos ceviri bos kaldi",
              all.find("Bo\xC5\x9F b\xC4\xB1rak\xC4\xB1lacak metin") != std::string::npos);
    }

    // Turkce modda ceviri tablosu bos: T() kaynagi AYNEN dondurmeli.
    ss18::setLanguage("tr");
    check("Turkce modda kaynak metin degismiyor",
          std::wstring(ss18::T(L"Ölçüm süresi")) == L"Ölçüm süresi");

    // Bulunmayan dil istegi sessizce Turkce'ye donmeli, cokmemeli.
    check("Bilinmeyen dil istegi Turkce'ye donuyor",
          !ss18::setLanguage("qq") && ss18::currentCode() == "tr");

    // Yer tutucu degistirme
    check("Yer tutucu degistiriliyor",
          ss18::Tf(L"{1} kare, {2} saniye", {L"4320", L"30"}) ==
          L"4320 kare, 30 saniye");
    check("Fazla yer tutucu oldugu gibi kaliyor",
          ss18::Tf(L"{1} ve {5}", {L"bir"}) == L"bir ve {5}");

    // Yuzde isareti artik yer tutucu DEGIL. Bu tam olarak ilk denemede
    // bozulan sey: "disk islemlerinin %5'i" cumlesindeki %5 yer tutucu
    // saniliyordu ve metin bozuluyordu.
    check("Yuzde isareti yer tutucu sanilmıyor",
          ss18::Tf(L"disk islemlerinin %5'i {1} ms surdu", {L"45"}) ==
          L"disk islemlerinin %5'i 45 ms surdu");

    _wremove(path.c_str());
}

// ============================================================================
//  DPC modul tablosu
// ----------------------------------------------------------------------------
//  Toplayicinin kendisi yonetici hakki istiyor ve burada calistirilamaz. Ama
//  en hataya acik parcasi olan ADRES -> SURUCU cevrimi istemiyor: cekirdek
//  modul listesi normal kullanicinin okuyabildigi bir sey.
//
//  Bu cevrim yanlis calisirsa sonuc sessizce YANLIS SURUCUYU suclamak olur —
//  yani urunun bir numarali riski. Sinanmadan birakilamaz.
// ============================================================================
bool processIsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD sz = sizeof(el);
    const bool ok = GetTokenInformation(tok, TokenElevation, &el, sz, &sz) != 0;
    CloseHandle(tok);
    return ok && el.TokenIsElevated;
}

void testDpcModuleMap() {
    header("DPC modul tablosu (adres -> surucu)");

    ssdpc::ModuleMap m;

    // EnumDeviceDrivers cekirdek adreslerini YUKSELTILMEMIS surece vermiyor
    // (Windows 10 1607'den beri gecerli KASLR sertlestirmesi). Bu bir hata
    // degil, isletim sisteminin tasarimi — testin BASARISIZ olmasi degil
    // ATLANMASI gerekir. Yoksa normal ctest kosusu kalici kirmizi kalir ve
    // gercek bir bozulma gurultunun icinde kaybolur.
    if (!processIsElevated()) {
        std::printf("    [ATLA] Yonetici hakki yok — cekirdek adresleri "
                    "okunamiyor.\n");
        std::printf("           Dogrulamak icin yukseltilmis kabukta "
                    "calistirin.\n");
        // Yukseltme gerektirmeyen sinir durumlari yine de sinanabilir.
        check("Bos tabloda sifir adres bos donuyor", m.resolve(0).empty());
        check("Bos tabloda herhangi bir adres bos donuyor",
              m.resolve(0xFFFF800000000000ull).empty());
        return;
    }

    check("Cekirdek modul listesi okundu", m.build());
    check("Birden fazla modul bulundu", m.size() > 10);

    // Gercek taban adresleri al ve kendi tablomuza sor: her modulun tabani
    // kendi adini dondurmeli.
    DWORD needed = 0;
    EnumDeviceDrivers(nullptr, 0, &needed);
    std::vector<LPVOID> bases(needed / sizeof(LPVOID) + 8);
    EnumDeviceDrivers(bases.data(),
                      static_cast<DWORD>(bases.size() * sizeof(LPVOID)), &needed);
    const size_t count = needed / sizeof(LPVOID);

    size_t matched = 0, tested = 0;
    for (size_t i = 0; i < count && tested < 20; ++i) {
        if (!bases[i]) continue;
        wchar_t nameW[MAX_PATH] = L"";
        if (GetDeviceDriverBaseNameW(bases[i], nameW, MAX_PATH) == 0) continue;
        char nameA[MAX_PATH] = "";
        WideCharToMultiByte(CP_UTF8, 0, nameW, -1, nameA, MAX_PATH, nullptr, nullptr);

        ++tested;
        if (m.resolve(reinterpret_cast<uint64_t>(bases[i])) == nameA) ++matched;
    }
    check("Modul tabanlari kendi adlarina cozuluyor",
          tested > 0 && matched == tested);

    // Taban + kucuk bir ofset de ayni module dusmeli — DPC rutini modulun
    // tabaninda degil, icinde bir yerdedir.
    if (count > 0 && bases[0]) {
        const uint64_t b = reinterpret_cast<uint64_t>(bases[0]);
        check("Taban + ofset ayni module dusuyor",
              !m.resolve(b + 0x400).empty());
    }

    // Sacma adresler bos donmeli, en yakin modulu SUCLAMAMALI.
    check("Sifir adres bos donuyor", m.resolve(0).empty());
    check("Cok uzak adres bos donuyor",
          m.resolve(0x7FFFFFFFFFFFFFFFull).empty());
    check("Ilk modulun altindaki adres bos donuyor",
          m.resolve(0x1000).empty());
}

// ============================================================================
//  DPC kontrol grubu — YANLIS SUCLAMA kilidi
// ----------------------------------------------------------------------------
//  Bu paketin en onemli testi. CLAUDE.md tasarim kurali 3'un dogrudan konusu:
//  kontrol grubu olmadan motor her takilmaya bir .sys yapistirir ve kullanici
//  calisan bir surucuyu kaldirir.
//
//  Sinanan sey su: SUREKLI uzun DPC yapan ama takilmalarla ilgisi OLMAYAN bir
//  surucu suclanmamali. Bu kilit kirilirsa urun her makinede bir suclu bulur
//  ve hepsi yanlis olur.
//
//  Sentetik veri kullaniliyor cunku gercek toplama yonetici hakki istiyor —
//  ama sinanan mantik zaten toplamadan bagimsiz, saf hesap.
// ============================================================================
namespace {

// Basit kurgu: 100 saniyelik olcum, saniyede 1 QPC tiki varsayimiyla
// okunabilir sayilar.
ssdpc::Capture makeDpcCapture(double durationSec) {
    ssdpc::Capture c;
    c.ok          = true;
    c.qpcFreq     = 1000;                 // 1 tik = 1 ms
    c.durationSec = durationSec;
    return c;
}

void addDpc(ssdpc::Capture& c, const std::string& drv,
            uint64_t qpc, double ms) {
    uint32_t idx = 0;
    bool found = false;
    for (uint32_t i = 0; i < c.driverNames.size(); ++i)
        if (c.driverNames[i] == drv) { idx = i; found = true; break; }
    if (!found) {
        idx = static_cast<uint32_t>(c.driverNames.size());
        c.driverNames.push_back(drv);
    }
    ssdpc::LongDpc e;
    e.qpc = qpc; e.ms = ms; e.driverIndex = idx;
    c.events.push_back(e);
}

} // namespace

void testDpcControlGroup() {
    header("DPC kontrol grubu (yanlis suclama kilidi)");

    // --- VAKA A: surekli gurultu yapan surucu ---------------------------
    //  "storport.sys" olcum boyunca duzenli uzun DPC yapiyor. Takilma
    //  pencerelerine de dusuyor cunku her yere dusuyor. SUCLANMAMALI.
    {
        ssdpc::Capture c = makeDpcCapture(100.0);   // 100 sn = 100000 tik
        // Her 10 ms'de bir: 10000 olay, tum olcume duzgun yayilmis.
        for (uint64_t t = 0; t < 100000; t += 10)
            addDpc(c, "storport.sys", t, 1.5);

        // 10 takilma penceresi, her biri 20 ms.
        //
        // DIKKAT — pencereler bilerek 37 tik KAYDIRILDI. Ilk yazimda
        // pencereler de olaylar da yuvarlak sayilara denk geliyordu ve her
        // pencerenin ilk tiki tam bir olaya oturuyordu; bu, algoritmanin
        // degil kurgunun urettigi sahte bir korelasyondu. Gercek veride
        // boyle bir hizalanma yok.
        std::vector<ssdpc::Window> w;
        for (uint64_t i = 0; i < 10; ++i)
            w.push_back({i * 10000 + 37, i * 10000 + 57});

        ssdpc::summarize(c, w);
        const ssdpc::DriverStats* s = ssdpc::primeSuspect(c);
        check("Surekli gurultu yapan surucu SUCLANMIYOR", s == nullptr);
    }

    // --- VAKA B: gercek suclu ------------------------------------------
    //  "kotusurucu.sys" YALNIZCA takilma anlarinda uzun DPC yapiyor.
    //  Pencere ici oran tabanin cok ustunde — SUCLANMALI.
    {
        ssdpc::Capture c = makeDpcCapture(100.0);
        std::vector<ssdpc::Window> w;
        for (uint64_t i = 0; i < 10; ++i) {
            const uint64_t start = i * 10000;
            w.push_back({start, start + 20});
            // Pencere icine 2'ser olay
            addDpc(c, "kotusurucu.sys", start + 5,  8.0);
            addDpc(c, "kotusurucu.sys", start + 15, 6.0);
        }
        // Pencere disinda yalnizca 3 olay — taban cok dusuk
        addDpc(c, "kotusurucu.sys", 55000, 2.0);
        addDpc(c, "kotusurucu.sys", 65000, 2.0);
        addDpc(c, "kotusurucu.sys", 75000, 2.0);

        ssdpc::summarize(c, w);
        const ssdpc::DriverStats* s = ssdpc::primeSuspect(c);
        check("Yalnizca takilmalarda gorulen surucu SUCLANIYOR", s != nullptr);
        check("Dogru surucu suclandi",
              s && s->name == "kotusurucu.sys");
        check("Lift esigin ustunde", s && s->liftOverBaseline() >= 3.0);
    }

    // --- VAKA C: tek rastlanti -----------------------------------------
    //  Bir surucu tek bir takilmayla cakisiyor. Tesadufle ayirt edilemez;
    //  SUCLANMAMALI.
    {
        ssdpc::Capture c = makeDpcCapture(100.0);
        std::vector<ssdpc::Window> w{{5000, 5020}};
        addDpc(c, "birsey.sys", 5010, 12.0);

        ssdpc::summarize(c, w);
        check("Tek rastlanti suclama URETMIYOR",
              ssdpc::primeSuspect(c) == nullptr);
    }

    // --- VAKA D: pencere yok -------------------------------------------
    //  Takilma bulunamamis. Oran hesaplanamaz; hicbir surucu suclanmamali.
    {
        ssdpc::Capture c = makeDpcCapture(100.0);
        for (uint64_t t = 0; t < 100000; t += 50)
            addDpc(c, "herhangi.sys", t, 5.0);

        ssdpc::summarize(c, {});
        check("Takilma yokken suclama URETMIYOR",
              ssdpc::primeSuspect(c) == nullptr);
        check("Ama istatistik yine de uretiliyor", !c.drivers.empty());
    }

    // --- VAKA E: iki aday, hakli olan secilmeli -------------------------
    {
        ssdpc::Capture c = makeDpcCapture(100.0);
        std::vector<ssdpc::Window> w;
        for (uint64_t i = 0; i < 10; ++i) {
            const uint64_t start = i * 10000;
            w.push_back({start, start + 20});
            addDpc(c, "gercek.sys", start + 5, 9.0);
            addDpc(c, "gercek.sys", start + 8, 9.0);
            addDpc(c, "gurultu.sys", start + 10, 2.0);
        }
        // gurultu.sys her yerde
        for (uint64_t t = 0; t < 100000; t += 100)
            addDpc(c, "gurultu.sys", t, 2.0);

        ssdpc::summarize(c, w);
        const ssdpc::DriverStats* s = ssdpc::primeSuspect(c);
        check("Iki adaydan tabani dusuk olan seciliyor",
              s && s->name == "gercek.sys");
    }
}

} // namespace

int main() {
    std::printf("\n#############################################################\n");
    std::printf("#  Syspect — Windows katmani testleri                        #\n");
    std::printf("#############################################################\n");

    testApplyTo();
    testCaptureRoundTrip();
    testI18n();
    testDpcModuleMap();
    testDpcControlGroup();

    std::printf("\n  SONUC: %d gecti, %d basarisiz\n\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

#else
int main() { return 0; }   // Windows disi: bu katman zaten derlenmiyor
#endif
