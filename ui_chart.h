// ============================================================================
//  StutterScope — telemetri grafigi (GDI)
//  ----------------------------------------------------------------------
//  NEDEN GRAFIK: Kullanici oyun oynarken anlik degerlere bakamaz. Olcum
//  boyunca kaydedip SONRADAN geri donuk incelemek tek pratik yol. Grafigin
//  isi "ne oldu" sorusuna gorsel cevap vermek; hukum vermek motorun isi.
//
//  IKI EKSEN: Yuzde ve sicaklik dogal olarak 0-100 araliginda; FPS degil.
//  Tek eksende cizilirse FPS egrisi digerlerini ezer. Bu yuzden sol eksen
//  0-100 (%/derece), sag eksen 0-maxFPS.
//
//  Sicaklik burada BILGI olarak cizilir. Tasarim kurali 1 geregi hicbir
//  teshis kuralina girmez.
// ============================================================================
#pragma once

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "core.h"
#include "telemetry.h"

#include <vector>

namespace ssui {

struct ChartSeries {
    std::wstring       name;
    COLORREF           color = RGB(0, 0, 0);
    bool               rightAxis = false;     // FPS sag eksende
    std::vector<double> values;               // saniye basina bir deger
    bool               visible = true;
    std::wstring       unit;
};

// Kare orneklerinden saniye basina FPS serisi uretir.
std::vector<double> buildFpsSeries(const std::vector<ss::FrameSample>& frames);

// Telemetri ve kare verisinden cizilecek serileri hazirlar.
std::vector<ChartSeries> buildSeries(const std::vector<sstelem::Sample>& telemetry,
                                     const std::vector<ss::FrameSample>& frames);

// Grafigi verilen dikdortgene cizer. dpi olcekleme icin scale kullanilir.
void drawChart(HDC dc, const RECT& area,
               const std::vector<ChartSeries>& series,
               HFONT fontSmall, HFONT fontBody,
               int dpi);

} // namespace ssui

#endif // _WIN32
