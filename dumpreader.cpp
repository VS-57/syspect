// ============================================================================
//  StutterScope — Kernel Crash Dump Okuyucu
//  ----------------------------------------------------------------------
//  %SystemRoot%\Minidump\*.dmp icindeki Windows cekirdek dump dosyalarini
//  okur; bugcheck kodunu, parametrelerini ve iceride gecen surucu adlarini
//  cikarir. WinDbg / Debugging Tools for Windows KURULUMU GEREKTIRMEZ.
//
//  Onemli: bu dosyalar KULLANICI-MOD minidump (MDMP) DEGILDIR. Cekirdek
//  dump formatidir: imza "PAGEDU64" (x64) / "PAGEDUMP" (x86).
//  dbghelp.dll'in MiniDumpReadDumpStream API'si bu dosyalari ACAMAZ.
//
//  Derleme:
//    Windows : cl /std:c++17 /EHsc /O2 /MT dumpreader.cpp
//    Linux   : g++ -std=c++17 -O2 -o dumpreader dumpreader.cpp
//  (Ayristirma tamamen tasinabilir; Windows'a ozgu kisimlar #ifdef _WIN32)
//
//  Kullanim:
//    dumpreader <dosya.dmp>          Teshis ciktisi
//    dumpreader <dosya.dmp> --probe  Dogrulama icin ham alan dokumu
//    dumpreader --selftest           Sentetik dump ile kendini test eder
//    dumpreader --check-crashcontrol Dump kaydinin acik olup olmadigi (Win)
// ============================================================================

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
// NOMINMAX sart: windows.h min/max makrolarini tanimlar ve std::max(...)
// cagrilarini bozar (MSVC: error C2589 "illegal token on right side of '::'").
// Bu dosya once yalnizca Linux'ta derlendigi icin hata fark edilmemisti.
#  define NOMINMAX
#  include <windows.h>
#endif

// ============================================================================
//  1. DUMP_HEADER64 alan ofsetleri
// ----------------------------------------------------------------------------
//  Bu ofsetler cekirdek dump formatinda uzun suredir sabittir ve Volatility
//  ile kdmp-parser gibi bagimsiz projelerce dogrulanmistir. Kritik olan
//  BugCheckCode = 0x038; parametreler 8 bayt hizali oldugu icin 0x03C'de
//  bir dolgu alani vardir ve parametreler 0x040'tan baslar.
// ============================================================================
namespace hdr {
    constexpr size_t SIGNATURE            = 0x000; // "PAGE"
    constexpr size_t VALID_DUMP           = 0x004; // "DU64" (x64) / "DUMP" (x86)
    constexpr size_t MAJOR_VERSION        = 0x008;
    constexpr size_t MINOR_VERSION        = 0x00C;
    constexpr size_t DIRECTORY_TABLE_BASE = 0x010;
    constexpr size_t PFN_DATABASE         = 0x018;
    constexpr size_t PS_LOADED_MODULE_LIST= 0x020;
    constexpr size_t PS_ACTIVE_PROC_HEAD  = 0x028;
    constexpr size_t MACHINE_IMAGE_TYPE   = 0x030;
    constexpr size_t NUMBER_PROCESSORS    = 0x034;
    constexpr size_t BUGCHECK_CODE        = 0x038;
    // 0x03C : dolgu (alignment padding)
    constexpr size_t BUGCHECK_PARAM1      = 0x040;
    constexpr size_t BUGCHECK_PARAM2      = 0x048;
    constexpr size_t BUGCHECK_PARAM3      = 0x050;
    constexpr size_t BUGCHECK_PARAM4      = 0x058;
    constexpr size_t VERSION_USER         = 0x060; // CHAR[32]
    constexpr size_t KD_DEBUGGER_DATA_BLK = 0x080;
    constexpr size_t DUMP_TYPE            = 0xF98;
    constexpr size_t SYSTEM_TIME          = 0xFB8; // FILETIME
    constexpr size_t COMMENT              = 0xFC0; // CHAR[128]
    constexpr size_t HEADER_SIZE_X64      = 0x2000;
}

// DumpType degerleri
enum DumpType : uint32_t {
    DUMP_TYPE_FULL     = 1,
    DUMP_TYPE_SUMMARY  = 2,
    DUMP_TYPE_HEADER   = 3,
    DUMP_TYPE_TRIAGE   = 4,
    DUMP_TYPE_BITMAP_FULL    = 5,
    DUMP_TYPE_BITMAP_KERNEL  = 6,
    DUMP_TYPE_AUTOMATIC      = 7,
};

// ============================================================================
//  2. Guvenli bayt okuma
// ============================================================================
class ByteReader {
public:
    explicit ByteReader(std::vector<uint8_t> data) : buf_(std::move(data)) {}

    size_t size() const { return buf_.size(); }
    const uint8_t* raw() const { return buf_.data(); }

    bool ok(size_t off, size_t len) const {
        return off <= buf_.size() && len <= buf_.size() - off;
    }

    uint32_t u32(size_t off, bool* good = nullptr) const {
        if (!ok(off, 4)) { if (good) *good = false; return 0; }
        if (good) *good = true;
        uint32_t v; std::memcpy(&v, buf_.data() + off, 4);
        return v; // x86/x64 little-endian; dump formati da little-endian
    }

    uint64_t u64(size_t off, bool* good = nullptr) const {
        if (!ok(off, 8)) { if (good) *good = false; return 0; }
        if (good) *good = true;
        uint64_t v; std::memcpy(&v, buf_.data() + off, 8);
        return v;
    }

    std::string ascii(size_t off, size_t maxLen) const {
        if (!ok(off, maxLen)) return {};
        std::string s;
        for (size_t i = 0; i < maxLen; ++i) {
            char c = static_cast<char>(buf_[off + i]);
            if (c == '\0') break;
            s.push_back((c >= 32 && c < 127) ? c : '.');
        }
        return s;
    }

    std::string tag4(size_t off) const {
        if (!ok(off, 4)) return {};
        return std::string(reinterpret_cast<const char*>(buf_.data() + off), 4);
    }

private:
    std::vector<uint8_t> buf_;
};

// ============================================================================
//  3. Bugcheck sozlugu
// ----------------------------------------------------------------------------
//  "aciklama" teknik ad, "kullanici" ise son kullaniciya gosterilecek cumle.
//  Sozluk bilincli olarak kucuk tutuldu: oyuncu makinelerinde pratikte
//  gorulen kodlar. Bilinmeyen kod icin genel metin uretilir.
// ============================================================================
struct BugcheckInfo {
    const char* name;
    const char* userText;
    const char* action;
};

static const std::map<uint32_t, BugcheckInfo>& bugcheckTable() {
    static const std::map<uint32_t, BugcheckInfo> t = {
        {0x0000000A, {"IRQL_NOT_LESS_OR_EQUAL",
            "Bir surucu izin verilmeyen bir bellek adresine eristi.",
            "Son guncellenen surucuyu geri alin. Sik tekrarliyorsa bellek testi yapin."}},
        {0x00000012, {"TRAP_CAUSE_UNKNOWN",
            "Sebebi belirlenemeyen bir cekirdek hatasi olustu.",
            "Genellikle donanim kararsizligi. Overclock/EXPO ayarlarini sifirlayin."}},
        {0x0000001A, {"MEMORY_MANAGEMENT",
            "Bellek yonetiminde hata olustu.",
            "EXPO/XMP profilini kapatip test edin. Duzelmezse Windows Bellek Tanilamayi calistirin."}},
        {0x0000001E, {"KMODE_EXCEPTION_NOT_HANDLED",
            "Bir cekirdek bileseni beklenmeyen bir hata verdi.",
            "Surucu guncellemelerini kontrol edin."}},
        {0x00000050, {"PAGE_FAULT_IN_NONPAGED_AREA",
            "Gecersiz bir bellek adresine erisilmeye calisildi.",
            "Cogunlukla RAM veya surucu kaynakli. EXPO kapatarak test edin."}},
        {0x0000007E, {"SYSTEM_THREAD_EXCEPTION_NOT_HANDLED",
            "Bir sistem surucusu hata verdi.",
            "Sucla anilan surucu adini kontrol edip guncelleyin."}},
        {0x0000009F, {"DRIVER_POWER_STATE_FAILURE",
            "Bir surucu uyku/uyanma gecisinde takildi.",
            "Chipset ve depolama surucularini guncelleyin. Hizli baslatmayi kapatmayi deneyin."}},
        {0x000000D1, {"DRIVER_IRQL_NOT_LESS_OR_EQUAL",
            "Bir surucu yanlis bellek adresine eristi.",
            "Genellikle ag veya ses surucusu. Sucla anilan .sys dosyasini guncelleyin."}},
        {0x00000101, {"CLOCK_WATCHDOG_TIMEOUT",
            "Bir islemci cekirdegi yanit vermedi.",
            "Klasik undervolt/Curve Optimizer kararsizligi belirtisi. CO degerini sifirlayin."}},
        {0x00000109, {"CRITICAL_STRUCTURE_CORRUPTION",
            "Cekirdek veri yapilari bozuldu.",
            "Bellek kararsizligi veya uyumsuz surucu. EXPO kapatip test edin."}},
        {0x00000116, {"VIDEO_TDR_FAILURE",
            "Ekran karti surucusu yanit vermedi ve sifirlanamadi.",
            "Ekran karti surucusunu DDU ile temiz kurun. GPU overclock varsa kaldirin."}},
        {0x00000117, {"VIDEO_TDR_TIMEOUT_DETECTED",
            "Ekran karti surucusu zaman asimina ugradi.",
            "Ekran karti surucusunu DDU ile temiz kurun. Guc kaynagini da degerlendirin."}},
        {0x00000119, {"VIDEO_SCHEDULER_INTERNAL_ERROR",
            "Ekran karti zamanlayicisinda hata olustu.",
            "GPU surucusunu temiz kurun."}},
        {0x00000124, {"WHEA_UNCORRECTABLE_ERROR",
            "Islemci veya anakart duzeltilemeyen bir DONANIM hatasi bildirdi.",
            "Bu bir yazilim hatasi degildir. Tum overclock/undervolt/EXPO ayarlarini sifirlayin."}},
        {0x00000133, {"DPC_WATCHDOG_VIOLATION",
            "Bir surucu islemciyi cok uzun sure bloke etti.",
            "Depolama ve ag surucularini guncelleyin. Sucla anilan .sys dosyasina bakin."}},
        {0x0000013A, {"KERNEL_MODE_HEAP_CORRUPTION",
            "Cekirdek bellek yigini bozuldu.",
            "Bellek kararsizligi olasi. EXPO kapatip test edin."}},
        {0x00000139, {"KERNEL_SECURITY_CHECK_FAILURE",
            "Cekirdek guvenlik dogrulamasi basarisiz oldu.",
            "Surucu uyumsuzlugu veya bellek kararsizligi."}},
        {0x000000EF, {"CRITICAL_PROCESS_DIED",
            "Kritik bir Windows sureci sonlandi.",
            "Sistem dosyalarini onarin: sfc /scannow ve DISM."}},
        {0x000000C4, {"DRIVER_VERIFIER_DETECTED_VIOLATION",
            "Driver Verifier bir surucu ihlali yakaladi.",
            "Verifier'i kapatmadan once sucla anilan surucuyu not edin."}},
        {0x0000004E, {"PFN_LIST_CORRUPT",
            "Bellek sayfa listesi bozuldu.",
            "Guclu RAM kararsizligi belirtisi. EXPO kapatin, tek cubukla test edin."}},
    };
    return t;
}

// ============================================================================
//  3b. Olasilik motoru — sebep kategorileri ve on olasiliklar
// ----------------------------------------------------------------------------
//  TASARIM KARARI: Tek bir "suclu" ilan etmiyoruz. Bir dump dosyasi tek
//  basina kesin sebep vermez; ihtimal dagilimi verir. Kullaniciya siralı
//  bir olasilik listesi gostermek hem daha durust hem daha faydalidir:
//  en ustteki maddeden baslayip sirayla deneyebilir.
//
//  Agirliklar, bugcheck kodunun tarihsel olarak hangi sebeplerden
//  kaynaklandigina dair on olasiliklardir (prior). Kanit bulundukca
//  (ornegin ucuncu parti surucu adi) ilgili kategori guclendirilir.
// ============================================================================
enum class Cause {
    DRIVER,        // ucuncu parti surucu
    GPU_DRIVER,    // ekran karti surucusu
    MEMORY,        // RAM / EXPO / XMP / IMC kararsizligi
    OVERCLOCK,     // OC / undervolt / Curve Optimizer
    PSU,           // guc kaynagi
    STORAGE,       // SSD / HDD / pagefile
    POWER_MGMT,    // C-state, guc plani, hizli baslatma
    SYSFILE,       // bozuk Windows sistem dosyasi
    HARDWARE,      // fiziksel donanim arizasi
    BIOS,          // BIOS / mikrokod
    SECURITY_SW,   // antivirus / anti-cheat filtre surucusu
};

struct CauseInfo { const char* label; const char* action; };

static const CauseInfo& causeInfo(Cause c) {
    static const std::map<Cause, CauseInfo> m = {
        {Cause::DRIVER,      {"Ucuncu parti surucu",
            "Ses, ag, depolama ve chipset surucularini uretici sitesinden guncelleyin."}},
        {Cause::GPU_DRIVER,  {"Ekran karti surucusu",
            "DDU ile temiz kaldirip guncel surucuyu kurun."}},
        {Cause::MEMORY,      {"Bellek / EXPO-XMP kararsizligi",
            "BIOS'ta EXPO/XMP profilini KAPATIP bir gun kullanin. Duzelirse hizi bir kademe dusurup tekrar acin."}},
        {Cause::OVERCLOCK,   {"Overclock / undervolt (Curve Optimizer)",
            "Tum CO/PBO/undervolt ayarlarini varsayilana dondurup test edin."}},
        {Cause::PSU,         {"Guc kaynagi yetersizligi",
            "Baska bir guc kaynagi ile test edin. GPU guc kablolarini ayri hatlardan cekin."}},
        {Cause::STORAGE,     {"Depolama (SSD/HDD) veya pagefile",
            "SSD firmware'ini guncelleyin, SMART degerlerini kontrol edin."}},
        {Cause::POWER_MGMT,  {"Guc yonetimi ayarlari",
            "BIOS'ta Global C-State'i kapatin. Windows'ta Hizli Baslatma'yi devre disi birakin."}},
        {Cause::SYSFILE,     {"Bozuk Windows sistem dosyasi",
            "Yonetici olarak: sfc /scannow  ardindan  DISM /Online /Cleanup-Image /RestoreHealth"}},
        {Cause::HARDWARE,    {"Fiziksel donanim arizasi",
            "Parcalari tek tek eleyerek test edin. Garanti sureci gerekebilir."}},
        {Cause::BIOS,        {"BIOS / mikrokod surumu",
            "Anakart ureticisinin en guncel BIOS surumunu yukleyin."}},
        {Cause::SECURITY_SW, {"Antivirus veya anti-cheat filtre surucusu",
            "Ucuncu parti antivirusu gecici olarak kaldirip test edin."}},
    };
    return m.at(c);
}

struct Weighted { Cause cause; int weight; };

// Bugcheck koduna gore on olasilik dagilimlari (toplam 100)
static const std::map<uint32_t, std::vector<Weighted>>& priorTable() {
    static const std::map<uint32_t, std::vector<Weighted>> t = {
        {0x0000000A, {{Cause::DRIVER,50},{Cause::MEMORY,30},{Cause::SECURITY_SW,20}}},
        {0x00000012, {{Cause::OVERCLOCK,50},{Cause::MEMORY,30},{Cause::DRIVER,20}}},
        {0x0000001A, {{Cause::MEMORY,55},{Cause::DRIVER,20},{Cause::STORAGE,15},{Cause::HARDWARE,10}}},
        {0x0000001E, {{Cause::DRIVER,50},{Cause::MEMORY,30},{Cause::OVERCLOCK,20}}},
        {0x0000004E, {{Cause::MEMORY,60},{Cause::DRIVER,25},{Cause::HARDWARE,15}}},
        {0x00000050, {{Cause::MEMORY,40},{Cause::DRIVER,40},{Cause::SECURITY_SW,20}}},
        {0x0000007E, {{Cause::DRIVER,65},{Cause::MEMORY,20},{Cause::SYSFILE,15}}},
        {0x0000009F, {{Cause::DRIVER,60},{Cause::POWER_MGMT,25},{Cause::BIOS,15}}},
        {0x000000C4, {{Cause::DRIVER,90},{Cause::MEMORY,10}}},
        {0x000000D1, {{Cause::DRIVER,70},{Cause::MEMORY,20},{Cause::SECURITY_SW,10}}},
        {0x000000EF, {{Cause::SYSFILE,45},{Cause::STORAGE,30},{Cause::DRIVER,25}}},
        {0x00000101, {{Cause::OVERCLOCK,60},{Cause::BIOS,20},{Cause::MEMORY,12},{Cause::HARDWARE,8}}},
        {0x00000109, {{Cause::MEMORY,45},{Cause::SECURITY_SW,35},{Cause::OVERCLOCK,20}}},
        {0x00000116, {{Cause::GPU_DRIVER,45},{Cause::OVERCLOCK,25},{Cause::PSU,20},{Cause::HARDWARE,10}}},
        {0x00000117, {{Cause::GPU_DRIVER,45},{Cause::OVERCLOCK,25},{Cause::PSU,20},{Cause::HARDWARE,10}}},
        {0x00000119, {{Cause::GPU_DRIVER,60},{Cause::OVERCLOCK,25},{Cause::HARDWARE,15}}},
        {0x00000124, {{Cause::OVERCLOCK,45},{Cause::MEMORY,25},{Cause::PSU,15},{Cause::HARDWARE,15}}},
        {0x00000133, {{Cause::DRIVER,55},{Cause::STORAGE,20},{Cause::POWER_MGMT,15},{Cause::MEMORY,10}}},
        {0x00000139, {{Cause::DRIVER,45},{Cause::MEMORY,35},{Cause::SYSFILE,20}}},
        {0x0000013A, {{Cause::DRIVER,45},{Cause::MEMORY,40},{Cause::SYSFILE,15}}},
    };
    return t;
}

// Bilinmeyen bugcheck kodlari icin genel dagilim
static const std::vector<Weighted>& genericPrior() {
    static const std::vector<Weighted> g = {
        {Cause::DRIVER,35},{Cause::MEMORY,30},{Cause::OVERCLOCK,20},{Cause::STORAGE,15}
    };
    return g;
}

struct RankedCause {
    Cause  cause;
    int    percent;
    std::string label;
    std::string action;
    std::string evidence;   // neden bu oran verildi
};

// ============================================================================
//  4. Ayristirma sonucu
// ============================================================================
struct DumpResult {
    bool     valid            = false;
    std::string error;

    std::string signature;          // "PAGE"
    std::string validDumpTag;       // "DU64" / "DUMP"
    bool     is64             = false;
    uint32_t majorVersion     = 0;  // NT build
    uint32_t minorVersion     = 0;
    uint32_t numberProcessors = 0;
    uint32_t machineImageType = 0;
    uint32_t dumpType         = 0;
    uint64_t systemTime       = 0;

    uint32_t bugcheckCode     = 0;
    uint64_t param[4]         = {0,0,0,0};

    std::string versionUser;
    std::string comment;

    std::vector<std::string> drivers;   // dump icinde gecen .sys adlari
    std::string suspectDriver;          // en olasi suclu (varsa)

    std::vector<RankedCause> ranked;    // oranli olasilik listesi
    int  confidence = 0;                // genel teshis guveni (0-100)
    bool lowData    = false;            // veri yetersiz mi
};

// ---- yardimci: UTF-16LE tarayarak ".sys" ile biten adlari topla -------------
//  Cekirdek triage dump'i yuklu surucu adlarini UTF-16LE olarak tasir.
//  DUMP_DRIVER_ENTRY yapisinin tam yerlesimi Windows surumleri arasinda
//  degisebildigi icin, burada yapiya bagimli olmayan saglam bir tarama
//  kullaniyoruz. Yapi tabanli cozumleme --probe ciktisi ile gercek bir
//  dump uzerinde dogrulandiktan sonra eklenebilir.
static std::vector<std::string> scanSysNames(const ByteReader& br) {
    std::set<std::string> found;
    const uint8_t* p = br.raw();
    const size_t n = br.size();

    std::string cur;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint16_t wc = static_cast<uint16_t>(p[i] | (p[i + 1] << 8));
        if (wc >= 0x20 && wc < 0x7F) {
            cur.push_back(static_cast<char>(wc));
            if (cur.size() > 260) cur.erase(cur.begin());
        } else {
            if (cur.size() >= 5) {
                std::string low = cur;
                std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                if (low.size() > 4 && low.compare(low.size() - 4, 4, ".sys") == 0) {
                    // yol varsa sadece dosya adini al
                    size_t slash = low.find_last_of("\\/");
                    std::string name = (slash == std::string::npos)
                                     ? low : low.substr(slash + 1);
                    if (name.size() >= 5 && name.size() <= 64) found.insert(name);
                }
            }
            cur.clear();
        }
    }
    return std::vector<std::string>(found.begin(), found.end());
}

// Windows'un kendi cekirdek bilesenleri: bunlar neredeyse her dump'ta bulunur
// ve "suclu" olarak gosterilmemelidir.
static bool isMicrosoftCore(const std::string& name) {
    static const std::set<std::string> core = {
        "ntoskrnl.sys","ntkrnlmp.sys","hal.sys","halmacpi.sys","kdcom.sys",
        "ci.dll","ci.sys","clfs.sys","cng.sys","msrpc.sys","ndis.sys",
        "netio.sys","tcpip.sys","fltmgr.sys","ksecdd.sys","werkernel.sys",
        "pshed.sys","bootvid.sys","wdf01000.sys","wdfldr.sys","acpi.sys",
        "wmilib.sys","msisadrv.sys","pci.sys","vdrvroot.sys","partmgr.sys",
        "volmgr.sys","volmgrx.sys","mountmgr.sys","storport.sys","ntfs.sys",
        "win32k.sys","win32kbase.sys","win32kfull.sys","dxgkrnl.sys",
        "watchdog.sys","cdd.dll","classpnp.sys","disk.sys","crashdmp.sys",
        "pcw.sys","fs_rec.sys","null.sys","beep.sys","fvevol.sys","rdyboost.sys",
        "iorate.sys","volsnap.sys","spaceport.sys","tm.sys","pdc.sys",
    };
    return core.count(name) > 0;
}

// ---- olasilik hesaplama ----------------------------------------------------
//  On olasiliklari alir, dump'tan cikan kanitlarla duzeltir, %100'e
//  normalize eder ve buyukten kucuge siralar.
static void computeRanking(DumpResult& r) {
    auto it = priorTable().find(r.bugcheckCode);
    const std::vector<Weighted>& prior =
        (it != priorTable().end()) ? it->second : genericPrior();

    bool knownCode = (it != priorTable().end());

    std::map<Cause,int> score;
    std::map<Cause,std::string> why;
    for (const auto& w : prior) {
        score[w.cause] = w.weight;
        why[w.cause]   = knownCode
            ? "bu hata kodunun tipik sebebi"
            : "hata kodu taninmadi, genel dagilim";
    }

    // --- Kanit 1: ucuncu parti surucu adlari -------------------------------
    std::vector<std::string> thirdParty;
    for (const auto& d : r.drivers)
        if (!isMicrosoftCore(d)) thirdParty.push_back(d);

    if (thirdParty.size() == 1) {
        // Tek bir ucuncu parti surucu = guclu isaret
        if (score.count(Cause::DRIVER)) {
            score[Cause::DRIVER] += 20;
            why[Cause::DRIVER] = "dump icinde tek ucuncu parti surucu bulundu: " + thirdParty[0];
        }
    } else if (thirdParty.size() >= 2 && thirdParty.size() <= 5) {
        if (score.count(Cause::DRIVER)) {
            score[Cause::DRIVER] += 8;
            why[Cause::DRIVER] = std::to_string(thirdParty.size()) +
                                 " ucuncu parti surucu dump icinde gecti";
        }
    } else if (thirdParty.empty()) {
        // Hic ucuncu parti surucu yok: surucu ihtimali zayiflar,
        // donanim/bellek ihtimali guclenir.
        if (score.count(Cause::DRIVER)) {
            score[Cause::DRIVER] = std::max(5, score[Cause::DRIVER] - 20);
            why[Cause::DRIVER] = "dump icinde ucuncu parti surucu bulunamadi";
        }
        if (score.count(Cause::MEMORY)) {
            score[Cause::MEMORY] += 10;
            why[Cause::MEMORY] += "; ucuncu parti surucu yok";
        }
    }

    // --- Kanit 2: GPU surucusu dump icinde mi ------------------------------
    static const std::set<std::string> gpuDrivers = {
        "nvlddmkm.sys","amdkmdag.sys","atikmdag.sys","igdkmd64.sys","igdkmdn64.sys"
    };
    for (const auto& d : r.drivers) {
        if (gpuDrivers.count(d)) {
            if (score.count(Cause::GPU_DRIVER)) {
                score[Cause::GPU_DRIVER] += 15;
                why[Cause::GPU_DRIVER] = "GPU surucusu (" + d + ") dump icinde bulundu";
            }
            break;
        }
    }

    // --- Kanit 3: dump turu ------------------------------------------------
    // Triage (kucuk) dump sinirli veri tasir; guveni dusurur.
    if (r.dumpType == DUMP_TYPE_TRIAGE && thirdParty.empty()) r.lowData = true;

    // --- Normalizasyon -----------------------------------------------------
    int total = 0;
    for (const auto& kv : score) total += kv.second;
    if (total <= 0) return;

    for (const auto& kv : score) {
        RankedCause rc;
        rc.cause    = kv.first;
        rc.percent  = static_cast<int>((kv.second * 100.0) / total + 0.5);
        rc.label    = causeInfo(kv.first).label;
        rc.action   = causeInfo(kv.first).action;
        rc.evidence = why[kv.first];
        if (rc.percent > 0) r.ranked.push_back(rc);
    }

    std::sort(r.ranked.begin(), r.ranked.end(),
              [](const RankedCause& a, const RankedCause& b) {
                  return a.percent > b.percent;
              });

    // Yuvarlama farkini en buyuk maddeye ekleyerek toplami 100 yap
    int sum = 0;
    for (const auto& x : r.ranked) sum += x.percent;
    if (!r.ranked.empty() && sum != 100) r.ranked.front().percent += (100 - sum);

    // --- Genel guven -------------------------------------------------------
    // En ustteki madde ne kadar one cikiyorsa guven o kadar yuksek.
    if (!r.ranked.empty()) {
        int top    = r.ranked.front().percent;
        int second = (r.ranked.size() > 1) ? r.ranked[1].percent : 0;
        r.confidence = std::min(95, top + (top - second) / 2);
        if (!knownCode) r.confidence = std::min(r.confidence, 40);
        if (r.lowData)  r.confidence = std::min(r.confidence, 55);
    }
}

// ============================================================================
//  5. Ana ayristirici
// ============================================================================
static DumpResult parseDump(const ByteReader& br) {
    DumpResult r;

    if (br.size() < 0x1000) {
        r.error = "Dosya cok kucuk, gecerli bir dump degil.";
        return r;
    }

    r.signature    = br.tag4(hdr::SIGNATURE);
    r.validDumpTag = br.tag4(hdr::VALID_DUMP);

    if (r.signature != "PAGE") {
        if (r.signature == "MDMP") {
            r.error = "Bu bir KULLANICI-MOD minidump (MDMP). Cekirdek dump bekleniyordu. "
                      "Uygulama cokmesi dosyasi olabilir.";
        } else {
            r.error = "Imza 'PAGE' degil (bulunan: '" + r.signature + "'). Gecerli bir cekirdek dump degil.";
        }
        return r;
    }

    if (r.validDumpTag == "DU64")      r.is64 = true;
    else if (r.validDumpTag == "DUMP") r.is64 = false;
    else {
        r.error = "Bilinmeyen dump etiketi: '" + r.validDumpTag + "'";
        return r;
    }

    if (!r.is64) {
        r.error = "32-bit (x86) dump tespit edildi. Bu surum yalnizca x64 destekliyor.";
        return r;
    }

    r.majorVersion     = br.u32(hdr::MAJOR_VERSION);
    r.minorVersion     = br.u32(hdr::MINOR_VERSION);
    r.machineImageType = br.u32(hdr::MACHINE_IMAGE_TYPE);
    r.numberProcessors = br.u32(hdr::NUMBER_PROCESSORS);
    r.bugcheckCode     = br.u32(hdr::BUGCHECK_CODE);
    r.param[0]         = br.u64(hdr::BUGCHECK_PARAM1);
    r.param[1]         = br.u64(hdr::BUGCHECK_PARAM2);
    r.param[2]         = br.u64(hdr::BUGCHECK_PARAM3);
    r.param[3]         = br.u64(hdr::BUGCHECK_PARAM4);
    r.versionUser      = br.ascii(hdr::VERSION_USER, 32);
    r.dumpType         = br.u32(hdr::DUMP_TYPE);
    r.systemTime       = br.u64(hdr::SYSTEM_TIME);
    r.comment          = br.ascii(hdr::COMMENT, 128);

    // Surucu adlarini tara
    r.drivers = scanSysNames(br);

    // Suclu tahmini: Microsoft cekirdek bilesenlerini eleyip kalanlara bak.
    // NOT: bu bir *ipucu*dur, kanit degildir. Kesin suclu tespiti cagri
    // yiginindan yapilir ve v0.2'de eklenecektir.
    std::vector<std::string> thirdParty;
    for (const auto& d : r.drivers)
        if (!isMicrosoftCore(d)) thirdParty.push_back(d);
    if (thirdParty.size() == 1) r.suspectDriver = thirdParty[0];

    r.valid = true;
    computeRanking(r);
    return r;
}

// ============================================================================
//  6. Ciktilar
// ============================================================================
static std::string hex64(uint64_t v) {
    char b[32]; std::snprintf(b, sizeof(b), "0x%016llX", (unsigned long long)v);
    return b;
}
static std::string hex32(uint32_t v) {
    char b[16]; std::snprintf(b, sizeof(b), "0x%08X", v);
    return b;
}

static const char* dumpTypeName(uint32_t t) {
    switch (t) {
        case DUMP_TYPE_FULL:          return "Full";
        case DUMP_TYPE_SUMMARY:       return "Summary (Kernel)";
        case DUMP_TYPE_HEADER:        return "Header only";
        case DUMP_TYPE_TRIAGE:        return "Triage (Minidump)";
        case DUMP_TYPE_BITMAP_FULL:   return "Bitmap Full";
        case DUMP_TYPE_BITMAP_KERNEL: return "Bitmap Kernel";
        case DUMP_TYPE_AUTOMATIC:     return "Automatic";
        default:                      return "Bilinmiyor";
    }
}

static void printDiagnosis(const DumpResult& r, const std::string& path) {
    std::printf("=====================================================\n");
    std::printf(" Dosya : %s\n", path.c_str());
    std::printf("=====================================================\n\n");

    if (!r.valid) {
        std::printf("  [HATA] %s\n\n", r.error.c_str());
        return;
    }

    std::printf("  Windows build      : %u\n", r.majorVersion);
    std::printf("  Islemci sayisi     : %u\n", r.numberProcessors);
    std::printf("  Dump turu          : %s (%u)\n", dumpTypeName(r.dumpType), r.dumpType);
    if (!r.versionUser.empty())
        std::printf("  Surum bilgisi      : %s\n", r.versionUser.c_str());
    std::printf("\n");

    std::printf("  BUGCHECK           : %s\n", hex32(r.bugcheckCode).c_str());

    auto it = bugcheckTable().find(r.bugcheckCode);
    if (it != bugcheckTable().end()) {
        std::printf("  Teknik ad          : %s\n\n", it->second.name);
        std::printf("  ---------------------------------------------------\n");
        std::printf("  NE OLDU?\n    %s\n\n", it->second.userText);
        std::printf("  NE YAPMALI?\n    %s\n", it->second.action);
        std::printf("  ---------------------------------------------------\n\n");
    } else {
        std::printf("  Teknik ad          : (sozlukte yok)\n\n");
        std::printf("  ---------------------------------------------------\n");
        std::printf("  NE OLDU?\n    Windows cekirdek seviyesinde bir hata ile durdu.\n"
                    "    Bu hata kodu sozlugumuzde kayitli degil.\n\n");
        std::printf("  NE YAPMALI?\n    Hata kodunu (%s) arama motorunda aratin.\n",
                    hex32(r.bugcheckCode).c_str());
        std::printf("  ---------------------------------------------------\n\n");
    }

    std::printf("  Parametreler:\n");
    for (int i = 0; i < 4; ++i)
        std::printf("    %d : %s\n", i + 1, hex64(r.param[i]).c_str());
    std::printf("\n");

    // ---- OLASILIK LISTESI -------------------------------------------------
    if (!r.ranked.empty()) {
        std::printf("  ===================================================\n");
        std::printf("   OLASI SEBEPLER (yukaridan asagi sirayla deneyin)\n");
        std::printf("  ===================================================\n\n");

        int idx = 1;
        for (const auto& rc : r.ranked) {
            // orana gore bar cizimi (her blok ~%4)
            int blocks = (rc.percent + 2) / 4;
            std::string bar(static_cast<size_t>(blocks), '#');

            std::printf("  %d) %%%-3d  %-42s\n", idx, rc.percent, rc.label.c_str());
            std::printf("      %-25s\n", bar.c_str());
            std::printf("      Neden : %s\n", rc.evidence.c_str());
            std::printf("      Yapin : %s\n\n", rc.action.c_str());
            ++idx;
        }

        std::printf("  ---------------------------------------------------\n");
        std::printf("  Teshis guveni: %%%d", r.confidence);
        if (r.confidence >= 70)      std::printf("  (yuksek)\n");
        else if (r.confidence >= 45) std::printf("  (orta)\n");
        else                         std::printf("  (dusuk - tek dump yeterli kanit degil)\n");

        if (r.lowData) {
            std::printf("\n  [NOT] Bu kucuk (triage) bir dump ve icinde ucuncu parti\n");
            std::printf("  surucu bulunamadi. Daha kesin teshis icin CrashDumpEnabled\n");
            std::printf("  degerini 7 (Automatic) yapip bir sonraki mavi ekrani bekleyin.\n");
        }
        if (r.ranked.size() > 1 && r.confidence < 70) {
            std::printf("\n  [NOT] Tek bir mavi ekran kesin sonuc vermez. Ayni hata\n");
            std::printf("  kodu tekrar ederse olasiliklar netlesir.\n");
        }
        std::printf("  ---------------------------------------------------\n\n");
    }

    if (!r.suspectDriver.empty()) {
        std::printf("  SUPHELI SURUCU     : %s\n", r.suspectDriver.c_str());
        std::printf("  (ipucu niteligindedir, kesin kanit degildir)\n\n");
    }

    if (!r.drivers.empty()) {
        std::printf("  Dump icinde gecen surucu adlari (%zu):\n", r.drivers.size());
        size_t shown = 0;
        for (const auto& d : r.drivers) {
            if (isMicrosoftCore(d)) continue;
            std::printf("    - %s\n", d.c_str());
            if (++shown >= 25) { std::printf("    ... (kirpildi)\n"); break; }
        }
        if (shown == 0) std::printf("    (yalnizca Windows cekirdek bilesenleri bulundu)\n");
        std::printf("\n");
    }
}

static void printProbe(const ByteReader& br, const DumpResult& r) {
    std::printf("--- PROBE: ham alan dokumu (gercek dump ile dogrulama icin) ---\n\n");
    struct F { const char* name; size_t off; int width; };
    const F fields[] = {
        {"Signature",          hdr::SIGNATURE,             0},
        {"ValidDump",          hdr::VALID_DUMP,            0},
        {"MajorVersion",       hdr::MAJOR_VERSION,         4},
        {"MinorVersion",       hdr::MINOR_VERSION,         4},
        {"DirectoryTableBase", hdr::DIRECTORY_TABLE_BASE,  8},
        {"PfnDataBase",        hdr::PFN_DATABASE,          8},
        {"PsLoadedModuleList", hdr::PS_LOADED_MODULE_LIST, 8},
        {"PsActiveProcessHead",hdr::PS_ACTIVE_PROC_HEAD,   8},
        {"MachineImageType",   hdr::MACHINE_IMAGE_TYPE,    4},
        {"NumberProcessors",   hdr::NUMBER_PROCESSORS,     4},
        {"BugCheckCode",       hdr::BUGCHECK_CODE,         4},
        {"BugCheckParameter1", hdr::BUGCHECK_PARAM1,       8},
        {"BugCheckParameter2", hdr::BUGCHECK_PARAM2,       8},
        {"BugCheckParameter3", hdr::BUGCHECK_PARAM3,       8},
        {"BugCheckParameter4", hdr::BUGCHECK_PARAM4,       8},
        {"KdDebuggerDataBlock",hdr::KD_DEBUGGER_DATA_BLK,  8},
        {"DumpType",           hdr::DUMP_TYPE,             4},
        {"SystemTime",         hdr::SYSTEM_TIME,           8},
    };
    for (const auto& f : fields) {
        std::printf("  +0x%04zX  %-22s ", f.off, f.name);
        if (f.width == 0)      std::printf("'%s'\n", br.tag4(f.off).c_str());
        else if (f.width == 4) std::printf("%s\n", hex32(br.u32(f.off)).c_str());
        else                   std::printf("%s\n", hex64(br.u64(f.off)).c_str());
    }
    std::printf("\n  Dosya boyutu: %zu bayt (0x%zX)\n", br.size(), br.size());
    std::printf("  Bulunan .sys adedi: %zu\n\n", r.drivers.size());

    std::printf("  Ilk 64 bayt:\n   ");
    for (size_t i = 0; i < 64 && i < br.size(); ++i) {
        std::printf(" %02X", br.raw()[i]);
        if ((i + 1) % 16 == 0) std::printf("\n   ");
    }
    std::printf("\n\n");
}

// ============================================================================
//  7. Dosya yukleme
// ============================================================================
static bool loadFile(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { err = "Dosya acilamadi: " + path; return false; }
    std::streamsize sz = f.tellg();
    if (sz <= 0) { err = "Dosya bos."; return false; }
    // Triage dump'lar tipik olarak 300 KB - 2 MB. Guvenlik icin ust sinir.
    const std::streamsize kMax = 256ll * 1024 * 1024;
    if (sz > kMax) sz = kMax;
    f.seekg(0);
    out.resize(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(out.data()), sz)) { err = "Okuma hatasi."; return false; }
    return true;
}

// ============================================================================
//  8. CrashControl kontrolu (yalnizca Windows)
// ----------------------------------------------------------------------------
//  Bircok oyuncu makinesinde dump kaydi kapalidir. Bu durumda mavi ekran
//  yasansa bile Minidump klasoru bos kalir. Uygulamanin ilk calistiginda
//  bunu kontrol edip duzeltmesi gerekir.
// ============================================================================
#ifdef _WIN32
static void checkCrashControl() {
    HKEY k;
    LONG rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\CrashControl", 0, KEY_READ, &k);
    if (rc != ERROR_SUCCESS) {
        std::printf("CrashControl anahtari okunamadi (hata %ld).\n", rc);
        return;
    }
    DWORD val = 0, sz = sizeof(val), type = 0;
    rc = RegQueryValueExA(k, "CrashDumpEnabled", nullptr, &type,
                          reinterpret_cast<LPBYTE>(&val), &sz);
    RegCloseKey(k);

    if (rc != ERROR_SUCCESS) {
        std::printf("CrashDumpEnabled degeri bulunamadi.\n");
        return;
    }

    const char* desc = "bilinmiyor";
    bool minidumpProduced = false;
    switch (val) {
        case 0: desc = "KAPALI - dump uretilmiyor";      break;
        case 1: desc = "Complete memory dump";  minidumpProduced = true; break;
        case 2: desc = "Kernel memory dump";    minidumpProduced = true; break;
        case 3: desc = "Small memory dump (minidump)"; minidumpProduced = true; break;
        case 7: desc = "Automatic memory dump"; minidumpProduced = true; break;
    }
    std::printf("CrashDumpEnabled = %lu  (%s)\n", (unsigned long)val, desc);

    if (!minidumpProduced) {
        std::printf("\n  [UYARI] Dump kaydi kapali. Mavi ekran yasansa bile\n");
        std::printf("  %%SystemRoot%%\\Minidump klasoru bos kalir ve teshis yapilamaz.\n");
        std::printf("  Cozum: CrashDumpEnabled degerini 7 (Automatic) yapin.\n");
    } else {
        char sysdir[MAX_PATH] = {0};
        GetSystemWindowsDirectoryA(sysdir, MAX_PATH);
        std::printf("Minidump klasoru: %s\\Minidump\n", sysdir);
    }
}
#endif

// ============================================================================
//  9. Selftest — sentetik dump uretip ayristiriciyi dogrular
// ============================================================================
static int selftest() {
    std::printf("--- SELFTEST ---\n\n");
    int failures = 0;
    auto check = [&](const char* what, bool cond) {
        std::printf("  [%s] %s\n", cond ? " OK " : "FAIL", what);
        if (!cond) ++failures;
    };

    // Bilinen degerlerle sahte bir PAGEDU64 basligi kur
    std::vector<uint8_t> buf(hdr::HEADER_SIZE_X64 + 0x1000, 0);
    auto put32 = [&](size_t off, uint32_t v){ std::memcpy(buf.data()+off, &v, 4); };
    auto put64 = [&](size_t off, uint64_t v){ std::memcpy(buf.data()+off, &v, 8); };

    std::memcpy(buf.data() + hdr::SIGNATURE,  "PAGE", 4);
    std::memcpy(buf.data() + hdr::VALID_DUMP, "DU64", 4);
    put32(hdr::MAJOR_VERSION,     26100);       // Windows 11 24H2 build
    put32(hdr::MINOR_VERSION,     1);
    put32(hdr::NUMBER_PROCESSORS, 32);
    put32(hdr::BUGCHECK_CODE,     0x00000133);  // DPC_WATCHDOG_VIOLATION
    put64(hdr::BUGCHECK_PARAM1,   0x0000000000000001ULL);
    put64(hdr::BUGCHECK_PARAM2,   0x0000000000001E00ULL);
    put64(hdr::BUGCHECK_PARAM3,   0xFFFFF80012345678ULL);
    put64(hdr::BUGCHECK_PARAM4,   0x0000000000000000ULL);
    put32(hdr::DUMP_TYPE,         DUMP_TYPE_TRIAGE);
    std::memcpy(buf.data() + hdr::VERSION_USER, "Windows 11 Kernel Version", 25);

    // UTF-16LE surucu adlari goem
    auto putW = [&](size_t off, const char* s) {
        size_t i = 0;
        for (; s[i]; ++i) { buf[off + i*2] = (uint8_t)s[i]; buf[off + i*2 + 1] = 0; }
        buf[off + i*2] = 0; buf[off + i*2 + 1] = 0;
    };
    putW(hdr::HEADER_SIZE_X64 + 0x100, "\\SystemRoot\\System32\\ntoskrnl.sys");
    putW(hdr::HEADER_SIZE_X64 + 0x200, "\\SystemRoot\\System32\\drivers\\rtkvhd64.sys");
    putW(hdr::HEADER_SIZE_X64 + 0x300, "\\SystemRoot\\System32\\drivers\\tcpip.sys");

    ByteReader br(buf);
    DumpResult r = parseDump(br);

    check("Gecerli dump olarak ayristirildi",   r.valid);
    check("Imza PAGE",                          r.signature == "PAGE");
    check("Etiket DU64 ve 64-bit",              r.is64 && r.validDumpTag == "DU64");
    check("Build 26100 okundu",                 r.majorVersion == 26100);
    check("Islemci sayisi 32",                  r.numberProcessors == 32);
    check("Bugcheck 0x133 (ofset 0x38)",        r.bugcheckCode == 0x133);
    check("Parametre1 dogru",                   r.param[0] == 0x1ULL);
    check("Parametre2 dogru",                   r.param[1] == 0x1E00ULL);
    check("Parametre3 dogru (ofset 0x50)",      r.param[2] == 0xFFFFF80012345678ULL);
    check("DumpType = Triage",                  r.dumpType == DUMP_TYPE_TRIAGE);
    check("Bugcheck sozlukte bulundu",
          bugcheckTable().count(r.bugcheckCode) > 0);

    bool foundRtk = std::find(r.drivers.begin(), r.drivers.end(), "rtkvhd64.sys") != r.drivers.end();
    bool foundNt  = std::find(r.drivers.begin(), r.drivers.end(), "ntoskrnl.sys") != r.drivers.end();
    check("UTF-16 tarama rtkvhd64.sys buldu",   foundRtk);
    check("UTF-16 tarama ntoskrnl.sys buldu",   foundNt);
    check("Microsoft bilesenleri elendi, tek suphe rtkvhd64.sys",
          r.suspectDriver == "rtkvhd64.sys");

    // ---- Olasilik motoru testleri ----
    check("Olasilik listesi uretildi", !r.ranked.empty());
    {
        int sum = 0;
        for (const auto& rc : r.ranked) sum += rc.percent;
        check("Oranlar toplami tam %100", sum == 100);
    }
    {
        bool desc = true;
        for (size_t i = 1; i < r.ranked.size(); ++i)
            if (r.ranked[i-1].percent < r.ranked[i].percent) desc = false;
        check("Oranlar buyukten kucuge sirali", desc);
    }
    check("0x133'te en olasi sebep surucu",
          !r.ranked.empty() && r.ranked.front().cause == Cause::DRIVER);
    check("Tek ucuncu parti surucu bulunca guven >= %50", r.confidence >= 50);

    // Ucuncu parti surucu YOKKEN bellek ihtimali one cikmali
    {
        std::vector<uint8_t> b2 = buf;
        // rtkvhd64 adini bozarak ucuncu parti surucuyu kaldir
        for (size_t i = hdr::HEADER_SIZE_X64 + 0x200; i < hdr::HEADER_SIZE_X64 + 0x260; ++i)
            b2[i] = 0;
        DumpResult r2 = parseDump(ByteReader(b2));
        int drvPct = 0, memPct = 0;
        for (const auto& rc : r2.ranked) {
            if (rc.cause == Cause::DRIVER) drvPct = rc.percent;
            if (rc.cause == Cause::MEMORY) memPct = rc.percent;
        }
        check("Surucu yoksa surucu orani dusuyor", drvPct < 55);
        check("Surucu yoksa bellek orani yukseliyor", memPct > 10);
        check("Veri yetersiz bayragi kalkti", r2.lowData);
    }

    // Bilinmeyen bugcheck kodunda guven dusuk olmali
    {
        std::vector<uint8_t> b3 = buf;
        uint32_t weird = 0x00ABCDEF;
        std::memcpy(b3.data() + hdr::BUGCHECK_CODE, &weird, 4);
        DumpResult r3 = parseDump(ByteReader(b3));
        check("Bilinmeyen kodda guven <= %40", r3.confidence <= 40);
        check("Bilinmeyen kodda yine de oneri veriliyor", !r3.ranked.empty());
    }

    // WHEA 0x124'te overclock ilk sirada olmali (vaka 1'deki CO senaryosu)
    {
        std::vector<uint8_t> b4 = buf;
        uint32_t whea = 0x00000124;
        std::memcpy(b4.data() + hdr::BUGCHECK_CODE, &whea, 4);
        DumpResult r4 = parseDump(ByteReader(b4));
        bool ocTop = !r4.ranked.empty() && r4.ranked.front().cause == Cause::OVERCLOCK;
        check("0x124'te overclock/undervolt ilk sirada", ocTop);
    }

    // Negatif testler
    {
        std::vector<uint8_t> bad(0x2000, 0);
        std::memcpy(bad.data(), "MDMP", 4);
        DumpResult br2 = parseDump(ByteReader(bad));
        check("Kullanici-mod MDMP reddedildi", !br2.valid);
    }
    {
        std::vector<uint8_t> tiny(16, 0);
        DumpResult br3 = parseDump(ByteReader(tiny));
        check("Cok kucuk dosya reddedildi", !br3.valid);
    }
    {
        std::vector<uint8_t> x86(0x2000, 0);
        std::memcpy(x86.data(), "PAGE", 4);
        std::memcpy(x86.data() + 4, "DUMP", 4);
        DumpResult br4 = parseDump(ByteReader(x86));
        check("x86 dump nazikce reddedildi", !br4.valid && br4.signature == "PAGE");
    }

    std::printf("\n  Ornek teshis ciktisi:\n\n");
    printDiagnosis(r, "<sentetik>");

    std::printf("--- SONUC: %s (%d hata) ---\n",
                failures == 0 ? "TUM TESTLER GECTI" : "BASARISIZ", failures);
    return failures == 0 ? 0 : 1;
}

// ============================================================================
//  9b. Kutuphane arayuzu — makinedeki dumplari otomatik bul ve ayristir
// ----------------------------------------------------------------------------
//  Bu dosya iki kez derlenir: bir kez bagimsiz exe olarak (main ile), bir kez
//  SS_DUMP_LIB tanimliyken kutuphane olarak (main'siz). Ayni ceviri biriminde
//  oldugu icin yukaridaki 'static' ayristirici yardimcilarini dogrudan
//  kullanabiliyoruz — parseDump'i disa acmak icin dosyayi bolmeye gerek yok.
// ============================================================================
#ifdef _WIN32
#include "dump_scan.h"

namespace ssdump {
namespace {

bool crashDumpEnabled() {
    HKEY k;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Control\\CrashControl",
            0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD val = 0, sz = sizeof(val);
    const bool ok = RegQueryValueExA(k, "CrashDumpEnabled", nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(&val), &sz)
                    == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok && val != 0;
}

std::string systemRoot() {
    char buf[MAX_PATH];
    const UINT n = GetWindowsDirectoryA(buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::string(buf, n) : std::string("C:\\Windows");
}

// Donen deger: Win32 hata kodu (0 = sorunsuz). Cagiran ERROR_ACCESS_DENIED
// ile ERROR_FILE_NOT_FOUND'u AYIRMAK zorunda — biri "okuyamadim", digeri
// "yok" demek ve ikisi bambaska hukumler dogurur.
DWORD collect(const std::string& pattern, std::vector<DumpFinding>& out) {
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();

    const std::string dir = pattern.substr(0, pattern.find_last_of('\\') + 1);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        DumpFinding f;
        f.fileName = fd.cFileName;
        f.path     = dir + fd.cFileName;
        f.fileTimeUtc =
            (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) |
            fd.ftLastWriteTime.dwLowDateTime;
        out.push_back(std::move(f));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}

void parseInto(DumpFinding& f) {
    std::vector<uint8_t> bytes;
    std::string err;
    if (!loadFile(f.path, bytes, err)) { f.error = err; return; }

    ByteReader br(std::move(bytes));
    const DumpResult r = parseDump(br);
    if (!r.valid) { f.error = r.error; return; }

    f.parsed        = true;
    f.bugcheckCode  = r.bugcheckCode;
    f.suspectDriver = r.suspectDriver;
    f.confidence    = r.confidence;

    const auto& table = bugcheckTable();
    auto it = table.find(r.bugcheckCode);
    if (it != table.end()) {
        f.bugcheckName    = it->second.name;
        f.bugcheckMeaning = it->second.userText;
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%08X", r.bugcheckCode);
        f.bugcheckName    = buf;
        f.bugcheckMeaning = "Bu durdurma kodu tabloda yok; ham kod gosteriliyor.";
    }

    for (const RankedCause& c : r.ranked)
        f.ranked.push_back({c.percent, c.label, c.action, c.evidence});
}

} // namespace

DumpScan scanSystemDumps(size_t limit) {
    DumpScan scan;
    scan.dumpsEnabled = crashDumpEnabled();

    const std::string root = systemRoot();
    const std::string miniDir = root + "\\Minidump";

    const DWORD rcMini = collect(miniDir + "\\*.dmp", scan.findings);
    collect(root + "\\MEMORY.DMP", scan.findings);

    if (rcMini == ERROR_ACCESS_DENIED) {
        scan.accessDenied = true;
    } else if (rcMini == ERROR_PATH_NOT_FOUND || rcMini == ERROR_FILE_NOT_FOUND) {
        // Klasor yoksa gercekten kayit yok; klasor VARSA ama bos ise de
        // FindFirstFile ERROR_FILE_NOT_FOUND doner — ikisi de "kayit yok"
        // demek, ayirmaya gerek yok.
        const DWORD attr = GetFileAttributesA(miniDir.c_str());
        scan.folderMissing = (attr == INVALID_FILE_ATTRIBUTES);
    }

    // En yeni once
    std::sort(scan.findings.begin(), scan.findings.end(),
              [](const DumpFinding& a, const DumpFinding& b) {
                  return a.fileTimeUtc > b.fileTimeUtc;
              });

    const size_t total = scan.findings.size();
    if (scan.findings.size() > limit) scan.findings.resize(limit);

    for (DumpFinding& f : scan.findings) parseInto(f);

    if (scan.accessDenied) {
        // Buraya "kayit yok" YAZMIYORUZ. Bilmiyoruz.
        scan.note = "Mavi ekran kayıtları OKUNAMADI. Windows bu klasörü "
                    "yalnızca yönetici haklarıyla açtırıyor; programı "
                    "yönetici olarak çalıştırmadan bu soruya cevap "
                    "veremiyoruz.";
    } else if (total == 0) {
        scan.note = scan.dumpsEnabled
            ? "Mavi ekran kaydı bulunamadı. Bu iyi haber."
            : "Mavi ekran kaydı bulunamadı. DİKKAT: dump kaydı KAPALI, yani "
              "mavi ekran yaşansanız bile kayıt oluşmayacak.";
    } else {
        scan.note = std::to_string(total) + " mavi ekran kaydı bulundu";
        if (total > limit)
            scan.note += " (en yeni " + std::to_string(limit) + " tanesi okundu)";
        if (!scan.dumpsEnabled)
            scan.note += ". Dump kaydı şu anda KAPALI — yeni mavi ekranlar "
                         "kaydedilmeyecek.";
        else
            scan.note += ".";
    }
    return scan;
}

} // namespace ssdump
#endif // _WIN32

// ============================================================================
//  10. main
// ============================================================================
#ifndef SS_DUMP_LIB
int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf(
            "StutterScope dump okuyucu\n\n"
            "Kullanim:\n"
            "  %s <dosya.dmp>            Teshis ciktisi\n"
            "  %s <dosya.dmp> --probe    Ham alan dokumu (dogrulama icin)\n"
            "  %s --selftest             Sentetik dump ile kendini test et\n"
#ifdef _WIN32
            "  %s --check-crashcontrol   Dump kaydi acik mi?\n"
#endif
            , argv[0], argv[0], argv[0]
#ifdef _WIN32
            , argv[0]
#endif
        );
        return 1;
    }

    std::string arg1 = argv[1];

    if (arg1 == "--selftest") return selftest();

#ifdef _WIN32
    if (arg1 == "--check-crashcontrol") { checkCrashControl(); return 0; }
#endif

    std::vector<uint8_t> data;
    std::string err;
    if (!loadFile(arg1, data, err)) {
        std::printf("[HATA] %s\n", err.c_str());
        return 2;
    }

    ByteReader br(std::move(data));
    DumpResult r = parseDump(br);

    printDiagnosis(r, arg1);

    if (argc >= 3 && std::string(argv[2]) == "--probe") printProbe(br, r);

    return r.valid ? 0 : 3;
}
#endif // SS_DUMP_LIB
