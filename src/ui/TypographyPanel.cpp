#include "TypographyPanel.h"
#include "window.h"
#include "InspectorPanel.h"
#include "LayersPanel.h"
#include "../core/Shape.h"

WindowRef gTypographyWindow = nullptr;
static bool sTypographyVisible = false;

bool IsTypographyPanelVisible() { return sTypographyVisible; }

// --------------------------------------------------------------------------
// Font + style name lists
// --------------------------------------------------------------------------

static const char* const kFontNames[] = {
    "(System Default)",
    "Geneva", "Helvetica", "Helvetica Neue", "Arial",
    "Times", "Times New Roman", "Courier", "Courier New",
    "Monaco", "Palatino", "Charcoal", "Chicago", "Symbol",
    nullptr
};

static const char* const kStyleNames[] = {
    "Regular", "Bold", "Italic", "Bold Italic", nullptr
};

// --------------------------------------------------------------------------
// Hit-test rects (rebuilt each draw)
// --------------------------------------------------------------------------

static Rect sFamilyPopupRect  = {0,0,0,0};
static Rect sStylePopupRect   = {0,0,0,0};
static Rect sSizeFieldRect    = {0,0,0,0};
static Rect sLineHFieldRect   = {0,0,0,0};
static Rect sSpacingFieldRect = {0,0,0,0};
static Rect sAlignLeftRect    = {0,0,0,0};
static Rect sAlignCenterRect  = {0,0,0,0};
static Rect sAlignRightRect   = {0,0,0,0};
static Rect sBoldDecRect      = {0,0,0,0};
static Rect sItalicDecRect    = {0,0,0,0};
static Rect sUnderlineDecRect = {0,0,0,0};

// --------------------------------------------------------------------------
// Inline edit state
// --------------------------------------------------------------------------

enum TypoField { kNoTypoField, kTypoFieldSize, kTypoFieldLineH, kTypoFieldSpacing };
static TypoField sTypoActive = kNoTypoField;
static char      sTypoBuf[8] = {};
static int       sTypoLen    = 0;

// --------------------------------------------------------------------------
// Helpers (local copies — avoids cross-unit linkage issues)
// --------------------------------------------------------------------------

static void TPC(const char* src, Str255& dst) {
    dst[0] = 0;
    for (int i = 0; src[i] && i < 63; ++i) {
        dst[i+1] = static_cast<unsigned char>(src[i]); dst[0]++;
    }
}

static std::string TyNumStr(SInt32 n) {
    if (n == 0) return "0";
    bool neg = (n < 0);
    SInt32 v = neg ? -n : n;
    char buf[12]; int i = 11; buf[i] = '\0';
    while (v > 0) { buf[--i] = '0' + (int)(v % 10); v /= 10; }
    if (neg) buf[--i] = '-';
    return std::string(&buf[i]);
}

static void InvalidateTypo() {
    if (!gTypographyWindow) return;
    Rect r; GetWindowPortBounds(gTypographyWindow, &r);
    InvalWindowRect(gTypographyWindow, &r);
}

// Draw a Platinum-style popup button; stores hit rect in outRect
static void DrawPopupBtn(short x, short y, short w, short h,
                          const char* label, Rect& outRect) {
    outRect = { y, x, static_cast<short>(y+h), static_cast<short>(x+w) };
    RGBColor gray = {0xCCCC,0xCCCC,0xCCCC}; RGBForeColor(&gray); PaintRect(&outRect);
    RGBColor blk  = {0,0,0};               RGBForeColor(&blk);  FrameRect(&outRect);
    // Bevel
    RGBColor wht  = {0xEEEE,0xEEEE,0xEEEE};
    RGBForeColor(&wht);
    MoveTo(static_cast<short>(x+1), static_cast<short>(y+h-2));
    LineTo(static_cast<short>(x+1), static_cast<short>(y+1));
    LineTo(static_cast<short>(x+w-2), static_cast<short>(y+1));
    RGBColor shd  = {0x6666,0x6666,0x6666};
    RGBForeColor(&shd);
    MoveTo(static_cast<short>(x+2), static_cast<short>(y+h-1));
    LineTo(static_cast<short>(x+w-1), static_cast<short>(y+h-1));
    LineTo(static_cast<short>(x+w-1), static_cast<short>(y+2));
    // Separator (14px from right)
    short sepX = static_cast<short>(x+w-15);
    RGBForeColor(&blk);
    MoveTo(sepX, static_cast<short>(y+1)); LineTo(sepX, static_cast<short>(y+h-1));
    // Down arrow
    short ax = static_cast<short>((sepX + x+w) / 2);
    short ay = static_cast<short>(y + h/2 - 2);
    PolyHandle tri = OpenPoly();
    MoveTo(ax,                         ay);
    LineTo(static_cast<short>(ax+4),   ay);
    LineTo(static_cast<short>(ax+2),   static_cast<short>(ay+3));
    LineTo(ax,                         ay);
    ClosePoly(); PaintPoly(tri); KillPoly(tri);
    // Label (truncated)
    RGBForeColor(&blk); TextSize(10);
    Str255 ps; TPC(label, ps);
    short tw = StringWidth(ps);
    short avail = static_cast<short>(w - 18);
    if (tw > avail) {
        // truncate to fit
        while (ps[0] > 0 && StringWidth(ps) > avail) ps[0]--;
    }
    MoveTo(static_cast<short>(x+4), static_cast<short>(y+h-4)); DrawString(ps);
    TextSize(11);
}

// Draw an inline numeric edit field; stores hit rect in outRect
static void DrawTypoField(short x, short y, short w,
                           TypoField field, SInt32 val, const char* suffix,
                           Rect& outRect) {
    outRect = { static_cast<short>(y-10), static_cast<short>(x-2),
                static_cast<short>(y+3),  static_cast<short>(x+w) };
    Str255 ps;
    if (sTypoActive == field) {
        RGBColor bg  = {0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&bg); PaintRect(&outRect);
        RGBColor bd  = {0x3333,0x6666,0xCCCC}; RGBForeColor(&bd); FrameRect(&outRect);
        RGBColor tc  = {0,0,0}; RGBForeColor(&tc);
        std::string s(sTypoBuf, sTypoLen); s += '_';
        TPC(s.c_str(), ps); MoveTo(x, y); DrawString(ps);
    } else {
        RGBColor tc = {0x1111,0x1111,0x1111}; RGBForeColor(&tc);
        std::string s = TyNumStr(val);
        if (suffix && suffix[0]) s += suffix;
        TPC(s.c_str(), ps); MoveTo(x, y); DrawString(ps);
        RGBColor ul = {0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&ul);
        MoveTo(outRect.left, outRect.bottom); LineTo(outRect.right, outRect.bottom);
    }
}

// Draw a small toggle button (B/I/U/L/C/R); blue when active, gray when not
static void DrawToggleBtn(short x, short y, short w, short h,
                           const char* label, bool active, Rect& outRect) {
    outRect = { y, x, static_cast<short>(y+h), static_cast<short>(x+w) };
    if (active) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
    else        { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
    PaintRect(&outRect);
    RGBColor bd={0x7777,0x7777,0x7777}; RGBForeColor(&bd); FrameRect(&outRect);
    if (active) { RGBColor tc={0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&tc); }
    else        { RGBColor tc={0,0,0}; RGBForeColor(&tc); }
    Str255 ps; TPC(label, ps);
    short lw = StringWidth(ps);
    short lh = 10;  // approximate
    MoveTo(static_cast<short>(x + (w-lw)/2), static_cast<short>(y + (h+lh)/2 - 1));
    DrawString(ps);
}

// Section header (same style as Inspector)
static short DrawTypoHeader(short y, const char* title) {
    Rect portR; GetWindowPortBounds(gTypographyWindow, &portR);
    RGBColor bg = {0xEEEE,0xEEEE,0xEEEE}; RGBForeColor(&bg);
    Rect hdr = { y, 0, static_cast<short>(y+16), portR.right };
    PaintRect(&hdr);
    RGBColor sep = {0xCCCC,0xCCCC,0xCCCC}; RGBForeColor(&sep);
    MoveTo(0, y); LineTo(portR.right, y);
    MoveTo(0, static_cast<short>(y+15)); LineTo(portR.right, static_cast<short>(y+15));
    RGBColor tc = {0x5555,0x5555,0x5555}; RGBForeColor(&tc); TextSize(9);
    Str255 ps; TPC(title, ps);
    MoveTo(6, static_cast<short>(y+12)); DrawString(ps);
    TextSize(11);
    return static_cast<short>(y+16);
}

// --------------------------------------------------------------------------
// Setup
// --------------------------------------------------------------------------

void SetupTypographyPanel() {
    if (!gMainWindow) return;
    Rect mb; GetWindowPortBounds(gMainWindow, &mb);
    Point tr = { mb.top, mb.right };
    SetPortWindowPort(gMainWindow); LocalToGlobal(&tr);

    Rect pr;
    pr.top    = static_cast<short>(tr.v + kLayersPanelHeight + 24);
    pr.left   = static_cast<short>(tr.h + 4 + kInspectorWidth + 4);
    pr.right  = static_cast<short>(pr.left + kTypographyWidth);
    pr.bottom = static_cast<short>(pr.top  + kTypographyHeight);

    gTypographyWindow = NewCWindow(nullptr, &pr, "\pTypography", false,
                                   noGrowDocProc, (WindowRef)-1L, true, 0);
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

void DrawTypographyPanel() {
    if (!gTypographyWindow) return;
    SetPortWindowPort(gTypographyWindow);

    // Reset all hit rects
    sFamilyPopupRect = sStylePopupRect = {0,0,0,0};
    sSizeFieldRect = sLineHFieldRect = sSpacingFieldRect = {0,0,0,0};
    sAlignLeftRect = sAlignCenterRect = sAlignRightRect = {0,0,0,0};
    sBoldDecRect = sItalicDecRect = sUnderlineDecRect = {0,0,0,0};

    Rect portR; GetWindowPortBounds(gTypographyWindow, &portR);
    RGBColor white = {0xFFFF,0xFFFF,0xFFFF}; RGBColor black = {0,0,0};
    RGBBackColor(&white); RGBForeColor(&black);
    EraseRect(&portR);
    TextFont(0); TextSize(11);

    // No text shape selected
    bool isText = (gSelectedShape && gSelectedShape->GetType() == Shape::kText);
    if (!isText) {
        RGBColor gray = {0x9999,0x9999,0x9999}; RGBForeColor(&gray); TextSize(10);
        Str255 ps; TPC("Select a text layer", ps); MoveTo(6, 28); DrawString(ps);
        TextSize(11); RGBForeColor(&black); RGBBackColor(&white);
        return;
    }

    const TextShape& ts = static_cast<const TextShape&>(*gSelectedShape);
    RGBColor labelClr = {0x6666,0x6666,0x6666};
    Str255 ps;

    short y = 0;

    // ---- FONT FAMILY ----
    y = DrawTypoHeader(y, "FAMILY");
    y = static_cast<short>(y + 4);
    // Current font label
    const char* familyLabel = ts.fontFamily.empty() ? "(System Default)" : ts.fontFamily.c_str();
    DrawPopupBtn(6, y, static_cast<short>(portR.right-12), 20, familyLabel, sFamilyPopupRect);
    y = static_cast<short>(y + 26);

    // ---- STYLE + SIZE ----
    y = DrawTypoHeader(y, "STYLE & SIZE");
    y = static_cast<short>(y + 5);
    // Style label
    RGBForeColor(&labelClr); TPC("Style", ps); MoveTo(6, static_cast<short>(y+11)); DrawString(ps);
    // Style popup (left half)
    short half = static_cast<short>((portR.right - 12) / 2);
    const char* styleName;
    switch (ts.fontFace & 3) {
        case 1: styleName = "Bold";        break;
        case 2: styleName = "Italic";      break;
        case 3: styleName = "Bold Italic"; break;
        default: styleName = "Regular";    break;
    }
    DrawPopupBtn(6, static_cast<short>(y+14), half, 20, styleName, sStylePopupRect);
    // Size field (right half)
    RGBForeColor(&labelClr); TPC("Size", ps);
    MoveTo(static_cast<short>(6+half+6), static_cast<short>(y+11)); DrawString(ps);
    DrawTypoField(static_cast<short>(6+half+6+24), static_cast<short>(y+24), 38,
                  kTypoFieldSize, static_cast<SInt32>(ts.fontSize), nullptr, sSizeFieldRect);
    y = static_cast<short>(y + 40);

    // ---- LINE HEIGHT + LETTER SPACING ----
    y = DrawTypoHeader(y, "SPACING");
    y = static_cast<short>(y + 5);
    // Line height
    RGBForeColor(&labelClr); TPC("Line h.", ps); MoveTo(6, static_cast<short>(y+11)); DrawString(ps);
    DrawTypoField(48, static_cast<short>(y+24), 36, kTypoFieldLineH,
                  static_cast<SInt32>(ts.lineHeight), "%", sLineHFieldRect);
    // Letter spacing
    RGBForeColor(&labelClr); TPC("Spacing", ps);
    MoveTo(static_cast<short>(portR.right/2+2), static_cast<short>(y+11)); DrawString(ps);
    DrawTypoField(static_cast<short>(portR.right/2+50), static_cast<short>(y+24), 28,
                  kTypoFieldSpacing, static_cast<SInt32>(ts.letterSpacing), "px", sSpacingFieldRect);
    y = static_cast<short>(y + 40);

    // ---- ALIGNMENT ----
    y = DrawTypoHeader(y, "ALIGNMENT");
    y = static_cast<short>(y + 5);
    DrawToggleBtn(6,  y, 26, 22, "L", ts.textAlign == 0, sAlignLeftRect);
    DrawToggleBtn(35, y, 26, 22, "C", ts.textAlign == 1, sAlignCenterRect);
    DrawToggleBtn(64, y, 26, 22, "R", ts.textAlign == 2, sAlignRightRect);
    y = static_cast<short>(y + 28);

    // ---- DECORATION ----
    y = DrawTypoHeader(y, "DECORATION");
    y = static_cast<short>(y + 5);
    {
        // Bold button (mirrors inspector)
        bool isBold  = (ts.fontFace & 1) != 0;
        bool isItal  = (ts.fontFace & 2) != 0;
        bool isUnder = (ts.fontFace & 4) != 0;
        sBoldDecRect = { y, 6, static_cast<short>(y+22), 32 };
        { TextFace(1); DrawToggleBtn(6, y, 26, 22, "B", isBold, sBoldDecRect); TextFace(0); }
        sItalicDecRect = { y, 35, static_cast<short>(y+22), 61 };
        { TextFace(2); DrawToggleBtn(35, y, 26, 22, "I", isItal, sItalicDecRect); TextFace(0); }
        sUnderlineDecRect = { y, 64, static_cast<short>(y+22), 90 };
        DrawToggleBtn(64, y, 26, 22, "U", isUnder, sUnderlineDecRect);
        // Draw underline indicator under U button
        if (!isUnder) {
            RGBColor uc = {0,0,0}; RGBForeColor(&uc);
            MoveTo(66, static_cast<short>(y+23)); LineTo(88, static_cast<short>(y+23));
        }
    }

    PenNormal(); TextFace(0); TextSize(11); RGBForeColor(&black); RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

void RefreshTypographyPanel() { InvalidateTypo(); }

void ToggleTypographyPanel() {
    if (!gTypographyWindow) return;
    if (sTypographyVisible) {
        HideWindow(gTypographyWindow);
        sTypographyVisible = false;
    } else {
        ShowWindow(gTypographyWindow);
        sTypographyVisible = true;
    }
}

void CancelTypographyEdit() {
    if (sTypoActive != kNoTypoField) {
        sTypoActive = kNoTypoField; sTypoLen = 0; sTypoBuf[0] = '\0';
        InvalidateTypo();
    }
}

bool TypographyInEditMode() { return sTypoActive != kNoTypoField; }

// --------------------------------------------------------------------------
// Click handling
// --------------------------------------------------------------------------

void HandleTypographyPanelClick(Point localPt) {
    if (!gDocument) return;
    if (!gSelectedShape || gSelectedShape->GetType() != Shape::kText) return;
    TextShape& ts = static_cast<TextShape&>(*gSelectedShape);
    bool isLocked = ts.locked;
    if (isLocked) return;

    CancelInspectorEdit();
    CancelTypographyEdit();

    // Font family popup
    if (PtInRect(localPt, &sFamilyPopupRect)) {
        MenuRef m = NewMenu(6001, "\p");
        for (int i = 0; kFontNames[i]; ++i) {
            Str255 ps; TPC(kFontNames[i], ps);
            AppendMenu(m, ps);
        }
        InsertMenu(m, -1);
        // Find current item
        int curItem = 1;
        for (int i = 1; kFontNames[i]; ++i) {
            if (ts.fontFamily == kFontNames[i]) { curItem = i+1; break; }
        }
        Point popPt = { sFamilyPopupRect.bottom, sFamilyPopupRect.left };
        SetPortWindowPort(gTypographyWindow); LocalToGlobal(&popPt);
        long res = PopUpMenuSelect(m, sFamilyPopupRect.top + popPt.v - sFamilyPopupRect.bottom,
                                   popPt.h, curItem);
        DeleteMenu(6001); DisposeMenu(m);
        short item = static_cast<short>(res & 0xFFFF);
        if (item > 0) {
            PushUndo();
            ts.fontFamily = (item == 1) ? "" : kFontNames[item-1];
            InvalidateTypo();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow,&r); InvalWindowRect(gMainWindow,&r); }
        }
        return;
    }

    // Style popup
    if (PtInRect(localPt, &sStylePopupRect)) {
        MenuRef m = NewMenu(6002, "\p");
        for (int i = 0; kStyleNames[i]; ++i) {
            Str255 ps; TPC(kStyleNames[i], ps); AppendMenu(m, ps);
        }
        InsertMenu(m, -1);
        int curItem = 1;
        switch (ts.fontFace & 3) {
            case 1: curItem=2; break; case 2: curItem=3; break; case 3: curItem=4; break;
        }
        Point popPt = { sStylePopupRect.bottom, sStylePopupRect.left };
        SetPortWindowPort(gTypographyWindow); LocalToGlobal(&popPt);
        long res = PopUpMenuSelect(m, sStylePopupRect.top + popPt.v - sStylePopupRect.bottom,
                                   popPt.h, curItem);
        DeleteMenu(6002); DisposeMenu(m);
        short item = static_cast<short>(res & 0xFFFF);
        if (item > 0) {
            PushUndo();
            UInt8 newFace = static_cast<UInt8>(ts.fontFace & ~3);
            if (item==2) newFace|=1; else if (item==3) newFace|=2; else if (item==4) newFace|=3;
            ts.fontFace = newFace;
            InvalidateTypo();
            RefreshInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow,&r); InvalWindowRect(gMainWindow,&r); }
        }
        return;
    }

    // Size field
    if (PtInRect(localPt, &sSizeFieldRect)) {
        sTypoActive = kTypoFieldSize; sTypoLen = 0;
        std::string s = TyNumStr(static_cast<SInt32>(ts.fontSize));
        for (int i = 0; i < (int)s.size() && i < 7; ++i) sTypoBuf[sTypoLen++] = s[i];
        sTypoBuf[sTypoLen] = '\0';
        InvalidateTypo(); return;
    }
    // Line height field
    if (PtInRect(localPt, &sLineHFieldRect)) {
        sTypoActive = kTypoFieldLineH; sTypoLen = 0;
        std::string s = TyNumStr(static_cast<SInt32>(ts.lineHeight));
        for (int i = 0; i < (int)s.size() && i < 7; ++i) sTypoBuf[sTypoLen++] = s[i];
        sTypoBuf[sTypoLen] = '\0';
        InvalidateTypo(); return;
    }
    // Letter spacing field
    if (PtInRect(localPt, &sSpacingFieldRect)) {
        sTypoActive = kTypoFieldSpacing; sTypoLen = 0;
        std::string s = TyNumStr(static_cast<SInt32>(ts.letterSpacing));
        for (int i = 0; i < (int)s.size() && i < 7; ++i) sTypoBuf[sTypoLen++] = s[i];
        sTypoBuf[sTypoLen] = '\0';
        InvalidateTypo(); return;
    }

    // Alignment buttons
    if (PtInRect(localPt, &sAlignLeftRect))   { PushUndo(); ts.textAlign=0; goto redrawAll; }
    if (PtInRect(localPt, &sAlignCenterRect)) { PushUndo(); ts.textAlign=1; goto redrawAll; }
    if (PtInRect(localPt, &sAlignRightRect))  { PushUndo(); ts.textAlign=2; goto redrawAll; }

    // Decoration toggles
    if (PtInRect(localPt, &sBoldDecRect))      { PushUndo(); ts.fontFace ^= 1; goto redrawAll; }
    if (PtInRect(localPt, &sItalicDecRect))    { PushUndo(); ts.fontFace ^= 2; goto redrawAll; }
    if (PtInRect(localPt, &sUnderlineDecRect)) { PushUndo(); ts.fontFace ^= 4; goto redrawAll; }
    return;

redrawAll:
    InvalidateTypo();
    RefreshInspector();
    if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow,&r); InvalWindowRect(gMainWindow,&r); }
}

// --------------------------------------------------------------------------
// Key handling (for inline edit fields)
// --------------------------------------------------------------------------

bool HandleTypographyPanelKey(char key) {
    if (sTypoActive == kNoTypoField) return false;
    if (!gSelectedShape || gSelectedShape->GetType() != Shape::kText) {
        CancelTypographyEdit(); return false;
    }
    TextShape& ts = static_cast<TextShape&>(*gSelectedShape);

    if (key == 0x1B) { CancelTypographyEdit(); return true; }

    if (key == 0x0D || key == 0x03) {  // Return or Enter — apply
        sTypoBuf[sTypoLen] = '\0';
        SInt32 val = 0; int i = 0; bool neg = false;
        if (sTypoLen > 0 && sTypoBuf[0] == '-') { neg = true; i = 1; }
        for (; i < sTypoLen; ++i)
            if (sTypoBuf[i] >= '0' && sTypoBuf[i] <= '9')
                val = val * 10 + (sTypoBuf[i] - '0');
        if (neg) val = -val;

        bool changed = false;
        switch (sTypoActive) {
            case kTypoFieldSize:
                if (val < 4) val=4; if (val>144) val=144;
                if (val != ts.fontSize) { PushUndo(); ts.fontSize=static_cast<SInt16>(val); changed=true; }
                break;
            case kTypoFieldLineH:
                if (val < 10)  val=10; if (val>400) val=400;
                if (val != ts.lineHeight) { PushUndo(); ts.lineHeight=static_cast<UInt16>(val); changed=true; }
                break;
            case kTypoFieldSpacing:
                if (val < -20) val=-20; if (val>100) val=100;
                if (val != ts.letterSpacing) { PushUndo(); ts.letterSpacing=static_cast<SInt16>(val); changed=true; }
                break;
            default: break;
        }
        sTypoActive = kNoTypoField; sTypoLen = 0; sTypoBuf[0] = '\0';
        InvalidateTypo();
        RefreshInspector();
        if (changed && gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow,&r); InvalWindowRect(gMainWindow,&r); }
        return true;
    }

    if (key == 0x08) {  // Backspace
        if (sTypoLen > 0) sTypoBuf[--sTypoLen] = '\0';
        InvalidateTypo(); return true;
    }
    if (key >= '0' && key <= '9' && sTypoLen < 7) {
        sTypoBuf[sTypoLen++] = key; sTypoBuf[sTypoLen] = '\0';
        InvalidateTypo(); return true;
    }
    if (key == '-' && sTypoLen == 0 && sTypoActive == kTypoFieldSpacing) {
        sTypoBuf[sTypoLen++] = '-'; sTypoBuf[sTypoLen] = '\0';
        InvalidateTypo(); return true;
    }
    return true;  // consume other keys while in edit mode
}
