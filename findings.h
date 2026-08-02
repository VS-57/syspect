// ============================================================================
//  Syspect — bulgular
//  ----------------------------------------------------------------------
//  Teshis motoru OLASILIK uretir: "%55 VRR / kare zamanlama". Bu dogru ama
//  cogu kullanicinin okumak istedigi sey degil. Onlarin sorusu daha basit:
//  "sistemimde ne yanlis, neye bakayim?"
//
//  Bulgular bunu karsilar. Her biri OLCULMUS, ikili bir olgu:
//    "Guc modu Dengeli"          "EXPO kapali"        "Isinma kisitlamasi var"
//  Olasilik degil, tespit. Kullanici listeye bakip tek tek gecebilir.
//
//  TASARIM KURALI: Bir bulgu ancak GERCEKTEN OLCULDUYSE listeye girer.
//  Okunamayan bir seyi "sorun yok" diye yesil gostermek, mavi ekran
//  yanlis negatifinde oldugu gibi, teshissizlikten daha zararlidir.
//  Olculemeyen sey Unknown seviyesiyle ayrica gosterilir.
// ============================================================================
#pragma once

#ifdef _WIN32

#include "core.h"
#include "frame_source.h"
#include "telemetry.h"
#include "system_probe.h"
#include "storage_probe.h"
#include "event_log.h"
#include "dump_scan.h"

#include <string>
#include <vector>

namespace ssfind {

enum class Severity {
    Bad,      // kirmizi — dogrudan sorun
    Warn,     // turuncu — denemeye deger, muhtemel etki
    Unknown,  // gri    — olculemedi, hukum yok
    Good,     // yesil  — bakildi, sorun yok
};

struct Finding {
    Severity    severity = Severity::Good;
    std::string title;    // "Güç modu Dengeli"
    std::string detail;   // tek cumle, neden onemli
    std::string action;   // kullanicinin yapacagi TEK sey (bossa yok)
};

struct Input {
    const ss::AnalysisResult*  result   = nullptr;
    const ss::FrameSourceInfo* source   = nullptr;
    const ss::SystemInfo*      sys      = nullptr;
    const std::vector<sstelem::Sample>* telemetry = nullptr;
    const sstelem::GpuStatic*  gpu      = nullptr;
    const sstelem::Capabilities* caps   = nullptr;
    const ssprobe::PowerInfo*  power    = nullptr;
    const ssprobe::MemorySpec* memory   = nullptr;
    const ssprobe::DeviceScan* devices  = nullptr;
    const ssdump::DumpScan*    dumps    = nullptr;
    const ssprobe::FirmwareInfo* firmware = nullptr;
    const ssstore::StorageScan*  storage  = nullptr;

    // Olay gunlugu goruntusu. Olcumden BAGIMSIZ okunur: kullanici hic kayit
    // almadan uygulamayi acsa bile sistemin gecmisi hakkinda somut sey
    // soyleyebilmek icin. Olcumle cakisma bilgisi burada YOKTUR; o ayrimi
    // kural motoru yapar (bkz. SystemInfo::tdrDuringCapture).
    const sslog::Scan*         evtlog   = nullptr;

    // Gosterilen sonuc BASKA bir makineden gelmis olabilir (.syscap dosyasi).
    // Bu durumda cagiran taraf yerel yoklamalari (guc plani, aygitlar,
    // bellek, firmware, mavi ekran) nullptr birakir; bu bayrak yalnizca
    // kullaniciya listenin NEDEN kisa oldugunu soylemek icin.
    bool foreignCapture = false;
};

// Bulgular onem sirasina gore dizilir: once kirmizi, sonra turuncu,
// sonra olculemeyenler, en sonda "sorun yok" satirlari.
std::vector<Finding> collect(const Input& in);

} // namespace ssfind

#endif // _WIN32
