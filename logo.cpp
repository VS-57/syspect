// ============================================================================
//  Syspect — marka isareti
// ============================================================================
#ifdef _WIN32

#include "logo.h"

using namespace Gdiplus;

namespace sslogo {

void draw(Graphics& g, const RectF& box, const Color& body,
          const Color& accent, const Color& backdrop) {
    const float d  = box.Width;
    const float cx = box.X + d / 2.0f;
    const float cy = box.Y + d / 2.0f;

    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // --- Zemin dolgusu (yalnizca simge icin) ---
    if (backdrop.GetA() > 0) {
        SolidBrush fill(backdrop);
        g.FillEllipse(&fill, box.X, box.Y, d, d);
    }

    // --- Halka ---
    //  Referansta halka ince: capin ~%5'i. Kalin halka kucuk boyutlarda
    //  ici doldurup S'i bogar.
    const float ringW = d * 0.050f;
    RectF ring(box.X + ringW / 2.0f, box.Y + ringW / 2.0f, d - ringW, d - ringW);
    Pen ringPen(body, ringW);
    g.DrawEllipse(&ringPen, ring);

    // --- S harfi ---
    //  Kendi bezier egrisini cizmek yerine yazi tipinin S'i kullaniliyor:
    //  her boyutta duzgun kaliyor ve arayuzun geri kalaniyla ayni ailede.
    //  Boyut capin ~%52'si: daha buyugu halkaya degiyor, kucugu kayboluyor.
    FontFamily fam(L"Segoe UI");
    Font sFont(&fam, d * 0.54f, FontStyleBold, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush sBrush(body);
    g.DrawString(L"S", -1, &sFont, RectF(box.X, box.Y, d, d), &sf, &sBrush);

    // --- Ibre ---
    //  Gobek S'in SAGINDA duruyor, uzerinde degil. Ilk denemede gobek
    //  merkeze yakindi ve ibre harfin uzerinden geciyordu.
    Pen needle(accent, d * 0.042f);
    needle.SetStartCap(LineCapRound);
    needle.SetEndCap(LineCapTriangle);
    const float hubX = cx + d * 0.115f;
    const float hubY = cy - d * 0.075f;
    g.DrawLine(&needle, hubX, hubY, cx + d * 0.395f, cy - d * 0.355f);

    SolidBrush hub(accent);
    const float hr = d * 0.048f;
    g.FillEllipse(&hub, hubX - hr, hubY - hr, hr * 2, hr * 2);

    // --- Nabiz ---
    //  Sol kenardan girip S'in ALT govdesinin hizasinda bitiyor. Referansta
    //  cizgi harfin altindan gecer, ortasindan degil.
    Pen pulse(accent, d * 0.040f);
    pulse.SetLineJoin(LineJoinRound);
    pulse.SetStartCap(LineCapRound);
    const float py = cy + d * 0.155f;
    const PointF pts[] = {
        PointF(box.X + d * 0.075f, py),
        PointF(box.X + d * 0.215f, py),
        PointF(box.X + d * 0.265f, py - d * 0.115f),
        PointF(box.X + d * 0.330f, py + d * 0.120f),
        PointF(box.X + d * 0.375f, py),
        PointF(box.X + d * 0.445f, py),
    };
    g.DrawLines(&pulse, pts, 6);

    SolidBrush dot(accent);
    const float dr = d * 0.032f;
    g.FillEllipse(&dot, box.X + d * 0.425f - dr, py - dr, dr * 2, dr * 2);
}

} // namespace sslogo

#endif // _WIN32
