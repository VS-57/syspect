// ============================================================================
//  Syspect — marka isareti
//  ----------------------------------------------------------------------
//  Logo TEK YERDE tanimli ve vektorel ciziliyor. Iki tuketicisi var:
//    - ss_ui : pencere basliginda
//    - ss_makeicon : .ico dosyasini uretir (gorev cubugu, Explorer, kisayol)
//  Ayni cizim koduyla uretildikleri icin ikisi asla birbirinden ayrilmaz.
//
//  Isaret uc parcadan olusur:
//    halka        — olcum aleti kadrani
//    S            — marka harfi
//    ibre + nabiz — turuncu; "olcuyor" fikri
// ============================================================================
#pragma once

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

namespace sslogo {

// backdrop.GetA() > 0 ise halkanin ici once o renkle doldurulur. Simge icin
// gerekli: koyu gri bir isaret koyu gorev cubugunda kaybolur, acik zeminde
// ise beyaz dolgu onu her yerde okunur tutar.
void draw(Gdiplus::Graphics& g,
          const Gdiplus::RectF& box,
          const Gdiplus::Color& body,
          const Gdiplus::Color& accent,
          const Gdiplus::Color& backdrop = Gdiplus::Color(0, 0, 0, 0));

} // namespace sslogo

#endif // _WIN32
