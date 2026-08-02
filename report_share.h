// ============================================================================
//  Syspect — paylasilabilir rapor
//  ----------------------------------------------------------------------
//  Kullanicinin foruma acacagi baslikta ya da ChatGPT/Gemini'ye soracagi
//  soruda ihtiyaci olan HER SEYI tek metne toplar.
//
//  NEDEN MARKDOWN: Forumlarin cogu kod blogunu ve tabloyu destekliyor; dil
//  modelleri de markdown'i en iyi bu bicimde okuyor. HTML rapor gozle
//  bakmak icin, bu metin BASKASINA SORMAK icin.
//
//  TASARIM KURALI: Rapor OLCUM ve OLCULEMEYEN'i acikca ayirir. "GPU sicaklik:
//  ölçülemedi" yazmak, o satiri hic yazmamaktan iyidir — karsi taraf neyin
//  eksik oldugunu bilmeli.
// ============================================================================
#pragma once

#ifdef _WIN32

#include "frame_source.h"
#include "telemetry.h"
#include "system_probe.h"
#include "event_log.h"
#include "dump_scan.h"

#include <string>
#include <vector>

namespace ssreport {

struct Bundle {
    const ss::AnalysisResult*  result   = nullptr;
    const ss::FrameSourceInfo* source   = nullptr;
    const ss::SystemInfo*      sys      = nullptr;
    const std::vector<sstelem::Sample>* telemetry = nullptr;
    const sstelem::GpuStatic*  gpu      = nullptr;
    const ssprobe::PowerInfo*  power    = nullptr;
    const ssprobe::MemorySpec* memory   = nullptr;
    const ssprobe::DeviceScan* devices  = nullptr;
    const ssdump::DumpScan*    dumps    = nullptr;
    const sslog::Scan*         evtlog   = nullptr;

    std::string cpuName;
    std::string gpuName;
    uint64_t    ramInstalledMb = 0;
    std::string osBuild;
    std::string appVersion = "0.1";
    std::string timestamp;          // yerel saat, ISO benzeri
};

// Foruma / dil modeline yapistirilacak tam metin (UTF-8, markdown).
std::string buildShareText(const Bundle& b);

// Sistem gecmisi ve tanilama icin ayrintili gunluk. Paylasim metninden
// farki: ham ornekleri de tasir, uzundur, insan icin degil arama icindir.
std::string buildDiagnosticLog(const Bundle& b);

} // namespace ssreport

#endif // _WIN32
