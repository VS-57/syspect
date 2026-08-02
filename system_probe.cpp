// ============================================================================
//  StutterScope — sistem yoklamasi
// ============================================================================
#ifdef _WIN32

#include "system_probe.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <powrprof.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>

#include <cstdio>

namespace ssprobe {
namespace {

std::string toUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string o(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, o.data(), n, nullptr, nullptr);
    return o;
}

// Windows'un yerlesik plan GUID'leri
const GUID kPowerSaver = {0xa1841308, 0x3541, 0x4fab,
                          {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};
const GUID kBalanced   = {0x381b4222, 0xf694, 0x41f0,
                          {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
const GUID kHighPerf   = {0x8c5e7fda, 0xe8bf, 0x4a96,
                          {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
const GUID kUltimate   = {0xe9a42b02, 0xd5df, 0x448d,
                          {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};

// CM_PROB_* kodlarindan kullaniciya donuk metin. Tam liste uzun; oyun
// performansiyla ilgisi olanlari aciyoruz, gerisi genel metne dusuyor.
std::string problemText(uint32_t code, bool& driverMissing) {
    driverMissing = false;
    switch (code) {
        case 1:  driverMissing = true;
                 return "Aygit yanlis yapilandirilmis — surucu yeniden kurulmali.";
        case 3:  return "Surucu bozuk olabilir ya da sistem bellegi yetersiz.";
        case 10: return "Aygit baslatilamiyor. Surucuyu guncelleyin.";
        case 12: return "Yeterli boş kaynak bulunamadi (IRQ/bellek cakismasi).";
        case 14: return "Degisikligin etkili olmasi icin bilgisayar yeniden baslatilmali.";
        case 18: driverMissing = true;
                 return "Surucunun yeniden kurulmasi gerekiyor.";
        case 19: return "Kayit defteri bilgisi bozuk. Surucuyu kaldirip yeniden kurun.";
        case 22: return "Aygit devre disi birakilmis.";
        case 24: return "Aygit yok ya da duzgun calismiyor.";
        case 28: driverMissing = true;
                 return "SURUCU YUKLU DEGIL. Bu aygit icin surucu hic kurulmamis.";
        case 31: driverMissing = true;
                 return "Windows bu aygit icin gerekli suruculeri yukleyemiyor.";
        case 43: return "Windows aygiti durdurdu cunku aygit sorun bildirdi.";
        case 45: return "Aygit su anda bilgisayara bagli degil.";
        default: {
            char b[96];
            std::snprintf(b, sizeof(b),
                          "Aygit yoneticisi %u numarali sorunu bildiriyor.", code);
            return b;
        }
    }
}

std::string deviceProperty(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD prop) {
    DWORD size = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, prop, nullptr, nullptr, 0, &size);
    if (size == 0) return {};
    std::vector<BYTE> buf(size + sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, prop, nullptr,
                                           buf.data(), size, nullptr)) return {};
    return toUtf8(reinterpret_cast<const wchar_t*>(buf.data()));
}

} // namespace

// ============================================================================
PowerInfo readPowerPlan() {
    PowerInfo info;

    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        info.onBattery = (sps.ACLineStatus == 0);
        // BatteryFlag 128 = sistemde pil YOK. 255 = durum bilinmiyor; o
        // durumda pil var demiyoruz — bilmiyoruz.
        info.hasBattery = (sps.BatteryFlag != 128 && sps.BatteryFlag != 255);
    }

    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active)
        return info;

    DWORD size = 0;
    PowerReadFriendlyName(nullptr, active, nullptr, nullptr, nullptr, &size);
    if (size > 0) {
        std::vector<UCHAR> buf(size + 2, 0);
        if (PowerReadFriendlyName(nullptr, active, nullptr, nullptr,
                                  buf.data(), &size) == ERROR_SUCCESS) {
            info.friendlyName =
                toUtf8(reinterpret_cast<const wchar_t*>(buf.data()));
        }
    }

    if      (IsEqualGUID(*active, kPowerSaver)) info.plan = PowerPlan::PowerSaver;
    else if (IsEqualGUID(*active, kBalanced))   info.plan = PowerPlan::Balanced;
    else if (IsEqualGUID(*active, kHighPerf))   info.plan = PowerPlan::HighPerformance;
    else if (IsEqualGUID(*active, kUltimate))   info.plan = PowerPlan::Ultimate;
    else                                        info.plan = PowerPlan::Custom;

    LocalFree(active);

    switch (info.plan) {
        case PowerPlan::PowerSaver:
            info.shouldWarn = true;
            info.warning = "Güç planı \"Güç tasarrufu\". Bu plan işlemci "
                           "frekansını bilerek düşük tutar ve çekirdekleri "
                           "park eder — oyunda doğrudan FPS kaybı ve takılma "
                           "üretir.";
            info.action  = "Denetim Masası > Güç Seçenekleri'nden \"Yüksek "
                           "performans\" planına geçin.";
            break;

        case PowerPlan::Balanced:
            // "Dengeli" bozuk bir ayar DEGILDIR; masaustunde dogru secimdir.
            // Ama cekirdek park etme ve frekans dalgalanmasi acik oldugu icin
            // takilma sikayeti VARSA denemeye deger bir degisken.
            info.shouldWarn = true;
            info.warning = "Güç planı \"Dengeli\". Bu Windows'un varsayılanı ve "
                           "çoğu makinede sorun çıkarmaz; ancak çekirdek park "
                           "etme ve frekans dalgalanması açıktır. Takılma "
                           "şikâyetiniz varsa denenmesi ucuz bir değişkendir.";
            info.action  = "\"Yüksek performans\" planına geçip bir gün "
                           "kullanın. Fark görmezseniz geri alın — kalıcı "
                           "zararı yoktur.";
            break;

        case PowerPlan::Custom:
            info.shouldWarn = true;
            info.warning = "Üreticiye özel bir güç planı etkin (" +
                           info.friendlyName + "). Bu planlar genelde sessizlik "
                           "veya pil ömrü için performansı kısar.";
            info.action  = "Windows'un \"Yüksek performans\" planına geçip "
                           "farkı ölçün.";
            break;

        default:
            break;
    }

    // Pilde calisan bir dizustu, plan ne olursa olsun kisitlanir.
    if (info.onBattery) {
        info.shouldWarn = true;
        info.warning = "Bilgisayar PİLDEN çalışıyor. Dizüstülerde pil modunda "
                       "işlemci ve ekran kartı güç limitleri ciddi biçimde "
                       "düşürülür; bu tek başına yarı yarıya performans kaybı "
                       "anlamına gelebilir." +
                       std::string(info.warning.empty() ? "" : " ") +
                       info.warning;
        info.action = "Şarj adaptörünü takıp ölçümü tekrarlayın.";
    }

    return info;
}

// ============================================================================
DeviceScan scanProblemDevices() {
    DeviceScan scan;

    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
                                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        scan.note = "Aygıt listesi okunamadı.";
        return scan;
    }

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        ++scan.totalDevices;

        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0) != CR_SUCCESS)
            continue;
        if (!(status & DN_HAS_PROBLEM)) continue;

        // Kod 45 "su anda bagli degil" — DIGCF_PRESENT ile zaten gelmemeli,
        // gelirse gurultudur, atliyoruz.
        if (problem == 45) continue;

        ProblemDevice d;
        d.problemCode = static_cast<uint32_t>(problem);
        d.problemText = problemText(d.problemCode, d.driverMissing);
        d.name = deviceProperty(set, data, SPDRP_FRIENDLYNAME);
        if (d.name.empty()) d.name = deviceProperty(set, data, SPDRP_DEVICEDESC);
        if (d.name.empty()) d.name = "Bilinmeyen aygıt";
        d.cls = deviceProperty(set, data, SPDRP_CLASS);

        scan.problems.push_back(std::move(d));
    }
    SetupDiDestroyDeviceInfoList(set);

    size_t missing = 0;
    for (const ProblemDevice& d : scan.problems) if (d.driverMissing) ++missing;

    if (scan.problems.empty()) {
        scan.note = "Sürücüsü eksik veya sorunlu aygıt yok.";
    } else {
        scan.note = std::to_string(scan.problems.size()) +
                    " aygıt sorun bildiriyor";
        if (missing > 0)
            scan.note += " — bunlardan " + std::to_string(missing) +
                         " tanesinde sürücü hiç yüklü değil";
        scan.note += ".";
    }
    return scan;
}

// ============================================================================
//  Ureti yazilimi ve guvenlik ayarlari
// ============================================================================
namespace {

// DWORD degeri okur. Anahtar ya da deger yoksa false doner — bu "kapali"
// DEGIL "bilinmiyor" demektir ve cagiran ikisini ayirmak zorundadir.
bool readDword(HKEY root, const wchar_t* path, const wchar_t* name,
               DWORD& out) {
    HKEY k;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    DWORD size = sizeof(DWORD), type = 0;
    const bool ok = RegQueryValueExW(k, name, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(&out), &size)
                    == ERROR_SUCCESS && type == REG_DWORD;
    RegCloseKey(k);
    return ok;
}

FirmwareInfo::Tri triFromDword(HKEY root, const wchar_t* path,
                               const wchar_t* name, DWORD onValue = 1) {
    DWORD v = 0;
    if (!readDword(root, path, name, v)) return FirmwareInfo::Tri::Unknown;
    return (v == onValue) ? FirmwareInfo::Tri::On : FirmwareInfo::Tri::Off;
}

} // namespace

FirmwareInfo readFirmwareInfo() {
    FirmwareInfo f;

    // --- UEFI mi eski BIOS mu ---
    FIRMWARE_TYPE ft{};
    if (GetFirmwareType(&ft)) {
        f.firmwareKnown = true;
        f.uefi = (ft == FirmwareTypeUefi);
    }

    // --- Secure Boot ---
    // Confirm-SecureBootUEFI yonetici ister; bu anahtar istemez.
    f.secureBoot = triFromDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
        L"UEFISecureBootEnabled");

    // --- Bellek Butunlugu (HVCI) ---
    // "Enabled" ayarin ISTENEN halini, "WasEnabledBy" calisir halini
    // gosterir. Istenen degeri okuyoruz: kullanicinin gordugu anahtar bu.
    f.memoryIntegrity = triFromDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
        L"HypervisorEnforcedCodeIntegrity",
        L"Enabled");

    f.vbs = triFromDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
        L"EnableVirtualizationBasedSecurity");

    // --- Donanim hizlandirmali GPU zamanlama --- (2 = acik)
    f.gpuScheduling = triFromDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
        L"HwSchMode", 2);

    // --- Oyun Modu --- kullanici basina ayar
    f.gameMode = triFromDword(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled");

    return f;
}

// ============================================================================
//  SMBIOS Type 17 — bellek modulleri
// ============================================================================
namespace {

#pragma pack(push, 1)
struct SmbiosHeader { uint8_t type; uint8_t length; uint16_t handle; };
#pragma pack(pop)

// SMBIOS string tablosu: yapinin formatlı alanindan sonra, 1'den baslayan
// indislerle, cift NUL ile biten dizi.
std::string smbiosString(const uint8_t* strTable, const uint8_t* end, uint8_t index) {
    if (index == 0) return {};
    const uint8_t* p = strTable;
    for (uint8_t i = 1; p < end; ++i) {
        const char* s = reinterpret_cast<const char*>(p);
        const size_t len = strnlen(s, static_cast<size_t>(end - p));
        if (i == index) {
            std::string out(s, len);
            while (!out.empty() && (out.back() == ' ' || out.back() == '\0'))
                out.pop_back();
            return out;
        }
        p += len + 1;
        if (p < end && *p == 0) break;   // tablo sonu
    }
    return {};
}

const char* memoryTypeName(uint8_t t) {
    switch (t) {
        case 0x18: return "DDR3";
        case 0x1A: return "DDR4";
        case 0x22: return "DDR5";
        case 0x23: return "LPDDR4";
        case 0x24: return "LPDDR5";
        default:   return "";
    }
}

} // namespace

// ----------------------------------------------------------------------------
//  Platforma gore niteleme
// ----------------------------------------------------------------------------
//  Bellek hizi Ryzen'de Infinity Fabric'e bagli oldugu icin oyun performansini
//  Intel'dekinden belirgin daha cok etkiler. Ikisi de gercek, siddetleri farkli
//  — metin bunu soylemeli. Uretici okunamadiysa hicbir marka adi gecmez.
const char* memorySpeedImpactNote(MemorySpec::Cpu v) {
    switch (v) {
        case MemorySpec::Cpu::Amd:
            return " Ryzen sistemlerde bellek hızı Infinity Fabric'e bağlı "
                   "olduğu için fark özellikle büyüktür.";
        case MemorySpec::Cpu::Intel:
            return " Intel sistemlerde kayıp daha küçüktür ama ölçülebilir "
                   "düzeydedir.";
        default:
            return "";
    }
}

const char* singleChannelImpactNote(MemorySpec::Cpu v) {
    switch (v) {
        case MemorySpec::Cpu::Amd:
            return " — AMD işlemcilerde kayıp özellikle ağırdır";
        case MemorySpec::Cpu::Intel:
            return " — Intel'de kayıp yaklaşık yarı şiddettedir ama yine de "
                   "belirgindir";
        default:
            return "";
    }
}

MemorySpec readMemorySpec() {
    MemorySpec spec;

    const DWORD size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (size == 0) return spec;

    std::vector<uint8_t> buf(size);
    if (GetSystemFirmwareTable('RSMB', 0, buf.data(), size) != size) return spec;

    // RawSMBIOSData basligi: 8 bayt, ardindan tablo verisi
    if (buf.size() < 8) return spec;
    const uint32_t tableLen = *reinterpret_cast<const uint32_t*>(buf.data() + 4);
    const uint8_t* p   = buf.data() + 8;
    const uint8_t* end = p + std::min<size_t>(tableLen, buf.size() - 8);

    while (p + sizeof(SmbiosHeader) <= end) {
        const auto* h = reinterpret_cast<const SmbiosHeader*>(p);
        if (h->length < sizeof(SmbiosHeader)) break;

        const uint8_t* fmt   = p;
        const uint8_t* strs  = p + h->length;

        // Yapinin sonunu bul (cift NUL)
        const uint8_t* q = strs;
        while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) ++q;
        const uint8_t* next = (q + 2 <= end) ? q + 2 : end;

        if (h->type == 17 && h->length >= 0x1B) {
            MemoryModule m;
            const uint16_t sz = *reinterpret_cast<const uint16_t*>(fmt + 0x0C);
            if (sz == 0 || sz == 0xFFFF) { p = next; continue; }   // bos slot

            if (sz == 0x7FFF && h->length >= 0x20) {
                m.sizeMb = *reinterpret_cast<const uint32_t*>(fmt + 0x1C);
            } else {
                // 15. bit: 0 = MB, 1 = KB
                m.sizeMb = (sz & 0x8000) ? (sz & 0x7FFF) / 1024 : (sz & 0x7FFF);
            }

            m.memoryType  = fmt[0x12];
            m.maxSpeedMTs = *reinterpret_cast<const uint16_t*>(fmt + 0x15);
            if (h->length >= 0x22)
                m.configuredMTs = *reinterpret_cast<const uint16_t*>(fmt + 0x20);

            m.locator      = smbiosString(strs, next, fmt[0x10]);
            m.manufacturer = smbiosString(strs, next, fmt[0x17]);
            m.partNumber   = smbiosString(strs, next, fmt[0x1A]);

            spec.totalMb += m.sizeMb;
            if (m.configuredMTs > spec.configuredMTs) spec.configuredMTs = m.configuredMTs;
            if (m.maxSpeedMTs  > spec.maxSpeedMTs)    spec.maxSpeedMTs   = m.maxSpeedMTs;
            if (spec.typeName.empty()) spec.typeName = memoryTypeName(m.memoryType);

            spec.modules.push_back(std::move(m));
        }
        p = next;
    }

    spec.singleChannelRisk = (spec.modules.size() == 1);

    // ------------------------------------------------------------------
    //  Karisik takim tespiti
    // ------------------------------------------------------------------
    //  Uc olcut: farkli anma hizi, farkli boyut, farkli tip. Ucu de SMBIOS'ta
    //  dogrudan okunur — cikarim degil, karsilastirmadir.
    bool mixedSpeeds = false;
    if (spec.modules.size() >= 2) {
        const auto& first = spec.modules.front();
        bool sizeDiff = false, typeDiff = false;

        for (const auto& m : spec.modules) {
            if (m.maxSpeedMTs != first.maxSpeedMTs) mixedSpeeds = true;
            if (m.sizeMb      != first.sizeMb)      sizeDiff    = true;
            if (m.memoryType  != first.memoryType)  typeDiff    = true;
        }

        spec.mixedModules = mixedSpeeds || sizeDiff || typeDiff;
        if (spec.mixedModules) {
            std::string parts;
            if (mixedSpeeds) parts  = "farklı hız";
            if (sizeDiff)    parts += (parts.empty() ? "" : ", ") + std::string("farklı kapasite");
            if (typeDiff)    parts += (parts.empty() ? "" : ", ") + std::string("farklı tip");
            spec.mixedModulesNote =
                "Takılı modüller birbirinin aynısı değil (" + parts + "). "
                "Bellek denetleyicisi en yavaş modüle iner ve iki takımın "
                "zamanlamaları uyuşmadığı için kararsızlık olasılığı belirgin "
                "artar. Modülleri TEK TEK test etmek bu durumu ELEMEZ — sorun "
                "modüllerin kendisinde değil, birlikte çalışmalarındadır.";
        }
    }

    // AMD platformlari EXPO, Intel XMP diyor. Islemci adindan ayirt ediyoruz.
    {
        HKEY k;
        std::string cpu;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                0, KEY_READ, &k) == ERROR_SUCCESS) {
            wchar_t b[256];
            DWORD bs = sizeof(b);
            if (RegQueryValueExW(k, L"ProcessorNameString", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(b), &bs) == ERROR_SUCCESS)
                cpu = toUtf8(b);
            RegCloseKey(k);
        }
        const bool amd = cpu.find("AMD")   != std::string::npos ||
                         cpu.find("Ryzen") != std::string::npos;
        const bool intel = cpu.find("Intel") != std::string::npos ||
                           cpu.find("Core")  != std::string::npos;

        if (amd)        spec.cpuVendor = MemorySpec::Cpu::Amd;
        else if (intel) spec.cpuVendor = MemorySpec::Cpu::Intel;

        // Etiket: uretici bilinmiyorsa "XMP" daha genel bir ad oldugu icin
        // varsayilan olarak kalir — kullaniciya "EXPO" demekten daha az yanlis.
        spec.profileLabel = amd ? "EXPO" : "XMP";

        // Platform cikarimi. SMBIOS "AM5" diye bir alan tasimaz; DDR5 + AMD
        // masaustu birlesimi pratikte AM5 demektir. HEDT hatlari (Threadripper,
        // EPYC) ayri bir bellek denetleyicisi kullanir, disarida birakilir.
        const bool ddr5 = !spec.modules.empty() &&
                          (spec.modules.front().memoryType == 0x22 ||
                           spec.modules.front().memoryType == 0x24);
        const bool hedt = cpu.find("Threadripper") != std::string::npos ||
                          cpu.find("EPYC")         != std::string::npos ||
                          cpu.find("Xeon")         != std::string::npos;

        spec.likelyAM5        = amd   && ddr5 && !hedt;
        spec.likelyIntelDdr5  = intel && ddr5 && !hedt;
    }

    // Hukum: yapilandirilmis hiz modulun destekledigi hizin altindaysa profil
    // uygulanmamis demektir. Bu bir tahmin degil, iki olculmus degerin farki.
    //
    // TEK ISTISNA — farkli hizda moduller. O durumda dusuk calisma hizi profil
    // kapali oldugu icin DEGIL, yavas modul hizli olanin anma hizina cikamadigi
    // icindir. Ayrimi yapmadan "profili acin" demek sistemi calisir halden
    // cikarabilir; tasarim kurali 3 geregi burada susuyoruz.
    if (mixedSpeeds) {
        spec.profile = MemorySpec::Profile::Unknown;
        spec.profileNote =
            "Modüllerin anma hızları farklı olduğu için " + spec.profileLabel +
            " durumu hakkında hüküm verilemiyor: düşük çalışma hızı profil "
            "kapalı olduğu için değil, yavaş modülün hızlı olanın hızına "
            "çıkamamasından kaynaklanıyor olabilir. " + spec.mixedModulesNote;
    } else if (spec.configuredMTs > 0 && spec.maxSpeedMTs > 0) {
        if (spec.configuredMTs + 100 < spec.maxSpeedMTs) {
            spec.profile = MemorySpec::Profile::Off;
            spec.profileNote =
                spec.profileLabel + " KAPALI görünüyor. Modüller " +
                std::to_string(spec.maxSpeedMTs) + " MT/s destekliyor ama " +
                std::to_string(spec.configuredMTs) + " MT/s'de çalışıyor. "
                "BIOS'ta profili açmak oyunlarda belirgin fark yaratır." +
                memorySpeedImpactNote(spec.cpuVendor);
        } else {
            spec.profile = MemorySpec::Profile::On;
            spec.profileNote =
                spec.profileLabel + " açık görünüyor: modüller " +
                std::to_string(spec.configuredMTs) + " MT/s'de, yani "
                "destekledikleri hızda çalışıyor.";
        }
    } else {
        spec.profileNote =
            "Bellek hızı SMBIOS'tan okunamadı; profil durumu belirlenemiyor.";
    }

    if (spec.singleChannelRisk) {
        spec.profileNote += " DİKKAT: tek bellek modülü takılı. Tek kanal "
                            "oyunlarda ciddi FPS kaybına yol açar" +
                            std::string(singleChannelImpactNote(spec.cpuVendor)) +
                            "; ikinci modül eklemek en ucuz iyileştirmedir.";
    }
    return spec;
}

} // namespace ssprobe

#endif // _WIN32
