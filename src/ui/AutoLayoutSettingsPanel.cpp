#include "AutoLayoutSettingsPanel.h"
#include "window.h"         // gSelectedFrame, gMainWindow, PushUndo
#include "../core/Frame.h"
#include <Carbon.h>

WindowRef gAutoLayoutSettingsWindow = nullptr;

static Rect sStrokesRect      = {0,0,0,0};
static Rect sCanvasStackRect  = {0,0,0,0};
static Rect sBaselineOffRect  = {0,0,0,0};
static Rect sBaselineOnRect   = {0,0,0,0};

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static void PStrC(const char* src, Str255& dst) {
    dst[0] = 0;
    for (int i = 0; src[i] && i < 63; ++i) {
        dst[i+1] = static_cast<unsigned char>(src[i]); dst[0]++;
    }
}

static void InvalidateSettings() {
    if (!gAutoLayoutSettingsWindow) return;
    Rect r; GetWindowPortBounds(gAutoLayoutSettingsWindow, &r);
    InvalWindowRect(gAutoLayoutSettingsWindow, &r);
}

static void InvalidateCanvas() {
    if (!gMainWindow) return;
    Rect r; GetWindowPortBounds(gMainWindow, &r);
    InvalWindowRect(gMainWindow, &r);
}

// Draw a Platinum-style popup button; returns the popup rect in outRect.
static void DrawSettingsPopup(short x, short y, short w, short h,
                               const char* label, Rect& outRect) {
    outRect = { y, x, static_cast<short>(y+h), static_cast<short>(x+w) };
    RGBColor bg = {0xCCCC,0xCCCC,0xCCCC}; RGBForeColor(&bg); PaintRect(&outRect);
    RGBColor blk = {0,0,0}; RGBForeColor(&blk); FrameRect(&outRect);
    RGBColor wht  = {0xFFFF,0xFFFF,0xFFFF};
    RGBColor shad = {0x5555,0x5555,0x5555};
    RGBForeColor(&wht);
    MoveTo(static_cast<short>(x+1), static_cast<short>(y+h-2));
    LineTo(static_cast<short>(x+1), static_cast<short>(y+1));
    LineTo(static_cast<short>(x+w-2), static_cast<short>(y+1));
    RGBForeColor(&shad);
    MoveTo(static_cast<short>(x+2), static_cast<short>(y+h-1));
    LineTo(static_cast<short>(x+w-1), static_cast<short>(y+h-1));
    LineTo(static_cast<short>(x+w-1), static_cast<short>(y+2));
    short sepX = static_cast<short>(x+w-16);
    RGBForeColor(&blk);
    MoveTo(sepX, static_cast<short>(y+1)); LineTo(sepX, static_cast<short>(y+h-1));
    short arrX = static_cast<short>((sepX + x+w) / 2);
    short arrY = static_cast<short>((y + y+h) / 2);
    PolyHandle dn = OpenPoly();
    MoveTo(arrX,                       static_cast<short>(arrY+2));
    LineTo(static_cast<short>(arrX+3), static_cast<short>(arrY-1));
    LineTo(static_cast<short>(arrX-3), static_cast<short>(arrY-1));
    LineTo(arrX,                       static_cast<short>(arrY+2));
    ClosePoly(); PaintPoly(dn); KillPoly(dn);
    TextSize(10); Str255 ps; PStrC(label, ps);
    MoveTo(static_cast<short>(x+5), static_cast<short>(y+h-4)); DrawString(ps);
    TextSize(11);
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

void SetupAutoLayoutSettingsPanel() {
    if (gAutoLayoutSettingsWindow) return;
    // 210×115: enough for 3 rows of controls + padding
    Rect bounds = {200, 200, 315, 410};
    gAutoLayoutSettingsWindow = NewCWindow(nullptr, &bounds,
                                            "\pAuto Layout Settings",
                                            false, noGrowDocProc,
                                            reinterpret_cast<WindowRef>(-1L), true, 0);
}

void OpenAutoLayoutSettingsPanel(Point globalAnchor) {
    if (!gAutoLayoutSettingsWindow) SetupAutoLayoutSettingsPanel();
    if (!gAutoLayoutSettingsWindow) return;
    // Position to the left of the anchor point so it doesn't cover the inspector.
    short wx = static_cast<short>(globalAnchor.h - 214);
    short wy = static_cast<short>(globalAnchor.v - 10);
    if (wx < 4) wx = 4;
    if (wy < 24) wy = 24;
    MoveWindow(gAutoLayoutSettingsWindow, wx, wy, false);
    ShowWindow(gAutoLayoutSettingsWindow);
    BringToFront(gAutoLayoutSettingsWindow);
    InvalidateSettings();
}

void RefreshAutoLayoutSettingsPanel() {
    if (!gAutoLayoutSettingsWindow) return;
    Rect portRect; GetWindowPortBounds(gAutoLayoutSettingsWindow, &portRect);
    // Only invalidate if visible (window manager tracks this)
    InvalWindowRect(gAutoLayoutSettingsWindow, &portRect);
}

void DrawAutoLayoutSettingsPanel() {
    if (!gAutoLayoutSettingsWindow) return;

    SetPortWindowPort(gAutoLayoutSettingsWindow);

    Rect portRect; GetWindowPortBounds(gAutoLayoutSettingsWindow, &portRect);
    RGBColor bgClr = {0xEEEE,0xEEEE,0xEEEE}; RGBForeColor(&bgClr);
    PaintRect(&portRect);

    sStrokesRect = sCanvasStackRect = {0,0,0,0};
    sBaselineOffRect = sBaselineOnRect = {0,0,0,0};

    if (!gSelectedFrame || gSelectedFrame->layoutMode == LayoutMode::None) {
        RGBColor gr = {0x8888,0x8888,0x8888}; RGBForeColor(&gr);
        Str255 ps; TextSize(10);
        PStrC("Select a layout frame", ps); MoveTo(8, 28); DrawString(ps);
        TextSize(11);
        return;
    }

    Frame* lf = gSelectedFrame;
    Str255 ps;
    RGBColor labelClr = {0x2222,0x2222,0x2222};
    short y = 10;

    // Row 1 — Strokes
    RGBForeColor(&labelClr); TextSize(10);
    PStrC("Strokes", ps); MoveTo(8, static_cast<short>(y+12)); DrawString(ps);
    DrawSettingsPopup(90, y, 112, 16,
                      lf->strokesInLayout ? "Included" : "Excluded",
                      sStrokesRect);
    y += 30;

    // Row 2 — Canvas stacking
    RGBForeColor(&labelClr); TextSize(10);
    PStrC("Canvas stacking", ps); MoveTo(8, static_cast<short>(y+12)); DrawString(ps);
    DrawSettingsPopup(90, y, 112, 16,
                      lf->canvasStackReverse ? "First on top" : "Last on top",
                      sCanvasStackRect);
    y += 30;

    // Row 3 — Align text baseline (two toggle buttons: [—] [✓])
    RGBForeColor(&labelClr); TextSize(10);
    PStrC("Align text baseline", ps); MoveTo(8, static_cast<short>(y+12)); DrawString(ps);

    const short bw = 22, bh = 16;
    short bx = 168;

    // "—" (disabled) button
    sBaselineOffRect = {y, bx, static_cast<short>(y+bh), static_cast<short>(bx+bw)};
    bool offActive = !lf->alignTextBaseline;
    if (offActive) { RGBColor a={0x3333,0x6666,0xCCCC}; RGBForeColor(&a); }
    else           { RGBColor a={0xCCCC,0xCCCC,0xCCCC}; RGBForeColor(&a); }
    PaintRect(&sBaselineOffRect);
    RGBColor blk={0,0,0}; RGBForeColor(&blk); FrameRect(&sBaselineOffRect);
    {
        // Draw em-dash (—) with a horizontal line
        RGBColor tc = offActive ? RGBColor{0xFFFF,0xFFFF,0xFFFF} : RGBColor{0x3333,0x3333,0x3333};
        RGBForeColor(&tc);
        short cy = static_cast<short>(y + bh/2 + 1);
        short cx = static_cast<short>(bx + bw/2);
        MoveTo(static_cast<short>(cx-4), cy); LineTo(static_cast<short>(cx+4), cy);
    }
    bx += bw + 4;

    // "✓" (enabled) button
    sBaselineOnRect = {y, bx, static_cast<short>(y+bh), static_cast<short>(bx+bw)};
    bool onActive = lf->alignTextBaseline;
    if (onActive)  { RGBColor a={0x3333,0x6666,0xCCCC}; RGBForeColor(&a); }
    else           { RGBColor a={0xCCCC,0xCCCC,0xCCCC}; RGBForeColor(&a); }
    PaintRect(&sBaselineOnRect);
    RGBForeColor(&blk); FrameRect(&sBaselineOnRect);
    {
        // Draw checkmark with QuickDraw lines
        RGBColor tc = onActive ? RGBColor{0xFFFF,0xFFFF,0xFFFF} : RGBColor{0x3333,0x3333,0x3333};
        RGBForeColor(&tc);
        short cx = static_cast<short>(bx + bw/2);
        short cy = static_cast<short>(y + bh/2 + 1);
        MoveTo(static_cast<short>(cx-4), cy);
        LineTo(static_cast<short>(cx-1), static_cast<short>(cy+3));
        LineTo(static_cast<short>(cx+4), static_cast<short>(cy-3));
    }

    TextSize(11);
}

void HandleAutoLayoutSettingsClick(Point localPt) {
    if (!gSelectedFrame || gSelectedFrame->layoutMode == LayoutMode::None) return;
    Frame* lf = gSelectedFrame;

    if (PtInRect(localPt, &sStrokesRect)) {
        MenuRef pm = NewMenu(7001, "\p");
        AppendMenu(pm, "\pExcluded");
        AppendMenu(pm, "\pIncluded");
        InsertMenu(pm, -1);
        short curItem = lf->strokesInLayout ? 2 : 1;
        Point pt = {sStrokesRect.top, sStrokesRect.left};
        SetPortWindowPort(gAutoLayoutSettingsWindow); LocalToGlobal(&pt);
        long result = PopUpMenuSelect(pm, pt.v, pt.h, curItem);
        DeleteMenu(7001); DisposeMenu(pm);
        short item = static_cast<short>(result & 0xFFFF);
        if (item > 0) { PushUndo(); lf->strokesInLayout = (item == 2); InvalidateSettings(); InvalidateCanvas(); }
        return;
    }

    if (PtInRect(localPt, &sCanvasStackRect)) {
        MenuRef pm = NewMenu(7002, "\p");
        AppendMenu(pm, "\pLast on top");
        AppendMenu(pm, "\pFirst on top");
        InsertMenu(pm, -1);
        short curItem = lf->canvasStackReverse ? 2 : 1;
        Point pt = {sCanvasStackRect.top, sCanvasStackRect.left};
        SetPortWindowPort(gAutoLayoutSettingsWindow); LocalToGlobal(&pt);
        long result = PopUpMenuSelect(pm, pt.v, pt.h, curItem);
        DeleteMenu(7002); DisposeMenu(pm);
        short item = static_cast<short>(result & 0xFFFF);
        if (item > 0) { PushUndo(); lf->canvasStackReverse = (item == 2); InvalidateSettings(); InvalidateCanvas(); }
        return;
    }

    if (PtInRect(localPt, &sBaselineOffRect)) {
        PushUndo(); lf->alignTextBaseline = false;
        InvalidateSettings(); InvalidateCanvas(); return;
    }

    if (PtInRect(localPt, &sBaselineOnRect)) {
        PushUndo(); lf->alignTextBaseline = true;
        InvalidateSettings(); InvalidateCanvas(); return;
    }
}
