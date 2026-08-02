// ============================================================================
//  Syspect — depolama envanteri (Windows)
//  ----------------------------------------------------------------------
//  Uc ayri kaynak birlestirilir:
//
//    1) IOCTL_STORAGE_QUERY_PROPERTY / StorageDeviceProperty
//         model, veri yolu (SATA / NVMe / USB)
//    2) IOCTL_STORAGE_QUERY_PROPERTY / StorageDeviceSeekPenaltyProperty
//         donen disk mi (arama cezasi varsa HDD)
//    3) IOCTL_STORAGE_PREDICT_FAILURE  +  NVMe SMART log sayfasi 0x02
//         ureticinin ariza tahmini ve asinma
//
//  Bolumlerin hangi fiziksel diske ait oldugu IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS
//  ile bulunur — bir bolum birden fazla diske yayilmis olabilir (span/RAID),
//  o durumda ilk diske sayilir.
// ============================================================================
#ifdef _WIN32

#include "storage_probe.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>

#include <algorithm>

namespace ssstore {
namespace {

// ----------------------------------------------------------------------------
//  NVMe SMART / Health Information — log sayfasi 0x02
// ----------------------------------------------------------------------------
//  Yapi NVMe belirtiminde tanimli, 512 bayt. Bizi ilgilendiren alanlar:
//    0x00  kritik uyari bayraklari
//    0x01  bilesik sicaklik (Kelvin, 2 bayt)
//    0x03  kalan yedek blok yuzdesi
//    0x05  kullanilan omur yuzdesi
//    0x30  yazilan veri birimi (16 bayt, birim = 512.000 bayt)
//    0x80  calisma saati (16 bayt)
struct NvmeHealth {
    uint8_t  criticalWarning;
    uint16_t compositeTempK;
    uint8_t  availableSpare;
    uint8_t  availableSpareThreshold;
    uint8_t  percentageUsed;
};

// Windows'un NVMe gecisi icin gereken sabitler. Bazi SDK surumlerinde
// STORAGE_PROTOCOL_SPECIFIC_DATA tanimli olmayabiliyor; kendi tanimlarimizi
// kullanmak SDK surumune bagimliligi kaldiriyor.
constexpr DWORD kNvmeLogHealth = 0x02;

std::string trimAscii(const char* p, size_t maxLen) {
    if (!p) return {};
    size_t n = 0;
    while (n < maxLen && p[n] != '\0') ++n;
    std::string s(p, n);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t b = s.find_first_not_of(" \t");
    return b == std::string::npos ? std::string{} : s.substr(b);
}

Bus busFrom(STORAGE_BUS_TYPE t) {
    switch (t) {
        case BusTypeSata:
        case BusTypeAta:   return Bus::Sata;
        case BusTypeNvme:  return Bus::Nvme;
        case BusTypeUsb:   return Bus::Usb;
        case BusTypeRAID:
        case BusTypeVirtual:
        case BusTypeFileBackedVirtual: return Bus::RaidOrVirtual;
        default:           return Bus::Unknown;
    }
}

// ---- 1) Model + veri yolu ---------------------------------------------------
bool readDeviceProperty(HANDLE h, Drive& d) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType  = PropertyStandardQuery;

    std::vector<uint8_t> buf(1024);
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                         buf.data(), static_cast<DWORD>(buf.size()), &ret, nullptr))
        return false;

    const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buf.data());
    d.bus = busFrom(desc->BusType);

    const char* base = reinterpret_cast<const char*>(buf.data());
    std::string vendor, product;
    if (desc->VendorIdOffset && desc->VendorIdOffset < ret)
        vendor = trimAscii(base + desc->VendorIdOffset, ret - desc->VendorIdOffset);
    if (desc->ProductIdOffset && desc->ProductIdOffset < ret)
        product = trimAscii(base + desc->ProductIdOffset, ret - desc->ProductIdOffset);

    // Seri numarasi BILEREK okunmuyor: rapor foruma yapistirilan bir metin ve
    // orada ise yaramayan her benzersiz tanimlayici gizlilik yuku demektir.
    d.model = vendor.empty() ? product
            : (product.empty() ? vendor : vendor + " " + product);
    return true;
}

// ---- 2) Donen disk mi -------------------------------------------------------
void readSeekPenalty(HANDLE h, Drive& d) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceSeekPenaltyProperty;
    q.QueryType  = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR sp{};
    DWORD ret = 0;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                        &sp, sizeof(sp), &ret, nullptr)) {
        d.kind = sp.IncursSeekPenalty ? Kind::Hdd : Kind::Ssd;
        return;
    }
    // Sorgu basarisizsa veri yolundan cikarim yap; NVMe her zaman katı hal.
    if (d.bus == Bus::Nvme) d.kind = Kind::Ssd;
}

// ---- 3a) Ureticinin ariza tahmini ------------------------------------------
void readPredictFailure(HANDLE h, Drive& d) {
    STORAGE_PREDICT_FAILURE pf{};
    DWORD ret = 0;
    if (DeviceIoControl(h, IOCTL_STORAGE_PREDICT_FAILURE, nullptr, 0,
                        &pf, sizeof(pf), &ret, nullptr)) {
        d.failurePredicted = pf.PredictFailure ? Drive::Tri::Yes : Drive::Tri::No;
    }
}

// ---- 3b) NVMe SMART ---------------------------------------------------------
//  STORAGE_PROTOCOL_COMMAND yerine StorageDeviceProtocolSpecificProperty
//  kullaniliyor: surucuye ham komut gondermiyoruz, Windows'un NVMe gecidinden
//  log sayfasi ISTIYORUZ. Fark onemli — ilki cogu sistemde reddedilir.
void readNvmeSmart(HANDLE h, Drive& d) {
    const size_t hdr = sizeof(STORAGE_PROPERTY_QUERY) - sizeof(ULONG)
                     + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    std::vector<uint8_t> buf(hdr + 512, 0);

    auto* q = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buf.data());
    q->PropertyId = StorageDeviceProtocolSpecificProperty;
    q->QueryType  = PropertyStandardQuery;

    auto* p = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA*>(q->AdditionalParameters);
    p->ProtocolType                = ProtocolTypeNvme;
    p->DataType                    = NVMeDataTypeLogPage;
    p->ProtocolDataRequestValue    = kNvmeLogHealth;
    p->ProtocolDataRequestSubValue = 0;
    p->ProtocolDataOffset          = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    p->ProtocolDataLength          = 512;

    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
                         buf.data(), static_cast<DWORD>(buf.size()),
                         buf.data(), static_cast<DWORD>(buf.size()),
                         &ret, nullptr))
        return;

    const auto* out = reinterpret_cast<const STORAGE_PROTOCOL_DATA_DESCRIPTOR*>(buf.data());
    if (out->ProtocolSpecificData.ProtocolDataLength < 512) return;

    const uint8_t* log = buf.data()
                       + out->ProtocolSpecificData.ProtocolDataOffset
                       + FIELD_OFFSET(STORAGE_PROTOCOL_DATA_DESCRIPTOR, ProtocolSpecificData);

    // Sinir kontrolu: ofsetler surucuden geliyor, korulukten okumayalim.
    if (log < buf.data() || log + 512 > buf.data() + buf.size()) return;

    d.criticalWarning = (log[0x00] != 0);

    const uint16_t tempK = static_cast<uint16_t>(log[0x01] | (log[0x02] << 8));
    if (tempK > 273) d.tempC = static_cast<int32_t>(tempK) - 273;

    d.spareLeftPct = log[0x03];
    d.wearPct      = log[0x05];

    // 128 bitlik sayaclar. Ust 64 bit pratikte hep sifir; alt 64 biti okumak
    // yeterli ve tasma riski yok.
    uint64_t units = 0, hours = 0;
    for (int i = 7; i >= 0; --i) units = (units << 8) | log[0x30 + i];
    for (int i = 7; i >= 0; --i) hours = (hours << 8) | log[0x80 + i];

    // Bir "veri birimi" 1000 x 512 bayt = 512.000 bayt.
    d.writtenGb    = static_cast<int64_t>(units * 512000ull / (1024ull*1024ull*1024ull));
    d.powerOnHours = static_cast<int32_t>(std::min<uint64_t>(hours, 0x7FFFFFFF));
    d.smartRead    = true;
}

// ---- Bolumleri disklere baglama --------------------------------------------
void attachVolumes(std::vector<Drive>& drives) {
    wchar_t sysDir[MAX_PATH] = L"";
    GetSystemDirectoryW(sysDir, MAX_PATH);
    const wchar_t sysLetter = sysDir[0];

    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;

        const wchar_t letter = static_cast<wchar_t>(L'A' + i);
        std::wstring root = std::wstring(1, letter) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;

        Volume v;
        v.letter    = std::string(1, static_cast<char>('A' + i)) + ":";
        v.hasSystem = (letter == sysLetter);

        ULARGE_INTEGER freeAvail{}, total{}, freeTotal{};
        if (GetDiskFreeSpaceExW(root.c_str(), &freeAvail, &total, &freeTotal)) {
            v.totalMb = total.QuadPart     / (1024ull * 1024ull);
            v.freeMb  = freeTotal.QuadPart / (1024ull * 1024ull);
        }

        // Bolum hangi fiziksel diskte?
        std::wstring dev = L"\\\\.\\" + std::wstring(1, letter) + L":";
        HANDLE h = CreateFileW(dev.c_str(), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        std::vector<uint8_t> buf(sizeof(VOLUME_DISK_EXTENTS) +
                                 sizeof(DISK_EXTENT) * 16);
        DWORD ret = 0;
        if (DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                            buf.data(), static_cast<DWORD>(buf.size()),
                            &ret, nullptr)) {
            const auto* ext = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buf.data());
            if (ext->NumberOfDiskExtents > 0) {
                const DWORD idx = ext->Extents[0].DiskNumber;
                for (auto& d : drives)
                    if (d.index == idx) { d.volumes.push_back(v); break; }
            }
        }
        CloseHandle(h);
    }
}

bool isElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD sz = sizeof(el);
    const bool ok = GetTokenInformation(tok, TokenElevation, &el, sz, &sz) != 0;
    CloseHandle(tok);
    return ok && el.TokenIsElevated;
}

} // namespace

const char* busName(Bus b) {
    switch (b) {
        case Bus::Sata:          return "SATA";
        case Bus::Nvme:          return "NVMe";
        case Bus::Usb:           return "USB";
        case Bus::RaidOrVirtual: return "RAID / sanal";
        default:                 return "bilinmiyor";
    }
}

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Ssd: return "SSD";
        case Kind::Hdd: return "Dönen disk (HDD)";
        default:        return "bilinmiyor";
    }
}

StorageScan scan() {
    StorageScan s;
    s.attempted = true;
    s.elevated  = isElevated();

    // En fazla 32 fiziksel disk taranir. Ustu pratikte sunucu yapilandirmasi
    // ve bu urunun hedefi degil.
    for (uint32_t i = 0; i < 32; ++i) {
        std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(i);

        // Once SIFIR erisim hakkiyla acilir: model ve tip icin bu yeterli ve
        // yonetici hakki gerektirmez.
        HANDLE h = CreateFileW(path.c_str(), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        Drive d;
        d.index = i;
        if (!readDeviceProperty(h, d)) { CloseHandle(h); continue; }
        readSeekPenalty(h, d);

        DISK_GEOMETRY_EX geo{};
        DWORD ret = 0;
        if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0,
                            &geo, sizeof(geo), &ret, nullptr))
            d.sizeMb = static_cast<uint64_t>(geo.DiskSize.QuadPart) / (1024ull*1024ull);
        CloseHandle(h);

        // SMART icin okuma hakki gerekiyor. Yukseltilmemis calisirken bu acilis
        // basarisiz olur ve alanlar "okunamadi" olarak kalir — sifir YAZILMAZ.
        HANDLE hr = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        if (hr != INVALID_HANDLE_VALUE) {
            readPredictFailure(hr, d);
            if (d.bus == Bus::Nvme) readNvmeSmart(hr, d);
            CloseHandle(hr);
        }

        s.drives.push_back(std::move(d));
    }

    attachVolumes(s.drives);

    s.ok = !s.drives.empty();
    if (!s.ok) s.error = "Hiçbir fiziksel disk okunamadı.";
    return s;
}

} // namespace ssstore

#endif // _WIN32
