// ============================================================================
//  StutterScope — Kare kaynagi arayuzu
//  ----------------------------------------------------------------------
//  Motorun kare verisini NEREDEN aldigi onemsizdir. Bu arayuz sayesinde
//  ayni teshis motoru su kaynaklarla calisir:
//
//    1) CsvFrameSource  — PresentMon'un urettigi CSV dosyasi   (BUGUN calisir)
//    2) EtwFrameSource  — kendi ETW oturumumuz                 (Windows, sonra)
//    3) SyntheticSource — testlerdeki sentetik izler           (her platform)
//
//  STRATEJI: v0.1 kendi ETW kodunu yazmadan cikabilir. PresentMon.exe zaten
//  frame time CSV'si uretiyor; onu okuyup teshis motoruna vermek, urunu
//  kullanicinin onune haftalar once koymayi saglar. Kendi toplayicimiz
//  performans ve "tek exe" hedefi icin sonra gelir.
// ============================================================================
#pragma once

#include "core.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ss {

// Kaynak hakkinda bilgi (teshis raporunda gosterilir)
struct FrameSourceInfo {
    std::string kind;          // "csv" | "etw" | "synthetic"
    std::string application;   // yakalanan oyunun adi (biliniyorsa)
    uint32_t    processId = 0;
    std::string note;
};

// ----------------------------------------------------------------------------
//  Kare kaynagi arayuzu
// ----------------------------------------------------------------------------
class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    // Bir sonraki kareyi al. Veri bittiyse false doner.
    virtual bool next(FrameSample& out) = 0;

    virtual const FrameSourceInfo& info() const = 0;

    // Kaynak acilirken olusan hata (bossa sorun yok)
    virtual const std::string& error() const = 0;
};

// ----------------------------------------------------------------------------
//  CSV kaynagi — PresentMon ciktisini okur
// ----------------------------------------------------------------------------
//  Sutun adlarina gore calisir, sutun SIRASINA gore DEGIL. PresentMon 1.x ve
//  2.x farkli sutun adlari kullaniyor; ikisi de desteklenir:
//
//    zaman      : TimeInSeconds        (1.x) | CPUStartTime      (2.x)
//    kare suresi: MsBetweenPresents    (1.x) | FrameTime         (2.x)
//    uygulama   : Application
//    dusen kare : Dropped
//
//  Bilinmeyen sutunlar sessizce atlanir; bu, PresentMon yeni sutun
//  eklediginde kodun bozulmamasini saglar.
// ----------------------------------------------------------------------------
class CsvFrameSource : public IFrameSource {
public:
    explicit CsvFrameSource(const std::string& path);

    // Bellekten okuma (test icin)
    static std::unique_ptr<CsvFrameSource> fromString(const std::string& csvText);

    bool next(FrameSample& out) override;
    const FrameSourceInfo& info() const override { return info_; }
    const std::string& error() const override { return error_; }

    size_t totalRows()   const { return rows_.size(); }
    size_t droppedRows() const { return dropped_; }

private:
    CsvFrameSource() = default;
    void parse(const std::string& text);

    std::vector<FrameSample> rows_;
    size_t          cursor_  = 0;
    size_t          dropped_ = 0;
    FrameSourceInfo info_;
    std::string     error_;
};

// ----------------------------------------------------------------------------
//  Sentetik kaynak — testler ve demo icin
// ----------------------------------------------------------------------------
class VectorFrameSource : public IFrameSource {
public:
    VectorFrameSource(std::vector<FrameSample> f, FrameSourceInfo i = {})
        : rows_(std::move(f)), info_(std::move(i)) {
        if (info_.kind.empty()) info_.kind = "synthetic";
    }
    bool next(FrameSample& out) override {
        if (cursor_ >= rows_.size()) return false;
        out = rows_[cursor_++];
        return true;
    }
    const FrameSourceInfo& info() const override { return info_; }
    const std::string& error() const override { return error_; }

private:
    std::vector<FrameSample> rows_;
    size_t          cursor_ = 0;
    FrameSourceInfo info_;
    std::string     error_;
};

// ----------------------------------------------------------------------------
//  Kolaylik: bir kaynagi bastan sona isleyip teshis uretir
// ----------------------------------------------------------------------------
struct AnalysisResult {
    std::vector<FrameSample>  frames;
    std::vector<StutterEvent> events;
    SessionStats              stats;
    Diagnosis                 diagnosis;
};

// ----------------------------------------------------------------------------
//  Yan sinyal saglayicisi
// ----------------------------------------------------------------------------
//  Takilma dedektoru bir olay urettiginde "o anda sistemde ne oluyordu?"
//  sorusunun cevabini buradan alir.
//
//  NEDEN ARAYUZ: Sinyaller Windows'a ozgu kaynaklardan gelir (NVML, PDH,
//  ETW) ama motor tasinabilir kalmali. Motor yalnizca bu arayuzu tanir;
//  doldurmak Windows katmaninin isidir.
//
//  ONEMLI TARIHCE: Bu arayuz eklenene kadar analyzeSource her olaya bos
//  bir SignalSnapshot veriyordu (`det.push(f, {})`). Yani DPC, VRAM,
//  golgelendirici, hard fault ve disk kurallari uretimde HIC atesleyemedi;
//  yalnizca testlerde calisiyorlardi. Kanal budur.
class ISignalProvider {
public:
    virtual ~ISignalProvider() = default;

    // Verilen ana ait sinyaller. Saglayici o an icin veri bulamazsa
    // varsayilan (yani "olculmedi") dondurmelidir — uydurma deger URETMEZ.
    virtual SignalSnapshot at(uint64_t timestampUs) const = 0;
};

// signals == nullptr ise motor yalnizca kare deseni kurallarini calistirir.
AnalysisResult analyzeSource(IFrameSource& src, const SystemInfo& sys,
                             const ISignalProvider* signals = nullptr);

} // namespace ss
