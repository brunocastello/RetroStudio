// Define USE_SYSTEM_COLOR_PICKER to use the Mac OS 9 GetColor() dialog instead
// of the built-in swatch picker.  Disabled by default: the Color Picker
// extension crashes on some emulators (UTM/QEMU) when switching picker panels.
// On a real PowerPC Mac or a stable emulator, define this flag in CMakeLists.txt:
//   target_compile_definitions(RetroStudio PRIVATE USE_SYSTEM_COLOR_PICKER)
// #define USE_SYSTEM_COLOR_PICKER

#include "InspectorPanel.h"
#include "AutoLayoutSettingsPanel.h"
#include "LayersPanel.h"
#include "TypographyPanel.h"
#include "window.h"
#include "../core/Shape.h"
#include "../canvas/AutoLayout.h"
#include <string>

WindowRef gInspectorWindow = nullptr;

static ControlHandle     gInspectorScrollCtrl = nullptr;
static short             gInspectorScrollY    = 0;
static short             gInspectorTotalH     = 0;
static ControlActionUPP  gInspectorScrollUPP  = nullptr;
static const short       kInspSBW             = 16;
static short             gInspectorPrevW      = 0;  // last known panel width (resize detection)
static short             gInspectorPrevH      = 0;

static void InvalidateInspector();  // forward-declare for the action proc

static void InspectorScrollAction(ControlHandle ctrl, short part) {
    short v  = GetControlValue(ctrl);
    short mn = GetControlMinimum(ctrl);
    short mx = GetControlMaximum(ctrl);
    short delta = 0;
    if (part == inUpButton)   delta = -16;
    if (part == inDownButton) delta = +16;
    if (part == inPageUp)     delta = -80;
    if (part == inPageDown)   delta = +80;
    if (delta != 0) {
        v += delta;
        if (v < mn) v = mn;
        if (v > mx) v = mx;
        SetControlValue(ctrl, v);
        gInspectorScrollY = v;
        InvalidateInspector();
    }
}

// Hit-test rects — rebuilt every draw; {0,0,0,0} = not clickable this frame
static Rect sFillSwatchRect      = {0, 0, 0, 0};
static Rect sStrokeToggleRect    = {0, 0, 0, 0};
static Rect sStrokeSwatchRect    = {0, 0, 0, 0};
static Rect sStrokeWidthDownRect = {0, 0, 0, 0};
static Rect sStrokeWidthUpRect   = {0, 0, 0, 0};
static Rect sStrokeAlignRect     = {0, 0, 0, 0};  // popup dropdown trigger
static Rect sFieldXRect          = {0, 0, 0, 0};
static Rect sFieldYRect          = {0, 0, 0, 0};
static Rect sFieldWRect          = {0, 0, 0, 0};
static Rect sFieldHRect          = {0, 0, 0, 0};
static Rect sFieldSwRect         = {0, 0, 0, 0};
static Rect sFontSizeRect        = {0, 0, 0, 0};
static Rect sBoldRect            = {0, 0, 0, 0};
static Rect sItalicRect          = {0, 0, 0, 0};
static Rect sTypographyBtnRect   = {0, 0, 0, 0};
// Text sizing mode (AutoWidth / AutoHeight / Fixed)
static Rect sTextSizingRect[3]   = {{0,0,0,0},{0,0,0,0},{0,0,0,0}};
// Shape sizing buttons (Fixed / Fill) for W and H when inside a layout frame
static Rect sShapeWFxRect        = {0,0,0,0};
static Rect sShapeWFlRect        = {0,0,0,0};
static Rect sShapeHFxRect        = {0,0,0,0};
static Rect sShapeHFlRect        = {0,0,0,0};
// Counter-axis gap field and mode popup (Wrap mode only)
static Rect sLayoutCounterGapRect     = {0,0,0,0};
static Rect sLayoutCounterGapModeRect = {0,0,0,0};
static Rect sCornerRadiusRect         = {0,0,0,0};
static Rect sCornerTLRect             = {0,0,0,0};
static Rect sCornerTRRect             = {0,0,0,0};
static Rect sCornerBRRect             = {0,0,0,0};
static Rect sCornerBLRect             = {0,0,0,0};
static Rect sCornerIndividualBtnRect  = {0,0,0,0};
static Rect sOpacityRect              = {0,0,0,0};
static Rect sRotationRect             = {0,0,0,0};
// Min/Max Width & Height clamps (SIZE section)
static Rect sMinWRect                 = {0,0,0,0};
static Rect sMaxWRect                 = {0,0,0,0};
static Rect sMinHRect                 = {0,0,0,0};
static Rect sMaxHRect                 = {0,0,0,0};
// Align row: Left, Center-H, Right, Top, Middle-V, Bottom
static Rect sAlignBtnRect[6]          = {};
// Absolute position toggle + Constraints dropdowns (H then V)
static Rect sAbsolutePositionRect     = {0,0,0,0};
static Rect sConstraintHRect          = {0,0,0,0};
static Rect sConstraintVRect          = {0,0,0,0};
// Popup menu item order for both constraint dropdowns
static const ConstraintMode kConstraintModes[5] = {
    ConstraintMode::Start, ConstraintMode::Center, ConstraintMode::End,
    ConstraintMode::StartEnd, ConstraintMode::Scale
};

// Auto Layout controls (frame selected)
static Rect sLayoutModeRect[3]       = {{0,0,0,0},{0,0,0,0},{0,0,0,0}};
static Rect sWrapRect                = {0, 0, 0, 0};  // Wrap toggle button
static Rect sLayoutGapRect           = {0, 0, 0, 0};  // numeric gap field (Fixed mode)
static Rect sLayoutGapModeRect       = {0, 0, 0, 0};  // Fixed / Auto popup
static Rect sLayoutSettingsRect      = {0, 0, 0, 0};  // ⚙ settings icon button
static Rect sAlignCellRect[9]        = {};   // 3×3 grid, index = row*3+col
// Padding controls
static bool sMixedPadding            = false;
static Rect sPadMixedBtnRect         = {0, 0, 0, 0};
static Rect sPadHRect                = {0, 0, 0, 0};  // compact H (left+right)
static Rect sPadVRect                = {0, 0, 0, 0};  // compact V (top+bottom)
static Rect sPadTopRect              = {0, 0, 0, 0};
static Rect sPadRightRect            = {0, 0, 0, 0};
static Rect sPadBottomRect           = {0, 0, 0, 0};
static Rect sPadLeftRect             = {0, 0, 0, 0};

// Sizing mode popups in SIZE section (frame only)
static Rect sWidthSizingPopupRect    = {0, 0, 0, 0};
static Rect sHeightSizingPopupRect   = {0, 0, 0, 0};
// Aspect ratio lock + clip content
static bool sAspectLocked            = false;
static Rect sAspectLockRect          = {0, 0, 0, 0};
static Rect sClipContentRect         = {0, 0, 0, 0};

// Inline text-edit state for numeric fields
enum EditField { kNoField, kFieldX, kFieldY, kFieldW, kFieldH, kFieldStrokeWidth, kFieldFontSize, kFieldLayoutGap,
                 kFieldPadH, kFieldPadV, kFieldPadTop, kFieldPadRight, kFieldPadBottom, kFieldPadLeft,
                 kFieldCounterGap, kFieldCornerRadius,
                 kFieldCornerTL, kFieldCornerTR, kFieldCornerBR, kFieldCornerBL,
                 kFieldOpacity, kFieldRotation,
                 kFieldMinW, kFieldMaxW, kFieldMinH, kFieldMaxH };
static EditField sActiveField = kNoField;
static char      sEditBuf[12] = {};
static int       sEditLen     = 0;

// dBoxProc = 1  (shadowed dialog box, no title bar)
static const short kDBoxProc = 1;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static void PStr(const std::string& src, Str255& dst) {
    dst[0] = 0;
    for (int i = 0; i < (int)src.size() && i < 63; ++i) {
        dst[i+1] = static_cast<unsigned char>(src[i]); dst[0]++;
    }
}

static void PStrC(const char* src, Str255& dst) {
    dst[0] = 0;
    for (int i = 0; src[i] && i < 63; ++i) {
        dst[i+1] = static_cast<unsigned char>(src[i]); dst[0]++;
    }
}

static std::string numStr(SInt32 n) {
    if (n == 0) return "0";
    bool neg = (n < 0);
    SInt32 abs_n = neg ? -n : n;
    char buf[12]; int i = 11; buf[i] = '\0';
    while (abs_n > 0) { buf[--i] = '0' + (int)(abs_n % 10); abs_n /= 10; }
    if (neg) buf[--i] = '-';
    return std::string(&buf[i]);
}

static void InvalidateInspector() {
    if (!gInspectorWindow) return;
    Rect r; GetWindowPortBounds(gInspectorWindow, &r);
    InvalWindowRect(gInspectorWindow, &r);
}

// Enter edit mode for a numeric field, pre-filling the buffer with its current value
static void StartEdit(EditField field, SInt32 val) {
    CancelTypographyEdit();
    sActiveField = field;
    sEditLen = 0;
    std::string s = numStr(val);
    for (int i = 0; i < (int)s.size() && i < 11; ++i)
        sEditBuf[sEditLen++] = s[i];
    sEditBuf[sEditLen] = '\0';
    InvalidateInspector();
}

static void StartEditStr(EditField field, const char* str) {
    CancelTypographyEdit();
    sActiveField = field;
    sEditLen = 0;
    for (int i = 0; str[i] && sEditLen < 11; ++i)
        sEditBuf[sEditLen++] = str[i];
    sEditBuf[sEditLen] = '\0';
    InvalidateInspector();
}

// Returns "a" when a==b, or "a, b" when they differ (for compact padding display)
static std::string padCompactStr(UInt16 a, UInt16 b) {
    if (a == b) return numStr(static_cast<SInt32>(a));
    return numStr(static_cast<SInt32>(a)) + ", " + numStr(static_cast<SInt32>(b));
}

// True when a multi-selection's W or H differs across items — the field should
// show "Mixed" (Figma's convention) rather than one item's value standing in for
// all of them, since that reads as "they all match" when they may not.
static bool MixedW(const std::vector<Frame*>& v) { for (size_t i=1;i<v.size();++i) if (v[i]->bounds.w != v[0]->bounds.w) return true; return false; }
static bool MixedH(const std::vector<Frame*>& v) { for (size_t i=1;i<v.size();++i) if (v[i]->bounds.h != v[0]->bounds.h) return true; return false; }
static bool MixedW(const std::vector<Shape*>& v) { for (size_t i=1;i<v.size();++i) if (v[i]->bounds.w != v[0]->bounds.w) return true; return false; }
static bool MixedH(const std::vector<Shape*>& v) { for (size_t i=1;i<v.size();++i) if (v[i]->bounds.h != v[0]->bounds.h) return true; return false; }

// Like DrawNumField but displays a custom string when not in edit mode
static short DrawStrField(short x, short y, short boxW, EditField field,
                          const char* display, Rect& outRect) {
    Str255 ps;
    outRect = { static_cast<short>(y-10), static_cast<short>(x-2),
                static_cast<short>(y+3),  static_cast<short>(x+boxW) };
    if (sActiveField == field) {
        RGBColor bg = {0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&bg); PaintRect(&outRect);
        RGBColor bd = {0x3333,0x6666,0xCCCC}; RGBForeColor(&bd); FrameRect(&outRect);
        RGBColor tc = {0,0,0}; RGBForeColor(&tc);
        std::string d(sEditBuf, sEditLen); d += '_';
        PStr(d, ps); MoveTo(x, y); DrawString(ps);
    } else {
        RGBColor tc = {0x1111,0x1111,0x1111}; RGBForeColor(&tc);
        PStrC(display, ps); MoveTo(x, y); DrawString(ps);
        RGBColor ul = {0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&ul);
        MoveTo(outRect.left, outRect.bottom); LineTo(outRect.right, outRect.bottom);
    }
    return outRect.right;
}

// Draw a section header bar; returns y after the bar
static short DrawSectionHeader(short y, const char* title, const Rect& pr) {
    RGBColor bg  = { 0xEEEE, 0xEEEE, 0xEEEE };
    Rect hdr = { y, 0, static_cast<short>(y + 16), pr.right };
    RGBForeColor(&bg); PaintRect(&hdr);

    RGBColor sep = { 0xCCCC, 0xCCCC, 0xCCCC };
    RGBForeColor(&sep);
    MoveTo(0, y); LineTo(pr.right, y);
    MoveTo(0, static_cast<short>(y + 15)); LineTo(pr.right, static_cast<short>(y + 15));

    RGBColor tc = { 0x5555, 0x5555, 0x5555 };
    RGBForeColor(&tc); TextSize(9);
    Str255 ps; PStrC(title, ps);
    MoveTo(6, static_cast<short>(y + 12)); DrawString(ps);
    return static_cast<short>(y + 16);
}

// Draw an editable numeric field; highlights with blue border when active.
// Stores the click rect in outRect. Returns the right edge.
static short DrawNumField(short x, short y, short boxW, EditField field,
                          SInt32 value, Rect& outRect) {
    Str255 ps;
    outRect = { static_cast<short>(y - 10), static_cast<short>(x - 2),
                static_cast<short>(y + 3),  static_cast<short>(x + boxW) };

    if (sActiveField == field) {
        RGBColor bg = { 0xFFFF, 0xFFFF, 0xFFFF };
        RGBForeColor(&bg); PaintRect(&outRect);
        RGBColor bd = { 0x3333, 0x6666, 0xCCCC };
        RGBForeColor(&bd); FrameRect(&outRect);
        RGBColor tc = { 0, 0, 0 }; RGBForeColor(&tc);
        std::string display(sEditBuf, sEditLen);
        display += '_';
        PStr(display, ps); MoveTo(x, y); DrawString(ps);
    } else {
        RGBColor tc = { 0x1111, 0x1111, 0x1111 }; RGBForeColor(&tc);
        PStr(numStr(value), ps); MoveTo(x, y); DrawString(ps);
        // Faint underline signals the field is clickable
        RGBColor ul = { 0xDDDD, 0xDDDD, 0xDDDD }; RGBForeColor(&ul);
        MoveTo(outRect.left, outRect.bottom);
        LineTo(outRect.right, outRect.bottom);
    }
    return outRect.right;
}

// Min/Max Width & Height fields: -1 = unset, shown blank rather than "-1".
static short DrawMinMaxField(short x, short y, short boxW, EditField field,
                              SInt32 value, Rect& outRect) {
    std::string disp = (value < 0) ? "" : numStr(value);
    return DrawStrField(x, y, boxW, field, disp.c_str(), outRect);
}

static void StartEditMinMax(EditField field, SInt32 value) {
    if (value < 0) StartEditStr(field, "");
    else           StartEdit(field, value);
}

// Draws the "Min/Max" mini grid used at the bottom of the SIZE section:
// a W/H header row, then a Min row and a Max row, right-aligned to cRight.
// Returns y after the block.
static short DrawMinMaxSizeRows(short y, short cRight, RGBColor labelClr,
                                 SInt32 minW, SInt32 maxW, SInt32 minH, SInt32 maxH) {
    Str255 ps;
    short colWX = 30;
    short colHX = static_cast<short>(cRight - 40);
    short fldW  = 34;

    RGBForeColor(&labelClr); TextSize(9);
    PStrC("W", ps); MoveTo(colWX, static_cast<short>(y + 9)); DrawString(ps);
    PStrC("H", ps); MoveTo(colHX, static_cast<short>(y + 9)); DrawString(ps);
    y = static_cast<short>(y + 12);

    RGBForeColor(&labelClr); TextSize(9);
    PStrC("Min", ps); MoveTo(6, static_cast<short>(y + 11)); DrawString(ps);
    TextSize(11);
    DrawMinMaxField(colWX, static_cast<short>(y + 11), fldW, kFieldMinW, minW, sMinWRect);
    DrawMinMaxField(colHX, static_cast<short>(y + 11), fldW, kFieldMinH, minH, sMinHRect);
    y = static_cast<short>(y + 18);

    RGBForeColor(&labelClr); TextSize(9);
    PStrC("Max", ps); MoveTo(6, static_cast<short>(y + 11)); DrawString(ps);
    TextSize(11);
    DrawMinMaxField(colWX, static_cast<short>(y + 11), fldW, kFieldMaxW, maxW, sMaxWRect);
    DrawMinMaxField(colHX, static_cast<short>(y + 11), fldW, kFieldMaxH, maxH, sMaxHRect);
    y = static_cast<short>(y + 18);

    return y;
}

// --------------------------------------------------------------------------
// Color swatch picker (24-preset, emulator-safe — no Color Picker extension)
// --------------------------------------------------------------------------

static const RGBColor kSwatchColors[24] = {
    {      0,      0,      0 },  { 0xFFFF, 0xFFFF, 0xFFFF },
    { 0xCCCC, 0xCCCC, 0xCCCC },  { 0x8888, 0x8888, 0x8888 },
    { 0x4444, 0x4444, 0x4444 },  { 0xFFFF, 0xFFFF, 0xDDDD },
    { 0xFFFF,      0,      0 },  { 0xFFFF, 0x7777,      0 },
    { 0xFFFF, 0xFFFF,      0 },  { 0xFFFF, 0xFFFF, 0x9999 },
    { 0xFFFF, 0x9999, 0x9999 },  { 0x8888,      0,      0 },
    {      0, 0xFFFF,      0 },  {      0, 0x8888,      0 },
    { 0x9999, 0xFFFF, 0x9999 },  { 0xAAAA, 0xFFFF, 0xCCCC },
    {      0, 0x8888, 0x8888 },  {      0, 0xFFFF, 0xFFFF },
    {      0,      0, 0xFFFF },  {      0,      0, 0x8888 },
    { 0x9999, 0xCCCC, 0xFFFF },  { 0xCCCC, 0xCCCC, 0xFFFF },
    { 0x8888,      0, 0x8888 },  { 0xFFFF,      0, 0xFFFF },
};

static const short kSwCellSize = 22;
static const short kSwCellGap  =  2;
static const short kSwPad      =  6;
static const short kSwCols     =  6;
static const short kSwRows     =  4;
static const short kSwWidth  = kSwPad*2 + kSwCols*(kSwCellSize+kSwCellGap) - kSwCellGap;
static const short kSwHeight = kSwPad*2 + kSwRows*(kSwCellSize+kSwCellGap) - kSwCellGap;

static void DrawSwatchPicker(WindowRef win) {
    SetPortWindowPort(win);
    Rect pr; GetWindowPortBounds(win, &pr);
    RGBColor bg = { 0xEEEE, 0xEEEE, 0xEEEE };
    RGBForeColor(&bg); PaintRect(&pr);
    for (int i = 0; i < 24; ++i) {
        int col = i % kSwCols, row = i / kSwCols;
        short x = static_cast<short>(kSwPad + col*(kSwCellSize+kSwCellGap));
        short y = static_cast<short>(kSwPad + row*(kSwCellSize+kSwCellGap));
        Rect cell = { y, x, static_cast<short>(y+kSwCellSize), static_cast<short>(x+kSwCellSize) };
        RGBColor c = kSwatchColors[i]; RGBForeColor(&c); PaintRect(&cell);
        RGBColor bd = { 0x6666, 0x6666, 0x6666 }; RGBForeColor(&bd); FrameRect(&cell);
    }
    RGBColor black = {0,0,0}; RGBForeColor(&black);
}

static bool ShowColorSwatchPicker(const Rect& anchorRect, RGBColor& outColor) {
    Point anchor = { anchorRect.bottom, anchorRect.right };
    SetPortWindowPort(gInspectorWindow);
    LocalToGlobal(&anchor);

    Rect wr;
    wr.top    = static_cast<short>(anchor.v + 4);
    wr.left   = static_cast<short>(anchor.h - kSwWidth);
    wr.right  = static_cast<short>(wr.left + kSwWidth);
    wr.bottom = static_cast<short>(wr.top  + kSwHeight);
    if (wr.right  > 1020) { short d=static_cast<short>(wr.right-1020);  wr.left-=d; wr.right-=d; }
    if (wr.bottom > 744)  { short d=static_cast<short>(wr.bottom-744);  wr.top -=d; wr.bottom-=d; }

    WindowRef pickerWin = NewCWindow(nullptr, &wr, "\p", true, kDBoxProc, (WindowRef)-1L, false, 0);
    if (!pickerWin) return false;
    DrawSwatchPicker(pickerWin);
    while (Button()) {}

    bool picked = false, done = false;
    EventRecord evt;
    while (!done) {
        if (WaitNextEvent(everyEvent, &evt, 5, nullptr)) {
            switch (evt.what) {
                case mouseDown: {
                    WindowRef cw; short part = FindWindow(evt.where, &cw);
                    if (part == inContent && cw == pickerWin) {
                        Point local = evt.where; SetPortWindowPort(pickerWin); GlobalToLocal(&local);
                        for (int i = 0; i < 24; ++i) {
                            int col=i%kSwCols, row=i/kSwCols;
                            short x=static_cast<short>(kSwPad+col*(kSwCellSize+kSwCellGap));
                            short y=static_cast<short>(kSwPad+row*(kSwCellSize+kSwCellGap));
                            Rect cell={y,x,static_cast<short>(y+kSwCellSize),static_cast<short>(x+kSwCellSize)};
                            if (PtInRect(local, &cell)) { outColor=kSwatchColors[i]; picked=true; break; }
                        }
                    }
                    done = true; break;
                }
                case keyDown:
                    if (static_cast<char>(evt.message & charCodeMask) == 0x1B) done = true;
                    break;
                case updateEvt: {
                    WindowRef uw = reinterpret_cast<WindowRef>(evt.message);
                    BeginUpdate(uw); if (uw == pickerWin) DrawSwatchPicker(pickerWin); EndUpdate(uw);
                    break;
                }
                default: break;
            }
        }
    }
    DisposeWindow(pickerWin);
    return picked;
}

// --------------------------------------------------------------------------
// Platinum-style popup button (reused for sizing dropdowns)
// --------------------------------------------------------------------------

static void DrawPlatinumBtn(short x, short y, short w, short h,
                             const char* label, Rect& outRect) {
    outRect = { y, x, static_cast<short>(y+h), static_cast<short>(x+w) };
    RGBColor platGray = {0xCCCC,0xCCCC,0xCCCC};
    RGBForeColor(&platGray); PaintRect(&outRect);
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
    PolyHandle up = OpenPoly();
    MoveTo(arrX,                       static_cast<short>(arrY-3));
    LineTo(static_cast<short>(arrX+3), static_cast<short>(arrY));
    LineTo(static_cast<short>(arrX-3), static_cast<short>(arrY));
    LineTo(arrX,                       static_cast<short>(arrY-3));
    ClosePoly(); PaintPoly(up); KillPoly(up);
    PolyHandle dn = OpenPoly();
    MoveTo(arrX,                       static_cast<short>(arrY+3));
    LineTo(static_cast<short>(arrX+3), static_cast<short>(arrY));
    LineTo(static_cast<short>(arrX-3), static_cast<short>(arrY));
    LineTo(arrX,                       static_cast<short>(arrY+3));
    ClosePoly(); PaintPoly(dn); KillPoly(dn);
    RGBForeColor(&blk); TextSize(10);
    Str255 ps; PStrC(label, ps);
    MoveTo(static_cast<short>(x+5), static_cast<short>(y+h-3)); DrawString(ps);
    TextSize(11);
}

// Draw a 10×10 padding icon. sides bitmask: 0x01=top 0x02=right 0x04=bottom 0x08=left
static void DrawPadIcon(short x, short y, UInt8 sides) {
    Rect box = { y, x, static_cast<short>(y+10), static_cast<short>(x+10) };
    RGBColor dim = { 0xAAAA, 0xAAAA, 0xAAAA }; RGBForeColor(&dim); FrameRect(&box);
    RGBColor hi  = { 0x1111, 0x1111, 0x1111 }; RGBForeColor(&hi);
    if (sides & 0x01) { MoveTo(static_cast<short>(x+1), y); LineTo(static_cast<short>(x+9), y); }
    if (sides & 0x02) { MoveTo(static_cast<short>(x+10), static_cast<short>(y+1)); LineTo(static_cast<short>(x+10), static_cast<short>(y+9)); }
    if (sides & 0x04) { MoveTo(static_cast<short>(x+1), static_cast<short>(y+10)); LineTo(static_cast<short>(x+9), static_cast<short>(y+10)); }
    if (sides & 0x08) { MoveTo(x, static_cast<short>(y+1)); LineTo(x, static_cast<short>(y+9)); }
}

// Draw a 12×12 padlock icon at (x,y). Closed = locked, open shackle = unlocked.
static void DrawLockIcon(short x, short y, bool locked) {
    RGBColor fg = {0x3333,0x3333,0x3333}; RGBForeColor(&fg);
    Rect body = { static_cast<short>(y+5), static_cast<short>(x+1),
                  static_cast<short>(y+12), static_cast<short>(x+11) };
    FrameRect(&body);
    Rect dot  = { static_cast<short>(y+7), static_cast<short>(x+4),
                  static_cast<short>(y+10), static_cast<short>(x+7) };
    PaintRect(&dot);
    // Shackle: left post always at x+3, top bar, right post locked=closes into body / unlocked=lifted
    MoveTo(static_cast<short>(x+3), static_cast<short>(y+5));
    LineTo(static_cast<short>(x+3), static_cast<short>(y+2));
    LineTo(static_cast<short>(x+8), static_cast<short>(y+2));
    if (locked) LineTo(static_cast<short>(x+8), static_cast<short>(y+5));
    else        LineTo(static_cast<short>(x+8), static_cast<short>(y+0)); // lifted open
}

static SizingMode ShowSizingPopup(const Rect& btn, SizingMode cur, bool hasFill) {
    MenuRef pm = NewMenu(6003, "\p");
    AppendMenu(pm, "\pFixed");
    AppendMenu(pm, "\pHug");
    if (hasFill) AppendMenu(pm, "\pFill");
    InsertMenu(pm, -1);
    short curItem = static_cast<short>(cur) + 1;
    if (!hasFill && cur == SizingMode::Fill) curItem = 1;
    Point pt = { btn.top, btn.left };
    SetPortWindowPort(gInspectorWindow); LocalToGlobal(&pt);
    long result = PopUpMenuSelect(pm, pt.v, pt.h, curItem);
    DeleteMenu(6003); DisposeMenu(pm);
    short item = static_cast<short>(result & 0xFFFF);
    return (item > 0) ? static_cast<SizingMode>(item - 1) : cur;
}

// --------------------------------------------------------------------------
// Setup
// --------------------------------------------------------------------------

void SetupInspectorPanel() {
    if (!gMainWindow) return;
    Rect mb; GetWindowPortBounds(gMainWindow, &mb);
    Point tr = { mb.top, mb.right };
    SetPortWindowPort(gMainWindow); LocalToGlobal(&tr);

    Rect pr;
    pr.top    = static_cast<short>(tr.v + kLayersPanelHeight + 24);
    pr.left   = static_cast<short>(tr.h + 4);
    pr.right  = static_cast<short>(pr.left + kInspectorWidth);
    pr.bottom = static_cast<short>(pr.top + 400);

    gInspectorWindow = NewCWindow(nullptr, &pr, "\pInspector", true,
                                  documentProc, (WindowRef)-1L, true, 0);

    Rect sbRect = {0, static_cast<short>(kInspectorWidth - kInspSBW), 400, kInspectorWidth};
    gInspectorScrollCtrl = NewControl(gInspectorWindow, &sbRect, "\p", true, 0, 0, 0, scrollBarProc, 0);
    gInspectorScrollUPP  = NewControlActionUPP(InspectorScrollAction);
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
// Align row (single-item align-to-parent / multi-select align-to-bbox)
// --------------------------------------------------------------------------

// kind: 0=Left 1=CenterH 2=Right 3=Top 4=MiddleV 5=Bottom
static void DrawAlignIcon(const Rect& btn, int kind, bool enabled) {
    RGBColor fg;
    if (enabled) { fg.red = 0x3333; fg.green = 0x3333; fg.blue = 0x3333; }
    else         { fg.red = 0xBBBB; fg.green = 0xBBBB; fg.blue = 0xBBBB; }
    RGBForeColor(&fg);

    short cx = static_cast<short>((btn.left + btn.right) / 2);
    short cy = static_cast<short>((btn.top + btn.bottom) / 2);
    short l  = static_cast<short>(btn.left + 3),  r = static_cast<short>(btn.right - 3);
    short t  = static_cast<short>(btn.top + 3),   b = static_cast<short>(btn.bottom - 3);

    switch (kind) {
        case 0: { // Align Left
            MoveTo(l, t); LineTo(l, b);
            Rect r1 = { static_cast<short>(t+1), l, static_cast<short>(t+4), static_cast<short>(l+5) };
            Rect r2 = { static_cast<short>(b-4), l, static_cast<short>(b-1), static_cast<short>(l+9) };
            PaintRect(&r1); PaintRect(&r2);
        } break;
        case 1: { // Align Center Horizontal
            MoveTo(cx, t); LineTo(cx, b);
            Rect r1 = { static_cast<short>(t+1), static_cast<short>(cx-2), static_cast<short>(t+4), static_cast<short>(cx+3) };
            Rect r2 = { static_cast<short>(b-4), static_cast<short>(cx-4), static_cast<short>(b-1), static_cast<short>(cx+5) };
            PaintRect(&r1); PaintRect(&r2);
        } break;
        case 2: { // Align Right
            MoveTo(r, t); LineTo(r, b);
            Rect r1 = { static_cast<short>(t+1), static_cast<short>(r-5), static_cast<short>(t+4), r };
            Rect r2 = { static_cast<short>(b-4), static_cast<short>(r-9), static_cast<short>(b-1), r };
            PaintRect(&r1); PaintRect(&r2);
        } break;
        case 3: { // Align Top
            MoveTo(l, t); LineTo(r, t);
            Rect r1 = { t, static_cast<short>(l+1), static_cast<short>(t+5), static_cast<short>(l+4) };
            Rect r2 = { t, static_cast<short>(r-9), static_cast<short>(t+9), static_cast<short>(r-6) };
            PaintRect(&r1); PaintRect(&r2);
        } break;
        case 4: { // Align Middle Vertical
            MoveTo(l, cy); LineTo(r, cy);
            Rect r1 = { static_cast<short>(cy-2), static_cast<short>(l+1), static_cast<short>(cy+3), static_cast<short>(l+4) };
            Rect r2 = { static_cast<short>(cy-4), static_cast<short>(r-9), static_cast<short>(cy+5), static_cast<short>(r-6) };
            PaintRect(&r1); PaintRect(&r2);
        } break;
        case 5: { // Align Bottom
            MoveTo(l, b); LineTo(r, b);
            Rect r1 = { static_cast<short>(b-5), static_cast<short>(l+1), b, static_cast<short>(l+4) };
            Rect r2 = { static_cast<short>(b-9), static_cast<short>(r-9), b, static_cast<short>(r-6) };
            PaintRect(&r1); PaintRect(&r2);
        } break;
    }
}

// Draws the 6-button align row and populates sAlignBtnRect[]. When `enabled`
// is false the buttons are drawn dimmed and left unclickable (rects zeroed).
static short DrawAlignRow(short y, bool enabled) {
    const short btnW = 22, btnH = 18, gap = 2;
    short x = 6;
    for (int i = 0; i < 6; ++i) {
        Rect btn = { y, x, static_cast<short>(y + btnH), static_cast<short>(x + btnW) };
        RGBColor bg; RGBColor bd;
        if (enabled) { bg = {0xDDDD,0xDDDD,0xDDDD}; bd = {0x7777,0x7777,0x7777}; }
        else         { bg = {0xF2F2,0xF2F2,0xF2F2}; bd = {0xCCCC,0xCCCC,0xCCCC}; }
        RGBForeColor(&bg); PaintRect(&btn);
        RGBForeColor(&bd); FrameRect(&btn);
        DrawAlignIcon(btn, i, enabled);
        sAlignBtnRect[i] = enabled ? btn : Rect{0,0,0,0};
        x = static_cast<short>(x + btnW + gap + (i == 2 ? 4 : 0));
    }
    RGBColor black = {0,0,0}; RGBForeColor(&black);
    return static_cast<short>(y + btnH + 6);
}

// True when at least one item in the current selection is free to be moved
// by an align command — i.e. not positioned by an Auto Layout parent. A child
// that has opted out of its Auto Layout parent's flow via Absolute Position is
// free either way, same as a plain child. Multi-select aligns to the
// selection's own bounding box (no parent needed); a single item aligns to
// its parent frame, so it needs one that isn't itself layout-managing this
// item's position (again unless the item is absolute-positioned).
static bool AnyAlignableSelected() {
    if (gSelectedFrames.size() > 1) {
        for (Frame* f : gSelectedFrames)
            if (!f->parent || f->parent->layoutMode == LayoutMode::None || f->isAbsolutePosition) return true;
        return false;
    }
    if (gSelectedShapes.size() > 1) {
        for (Shape* s : gSelectedShapes) {
            Frame* p = FindShapeParent(s);
            if (!p || p->layoutMode == LayoutMode::None || s->isAbsolutePosition) return true;
        }
        return false;
    }
    if (gSelectedShape) {
        Frame* p = FindShapeParent(gSelectedShape);
        return p && (p->layoutMode == LayoutMode::None || gSelectedShape->isAbsolutePosition);
    }
    if (gSelectedFrame) {
        Frame* p = gSelectedFrame->parent;
        return p && (p->layoutMode == LayoutMode::None || gSelectedFrame->isAbsolutePosition);
    }
    return false;
}

// kind: 0=Left 1=CenterH 2=Right 3=Top 4=MiddleV 5=Bottom
static void ApplyAlign(int kind) {
    if (!gDocument) return;
    const bool isMultiFrame = (gSelectedFrames.size() > 1);
    const bool isMulti      = (gSelectedShapes.size() > 1);
    bool isSingleAlign = false;  // single item only: align also updates the matching constraint

    std::vector<Bounds2*> items;
    SInt32 refX0 = 0, refY0 = 0, refX1 = 0, refY1 = 0;
    bool haveRef = false;

    auto extendRef = [&](const Bounds2& b) {
        if (!haveRef) { refX0 = b.x; refY0 = b.y; refX1 = b.x + b.w; refY1 = b.y + b.h; haveRef = true; return; }
        if (b.x < refX0) refX0 = b.x;
        if (b.y < refY0) refY0 = b.y;
        if (b.x + b.w > refX1) refX1 = b.x + b.w;
        if (b.y + b.h > refY1) refY1 = b.y + b.h;
    };

    if (isMultiFrame) {
        for (Frame* f : gSelectedFrames) {
            if (f->parent && f->parent->layoutMode != LayoutMode::None && !f->isAbsolutePosition) continue;
            items.push_back(&f->bounds);
            extendRef(f->bounds);
        }
    } else if (isMulti) {
        for (Shape* s : gSelectedShapes) {
            Frame* p = FindShapeParent(s);
            if (p && p->layoutMode != LayoutMode::None && !s->isAbsolutePosition) continue;
            items.push_back(&s->bounds);
            extendRef(s->bounds);
        }
    } else {
        Bounds2* b = nullptr; Frame* parent = nullptr; bool isAbs = false;
        if (gSelectedShape)      { b = &gSelectedShape->bounds; parent = FindShapeParent(gSelectedShape); isAbs = gSelectedShape->isAbsolutePosition; }
        else if (gSelectedFrame) { b = &gSelectedFrame->bounds; parent = gSelectedFrame->parent;           isAbs = gSelectedFrame->isAbsolutePosition; }
        if (b && parent && (parent->layoutMode == LayoutMode::None || isAbs)) {
            items.push_back(b);
            refX0 = parent->bounds.x; refY0 = parent->bounds.y;
            refX1 = parent->bounds.x + parent->bounds.w;
            refY1 = parent->bounds.y + parent->bounds.h;
            haveRef = true;
            isSingleAlign = true;
        }
    }

    if (items.empty() || !haveRef) return;

    bool changed = false;
    for (Bounds2* b : items) {
        SInt32 nx = b->x, ny = b->y;
        switch (kind) {
            case 0: nx = refX0; break;
            case 1: nx = refX0 + (refX1 - refX0) / 2 - b->w / 2; break;
            case 2: nx = refX1 - b->w; break;
            case 3: ny = refY0; break;
            case 4: ny = refY0 + (refY1 - refY0) / 2 - b->h / 2; break;
            case 5: ny = refY1 - b->h; break;
            default: break;
        }
        if (nx != b->x || ny != b->y) {
            if (!changed) PushUndo();
            b->x = nx; b->y = ny;
            changed = true;
        }
    }

    // Aligning a single free/absolute item to an edge is also a statement of
    // intent about its constraint on that axis — matches Figma, and means a
    // later parent resize keeps it pinned the way the align just placed it.
    if (isSingleAlign) {
        ConstraintMode* target = nullptr;
        ConstraintMode  newMode = ConstraintMode::Start;
        switch (kind) {
            case 0: target = gSelectedShape ? &gSelectedShape->constraintH : &gSelectedFrame->constraintH; newMode = ConstraintMode::Start;    break;
            case 1: target = gSelectedShape ? &gSelectedShape->constraintH : &gSelectedFrame->constraintH; newMode = ConstraintMode::Center;   break;
            case 2: target = gSelectedShape ? &gSelectedShape->constraintH : &gSelectedFrame->constraintH; newMode = ConstraintMode::End;      break;
            case 3: target = gSelectedShape ? &gSelectedShape->constraintV : &gSelectedFrame->constraintV; newMode = ConstraintMode::Start;    break;
            case 4: target = gSelectedShape ? &gSelectedShape->constraintV : &gSelectedFrame->constraintV; newMode = ConstraintMode::Center;   break;
            case 5: target = gSelectedShape ? &gSelectedShape->constraintV : &gSelectedFrame->constraintV; newMode = ConstraintMode::End;      break;
            default: break;
        }
        if (target && *target != newMode) {
            if (!changed) PushUndo();
            *target = newMode;
            changed = true;
        }
    }

    if (changed) {
        InvalidateInspector();
        if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
    }
}

// Small gray caption used to group controls within one section (Figma's
// "Alignment" / "Position" / "Constraints" / "Rotation" sub-labels inside
// its single Position panel) — no divider bar, unlike DrawSectionHeader.
static short DrawSubLabel(short y, const char* text) {
    RGBColor c = {0x8888,0x8888,0x8888};
    RGBForeColor(&c); TextSize(9);
    Str255 ps; PStrC(text, ps); MoveTo(6, static_cast<short>(y+8)); DrawString(ps);
    TextSize(11);
    RGBColor black = {0,0,0}; RGBForeColor(&black);
    return static_cast<short>(y + 12);
}

static const char* ConstraintModeLabel(ConstraintMode m, bool horizontal) {
    switch (m) {
        case ConstraintMode::Start:    return horizontal ? "Left"   : "Top";
        case ConstraintMode::End:      return horizontal ? "Right"  : "Bottom";
        case ConstraintMode::StartEnd: return horizontal ? "L & R"  : "T & B";
        case ConstraintMode::Center:   return "Center";
        case ConstraintMode::Scale:    return "Scale";
    }
    return "Left";
}

// Small "map" diagram (Figma's crosshair widget): an outer box standing in for
// the parent frame, an inner box positioned per ch/cv, and dashed pin lines to
// whichever edges are anchored (Start/End/StartEnd) — Center/Scale draw no pins.
static void DrawConstraintMap(short x, short y, short size, ConstraintMode ch, ConstraintMode cv) {
    Rect outer = { y, x, static_cast<short>(y+size), static_cast<short>(x+size) };
    RGBColor obg = {0xF2F2,0xF2F2,0xF2F2}; RGBForeColor(&obg); PaintRect(&outer);
    RGBColor obd = {0x9999,0x9999,0x9999}; RGBForeColor(&obd); FrameRect(&outer);

    const short pad = 6, dot = 10;
    short ix, iw, iy, ih;
    switch (ch) {
        case ConstraintMode::End:      ix = static_cast<short>(x+size-pad-dot); iw = dot; break;
        case ConstraintMode::StartEnd: ix = static_cast<short>(x+pad); iw = static_cast<short>(size-2*pad); break;
        default:                       ix = (ch == ConstraintMode::Start) ? static_cast<short>(x+pad)
                                                                            : static_cast<short>(x+(size-dot)/2);
                                        iw = dot; break;
    }
    switch (cv) {
        case ConstraintMode::End:      iy = static_cast<short>(y+size-pad-dot); ih = dot; break;
        case ConstraintMode::StartEnd: iy = static_cast<short>(y+pad); ih = static_cast<short>(size-2*pad); break;
        default:                       iy = (cv == ConstraintMode::Start) ? static_cast<short>(y+pad)
                                                                            : static_cast<short>(y+(size-dot)/2);
                                        ih = dot; break;
    }
    Rect inner = { iy, ix, static_cast<short>(iy+ih), static_cast<short>(ix+iw) };
    RGBColor ic  = {0x3333,0x6666,0xCCCC}; RGBForeColor(&ic); PaintRect(&inner);
    RGBColor icb = {0x1111,0x3333,0x7777}; RGBForeColor(&icb); FrameRect(&inner);

    // Dashed "pin" lines only on edges the current mode actually anchors to —
    // Center/Scale get no pins, which is itself the visual cue ("floats free").
    RGBColor lc = {0x7777,0x7777,0x7777}; RGBForeColor(&lc);
    short midY = static_cast<short>(iy + ih/2);
    short midX = static_cast<short>(ix + iw/2);
    bool pinL = (ch == ConstraintMode::Start || ch == ConstraintMode::StartEnd);
    bool pinR = (ch == ConstraintMode::End   || ch == ConstraintMode::StartEnd);
    bool pinT = (cv == ConstraintMode::Start || cv == ConstraintMode::StartEnd);
    bool pinB = (cv == ConstraintMode::End   || cv == ConstraintMode::StartEnd);
    if (pinL) for (short xx = x; xx < ix; xx += 3)
        { MoveTo(xx, midY); LineTo(static_cast<short>(xx+1), midY); }
    if (pinR) for (short xx = static_cast<short>(ix+iw); xx < x+size; xx += 3)
        { MoveTo(xx, midY); LineTo(static_cast<short>(xx+1), midY); }
    if (pinT) for (short yy = y; yy < iy; yy += 3)
        { MoveTo(midX, yy); LineTo(midX, static_cast<short>(yy+1)); }
    if (pinB) for (short yy = static_cast<short>(iy+ih); yy < y+size; yy += 3)
        { MoveTo(midX, yy); LineTo(midX, static_cast<short>(yy+1)); }

    RGBColor black = {0,0,0}; RGBForeColor(&black);
}

void DrawInspectorPanel() {
    if (!gInspectorWindow) return;
    if (!gDocument) {
        SetPortWindowPort(gInspectorWindow);
        Rect r; GetWindowPortBounds(gInspectorWindow, &r);
        RGBColor disabledBg = { 0xDDDD, 0xDDDD, 0xDDDD };
        RGBBackColor(&disabledBg);
        EraseRect(&r);
        RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
        RGBBackColor(&white);
        return;
    }
    RunDocumentLayout(gDocument);
    SetPortWindowPort(gInspectorWindow);

    Rect portRect; GetWindowPortBounds(gInspectorWindow, &portRect);
    short panelW = portRect.right;
    short panelH = portRect.bottom;
    short cRight = static_cast<short>(portRect.right - kInspSBW);

    // Refit scroll bar only when window was actually resized (MoveControl/SizeControl
    // call HideControl/ShowControl which call InvalWindowRect — doing this every draw
    // creates an infinite update loop that starves all other windows).
    if (gInspectorScrollCtrl && (panelW != gInspectorPrevW || panelH != gInspectorPrevH)) {
        gInspectorPrevW = panelW;
        gInspectorPrevH = panelH;
        MoveControl(gInspectorScrollCtrl, static_cast<short>(panelW - kInspSBW), 0);
        SizeControl(gInspectorScrollCtrl, kInspSBW, panelH);
    }

    // Clamp scroll
    short inspMax = (gInspectorTotalH > panelH) ? static_cast<short>(gInspectorTotalH - panelH) : 0;
    if (gInspectorScrollY > inspMax) gInspectorScrollY = inspMax;
    if (gInspectorScrollY < 0)       gInspectorScrollY = 0;

    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBColor black = { 0, 0, 0 };
    RGBBackColor(&white); RGBForeColor(&black);
    EraseRect(&portRect);  // clear in window coords before SetOrigin

    // Shift drawing for scroll
    SetOrigin(0, gInspectorScrollY);

    TextFont(0); TextSize(11);

    // Reset all hit-test rects each frame
    sFillSwatchRect = sStrokeToggleRect = sStrokeSwatchRect = {0,0,0,0};
    sStrokeWidthDownRect = sStrokeWidthUpRect = {0,0,0,0};
    sStrokeAlignRect = {0,0,0,0};
    sFieldXRect = sFieldYRect = sFieldWRect = sFieldHRect = sFieldSwRect = {0,0,0,0};
    sFontSizeRect = sBoldRect = sItalicRect = sTypographyBtnRect = {0,0,0,0};
    sLayoutGapRect = sLayoutGapModeRect = {0,0,0,0};
    sPadMixedBtnRect = sPadHRect = sPadVRect = {0,0,0,0};
    sPadTopRect = sPadRightRect = sPadBottomRect = sPadLeftRect = {0,0,0,0};
    sWidthSizingPopupRect = sHeightSizingPopupRect = {0,0,0,0};
    sAspectLockRect = sClipContentRect = sWrapRect = sLayoutSettingsRect = {0,0,0,0};
    for (int i=0;i<3;++i) sLayoutModeRect[i]={0,0,0,0};
    for (int i=0;i<9;++i) sAlignCellRect[i]={0,0,0,0};
    for (int i=0;i<3;++i) sTextSizingRect[i]={0,0,0,0};
    sShapeWFxRect = sShapeWFlRect = sShapeHFxRect = sShapeHFlRect = {0,0,0,0};
    sLayoutCounterGapRect = sLayoutCounterGapModeRect = {0,0,0,0};
    sCornerRadiusRect = sCornerTLRect = sCornerTRRect = sCornerBRRect = sCornerBLRect = {0,0,0,0};
    sCornerIndividualBtnRect = sOpacityRect = sRotationRect = {0,0,0,0};
    sMinWRect = sMaxWRect = sMinHRect = sMaxHRect = {0,0,0,0};
    for (int i=0;i<6;++i) sAlignBtnRect[i]={0,0,0,0};
    sAbsolutePositionRect = {0,0,0,0};
    sConstraintHRect = sConstraintVRect = {0,0,0,0};

    if (!gSelectedFrame && !gSelectedShape && gSelectedFrames.empty()) {
        SetOrigin(0, 0);
        RGBColor gray = { 0x9999, 0x9999, 0x9999 }; RGBForeColor(&gray); TextSize(10);
        Str255 ps;
        PStrC("Select an object", ps);       MoveTo(8, 28); DrawString(ps);
        PStrC("to view its properties.", ps); MoveTo(8, 44); DrawString(ps);
        TextSize(12); PenNormal(); RGBForeColor(&black); RGBBackColor(&white);
        return;
    }

    // Mixed selection (frames + shapes together): show count, no editable properties
    if (!gSelectedFrames.empty() && !gSelectedShapes.empty()) {
        SetOrigin(0, 0);
        RGBColor gray = { 0x9999, 0x9999, 0x9999 }; RGBForeColor(&gray); TextSize(10);
        Str255 ps;
        SInt32 total = static_cast<SInt32>(gSelectedFrames.size() + gSelectedShapes.size());
        std::string msg = numStr(total) + " objects selected";
        PStr(msg, ps); MoveTo(8, 28); DrawString(ps);
        PStrC("(mixed types)", ps); MoveTo(8, 44); DrawString(ps);
        TextSize(12); PenNormal(); RGBForeColor(&black); RGBBackColor(&white);
        return;
    }

    // Multi-frame selection: editable properties for all selected frames
    if (gSelectedFrames.size() > 1) {
        RGBColor labelClr2 = { 0x6666, 0x6666, 0x6666 };
        RGBColor valueClr2 = { 0x1111, 0x1111, 0x1111 };
        RGBColor hint2     = { 0x9999, 0x9999, 0x9999 };
        Str255 ps2;
        short y2 = 4;

        // NAME
        y2 = DrawSectionHeader(y2, "NAME", portRect);
        y2 = static_cast<short>(y2 + 6);
        std::string countLabel = numStr(static_cast<SInt32>(gSelectedFrames.size())) + " frames";
        RGBForeColor(&valueClr2); TextSize(11);
        PStr(countLabel, ps2); MoveTo(6, static_cast<short>(y2 + 12)); DrawString(ps2);
        y2 = static_cast<short>(y2 + 22);

        // FILL — uses gSelectedFrame (the last selected frame) as reference color
        y2 = DrawSectionHeader(y2, "FILL", portRect);
        y2 = static_cast<short>(y2 + 6);
        RGBColor mfc = gSelectedFrame->backgroundColor;
        sFillSwatchRect = { y2, 6, static_cast<short>(y2 + 18), 42 };
        RGBForeColor(&mfc); PaintRect(&sFillSwatchRect);
        { RGBColor swBd2 = { 0x7777, 0x7777, 0x7777 }; RGBForeColor(&swBd2); FrameRect(&sFillSwatchRect); }
        RGBForeColor(&hint2); TextSize(9);
        PStrC("Click to change", ps2); MoveTo(48, static_cast<short>(y2 + 13)); DrawString(ps2);
        TextSize(11);
        y2 = static_cast<short>(y2 + 26);

        // STROKE
        y2 = DrawSectionHeader(y2, "STROKE", portRect);
        y2 = static_cast<short>(y2 + 5);
        bool mhs = gSelectedFrame->hasStroke;
        sStrokeToggleRect = { static_cast<short>(y2+2), 5, static_cast<short>(y2+14), 17 };
        { RGBColor cbBd2 = { 0x7777, 0x7777, 0x7777 }; RGBForeColor(&cbBd2); FrameRect(&sStrokeToggleRect); }
        if (mhs) {
            RGBColor chk = { 0x3333, 0x6666, 0xCCCC }; RGBForeColor(&chk);
            Rect inner2 = { static_cast<short>(y2+4), 7, static_cast<short>(y2+12), 15 };
            PaintRect(&inner2);
            sStrokeSwatchRect = { y2, 22, static_cast<short>(y2+18), 58 };
            RGBColor msc = gSelectedFrame->strokeColor;
            RGBForeColor(&msc); PaintRect(&sStrokeSwatchRect);
            { RGBColor cbBd2 = { 0x7777, 0x7777, 0x7777 }; RGBForeColor(&cbBd2); FrameRect(&sStrokeSwatchRect); }
            RGBForeColor(&labelClr2); TextSize(9);
            PStrC("W", ps2); MoveTo(64, static_cast<short>(y2+13)); DrawString(ps2); TextSize(11);
            DrawNumField(78, static_cast<short>(y2+13), 26, kFieldStrokeWidth,
                         static_cast<SInt32>(gSelectedFrame->strokeWidth), sFieldSwRect);
        }
        y2 = static_cast<short>(y2 + 24);

        // APPEARANCE (opacity + corner radius)
        {
            y2 = DrawSectionHeader(y2, "APPEARANCE", portRect);
            y2 = static_cast<short>(y2 + 5);
            UInt8 opV2 = gSelectedFrame->opacity;
            SInt16 mfCr = gSelectedFrame->cornerRadius;
            // Left col: Opacity
            RGBForeColor(&labelClr2); TextSize(9);
            PStrC("Op", ps2); MoveTo(6, static_cast<short>(y2 + 12)); DrawString(ps2);
            TextSize(11);
            DrawNumField(22, static_cast<short>(y2 + 12), 32, kFieldOpacity,
                         static_cast<SInt32>(opV2), sOpacityRect);
            RGBForeColor(&labelClr2); TextSize(9);
            PStrC("%", ps2); MoveTo(56, static_cast<short>(y2 + 12)); DrawString(ps2);
            TextSize(11);
            // Right col: Corner radius
            PStrC("R", ps2); MoveTo(84, static_cast<short>(y2 + 12)); DrawString(ps2);
            TextSize(11);
            DrawNumField(96, static_cast<short>(y2 + 12), 40, kFieldCornerRadius,
                         static_cast<SInt32>(mfCr), sCornerRadiusRect);
            y2 = static_cast<short>(y2 + 22);
        }

        // POSITION — X/Y show gSelectedFrame; editing applies delta to all
        y2 = DrawSectionHeader(y2, "POSITION", portRect);
        y2 = static_cast<short>(y2 + 5);
        y2 = DrawAlignRow(y2, AnyAlignableSelected());
        RGBForeColor(&labelClr2); TextSize(9);
        PStrC("X", ps2); MoveTo(6,  static_cast<short>(y2+12)); DrawString(ps2);
        DrawNumField(20, static_cast<short>(y2+12), 64, kFieldX,
                     gSelectedFrame->bounds.x, sFieldXRect);
        PStrC("Y", ps2); MoveTo(92, static_cast<short>(y2+12)); DrawString(ps2);
        DrawNumField(106, static_cast<short>(y2+12), 62, kFieldY,
                     gSelectedFrame->bounds.y, sFieldYRect);
        y2 = static_cast<short>(y2 + 22);

        // Absolute position toggle + Constraints — gSelectedFrame is the reference
        // (its own parent decides visibility); editing applies to all selected frames.
        {
            Frame* posParent2 = gSelectedFrame->parent;
            bool inLayoutParent2 = posParent2 && posParent2->layoutMode != LayoutMode::None;
            bool isAbsPos2 = gSelectedFrame->isAbsolutePosition;

            if (inLayoutParent2) {
                sAbsolutePositionRect = { y2, 6, static_cast<short>(y2+14), 20 };
                RGBColor wbg2 = {0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&wbg2); PaintRect(&sAbsolutePositionRect);
                RGBColor bd2 = {0x7777,0x7777,0x7777}; RGBForeColor(&bd2);  FrameRect(&sAbsolutePositionRect);
                if (isAbsPos2) {
                    RGBColor chk2 = {0x3333,0x6666,0xCCCC}; RGBForeColor(&chk2);
                    Rect inner2 = { static_cast<short>(y2+3), 9, static_cast<short>(y2+11), 17 };
                    PaintRect(&inner2);
                }
                RGBForeColor(&labelClr2); TextSize(10);
                PStrC("Absolute position", ps2); MoveTo(26, static_cast<short>(y2+11)); DrawString(ps2);
                TextSize(11);
                y2 = static_cast<short>(y2 + 20);
            } else {
                sAbsolutePositionRect = {0,0,0,0};
            }

            if (posParent2 && (!inLayoutParent2 || isAbsPos2)) {
                y2 = DrawSubLabel(y2, "Constraints");
                short rowTop2 = y2;
                DrawPlatinumBtn(6, y2, 84, 18, ConstraintModeLabel(gSelectedFrame->constraintH, true),  sConstraintHRect);
                y2 = static_cast<short>(y2 + 22);
                DrawPlatinumBtn(6, y2, 84, 18, ConstraintModeLabel(gSelectedFrame->constraintV, false), sConstraintVRect);
                y2 = static_cast<short>(y2 + 22);
                DrawConstraintMap(100, rowTop2, 44, gSelectedFrame->constraintH, gSelectedFrame->constraintV);
                short mapEnd2 = static_cast<short>(rowTop2 + 44 + 4);
                if (mapEnd2 > y2) y2 = mapEnd2;
            } else {
                sConstraintHRect = sConstraintVRect = {0,0,0,0};
            }
        }

        // Rotation (°) in POSITION section
        {
            SInt16 rotV2 = gSelectedFrame->rotation;
            RGBForeColor(&labelClr2); TextSize(9);
            PStrC("\xB0", ps2); MoveTo(6, static_cast<short>(y2 + 12)); DrawString(ps2);
            TextSize(11);
            DrawNumField(18, static_cast<short>(y2 + 12), 36, kFieldRotation,
                         static_cast<SInt32>(rotV2), sRotationRect);
            y2 = static_cast<short>(y2 + 22);
        }

        // SIZE — shows "Mixed" when selected frames differ; typing a value sets it on all
        y2 = DrawSectionHeader(y2, "SIZE", portRect);
        y2 = static_cast<short>(y2 + 5);
        RGBForeColor(&labelClr2); TextSize(9);
        PStrC("W", ps2); MoveTo(6,  static_cast<short>(y2+12)); DrawString(ps2);
        if (MixedW(gSelectedFrames))
            DrawStrField(20, static_cast<short>(y2+12), 56, kFieldW, "Mixed", sFieldWRect);
        else
            DrawNumField(20, static_cast<short>(y2+12), 56, kFieldW,
                         gSelectedFrame->bounds.w, sFieldWRect);
        PStrC("H", ps2); MoveTo(86, static_cast<short>(y2+12)); DrawString(ps2);
        if (MixedH(gSelectedFrames))
            DrawStrField(100, static_cast<short>(y2+12), static_cast<short>(cRight - 104), kFieldH,
                         "Mixed", sFieldHRect);
        else
            DrawNumField(100, static_cast<short>(y2+12), static_cast<short>(cRight - 104), kFieldH,
                         gSelectedFrame->bounds.h, sFieldHRect);
        y2 = static_cast<short>(y2 + 22);

        y2 = DrawMinMaxSizeRows(y2, cRight, labelClr2,
                                 gSelectedFrame->minWidth, gSelectedFrame->maxWidth,
                                 gSelectedFrame->minHeight, gSelectedFrame->maxHeight);

        // LAYOUT — uses gSelectedFrame as reference; edits apply to all selected frames
        {
            Frame* lf2 = gSelectedFrame;
            y2 = DrawSectionHeader(y2, "LAYOUT", portRect);
            y2 = static_cast<short>(y2 + 3);

            struct { const char* label; short x; short w; } mfBtn[4] = {
                { "None", 5, 42 }, { "H", 51, 30 }, { "V", 85, 30 }, { "Wrap", 119, 50 },
            };
            for (int i = 0; i < 4; ++i) {
                short bx = mfBtn[i].x, bw = mfBtn[i].w;
                Rect btn2 = { y2, bx, static_cast<short>(y2+18), static_cast<short>(bx+bw) };
                bool active2 = (i < 3) ? (static_cast<UInt8>(lf2->layoutMode) == i)
                                       : (lf2->layoutMode != LayoutMode::None && lf2->layoutWrap);
                bool dimmed2 = (i == 3 && lf2->layoutMode == LayoutMode::None);
                if (active2)       { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
                else if (dimmed2)  { RGBColor bg={0xEEEE,0xEEEE,0xEEEE}; RGBForeColor(&bg); }
                else               { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
                PaintRect(&btn2);
                RGBColor bd2 = dimmed2 ? RGBColor{0xAAAA,0xAAAA,0xAAAA} : RGBColor{0x7777,0x7777,0x7777};
                RGBForeColor(&bd2); FrameRect(&btn2);
                RGBColor tc2 = active2 ? RGBColor{0xFFFF,0xFFFF,0xFFFF}
                             : (dimmed2 ? RGBColor{0xAAAA,0xAAAA,0xAAAA} : RGBColor{0x3333,0x3333,0x3333});
                RGBForeColor(&tc2); TextSize(9);
                PStrC(mfBtn[i].label, ps2);
                short tw2 = StringWidth(ps2);
                MoveTo(static_cast<short>(bx + (bw - tw2)/2), static_cast<short>(y2 + 13));
                DrawString(ps2); TextSize(11);
                if (i < 3) sLayoutModeRect[i] = btn2; else sWrapRect = btn2;
            }
            y2 = static_cast<short>(y2 + 24);

            if (lf2->layoutMode != LayoutMode::None) {
                static const short kCell = 14, kCellGap = 4;
                short gridX2 = 8, gridY2 = y2;
                short gridSpan2 = 3*kCell + 2*kCellGap;
                bool isSB2 = (lf2->primaryAlign == PrimaryAlign::SpaceBetween);
                for (int row = 0; row < 3; ++row) {
                    for (int col = 0; col < 3; ++col) {
                        short cx2 = static_cast<short>(gridX2 + col*(kCell+kCellGap));
                        short cy2 = static_cast<short>(gridY2 + row*(kCell+kCellGap));
                        Rect cell2 = { cy2, cx2, static_cast<short>(cy2+kCell), static_cast<short>(cx2+kCell) };
                        sAlignCellRect[row*3+col] = cell2;
                        bool actCell = false;
                        if (!isSB2) {
                            int priIdx = static_cast<int>(lf2->primaryAlign);
                            int secIdx = static_cast<int>(lf2->crossAlign);
                            actCell = (lf2->layoutMode == LayoutMode::Horizontal)
                                      ? (col == priIdx && row == secIdx)
                                      : (row == priIdx && col == secIdx);
                        }
                        if (actCell) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
                        else         { RGBColor bg={0xEEEE,0xEEEE,0xEEEE}; RGBForeColor(&bg); }
                        PaintRect(&cell2);
                        { RGBColor bd2={0xAAAA,0xAAAA,0xAAAA}; RGBForeColor(&bd2); FrameRect(&cell2); }
                        short dotX2 = static_cast<short>(cx2 + (kCell-4)/2);
                        short dotY2 = static_cast<short>(cy2 + (kCell-4)/2);
                        Rect dot2 = { dotY2, dotX2, static_cast<short>(dotY2+4), static_cast<short>(dotX2+4) };
                        if (actCell) { RGBColor dc={0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&dc); }
                        else         { RGBColor dc={0x7777,0x7777,0x7777}; RGBForeColor(&dc); }
                        PaintRect(&dot2);
                    }
                }

                // Gap field (right of alignment grid)
                short rX2   = static_cast<short>(gridX2 + gridSpan2 + 8);
                short popW2 = 46;
                short valX2 = static_cast<short>(rX2 + popW2 + 4);
                short valW2 = static_cast<short>(cRight - valX2 - 4);
                short row1Y2 = static_cast<short>(gridY2 + (gridSpan2 - 25) / 2);
                RGBForeColor(&labelClr2); TextSize(9);
                PStrC("Gap", ps2); MoveTo(rX2, static_cast<short>(row1Y2+9)); DrawString(ps2); TextSize(11);
                DrawPlatinumBtn(rX2, static_cast<short>(row1Y2+11), popW2, 14,
                                isSB2 ? "Auto" : "Fixed", sLayoutGapModeRect);
                if (!isSB2)
                    DrawNumField(valX2, static_cast<short>(row1Y2+23), valW2,
                                 kFieldLayoutGap, static_cast<SInt32>(lf2->layoutGap), sLayoutGapRect);
                else
                    sLayoutGapRect = {0,0,0,0};

                y2 = static_cast<short>(gridY2 + gridSpan2 + 6);

                // Compact padding row
                short tBtnX2 = static_cast<short>(cRight - 18);
                sPadMixedBtnRect = { y2, tBtnX2, static_cast<short>(y2+14), static_cast<short>(tBtnX2+14) };
                if (sMixedPadding) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
                else               { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
                PaintRect(&sPadMixedBtnRect);
                { RGBColor bd2={0x7777,0x7777,0x7777}; RGBForeColor(&bd2); FrameRect(&sPadMixedBtnRect); }

                short availW2 = static_cast<short>(tBtnX2 - 8);
                short halfW2  = static_cast<short>(availW2 / 2);
                short icoW2   = 12;
                short fldW2   = static_cast<short>(halfW2 - icoW2 - 2);
                short col1X2  = 4;
                short col2X2  = static_cast<short>(col1X2 + halfW2 + 2);
                if (!sMixedPadding) {
                    DrawPadIcon(col1X2, static_cast<short>(y2+2), 0x0A);
                    { std::string sh2 = padCompactStr(lf2->paddingLeft, lf2->paddingRight);
                      DrawStrField(static_cast<short>(col1X2+icoW2), static_cast<short>(y2+12), fldW2,
                                   kFieldPadH, sh2.c_str(), sPadHRect); }
                    DrawPadIcon(col2X2, static_cast<short>(y2+2), 0x05);
                    { std::string sv2 = padCompactStr(lf2->paddingTop, lf2->paddingBottom);
                      DrawStrField(static_cast<short>(col2X2+icoW2), static_cast<short>(y2+12), fldW2,
                                   kFieldPadV, sv2.c_str(), sPadVRect); }
                    sPadTopRect = sPadRightRect = sPadBottomRect = sPadLeftRect = {0,0,0,0};
                    y2 = static_cast<short>(y2 + 18);
                } else {
                    DrawPadIcon(col1X2, static_cast<short>(y2+2), 0x01);
                    DrawNumField(static_cast<short>(col1X2+icoW2), static_cast<short>(y2+12), fldW2,
                                 kFieldPadTop, static_cast<SInt32>(lf2->paddingTop), sPadTopRect);
                    DrawPadIcon(col2X2, static_cast<short>(y2+2), 0x02);
                    DrawNumField(static_cast<short>(col2X2+icoW2), static_cast<short>(y2+12), fldW2,
                                 kFieldPadRight, static_cast<SInt32>(lf2->paddingRight), sPadRightRect);
                    y2 = static_cast<short>(y2 + 18);
                    DrawPadIcon(col1X2, static_cast<short>(y2+2), 0x04);
                    DrawNumField(static_cast<short>(col1X2+icoW2), static_cast<short>(y2+12), fldW2,
                                 kFieldPadBottom, static_cast<SInt32>(lf2->paddingBottom), sPadBottomRect);
                    DrawPadIcon(col2X2, static_cast<short>(y2+2), 0x08);
                    DrawNumField(static_cast<short>(col2X2+icoW2), static_cast<short>(y2+12), fldW2,
                                 kFieldPadLeft, static_cast<SInt32>(lf2->paddingLeft), sPadLeftRect);
                    sPadHRect = sPadVRect = {0,0,0,0};
                    y2 = static_cast<short>(y2 + 18);
                }
            }
        }
        y2 = static_cast<short>(y2 + 4);

        gInspectorTotalH = y2;
        SetOrigin(0, 0);
        if (gInspectorScrollCtrl) {
            short iMax2 = (gInspectorTotalH > panelH) ? static_cast<short>(gInspectorTotalH - panelH) : 0;
            SetControlMaximum(gInspectorScrollCtrl, iMax2);
            SetControlValue(gInspectorScrollCtrl, gInspectorScrollY);
            HiliteControl(gInspectorScrollCtrl, (iMax2 > 0) ? 0 : 255);
            DrawControls(gInspectorWindow);
        }
        TextSize(12); PenNormal(); RGBForeColor(&black); RGBBackColor(&white);
        return;
    }

    // Multi-select: check if all shapes are the same type
    bool isMulti = (gSelectedShapes.size() > 1);
    bool multiAllSameType = false;
    if (isMulti) {
        Shape::Type multiType = gSelectedShapes[0]->GetType();
        multiAllSameType = true;
        for (size_t i = 1; i < gSelectedShapes.size(); ++i) {
            if (gSelectedShapes[i]->GetType() != multiType) { multiAllSameType = false; break; }
        }
        if (!multiAllSameType) {
            // Mixed types: show count + aggregate bbox + fill/stroke, then stop
            RGBColor labelClr2 = { 0x6666, 0x6666, 0x6666 };
            RGBColor valueClr2 = { 0x1111, 0x1111, 0x1111 };
            Str255 ps2;
            RGBForeColor(&labelClr2); TextSize(9);
            PStrC("NAME", ps2); MoveTo(4, 12); DrawString(ps2);
            std::string countLabel = numStr(static_cast<SInt32>(gSelectedShapes.size())) + " objects";
            RGBForeColor(&valueClr2); TextSize(11);
            PStr(countLabel, ps2); MoveTo(4, 26); DrawString(ps2);
            SInt32 minX = gSelectedShapes[0]->bounds.x, minY = gSelectedShapes[0]->bounds.y;
            SInt32 maxX = minX + gSelectedShapes[0]->bounds.w, maxY = minY + gSelectedShapes[0]->bounds.h;
            for (size_t i = 1; i < gSelectedShapes.size(); ++i) {
                const Bounds2& b = gSelectedShapes[i]->bounds;
                if (b.x < minX) minX = b.x;   if (b.y < minY) minY = b.y;
                if (b.x+b.w > maxX) maxX = b.x+b.w; if (b.y+b.h > maxY) maxY = b.y+b.h;
            }
            short y2 = 36;
            RGBForeColor(&labelClr2); TextSize(9);
            PStrC("SIZE", ps2); MoveTo(4, static_cast<short>(y2+8)); DrawString(ps2);
            y2 = static_cast<short>(y2 + 12);
            std::string xs = "X " + numStr(minX) + "   Y " + numStr(minY);
            std::string ws = "W " + numStr(maxX-minX) + "   H " + numStr(maxY-minY);
            RGBForeColor(&valueClr2); TextSize(11);
            PStr(xs, ps2); MoveTo(4, static_cast<short>(y2+10)); DrawString(ps2);
            PStr(ws, ps2); MoveTo(4, static_cast<short>(y2+24)); DrawString(ps2);
            TextSize(12); PenNormal(); RGBForeColor(&black); RGBBackColor(&white);
            SetOrigin(0, 0);
            return;
        }
        // Same type: fall through to full inspector (objName overridden below)
    }

    // Gather data from selection
    RGBColor fillColor   = { 0xCCCC, 0xCCCC, 0xCCCC };
    bool     hasStroke   = false;
    RGBColor strokeColor = { 0, 0, 0 };
    UInt16   strokeWidth = 1;
    UInt8    strokeAlign = 0;
    Bounds2  bounds      = { 0, 0, 100, 100 };
    std::string objName;

    if (gSelectedShape) {
        fillColor   = gSelectedShape->fillColor;
        hasStroke   = gSelectedShape->hasStroke;
        strokeColor = gSelectedShape->strokeColor;
        strokeWidth = gSelectedShape->strokeWidth;
        strokeAlign = gSelectedShape->strokeAlign;
        bounds      = gSelectedShape->bounds;
        objName     = gSelectedShape->name;
        if (objName.empty()) {
            if      (gSelectedShape->GetType() == Shape::kEllipse) objName = "Ellipse";
            else if (gSelectedShape->GetType() == Shape::kText)    objName = "Text";
            else                                                    objName = "Rectangle";
        }
    } else {
        fillColor   = gSelectedFrame->backgroundColor;
        hasStroke   = gSelectedFrame->hasStroke;
        strokeColor = gSelectedFrame->strokeColor;
        strokeWidth = gSelectedFrame->strokeWidth;
        strokeAlign = gSelectedFrame->strokeAlign;
        bounds      = gSelectedFrame->bounds;
        objName     = gSelectedFrame->name;
    }

    // For same-type multi-select, show "N objects" instead of the shape's name
    if (isMulti && multiAllSameType)
        objName = numStr(static_cast<SInt32>(gSelectedShapes.size())) + " objects";

    Str255 ps;
    RGBColor labelClr = { 0x6666, 0x6666, 0x6666 };
    RGBColor valueClr = { 0x1111, 0x1111, 0x1111 };
    RGBColor hint     = { 0x9999, 0x9999, 0x9999 };
    short y = 4;
    bool isTextShape = (gSelectedShape && gSelectedShape->GetType() == Shape::kText);

    // ---------------------------------------------------------------- NAME --
    y = DrawSectionHeader(y, "NAME", portRect);
    y = static_cast<short>(y + 6);
    RGBForeColor(&valueClr);
    PStr(objName, ps); MoveTo(6, static_cast<short>(y + 12)); DrawString(ps);
    y = static_cast<short>(y + 22);

    // ---------------------------------------------------------------- TEXT (TextShape only) --
    if (isTextShape) {
        const TextShape& ts = static_cast<const TextShape&>(*gSelectedShape);
        y = DrawSectionHeader(y, "TEXT", portRect);

        // "Aa" button in right corner of TEXT header opens/closes Typography panel
        sTypographyBtnRect = { static_cast<short>(y-16+1), static_cast<short>(cRight-26),
                               static_cast<short>(y-1),    static_cast<short>(cRight-2) };
        {
            bool open = IsTypographyPanelVisible();
            if (open) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
            else      { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
            PaintRect(&sTypographyBtnRect);
            RGBColor bd={0x7777,0x7777,0x7777}; RGBForeColor(&bd); FrameRect(&sTypographyBtnRect);
            if (open) { RGBColor tc={0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&tc); }
            else      { RGBColor tc={0x3333,0x3333,0x3333}; RGBForeColor(&tc); }
            TextSize(9); PStrC("Aa", ps);
            MoveTo(static_cast<short>(sTypographyBtnRect.left+3),
                   static_cast<short>(sTypographyBtnRect.bottom-3));
            DrawString(ps); TextSize(11);
        }
        y = static_cast<short>(y + 5);

        // Font size field
        RGBForeColor(&labelClr);
        PStrC("Size", ps); MoveTo(6, static_cast<short>(y + 12)); DrawString(ps);
        DrawNumField(36, static_cast<short>(y + 12), 36, kFieldFontSize,
                     static_cast<SInt32>(ts.fontSize), sFontSizeRect);

        // Bold toggle button
        sBoldRect = { static_cast<short>(y+1), 80, static_cast<short>(y+15), 96 };
        {
            bool bold = (ts.fontFace & 1) != 0;
            if (bold) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
            else      { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
            PaintRect(&sBoldRect);
            RGBColor bd={0x7777,0x7777,0x7777}; RGBForeColor(&bd); FrameRect(&sBoldRect);
            if (bold) { RGBColor tc={0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&tc); }
            else      { RGBColor tc={0,0,0}; RGBForeColor(&tc); }
            TextFace(1); PStrC("B", ps); MoveTo(84, static_cast<short>(y + 12)); DrawString(ps);
            TextFace(0);
        }

        // Italic toggle button
        sItalicRect = { static_cast<short>(y+1), 100, static_cast<short>(y+15), 116 };
        {
            bool ital = (ts.fontFace & 2) != 0;
            if (ital) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
            else      { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
            PaintRect(&sItalicRect);
            RGBColor bd={0x7777,0x7777,0x7777}; RGBForeColor(&bd); FrameRect(&sItalicRect);
            if (ital) { RGBColor tc={0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&tc); }
            else      { RGBColor tc={0,0,0}; RGBForeColor(&tc); }
            TextFace(2); PStrC("I", ps); MoveTo(104, static_cast<short>(y + 12)); DrawString(ps);
            TextFace(0);
        }

        y = static_cast<short>(y + 22);

        // Text sizing mode row: [AutoWidth] [AutoHeight] [Fixed]
        {
            y = DrawSectionHeader(y, "LAYOUT", portRect);
            y = static_cast<short>(y + 5);

            const TextShape& tsh = static_cast<const TextShape&>(*gSelectedShape);
            TextSizing tsz = tsh.textSizing;

            // Three equal-width buttons fitting the inspector width
            short bw = static_cast<short>((cRight - 10 - 4) / 3);
            short bh = 22;
            for (int i = 0; i < 3; ++i) {
                short bx = static_cast<short>(5 + i * (bw + 2));
                sTextSizingRect[i] = { y, bx,
                                       static_cast<short>(y + bh),
                                       static_cast<short>(bx + bw) };
                bool active = (static_cast<int>(tsz) == i);
                if (active) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
                else        { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
                PaintRect(&sTextSizingRect[i]);
                RGBColor bd={0x7777,0x7777,0x7777}; RGBForeColor(&bd);
                FrameRect(&sTextSizingRect[i]);
                RGBColor fg = active ? RGBColor{0xFFFF,0xFFFF,0xFFFF}
                                     : RGBColor{0x2222,0x2222,0x2222};
                RGBForeColor(&fg);

                short cx = static_cast<short>(bx + bw/2);
                short cy = static_cast<short>(y + bh/2);

                if (i == 0) {
                    // Auto Width: |→
                    MoveTo(static_cast<short>(cx-8), static_cast<short>(cy-5));
                    LineTo(static_cast<short>(cx-8), static_cast<short>(cy+5));
                    MoveTo(static_cast<short>(cx-6), cy);
                    LineTo(static_cast<short>(cx+5), cy);
                    MoveTo(static_cast<short>(cx+5), cy);
                    LineTo(static_cast<short>(cx+2), static_cast<short>(cy-3));
                    MoveTo(static_cast<short>(cx+5), cy);
                    LineTo(static_cast<short>(cx+2), static_cast<short>(cy+3));
                } else if (i == 1) {
                    // Auto Height: |=|
                    short lx = static_cast<short>(cx-8);
                    short rx = static_cast<short>(cx+8);
                    MoveTo(lx, static_cast<short>(cy-5)); LineTo(lx, static_cast<short>(cy+5));
                    MoveTo(rx, static_cast<short>(cy-5)); LineTo(rx, static_cast<short>(cy+5));
                    MoveTo(static_cast<short>(lx+2), static_cast<short>(cy-2));
                    LineTo(static_cast<short>(rx-2), static_cast<short>(cy-2));
                    MoveTo(static_cast<short>(lx+2), static_cast<short>(cy+2));
                    LineTo(static_cast<short>(rx-2), static_cast<short>(cy+2));
                } else {
                    // Fixed: [=]
                    Rect box = { static_cast<short>(cy-5), static_cast<short>(cx-7),
                                 static_cast<short>(cy+5), static_cast<short>(cx+7) };
                    FrameRect(&box);
                    MoveTo(static_cast<short>(cx-5), static_cast<short>(cy-2));
                    LineTo(static_cast<short>(cx+5), static_cast<short>(cy-2));
                    MoveTo(static_cast<short>(cx-5), static_cast<short>(cy+2));
                    LineTo(static_cast<short>(cx+5), static_cast<short>(cy+2));
                }
            }
            y = static_cast<short>(y + bh + 4);
        }
    }

    // ---------------------------------------------------------------- FILL / COLOR --
    y = DrawSectionHeader(y, isTextShape ? "COLOR" : "FILL", portRect);
    y = static_cast<short>(y + 6);

    sFillSwatchRect = { y, 6, static_cast<short>(y + 18), 42 };
    RGBForeColor(&fillColor); PaintRect(&sFillSwatchRect);
    RGBColor swBd = { 0x7777, 0x7777, 0x7777 };
    RGBForeColor(&swBd); FrameRect(&sFillSwatchRect);
    RGBForeColor(&hint); TextSize(9);
    PStrC("Click to change", ps); MoveTo(48, static_cast<short>(y + 13)); DrawString(ps);
    TextSize(11);
    y = static_cast<short>(y + 26);

    // -------------------------------------------------------------- STROKE --
    y = DrawSectionHeader(y, "STROKE", portRect);
    y = static_cast<short>(y + 5);

    // Checkbox
    sStrokeToggleRect = { static_cast<short>(y+2), 5, static_cast<short>(y+14), 17 };
    RGBColor cbBd = { 0x7777, 0x7777, 0x7777 };
    RGBForeColor(&cbBd); FrameRect(&sStrokeToggleRect);
    if (hasStroke) {
        RGBColor checked = { 0x3333, 0x6666, 0xCCCC }; RGBForeColor(&checked);
        Rect inner = { static_cast<short>(y+4), 7, static_cast<short>(y+12), 15 };
        PaintRect(&inner);
    }

    if (hasStroke) {
        // Color swatch
        sStrokeSwatchRect = { y, 22, static_cast<short>(y+18), 58 };
        RGBForeColor(&strokeColor); PaintRect(&sStrokeSwatchRect);
        RGBForeColor(&cbBd); FrameRect(&sStrokeSwatchRect);

        // Width: label + editable value + -/+ buttons
        RGBForeColor(&labelClr);
        PStrC("W", ps); MoveTo(64, static_cast<short>(y+13)); DrawString(ps);
        DrawNumField(78, static_cast<short>(y+13), 26,
                     kFieldStrokeWidth, static_cast<SInt32>(strokeWidth), sFieldSwRect);

        sStrokeWidthDownRect = { static_cast<short>(y+2), 108, static_cast<short>(y+14), 120 };
        RGBForeColor(&cbBd); FrameRect(&sStrokeWidthDownRect);
        RGBForeColor(&valueClr); PStrC("-", ps); MoveTo(112, static_cast<short>(y+12)); DrawString(ps);

        sStrokeWidthUpRect = { static_cast<short>(y+2), 123, static_cast<short>(y+14), 135 };
        RGBForeColor(&cbBd); FrameRect(&sStrokeWidthUpRect);
        RGBForeColor(&valueClr); PStrC("+", ps); MoveTo(127, static_cast<short>(y+12)); DrawString(ps);

        y = static_cast<short>(y + 22);

        // Stroke alignment — Platinum-style popup button (QuickDraw)
        const char* alignName = (strokeAlign == 2) ? "Outside" :
                                (strokeAlign == 1) ? "Inside"  : "Center";
        sStrokeAlignRect = { static_cast<short>(y+1), 6,
                             static_cast<short>(y+18), 142 };

        {
            short aL = sStrokeAlignRect.left,  aT = sStrokeAlignRect.top;
            short aR = sStrokeAlignRect.right, aB = sStrokeAlignRect.bottom;

            // Body fill: system gray
            RGBColor platGray = { 0xCCCC, 0xCCCC, 0xCCCC };
            RGBForeColor(&platGray); PaintRect(&sStrokeAlignRect);

            // Outer black border
            RGBColor blk = { 0, 0, 0 };
            RGBForeColor(&blk); FrameRect(&sStrokeAlignRect);

            // Top+left white inner bevel
            RGBColor wht  = { 0xFFFF, 0xFFFF, 0xFFFF };
            RGBColor shad = { 0x5555, 0x5555, 0x5555 };
            RGBForeColor(&wht);
            MoveTo(static_cast<short>(aL+1), static_cast<short>(aB-2));
            LineTo(static_cast<short>(aL+1), static_cast<short>(aT+1));
            LineTo(static_cast<short>(aR-2), static_cast<short>(aT+1));
            // Bottom+right dark inner bevel
            RGBForeColor(&shad);
            MoveTo(static_cast<short>(aL+2), static_cast<short>(aB-1));
            LineTo(static_cast<short>(aR-1), static_cast<short>(aB-1));
            LineTo(static_cast<short>(aR-1), static_cast<short>(aT+2));

            // Arrow section separator (16px from right)
            short sepX = static_cast<short>(aR - 17);
            RGBForeColor(&blk);
            MoveTo(sepX, static_cast<short>(aT+1));
            LineTo(sepX, static_cast<short>(aB-1));

            // ▲▼ stacked triangles in right section
            short arrX = static_cast<short>((sepX + aR) / 2);
            short arrY = static_cast<short>((aT + aB) / 2);
            RGBForeColor(&blk);

            PolyHandle upPoly = OpenPoly();
            MoveTo(arrX,                         static_cast<short>(arrY-4));
            LineTo(static_cast<short>(arrX+3),   static_cast<short>(arrY-1));
            LineTo(static_cast<short>(arrX-3),   static_cast<short>(arrY-1));
            LineTo(arrX,                         static_cast<short>(arrY-4));
            ClosePoly();
            PaintPoly(upPoly); KillPoly(upPoly);

            PolyHandle dnPoly = OpenPoly();
            MoveTo(arrX,                         static_cast<short>(arrY+4));
            LineTo(static_cast<short>(arrX+3),   static_cast<short>(arrY+1));
            LineTo(static_cast<short>(arrX-3),   static_cast<short>(arrY+1));
            LineTo(arrX,                         static_cast<short>(arrY+4));
            ClosePoly();
            PaintPoly(dnPoly); KillPoly(dnPoly);

            // Selection text
            RGBForeColor(&blk); TextSize(11);
            PStrC(alignName, ps);
            MoveTo(10, static_cast<short>(y + 13)); DrawString(ps);
        }

        y = static_cast<short>(y + 22);
    } else {
        RGBForeColor(&hint); TextSize(9);
        PStrC("None", ps); MoveTo(22, static_cast<short>(y+12)); DrawString(ps);
        TextSize(11);
        y = static_cast<short>(y + 22);
    }

    // ---------------------------------------------------------- APPEARANCE --
    // Always shown. Left col: Opacity. Right col: Corner radius (rects + frames only).
    {
        bool isRectSel  = (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle);
        bool isFrameSel = (!gSelectedShape && gSelectedFrame);
        bool showCorner = (isRectSel || isFrameSel);
        bool indiv = showCorner && (isRectSel
            ? static_cast<const RectShape*>(gSelectedShape)->cornerIndividual
            : gSelectedFrame->cornerIndividual);

        y = DrawSectionHeader(y, "APPEARANCE", portRect);

        // Individual-corners toggle in header — only when corner is applicable
        if (showCorner) {
            Rect tBtn = { static_cast<short>(y-15), static_cast<short>(cRight-16),
                          static_cast<short>(y-1),  static_cast<short>(cRight-2) };
            sCornerIndividualBtnRect = tBtn;
            if (indiv) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
            else        { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
            PaintRect(&tBtn);
            RGBColor bd={0x7777,0x7777,0x7777}; RGBForeColor(&bd); FrameRect(&tBtn);
            RGBColor ic = indiv ? RGBColor{0xFFFF,0xFFFF,0xFFFF} : RGBColor{0x3333,0x3333,0x3333};
            RGBForeColor(&ic);
            for (int row2=0; row2<2; ++row2) for (int col2=0; col2<2; ++col2) {
                Rect sq = { static_cast<short>(y-14+row2*6), static_cast<short>(cRight-15+col2*6),
                            static_cast<short>(y-9+row2*6),  static_cast<short>(cRight-10+col2*6) };
                FrameRect(&sq);
            }
        } else {
            sCornerIndividualBtnRect = {0,0,0,0};
        }

        y = static_cast<short>(y + 5);

        UInt8 opV = gSelectedShape ? gSelectedShape->opacity
                  : (gSelectedFrame ? gSelectedFrame->opacity : 100);

        if (!indiv) {
            // Single row: [Op% field] [R field]
            RGBForeColor(&labelClr); TextSize(9);
            PStrC("Op", ps); MoveTo(6, static_cast<short>(y + 12)); DrawString(ps);
            TextSize(11);
            DrawNumField(22, static_cast<short>(y + 12), 32, kFieldOpacity,
                         static_cast<SInt32>(opV), sOpacityRect);
            RGBForeColor(&labelClr); TextSize(9);
            PStrC("%", ps); MoveTo(56, static_cast<short>(y + 12)); DrawString(ps);
            TextSize(11);
            if (showCorner) {
                SInt16 crVal = isRectSel
                    ? static_cast<const RectShape*>(gSelectedShape)->cornerRadius
                    : gSelectedFrame->cornerRadius;
                RGBForeColor(&labelClr); TextSize(9);
                PStrC("R", ps); MoveTo(84, static_cast<short>(y + 12)); DrawString(ps);
                TextSize(11);
                DrawNumField(96, static_cast<short>(y + 12), 40, kFieldCornerRadius,
                             static_cast<SInt32>(crVal), sCornerRadiusRect);
                sCornerTLRect = sCornerTRRect = sCornerBRRect = sCornerBLRect = {0,0,0,0};
            } else {
                sCornerRadiusRect = sCornerTLRect = sCornerTRRect = sCornerBRRect = sCornerBLRect = {0,0,0,0};
            }
            y = static_cast<short>(y + 22);
        } else {
            // Individual mode: Opacity row, then 2×2 corner grid
            RGBForeColor(&labelClr); TextSize(9);
            PStrC("Op", ps); MoveTo(6, static_cast<short>(y + 12)); DrawString(ps);
            TextSize(11);
            DrawNumField(22, static_cast<short>(y + 12), 32, kFieldOpacity,
                         static_cast<SInt32>(opV), sOpacityRect);
            RGBForeColor(&labelClr); TextSize(9);
            PStrC("%", ps); MoveTo(56, static_cast<short>(y + 12)); DrawString(ps);
            TextSize(11);
            y = static_cast<short>(y + 22);

            SInt16 vtl, vtr, vbr, vbl;
            if (isRectSel) {
                const auto* rs2 = static_cast<const RectShape*>(gSelectedShape);
                vtl=rs2->cornerTL; vtr=rs2->cornerTR; vbr=rs2->cornerBR; vbl=rs2->cornerBL;
            } else {
                vtl=gSelectedFrame->cornerTL; vtr=gSelectedFrame->cornerTR;
                vbr=gSelectedFrame->cornerBR; vbl=gSelectedFrame->cornerBL;
            }
            short halfW2 = static_cast<short>((cRight - 8) / 2);
            short col2X2 = static_cast<short>(4 + halfW2 + 2);
            short fldW2  = static_cast<short>(halfW2 - 22);
            // Row 1: TL | TR
            RGBForeColor(&labelClr); TextSize(9);
            PStrC("TL", ps); MoveTo(4, static_cast<short>(y+12)); DrawString(ps);
            TextSize(11);
            DrawNumField(22, static_cast<short>(y+12), fldW2, kFieldCornerTL,
                         static_cast<SInt32>(vtl), sCornerTLRect);
            RGBForeColor(&labelClr); TextSize(9);
            PStrC("TR", ps); MoveTo(col2X2, static_cast<short>(y+12)); DrawString(ps);
            TextSize(11);
            DrawNumField(static_cast<short>(col2X2+18), static_cast<short>(y+12), fldW2,
                         kFieldCornerTR, static_cast<SInt32>(vtr), sCornerTRRect);
            y = static_cast<short>(y + 18);
            // Row 2: BL | BR
            RGBForeColor(&labelClr); TextSize(9);
            PStrC("BL", ps); MoveTo(4, static_cast<short>(y+12)); DrawString(ps);
            TextSize(11);
            DrawNumField(22, static_cast<short>(y+12), fldW2, kFieldCornerBL,
                         static_cast<SInt32>(vbl), sCornerBLRect);
            RGBForeColor(&labelClr); TextSize(9);
            PStrC("BR", ps); MoveTo(col2X2, static_cast<short>(y+12)); DrawString(ps);
            TextSize(11);
            DrawNumField(static_cast<short>(col2X2+18), static_cast<short>(y+12), fldW2,
                         kFieldCornerBR, static_cast<SInt32>(vbr), sCornerBRRect);
            sCornerRadiusRect = {0,0,0,0};
            y = static_cast<short>(y + 22);
        }
    }

    // ---------------------------------------------------------- LAYOUT --
    // (Only shown when a frame is selected — not for shapes)
    if (!gSelectedShape && gSelectedFrame) {
        Frame* lf = gSelectedFrame;
        y = DrawSectionHeader(y, "LAYOUT", portRect);
        y = static_cast<short>(y + 3);

        // Flow direction buttons: [None 42] [H 30] [V 30] [Wrap 50]
        // None/H/V toggle the layout mode; Wrap toggles wrapping within H or V.
        {
            struct { const char* label; short x; short w; } btnDef[4] = {
                { "None", 5,  42 },
                { "H",   51,  30 },
                { "V",   85,  30 },
                { "Wrap",119, 50 },
            };
            for (int i = 0; i < 4; ++i) {
                short bx = btnDef[i].x, bw = btnDef[i].w;
                Rect btn = { y, bx, static_cast<short>(y+18), static_cast<short>(bx+bw) };

                bool active = false;
                bool dimmed = false;
                if (i < 3) {
                    active = (static_cast<UInt8>(lf->layoutMode) == i);
                } else {
                    active = (lf->layoutMode != LayoutMode::None && lf->layoutWrap);
                    dimmed = (lf->layoutMode == LayoutMode::None);
                }

                if      (active)  { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
                else if (dimmed)  { RGBColor bg={0xEEEE,0xEEEE,0xEEEE}; RGBForeColor(&bg); }
                else              { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
                PaintRect(&btn);
                if (dimmed) { RGBColor bd={0xAAAA,0xAAAA,0xAAAA}; RGBForeColor(&bd); }
                else        { RGBColor bd={0x7777,0x7777,0x7777}; RGBForeColor(&bd); }
                FrameRect(&btn);

                if      (active)  { RGBColor tc={0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&tc); }
                else if (dimmed)  { RGBColor tc={0xAAAA,0xAAAA,0xAAAA}; RGBForeColor(&tc); }
                else              { RGBColor tc={0x3333,0x3333,0x3333}; RGBForeColor(&tc); }
                TextSize(9); PStrC(btnDef[i].label, ps);
                short tw = StringWidth(ps);
                MoveTo(static_cast<short>(bx + (bw - tw)/2), static_cast<short>(y + 13));
                DrawString(ps); TextSize(11);

                if (i < 3) sLayoutModeRect[i] = btn;
                else        sWrapRect = btn;
            }
        }
        y = static_cast<short>(y + 24);

        if (lf->layoutMode != LayoutMode::None) {
            // 3×3 alignment grid — left half
            // Each cell encodes a combination of primary + cross alignment.
            // For H layout: col=primary(S/C/E), row=cross(S/C/E).
            // For V layout: row=primary(S/C/E), col=cross(S/C/E).
            static const short kCell = 14, kCellGap = 4;
            short gridX = 8, gridY = y;
            short gridSpan = 3 * kCell + 2 * kCellGap; // 50px

            bool isSB = (lf->primaryAlign == PrimaryAlign::SpaceBetween);
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    short cx = static_cast<short>(gridX + col*(kCell+kCellGap));
                    short cy = static_cast<short>(gridY + row*(kCell+kCellGap));
                    Rect cell = { cy, cx, static_cast<short>(cy+kCell), static_cast<short>(cx+kCell) };
                    sAlignCellRect[row*3+col] = cell;

                    // Active cell = current alignment combo
                    bool active = false;
                    if (!isSB) {
                        int priIdx = static_cast<int>(lf->primaryAlign);
                        int secIdx = static_cast<int>(lf->crossAlign);
                        active = (lf->layoutMode == LayoutMode::Horizontal)
                                 ? (col == priIdx && row == secIdx)
                                 : (row == priIdx && col == secIdx);
                    }
                    if (active) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
                    else        { RGBColor bg={0xEEEE,0xEEEE,0xEEEE}; RGBForeColor(&bg); }
                    PaintRect(&cell);
                    RGBColor bd={0xAAAA,0xAAAA,0xAAAA}; RGBForeColor(&bd); FrameRect(&cell);

                    // Dot in cell centre
                    short dotX = static_cast<short>(cx + (kCell-4)/2);
                    short dotY = static_cast<short>(cy + (kCell-4)/2);
                    Rect dot = { dotY, dotX, static_cast<short>(dotY+4), static_cast<short>(dotX+4) };
                    if (active) { RGBColor dc={0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&dc); }
                    else        { RGBColor dc={0x7777,0x7777,0x7777}; RGBForeColor(&dc); }
                    PaintRect(&dot);
                }
            }

            // Right column: primary gap + optional counter gap (Wrap only)
            {
                short rX    = static_cast<short>(gridX + gridSpan + 8); // x=66
                short popW  = 46;
                short valX  = static_cast<short>(rX + popW + 4);        // x=116
                short valW  = static_cast<short>(cRight - valX - 4); // 56px

                // Row 1 y: centre in grid when no Wrap; shift up when Wrap to leave room
                short row1Y = lf->layoutWrap
                              ? static_cast<short>(gridY + 2)
                              : static_cast<short>(gridY + (gridSpan - 25) / 2);

                // — Primary gap row —
                RGBForeColor(&labelClr); TextSize(9);
                PStrC("Gap", ps); MoveTo(rX, static_cast<short>(row1Y+9)); DrawString(ps);
                TextSize(11);
                const char* gapModeName = isSB ? "Auto" : "Fixed";
                DrawPlatinumBtn(rX, static_cast<short>(row1Y+11), popW, 14, gapModeName, sLayoutGapModeRect);
                if (!isSB) {
                    DrawNumField(valX, static_cast<short>(row1Y+23), valW,
                                 kFieldLayoutGap, static_cast<SInt32>(lf->layoutGap), sLayoutGapRect);
                } else {
                    sLayoutGapRect = {0,0,0,0};
                }

                // — Counter gap row (cross-axis, Wrap only) —
                if (lf->layoutWrap) {
                    short row2Y = static_cast<short>(row1Y + 38);

                    // Small ↕ icon: top bar, vertical shaft, bottom bar
                    RGBForeColor(&labelClr);
                    short ix = rX, iy = static_cast<short>(row2Y + 2);
                    MoveTo(ix, iy); LineTo(static_cast<short>(ix+8), iy);
                    MoveTo(static_cast<short>(ix+4), static_cast<short>(iy+2));
                    LineTo(static_cast<short>(ix+4), static_cast<short>(iy+9));
                    MoveTo(ix, static_cast<short>(iy+11));
                    LineTo(static_cast<short>(ix+8), static_cast<short>(iy+11));

                    TextSize(9);
                    PStrC("Row", ps);
                    MoveTo(static_cast<short>(rX+10), static_cast<short>(row2Y+9));
                    DrawString(ps); TextSize(11);

                    const char* cgModeName = lf->layoutCounterGapAuto ? "Auto" : "Fixed";
                    DrawPlatinumBtn(rX, static_cast<short>(row2Y+11), popW, 14,
                                    cgModeName, sLayoutCounterGapModeRect);
                    if (!lf->layoutCounterGapAuto) {
                        DrawNumField(valX, static_cast<short>(row2Y+23), valW,
                                     kFieldCounterGap,
                                     static_cast<SInt32>(lf->layoutCounterGap),
                                     sLayoutCounterGapRect);
                    } else {
                        sLayoutCounterGapRect = {0,0,0,0};
                    }
                } else {
                    sLayoutCounterGapRect = sLayoutCounterGapModeRect = {0,0,0,0};
                }
            }

            // Advance y past the gap column content (may extend below 50px grid when Wrap)
            y = lf->layoutWrap
                ? static_cast<short>(gridY + 2 + 38 + 43)   // row2 (popup+field) + padding
                : static_cast<short>(gridY + gridSpan + 6);

            // Padding section (full width, below grid+gap area)
            {
                short tBtnX = static_cast<short>(cRight - 18);
                // Expand/collapse toggle button
                Rect tBtn = { y, tBtnX, static_cast<short>(y+14), static_cast<short>(tBtnX+14) };
                sPadMixedBtnRect = tBtn;
                if (sMixedPadding) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
                else               { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
                PaintRect(&tBtn);
                RGBColor bd={0x7777,0x7777,0x7777}; RGBForeColor(&bd); FrameRect(&tBtn);
                // 2×2 grid icon inside toggle
                if (sMixedPadding) { RGBColor ic={0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&ic); }
                else               { RGBColor ic={0x3333,0x3333,0x3333}; RGBForeColor(&ic); }
                for (int r=0; r<2; ++r) for (int c=0; c<2; ++c) {
                    Rect sq = { static_cast<short>(y+2+r*5), static_cast<short>(tBtnX+2+c*5),
                                static_cast<short>(y+6+r*5), static_cast<short>(tBtnX+6+c*5) };
                    FrameRect(&sq);
                }

                // Field geometry: two equal columns left of the toggle button
                short availW = static_cast<short>(tBtnX - 4 - 4);
                short halfW  = static_cast<short>(availW / 2);
                short icoW   = 12;  // icon(10) + gap(2)
                short fldW   = static_cast<short>(halfW - icoW - 2);
                short col1X  = 4;
                short col2X  = static_cast<short>(col1X + halfW + 2);

                if (!sMixedPadding) {
                    // Compact: H (left+right) | V (top+bottom) — shows "a, b" when sides differ
                    DrawPadIcon(col1X, static_cast<short>(y+2), 0x0A);  // left+right
                    {
                        std::string sh = padCompactStr(lf->paddingLeft, lf->paddingRight);
                        DrawStrField(static_cast<short>(col1X+icoW), static_cast<short>(y+12), fldW,
                                     kFieldPadH, sh.c_str(), sPadHRect);
                    }
                    DrawPadIcon(col2X, static_cast<short>(y+2), 0x05);  // top+bottom
                    {
                        std::string sv = padCompactStr(lf->paddingTop, lf->paddingBottom);
                        DrawStrField(static_cast<short>(col2X+icoW), static_cast<short>(y+12), fldW,
                                     kFieldPadV, sv.c_str(), sPadVRect);
                    }
                    sPadTopRect = sPadRightRect = sPadBottomRect = sPadLeftRect = {0,0,0,0};
                    y = static_cast<short>(y + 18);
                } else {
                    // Mixed: Top+Right on row 1, Bottom+Left on row 2
                    DrawPadIcon(col1X, static_cast<short>(y+2), 0x01);   // top
                    DrawNumField(static_cast<short>(col1X+icoW), static_cast<short>(y+12), fldW,
                                 kFieldPadTop, static_cast<SInt32>(lf->paddingTop), sPadTopRect);
                    DrawPadIcon(col2X, static_cast<short>(y+2), 0x02);   // right
                    DrawNumField(static_cast<short>(col2X+icoW), static_cast<short>(y+12), fldW,
                                 kFieldPadRight, static_cast<SInt32>(lf->paddingRight), sPadRightRect);
                    y = static_cast<short>(y + 18);
                    DrawPadIcon(col1X, static_cast<short>(y+2), 0x04);   // bottom
                    DrawNumField(static_cast<short>(col1X+icoW), static_cast<short>(y+12), fldW,
                                 kFieldPadBottom, static_cast<SInt32>(lf->paddingBottom), sPadBottomRect);
                    DrawPadIcon(col2X, static_cast<short>(y+2), 0x08);   // left
                    DrawNumField(static_cast<short>(col2X+icoW), static_cast<short>(y+12), fldW,
                                 kFieldPadLeft, static_cast<SInt32>(lf->paddingLeft), sPadLeftRect);
                    sPadHRect = sPadVRect = {0,0,0,0};
                    y = static_cast<short>(y + 18);
                }
            }
        }

        // Clip content checkbox (all frames, regardless of layout mode)
        y = static_cast<short>(y + 4);
        sClipContentRect = { static_cast<short>(y+2), 5, static_cast<short>(y+14), 17 };
        RGBColor cbBd2 = {0x7777,0x7777,0x7777}; RGBForeColor(&cbBd2); FrameRect(&sClipContentRect);
        if (lf->clipContent) {
            RGBColor ck={0x3333,0x6666,0xCCCC}; RGBForeColor(&ck);
            Rect inner2 = { static_cast<short>(y+4), 7, static_cast<short>(y+12), 15 };
            PaintRect(&inner2);
        }
        RGBForeColor(&labelClr); TextSize(10);
        PStrC("Clip content", ps); MoveTo(22, static_cast<short>(y+12)); DrawString(ps);
        TextSize(11);
        y = static_cast<short>(y + 18);

        // Layout settings button — visible below Clip content when a layout mode is active
        if (lf->layoutMode != LayoutMode::None) {
            DrawPlatinumBtn(5, y, static_cast<short>(cRight - 10), 14,
                            "Layout settings...", sLayoutSettingsRect);
            y = static_cast<short>(y + 18);
        } else {
            sLayoutSettingsRect = {0,0,0,0};
        }
    }

    // ---------------------------------------------------------- POSITION --
    // One section (matching Figma's single "Position" panel), broken into
    // sub-labeled groups: Alignment, Position (+ Absolute position toggle for
    // an Auto Layout child), Constraints (shown only for a free child, or an
    // absolute-positioned layout child), Rotation.
    Frame* posParent = gSelectedShape ? gSelectedFrame
                      : (gSelectedFrame ? gSelectedFrame->parent : nullptr);
    bool inLayoutParent = posParent && posParent->layoutMode != LayoutMode::None;
    bool isAbsPos = gSelectedShape ? gSelectedShape->isAbsolutePosition
                  : (gSelectedFrame ? gSelectedFrame->isAbsolutePosition : false);
    bool showConstraints = posParent && (!inLayoutParent || isAbsPos);

    y = DrawSectionHeader(y, "POSITION", portRect);
    y = static_cast<short>(y + 4);

    y = DrawSubLabel(y, "Alignment");
    y = DrawAlignRow(y, AnyAlignableSelected());
    y = static_cast<short>(y + 4);

    y = DrawSubLabel(y, "Position");
    RGBForeColor(&labelClr); PStrC("X", ps); MoveTo(6,  static_cast<short>(y+12)); DrawString(ps);
    DrawNumField(20, static_cast<short>(y+12), 64, kFieldX, bounds.x, sFieldXRect);
    RGBForeColor(&labelClr); PStrC("Y", ps); MoveTo(94, static_cast<short>(y+12)); DrawString(ps);
    DrawNumField(106, static_cast<short>(y+12), 62, kFieldY, bounds.y, sFieldYRect);
    y = static_cast<short>(y + 22);

    if (inLayoutParent) {
        sAbsolutePositionRect = { y, 6, static_cast<short>(y+14), 20 };
        RGBColor wbg = {0xFFFF,0xFFFF,0xFFFF}; RGBForeColor(&wbg); PaintRect(&sAbsolutePositionRect);
        RGBColor bd  = {0x7777,0x7777,0x7777}; RGBForeColor(&bd);  FrameRect(&sAbsolutePositionRect);
        if (isAbsPos) {
            RGBColor chk = {0x3333,0x6666,0xCCCC}; RGBForeColor(&chk);
            Rect inner = { static_cast<short>(y+3), 9, static_cast<short>(y+11), 17 };
            PaintRect(&inner);
        }
        RGBForeColor(&labelClr); TextSize(10);
        PStrC("Absolute position", ps); MoveTo(26, static_cast<short>(y+11)); DrawString(ps);
        TextSize(11);
        y = static_cast<short>(y + 20);
    } else {
        sAbsolutePositionRect = {0,0,0,0};
    }

    if (showConstraints) {
        ConstraintMode ch = gSelectedShape ? gSelectedShape->constraintH
                          : (gSelectedFrame ? gSelectedFrame->constraintH : ConstraintMode::Start);
        ConstraintMode cv = gSelectedShape ? gSelectedShape->constraintV
                          : (gSelectedFrame ? gSelectedFrame->constraintV : ConstraintMode::Start);

        y = DrawSubLabel(y, "Constraints");
        short rowTop = y;
        DrawPlatinumBtn(6, y, 84, 18, ConstraintModeLabel(ch, true),  sConstraintHRect);
        y = static_cast<short>(y + 22);
        DrawPlatinumBtn(6, y, 84, 18, ConstraintModeLabel(cv, false), sConstraintVRect);
        y = static_cast<short>(y + 22);

        DrawConstraintMap(100, rowTop, 44, ch, cv);
        short mapEnd = static_cast<short>(rowTop + 44 + 4);
        if (mapEnd > y) y = mapEnd;
    } else {
        sConstraintHRect = sConstraintVRect = {0,0,0,0};
    }

    y = DrawSubLabel(y, "Rotation");
    {
        SInt16 rotV = gSelectedShape ? gSelectedShape->rotation
                    : (gSelectedFrame ? gSelectedFrame->rotation : 0);
        RGBForeColor(&labelClr); TextSize(9);
        PStrC("\xB0", ps); MoveTo(6, static_cast<short>(y + 12)); DrawString(ps);
        TextSize(11);
        DrawNumField(18, static_cast<short>(y + 12), 36, kFieldRotation,
                     static_cast<SInt32>(rotV), sRotationRect);
        y = static_cast<short>(y + 22);
    }

    // -------------------------------------------------------------- SIZE --
    y = DrawSectionHeader(y, "SIZE", portRect);
    y = static_cast<short>(y + 5);

    bool isFrameSel = (!gSelectedShape && gSelectedFrame);

    if (isFrameSel) {
        // Frames: W and H on separate rows with sizing popup. Lock button at right edge.
        short lockW  = 14;
        short lockX  = static_cast<short>(cRight - lockW - 4);
        short popW   = 54;
        short popX   = static_cast<short>(lockX - 4 - popW);
        short valW   = static_cast<short>(popX - 20 - 4);
        short yStart = y;

        // W row — always editable; typing auto-switches to Fixed
        RGBForeColor(&labelClr); PStrC("W", ps); MoveTo(6, static_cast<short>(y+13)); DrawString(ps);
        DrawNumField(20, static_cast<short>(y+13), valW, kFieldW, bounds.w, sFieldWRect);
        const char* wSizName = (gSelectedFrame->widthSizing == SizingMode::Hug)  ? "Hug"
                             : (gSelectedFrame->widthSizing == SizingMode::Fill) ? "Fill" : "Fixed";
        DrawPlatinumBtn(popX, y, popW, 18, wSizName, sWidthSizingPopupRect);
        y = static_cast<short>(y + 22);

        // H row — always editable; typing auto-switches to Fixed
        RGBForeColor(&labelClr); PStrC("H", ps); MoveTo(6, static_cast<short>(y+13)); DrawString(ps);
        DrawNumField(20, static_cast<short>(y+13), valW, kFieldH, bounds.h, sFieldHRect);
        const char* hSizName = (gSelectedFrame->heightSizing == SizingMode::Hug)  ? "Hug"
                             : (gSelectedFrame->heightSizing == SizingMode::Fill) ? "Fill" : "Fixed";
        DrawPlatinumBtn(popX, y, popW, 18, hSizName, sHeightSizingPopupRect);
        y = static_cast<short>(y + 24);

        // Aspect-ratio lock button — centred vertically between the two rows
        {
            short ly = static_cast<short>(yStart + (y - yStart - lockW) / 2);
            sAspectLockRect = { ly, lockX, static_cast<short>(ly+lockW), static_cast<short>(lockX+lockW) };
            if (sAspectLocked) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); PaintRect(&sAspectLockRect); }
            else               { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); PaintRect(&sAspectLockRect); }
            RGBColor bd={0x7777,0x7777,0x7777}; RGBForeColor(&bd); FrameRect(&sAspectLockRect);
            DrawLockIcon(static_cast<short>(lockX+1), static_cast<short>(ly+1), sAspectLocked);
        }
    } else {
        // Shapes: W and H on same row with lock button between them.
        // Multi-select shows "Mixed" when selected shapes differ (typing a value
        // sets it on all — see ApplyInspectorEdit's applyMulti branch).
        RGBForeColor(&labelClr); PStrC("W", ps); MoveTo(6, static_cast<short>(y+12)); DrawString(ps);
        if (isMulti && MixedW(gSelectedShapes))
            DrawStrField(20, static_cast<short>(y+12), 56, kFieldW, "Mixed", sFieldWRect);
        else
            DrawNumField(20, static_cast<short>(y+12), 56, kFieldW, bounds.w, sFieldWRect);
        // Lock button
        sAspectLockRect = { static_cast<short>(y+1), 80, static_cast<short>(y+15), 94 };
        if (sAspectLocked) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); PaintRect(&sAspectLockRect); }
        else               { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); PaintRect(&sAspectLockRect); }
        RGBColor bd2={0x7777,0x7777,0x7777}; RGBForeColor(&bd2); FrameRect(&sAspectLockRect);
        DrawLockIcon(81, static_cast<short>(y+2), sAspectLocked);
        RGBForeColor(&labelClr); PStrC("H", ps); MoveTo(98, static_cast<short>(y+12)); DrawString(ps);
        if (isMulti && MixedH(gSelectedShapes))
            DrawStrField(112, static_cast<short>(y+12), static_cast<short>(cRight-116), kFieldH, "Mixed", sFieldHRect);
        else
            DrawNumField(112, static_cast<short>(y+12), static_cast<short>(cRight-116), kFieldH, bounds.h, sFieldHRect);
        y = static_cast<short>(y + 22);

        // Fill sizing buttons — only shown when shape lives inside an active layout frame
        if (gSelectedShape && gSelectedFrame && gSelectedFrame->layoutMode != LayoutMode::None) {
            const UInt8 wSz = gSelectedShape->wSizing;
            const UInt8 hSz = gSelectedShape->hSizing;
            const UInt8 kFx = 0, kFl = 2;  // Fixed=0, Fill=2 (matches SizingMode)

            auto drawSzBtn = [&](Rect& outR, short x, short y2, short w2, UInt8 cur, UInt8 mode,
                                 const char* lbl) {
                outR = { y2, x, static_cast<short>(y2+14), static_cast<short>(x+w2) };
                bool active = (cur == mode);
                if (active) { RGBColor bg={0x3333,0x6666,0xCCCC}; RGBForeColor(&bg); }
                else        { RGBColor bg={0xDDDD,0xDDDD,0xDDDD}; RGBForeColor(&bg); }
                PaintRect(&outR);
                RGBColor bde={0x7777,0x7777,0x7777}; RGBForeColor(&bde); FrameRect(&outR);
                RGBColor fg = active ? RGBColor{0xFFFF,0xFFFF,0xFFFF}
                                     : RGBColor{0x2222,0x2222,0x2222};
                RGBForeColor(&fg); TextSize(9);
                Str255 pss; PStrC(lbl, pss);
                MoveTo(static_cast<short>(x+3), static_cast<short>(y2+10)); DrawString(pss);
                TextSize(11);
            };

            RGBForeColor(&labelClr); TextSize(9);
            PStrC("W", ps); MoveTo(6,  static_cast<short>(y+11)); DrawString(ps);
            drawSzBtn(sShapeWFxRect, 16, y, 26, wSz, kFx, "Fixed");
            drawSzBtn(sShapeWFlRect, 45, y, 26, wSz, kFl, "Fill");

            RGBForeColor(&labelClr);
            PStrC("H", ps); MoveTo(82, static_cast<short>(y+11)); DrawString(ps);
            drawSzBtn(sShapeHFxRect, 92,  y, 26, hSz, kFx, "Fixed");
            drawSzBtn(sShapeHFlRect, 121, y, 26, hSz, kFl, "Fill");

            TextSize(11);
            y = static_cast<short>(y + 18);
        } else {
            sShapeWFxRect = sShapeWFlRect = sShapeHFxRect = sShapeHFlRect = {0,0,0,0};
        }
    }

    {
        SInt32 mnW = gSelectedShape ? gSelectedShape->minWidth  : (gSelectedFrame ? gSelectedFrame->minWidth  : -1);
        SInt32 mxW = gSelectedShape ? gSelectedShape->maxWidth  : (gSelectedFrame ? gSelectedFrame->maxWidth  : -1);
        SInt32 mnH = gSelectedShape ? gSelectedShape->minHeight : (gSelectedFrame ? gSelectedFrame->minHeight : -1);
        SInt32 mxH = gSelectedShape ? gSelectedShape->maxHeight : (gSelectedFrame ? gSelectedFrame->maxHeight : -1);
        y = DrawMinMaxSizeRows(y, cRight, labelClr, mnW, mxW, mnH, mxH);
    }

    gInspectorTotalH = static_cast<short>(y + 8);  // record content height

    // Restore origin before drawing controls
    SetOrigin(0, 0);

    // Update and redraw scroll bar
    if (gInspectorScrollCtrl) {
        short iMax = (gInspectorTotalH > panelH) ? static_cast<short>(gInspectorTotalH - panelH) : 0;
        SetControlMaximum(gInspectorScrollCtrl, iMax);
        SetControlValue(gInspectorScrollCtrl, gInspectorScrollY);
        HiliteControl(gInspectorScrollCtrl, (iMax > 0) ? 0 : 255);
        DrawControls(gInspectorWindow);
    }

    TextSize(12); PenNormal(); RGBForeColor(&black); RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Tab navigation helpers
// --------------------------------------------------------------------------

// Start editing a field, pre-filling with its current value.
static void StartEditForField(EditField field) {
    if (!gSelectedShape && !gSelectedFrame) return;
    Bounds2* b = gSelectedShape ? &gSelectedShape->bounds
                                : (gSelectedFrame ? &gSelectedFrame->bounds : nullptr);
    switch (field) {
        case kFieldFontSize:
            if (gSelectedShape && gSelectedShape->GetType() == Shape::kText)
                StartEdit(field, static_cast<SInt32>(static_cast<TextShape&>(*gSelectedShape).fontSize));
            break;
        case kFieldStrokeWidth: {
            UInt16 sw = gSelectedShape ? gSelectedShape->strokeWidth
                                       : (gSelectedFrame ? gSelectedFrame->strokeWidth : 1);
            StartEdit(field, static_cast<SInt32>(sw));
            break;
        }
        case kFieldLayoutGap:
            if (gSelectedFrame) StartEdit(field, static_cast<SInt32>(gSelectedFrame->layoutGap));
            break;
        case kFieldCounterGap:
            if (gSelectedFrame) StartEdit(field, static_cast<SInt32>(gSelectedFrame->layoutCounterGap));
            break;
        case kFieldCornerRadius: {
            SInt16 crv = 0;
            if (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
                crv = static_cast<RectShape&>(*gSelectedShape).cornerRadius;
            else if (gSelectedFrame)
                crv = gSelectedFrame->cornerRadius;
            StartEdit(field, static_cast<SInt32>(crv));
            break;
        }
        case kFieldCornerTL:
            if (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
                StartEdit(field, static_cast<RectShape&>(*gSelectedShape).cornerTL);
            else if (gSelectedFrame) StartEdit(field, gSelectedFrame->cornerTL);
            break;
        case kFieldCornerTR:
            if (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
                StartEdit(field, static_cast<RectShape&>(*gSelectedShape).cornerTR);
            else if (gSelectedFrame) StartEdit(field, gSelectedFrame->cornerTR);
            break;
        case kFieldCornerBR:
            if (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
                StartEdit(field, static_cast<RectShape&>(*gSelectedShape).cornerBR);
            else if (gSelectedFrame) StartEdit(field, gSelectedFrame->cornerBR);
            break;
        case kFieldCornerBL:
            if (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
                StartEdit(field, static_cast<RectShape&>(*gSelectedShape).cornerBL);
            else if (gSelectedFrame) StartEdit(field, gSelectedFrame->cornerBL);
            break;
        case kFieldOpacity:
            if (gSelectedShape)       StartEdit(field, static_cast<SInt32>(gSelectedShape->opacity));
            else if (gSelectedFrame)  StartEdit(field, static_cast<SInt32>(gSelectedFrame->opacity));
            break;
        case kFieldRotation:
            if (gSelectedShape)       StartEdit(field, static_cast<SInt32>(gSelectedShape->rotation));
            else if (gSelectedFrame)  StartEdit(field, static_cast<SInt32>(gSelectedFrame->rotation));
            break;
        case kFieldPadH:
            if (gSelectedFrame) {
                std::string s = padCompactStr(gSelectedFrame->paddingLeft, gSelectedFrame->paddingRight);
                StartEditStr(field, s.c_str());
            }
            break;
        case kFieldPadV:
            if (gSelectedFrame) {
                std::string s = padCompactStr(gSelectedFrame->paddingTop, gSelectedFrame->paddingBottom);
                StartEditStr(field, s.c_str());
            }
            break;
        case kFieldPadTop:    if (gSelectedFrame) StartEdit(field, gSelectedFrame->paddingTop);    break;
        case kFieldPadRight:  if (gSelectedFrame) StartEdit(field, gSelectedFrame->paddingRight);  break;
        case kFieldPadBottom: if (gSelectedFrame) StartEdit(field, gSelectedFrame->paddingBottom); break;
        case kFieldPadLeft:   if (gSelectedFrame) StartEdit(field, gSelectedFrame->paddingLeft);   break;
        case kFieldX: if (b) StartEdit(field, b->x); break;
        case kFieldY: if (b) StartEdit(field, b->y); break;
        case kFieldW: if (b) StartEdit(field, b->w); break;
        case kFieldH: if (b) StartEdit(field, b->h); break;
        case kFieldMinW:
            StartEditMinMax(field, gSelectedShape ? gSelectedShape->minWidth  : (gSelectedFrame ? gSelectedFrame->minWidth  : -1));
            break;
        case kFieldMaxW:
            StartEditMinMax(field, gSelectedShape ? gSelectedShape->maxWidth  : (gSelectedFrame ? gSelectedFrame->maxWidth  : -1));
            break;
        case kFieldMinH:
            StartEditMinMax(field, gSelectedShape ? gSelectedShape->minHeight : (gSelectedFrame ? gSelectedFrame->minHeight : -1));
            break;
        case kFieldMaxH:
            StartEditMinMax(field, gSelectedShape ? gSelectedShape->maxHeight : (gSelectedFrame ? gSelectedFrame->maxHeight : -1));
            break;
        default: break;
    }
}

// Build the ordered list of currently-visible editable fields and return the
// next (or previous) one relative to `cur`.
static EditField TabToNextField(EditField cur, bool reverse) {
    std::vector<EditField> order;

    // Text size (text shapes only)
    if (gSelectedShape && gSelectedShape->GetType() == Shape::kText)
        order.push_back(kFieldFontSize);

    // Stroke width (when stroke is enabled)
    bool hasStroke = gSelectedShape ? gSelectedShape->hasStroke
                                    : (gSelectedFrame ? gSelectedFrame->hasStroke : false);
    if (hasStroke) order.push_back(kFieldStrokeWidth);

    // Opacity (all shape and frame types)
    order.push_back(kFieldOpacity);

    // Corner radius (rect shapes and frames)
    bool isRectShape2 = (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle);
    if (isRectShape2 || (!gSelectedShape && gSelectedFrame)) {
        bool indiv2 = isRectShape2
            ? static_cast<const RectShape&>(*gSelectedShape).cornerIndividual
            : gSelectedFrame->cornerIndividual;
        if (indiv2) {
            order.push_back(kFieldCornerTL); order.push_back(kFieldCornerTR);
            order.push_back(kFieldCornerBR); order.push_back(kFieldCornerBL);
        } else {
            order.push_back(kFieldCornerRadius);
        }
    }

    // Layout fields (frame with active layout only)
    if (!gSelectedShape && gSelectedFrame && gSelectedFrame->layoutMode != LayoutMode::None) {
        bool isSB = (gSelectedFrame->primaryAlign == PrimaryAlign::SpaceBetween);
        if (!isSB) order.push_back(kFieldLayoutGap);
        if (sMixedPadding) {
            order.push_back(kFieldPadTop);
            order.push_back(kFieldPadRight);
            order.push_back(kFieldPadBottom);
            order.push_back(kFieldPadLeft);
        } else {
            order.push_back(kFieldPadH);
            order.push_back(kFieldPadV);
        }
    }

    // Position + Rotation
    order.push_back(kFieldX);
    order.push_back(kFieldY);
    order.push_back(kFieldRotation);

    // Size — always in tab order (typing auto-switches Hug/Fill frames to Fixed)
    order.push_back(kFieldW);
    order.push_back(kFieldH);
    order.push_back(kFieldMinW);
    order.push_back(kFieldMaxW);
    order.push_back(kFieldMinH);
    order.push_back(kFieldMaxH);

    if (order.empty()) return kNoField;

    int idx = -1;
    for (int i = 0; i < (int)order.size(); ++i)
        if (order[i] == cur) { idx = i; break; }

    if (idx < 0) return order[0];
    if (reverse) idx = static_cast<int>((idx - 1 + order.size()) % order.size());
    else         idx = static_cast<int>((idx + 1)               % order.size());
    return order[idx];
}

// --------------------------------------------------------------------------
// Inline numeric field key handling
// --------------------------------------------------------------------------

bool HandleInspectorKey(char key, UInt16 modifiers) {
    if (sActiveField == kNoField) return false;

    // If the object was locked while an edit was in progress, cancel it
    bool isLocked = (gSelectedShape ? gSelectedShape->locked
                                    : (gSelectedFrame ? gSelectedFrame->locked : false));
    if (isLocked) {
        sActiveField = kNoField; sEditLen = 0; sEditBuf[0] = '\0';
        InvalidateInspector();
        return false;
    }

    if (key == 0x1B) {  // Escape — cancel
        sActiveField = kNoField; sEditLen = 0; sEditBuf[0] = '\0';
        InvalidateInspector();
        return true;
    }

    if (key == 0x09) {  // Tab — apply current field and move to next (Shift+Tab = previous)
        EditField prev = sActiveField;
        ApplyInspectorEdit();
        EditField next = TabToNextField(prev, (modifiers & shiftKey) != 0);
        if (next != kNoField) StartEditForField(next);
        return true;
    }

    if (key == 0x0D || key == 0x03) {  // Return or Enter — apply
        ApplyInspectorEdit();
        return true;
    }

    if (key == 0x08) {  // Backspace
        if (sEditLen > 0) sEditBuf[--sEditLen] = '\0';
        InvalidateInspector();
        return true;
    }

    // Allow comma separator in compact padding fields ("a, b" format)
    if (key == ',' && (sActiveField == kFieldPadH || sActiveField == kFieldPadV) && sEditLen < 9) {
        bool hasComma = false;
        for (int i = 0; i < sEditLen; ++i) if (sEditBuf[i] == ',') { hasComma = true; break; }
        if (!hasComma && sEditLen > 0) {
            sEditBuf[sEditLen++] = ','; sEditBuf[sEditLen++] = ' '; sEditBuf[sEditLen] = '\0';
            InvalidateInspector();
            return true;
        }
    }

    {
        short maxLen = (sActiveField == kFieldPadH || sActiveField == kFieldPadV) ? 10 : 7;
        if (key >= '0' && key <= '9' && sEditLen < maxLen) {
            sEditBuf[sEditLen++] = key; sEditBuf[sEditLen] = '\0';
            InvalidateInspector();
            return true;
        }
    }

    if (key == '-' && sEditLen == 0 &&
        (sActiveField == kFieldX || sActiveField == kFieldY)) {
        sEditBuf[sEditLen++] = '-'; sEditBuf[sEditLen] = '\0';
        InvalidateInspector();
        return true;
    }

    return true;  // consume all other keys while in edit mode
}

bool InspectorInEditMode() { return sActiveField != kNoField; }

void ApplyInspectorEdit() {
    if (sActiveField == kNoField) return;
    // Min/Max fields are the one exception to "blank cancels": an empty buffer
    // there means "clear this bound" (-1 = unset), a legitimate value to commit,
    // not a no-op click-away.
    bool isMinMaxField = (sActiveField == kFieldMinW || sActiveField == kFieldMaxW ||
                          sActiveField == kFieldMinH || sActiveField == kFieldMaxH);
    // An empty buffer (e.g. clicking into a "Mixed" field, which starts blank, and
    // clicking away without typing) must cancel rather than commit — otherwise it
    // parses as 0, and every numeric field's clamp floor would silently apply that
    // as 1 to every selected item instead of leaving them alone.
    if (sEditLen == 0 && !isMinMaxField) { CancelInspectorEdit(); return; }
    sEditBuf[sEditLen] = '\0';

    SInt32 val = 0; int i = 0; bool neg = false;
    if (sEditLen == 0 && isMinMaxField) {
        val = -1;
    } else {
        if (sEditLen > 0 && sEditBuf[0] == '-') { neg = true; i = 1; }
        for (; i < sEditLen; ++i)
            if (sEditBuf[i] >= '0' && sEditBuf[i] <= '9')
                val = val * 10 + (sEditBuf[i] - '0');
        if (neg) val = -val;
        if (isMinMaxField && val < 0) val = 0;  // typed "-5" etc.: floor at 0, not the unset sentinel
    }

    const bool applyMulti      = (gSelectedShapes.size() > 1);
    const bool applyMultiFrame = (gSelectedFrames.size() > 1);

    Bounds2* b  = gSelectedShape ? &gSelectedShape->bounds
                                 : (gSelectedFrame ? &gSelectedFrame->bounds : nullptr);
    UInt16*  sw = gSelectedShape ? &gSelectedShape->strokeWidth
                                 : (gSelectedFrame ? &gSelectedFrame->strokeWidth : nullptr);

    SInt32 origW = b ? b->w : 0;
    SInt32 origH = b ? b->h : 0;
    EditField appliedField = sActiveField;

    bool changed = false;

    // Multi-frame edits: apply delta for X/Y, set value for W/H
    if (applyMultiFrame && b) {
        switch (sActiveField) {
            case kFieldX: {
                SInt32 dx = val - b->x;
                if (dx != 0) { PushUndo(); for (Frame* f : gSelectedFrames) f->bounds.x += dx; changed = true; }
            } break;
            case kFieldY: {
                SInt32 dy = val - b->y;
                if (dy != 0) { PushUndo(); for (Frame* f : gSelectedFrames) f->bounds.y += dy; changed = true; }
            } break;
            case kFieldW: {
                if (val < 1) val = 1;
                // Compare against every selected frame, not just b (the last-selected
                // representative) — a mixed selection can coincidentally match b's
                // current value while still needing to change on the others.
                bool any = false;
                for (Frame* f : gSelectedFrames) if (f->bounds.w != val) { any = true; break; }
                if (any) { PushUndo(); for (Frame* f : gSelectedFrames) f->bounds.w = val; changed = true; }
            } break;
            case kFieldH: {
                if (val < 1) val = 1;
                bool any = false;
                for (Frame* f : gSelectedFrames) if (f->bounds.h != val) { any = true; break; }
                if (any) { PushUndo(); for (Frame* f : gSelectedFrames) f->bounds.h = val; changed = true; }
            } break;
            default: break;
        }
        if (sActiveField == kFieldStrokeWidth) {
            if (val < 1) val = 1; if (val > 20) val = 20;
            UInt16 nv = static_cast<UInt16>(val);
            if (sw && nv != *sw) { PushUndo(); for (Frame* f : gSelectedFrames) f->strokeWidth = nv; changed = true; }
        }
    } else if (b) {
        switch (sActiveField) {
            case kFieldX:
                if (val != b->x) {
                    PushUndo();
                    if (applyMulti) {
                        SInt32 dx = val - b->x;
                        for (Shape* s : gSelectedShapes) s->bounds.x += dx;
                    } else { b->x = val; }
                    changed = true;
                } break;
            case kFieldY:
                if (val != b->y) {
                    PushUndo();
                    if (applyMulti) {
                        SInt32 dy = val - b->y;
                        for (Shape* s : gSelectedShapes) s->bounds.y += dy;
                    } else { b->y = val; }
                    changed = true;
                } break;
            case kFieldW:
                if (val < 1) val = 1;
                if (!applyMulti && gSelectedFrame &&
                        gSelectedFrame->widthSizing != SizingMode::Fixed) {
                    PushUndo();
                    gSelectedFrame->widthSizing = SizingMode::Fixed;
                    b->w = val;
                    changed = true;
                } else if (!applyMulti && gSelectedShape && gSelectedShape->wSizing != 0) {
                    PushUndo();
                    gSelectedShape->wSizing = 0;
                    b->w = val;
                    changed = true;
                } else if (applyMulti) {
                    // Compare against every selected shape, not just b — a mixed
                    // selection can coincidentally match b's current value while
                    // still needing to change on the others.
                    bool any = false;
                    for (Shape* s : gSelectedShapes) if (s->bounds.w != val) { any = true; break; }
                    if (any) { PushUndo(); for (Shape* s : gSelectedShapes) s->bounds.w = val; changed = true; }
                } else if (val != b->w) {
                    PushUndo();
                    b->w = val;
                    changed = true;
                } break;
            case kFieldH:
                if (val < 1) val = 1;
                if (!applyMulti && gSelectedFrame &&
                        gSelectedFrame->heightSizing != SizingMode::Fixed) {
                    PushUndo();
                    gSelectedFrame->heightSizing = SizingMode::Fixed;
                    b->h = val;
                    changed = true;
                } else if (!applyMulti && gSelectedShape && gSelectedShape->hSizing != 0) {
                    PushUndo();
                    gSelectedShape->hSizing = 0;
                    b->h = val;
                    changed = true;
                } else if (applyMulti) {
                    bool any = false;
                    for (Shape* s : gSelectedShapes) if (s->bounds.h != val) { any = true; break; }
                    if (any) { PushUndo(); for (Shape* s : gSelectedShapes) s->bounds.h = val; changed = true; }
                } else if (val != b->h) {
                    PushUndo();
                    b->h = val;
                    changed = true;
                } break;
            default: break;
        }
    }
    // Aspect ratio lock: single select only
    if (!applyMulti && !applyMultiFrame && sAspectLocked && changed && b && origW > 0 && origH > 0) {
        if (appliedField == kFieldW && val > 0) {
            b->h = val * origH / origW;
            if (b->h < 1) b->h = 1;
        } else if (appliedField == kFieldH && val > 0) {
            b->w = val * origW / origH;
            if (b->w < 1) b->w = 1;
        }
    }
    // Min/Max Width & Height: val here is either -1 (cleared) or a >=0 bound.
    // Applies to shapes and frames alike, single or multi-select, same "set
    // absolute value on every selected item" convention as W/H above.
    if (isMinMaxField) {
        auto setOn = [&](SInt32 Shape::* sf, SInt32 Frame::* ff) {
            if (applyMultiFrame) {
                bool any = false;
                for (Frame* f : gSelectedFrames) if (f->*ff != val) { any = true; break; }
                if (any) { PushUndo(); for (Frame* f : gSelectedFrames) f->*ff = val; changed = true; }
            } else if (applyMulti) {
                bool any = false;
                for (Shape* s : gSelectedShapes) if (s->*sf != val) { any = true; break; }
                if (any) { PushUndo(); for (Shape* s : gSelectedShapes) s->*sf = val; changed = true; }
            } else if (gSelectedShape) {
                if (gSelectedShape->*sf != val) { PushUndo(); gSelectedShape->*sf = val; changed = true; }
            } else if (gSelectedFrame) {
                if (gSelectedFrame->*ff != val) { PushUndo(); gSelectedFrame->*ff = val; changed = true; }
            }
        };
        switch (sActiveField) {
            case kFieldMinW: setOn(&Shape::minWidth,  &Frame::minWidth);  break;
            case kFieldMaxW: setOn(&Shape::maxWidth,  &Frame::maxWidth);  break;
            case kFieldMinH: setOn(&Shape::minHeight, &Frame::minHeight); break;
            case kFieldMaxH: setOn(&Shape::maxHeight, &Frame::maxHeight); break;
            default: break;
        }
    }
    if (sActiveField == kFieldCornerRadius) {
        if (val < 0) val = 0; if (val > 999) val = 999;
        SInt16 nv = static_cast<SInt16>(val);
        if (applyMultiFrame) {
            bool any = false;
            for (Frame* f : gSelectedFrames) if (f->cornerRadius != nv) { any = true; break; }
            if (any) { PushUndo(); for (Frame* f : gSelectedFrames) f->cornerRadius = nv; changed = true; }
        } else if (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle) {
            auto& rs = static_cast<RectShape&>(*gSelectedShape);
            if (rs.cornerRadius != nv) { PushUndo(); rs.cornerRadius = nv; changed = true; }
        } else if (gSelectedFrame && gSelectedFrame->cornerRadius != nv) {
            PushUndo(); gSelectedFrame->cornerRadius = nv; changed = true;
        }
    }
    if (sActiveField == kFieldCornerTL || sActiveField == kFieldCornerTR ||
        sActiveField == kFieldCornerBR || sActiveField == kFieldCornerBL) {
        if (val < 0) val = 0; if (val > 999) val = 999;
        SInt16 nv = static_cast<SInt16>(val);
        SInt16* target = nullptr;
        if (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle) {
            auto& rs = static_cast<RectShape&>(*gSelectedShape);
            if (sActiveField == kFieldCornerTL)      target = &rs.cornerTL;
            else if (sActiveField == kFieldCornerTR)  target = &rs.cornerTR;
            else if (sActiveField == kFieldCornerBR)  target = &rs.cornerBR;
            else                                       target = &rs.cornerBL;
        } else if (gSelectedFrame) {
            if (sActiveField == kFieldCornerTL)      target = &gSelectedFrame->cornerTL;
            else if (sActiveField == kFieldCornerTR)  target = &gSelectedFrame->cornerTR;
            else if (sActiveField == kFieldCornerBR)  target = &gSelectedFrame->cornerBR;
            else                                       target = &gSelectedFrame->cornerBL;
        }
        if (target && *target != nv) { PushUndo(); *target = nv; changed = true; }
    }
    if (sActiveField == kFieldOpacity) {
        if (val < 0) val = 0; if (val > 100) val = 100;
        UInt8 nv = static_cast<UInt8>(val);
        if (applyMultiFrame) {
            bool any = false;
            for (Frame* f : gSelectedFrames) if (f->opacity != nv) { any = true; break; }
            if (any) { PushUndo(); for (Frame* f : gSelectedFrames) f->opacity = nv; changed = true; }
        } else if (applyMulti) {
            bool any = false;
            for (Shape* s : gSelectedShapes) if (s->opacity != nv) { any = true; break; }
            if (any) { PushUndo(); for (Shape* s : gSelectedShapes) s->opacity = nv; changed = true; }
        } else if (gSelectedShape && gSelectedShape->opacity != nv) {
            PushUndo(); gSelectedShape->opacity = nv; changed = true;
        } else if (gSelectedFrame && gSelectedFrame->opacity != nv) {
            PushUndo(); gSelectedFrame->opacity = nv; changed = true;
        }
    }
    if (sActiveField == kFieldRotation) {
        // Wrap to 0–359
        val = ((val % 360) + 360) % 360;
        SInt16 nv = static_cast<SInt16>(val);
        if (applyMultiFrame) {
            bool any = false;
            for (Frame* f : gSelectedFrames) if (f->rotation != nv) { any = true; break; }
            if (any) { PushUndo(); for (Frame* f : gSelectedFrames) f->rotation = nv; changed = true; }
        } else if (applyMulti) {
            bool any = false;
            for (Shape* s : gSelectedShapes) if (s->rotation != nv) { any = true; break; }
            if (any) { PushUndo(); for (Shape* s : gSelectedShapes) s->rotation = nv; changed = true; }
        } else if (gSelectedShape && gSelectedShape->rotation != nv) {
            PushUndo(); gSelectedShape->rotation = nv; changed = true;
        } else if (gSelectedFrame && gSelectedFrame->rotation != nv) {
            PushUndo(); gSelectedFrame->rotation = nv; changed = true;
        }
    }
    if (!applyMultiFrame && sActiveField == kFieldStrokeWidth) {
        if (val < 1) val = 1; if (val > 20) val = 20;
        UInt16 nv = static_cast<UInt16>(val);
        if (applyMulti) {
            bool any = false;
            for (Shape* s : gSelectedShapes) if (s->strokeWidth != nv) { any = true; break; }
            if (any) { PushUndo(); for (Shape* s : gSelectedShapes) s->strokeWidth = nv; changed = true; }
        } else if (sw && nv != *sw) { PushUndo(); *sw = nv; changed = true; }
    }
    if (sActiveField == kFieldFontSize && gSelectedShape &&
            gSelectedShape->GetType() == Shape::kText) {
        if (val < 4)   val = 4;
        if (val > 144) val = 144;
        SInt16 fval = static_cast<SInt16>(val);
        if (applyMulti) {
            bool any = false;
            for (Shape* s : gSelectedShapes)
                if (s->GetType() == Shape::kText && static_cast<TextShape*>(s)->fontSize != fval) { any = true; break; }
            if (any) {
                PushUndo();
                for (Shape* s : gSelectedShapes)
                    if (s->GetType() == Shape::kText) static_cast<TextShape*>(s)->fontSize = fval;
                changed = true;
            }
        } else {
            TextShape& ts = static_cast<TextShape&>(*gSelectedShape);
            if (val != static_cast<SInt32>(ts.fontSize)) {
                PushUndo(); ts.fontSize = fval; changed = true;
            }
        }
    }
    if (sActiveField == kFieldLayoutGap && gSelectedFrame) {
        if (val < 0)   val = 0;
        if (val > 500) val = 500;
        UInt16 nv = static_cast<UInt16>(val);
        if (applyMultiFrame) {
            PushUndo(); for (Frame* f : gSelectedFrames) f->layoutGap = nv; changed = true;
        } else if (nv != gSelectedFrame->layoutGap) { PushUndo(); gSelectedFrame->layoutGap = nv; changed = true; }
    }
    if (sActiveField == kFieldCounterGap && gSelectedFrame) {
        if (val < 0)   val = 0;
        if (val > 500) val = 500;
        UInt16 nv = static_cast<UInt16>(val);
        if (applyMultiFrame) {
            PushUndo(); for (Frame* f : gSelectedFrames) f->layoutCounterGap = nv; changed = true;
        } else if (nv != gSelectedFrame->layoutCounterGap) { PushUndo(); gSelectedFrame->layoutCounterGap = nv; changed = true; }
    }
    if (gSelectedFrame && (sActiveField == kFieldPadH || sActiveField == kFieldPadV ||
        sActiveField == kFieldPadTop   || sActiveField == kFieldPadRight  ||
        sActiveField == kFieldPadBottom || sActiveField == kFieldPadLeft)) {
        PushUndo();
        auto applyPad = [&](Frame* f) {
            if (sActiveField == kFieldPadH || sActiveField == kFieldPadV) {
                SInt32 a = 0, b2 = 0;
                int sepIdx = -1;
                for (int j = 0; j < sEditLen; ++j)
                    if (sEditBuf[j] == ',') { sepIdx = j; break; }
                int end1 = (sepIdx >= 0) ? sepIdx : sEditLen;
                for (int j = 0; j < end1; ++j)
                    if (sEditBuf[j] >= '0' && sEditBuf[j] <= '9') a = a*10 + (sEditBuf[j]-'0');
                if (sepIdx >= 0) {
                    int s2 = sepIdx + 1;
                    while (s2 < sEditLen && sEditBuf[s2] == ' ') ++s2;
                    for (int j = s2; j < sEditLen; ++j)
                        if (sEditBuf[j] >= '0' && sEditBuf[j] <= '9') b2 = b2*10 + (sEditBuf[j]-'0');
                } else { b2 = a; }
                if (a < 0) a = 0; if (a > 9999) a = 9999;
                if (b2 < 0) b2 = 0; if (b2 > 9999) b2 = 9999;
                if (sActiveField == kFieldPadH) {
                    f->paddingLeft  = static_cast<UInt16>(a);
                    f->paddingRight = static_cast<UInt16>(b2);
                } else {
                    f->paddingTop    = static_cast<UInt16>(a);
                    f->paddingBottom = static_cast<UInt16>(b2);
                }
            } else {
                if (val < 0) val = 0; if (val > 9999) val = 9999;
                UInt16 nv2 = static_cast<UInt16>(val);
                switch (sActiveField) {
                    case kFieldPadTop:    f->paddingTop    = nv2; break;
                    case kFieldPadRight:  f->paddingRight  = nv2; break;
                    case kFieldPadBottom: f->paddingBottom = nv2; break;
                    case kFieldPadLeft:   f->paddingLeft   = nv2; break;
                    default: break;
                }
            }
        };
        if (applyMultiFrame) { for (Frame* f : gSelectedFrames) applyPad(f); }
        else                  { applyPad(gSelectedFrame); }
        changed = true;
    }

    sActiveField = kNoField; sEditLen = 0; sEditBuf[0] = '\0';
    InvalidateInspector();
    if (changed && gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
}

void CancelInspectorEdit() {
    if (sActiveField != kNoField) {
        sActiveField = kNoField; sEditLen = 0; sEditBuf[0] = '\0';
        InvalidateInspector();
    }
}

bool IsAspectLocked() { return sAspectLocked; }

// --------------------------------------------------------------------------
// Interaction
// --------------------------------------------------------------------------

void HandleInspectorClick(Point localPt) {
    if (!gDocument) return;
    if (!gSelectedFrame && !gSelectedShape) return;

    // Check scroll bar first (in window coords — no offset)
    if (gInspectorScrollCtrl) {
        ControlHandle hitCtrl = nullptr;
        short ctrlPart = FindControl(localPt, gInspectorWindow, &hitCtrl);
        if (ctrlPart && hitCtrl == gInspectorScrollCtrl) {
            TrackControl(hitCtrl, localPt, gInspectorScrollUPP);
            gInspectorScrollY = GetControlValue(hitCtrl);
            InvalidateInspector();
            return;
        }
    }

    // Adjust click y for scroll offset (hit-test rects are in document coordinates)
    localPt.v += gInspectorScrollY;

    const bool isMultiFrame = (gSelectedFrames.size() > 1);

    // Locked objects are read-only — no inspector edits allowed
    if (isMultiFrame) {
        for (Frame* f : gSelectedFrames) if (f->locked) return;
    } else {
        bool isLocked = gSelectedShape ? gSelectedShape->locked
                                       : gSelectedFrame->locked;
        if (isLocked) return;
    }

    // Multi-select: refuse edit if any shape is locked
    const bool isMulti = (gSelectedShapes.size() > 1);
    if (isMulti) {
        for (Shape* s : gSelectedShapes) if (s->locked) return;
    }

    // Apply any active edit when clicking elsewhere in the inspector (no Enter needed)
    ApplyInspectorEdit();

    // Align buttons
    for (int i = 0; i < 6; ++i) {
        if (PtInRect(localPt, &sAlignBtnRect[i])) {
            ApplyAlign(i);
            return;
        }
    }

    // Absolute position toggle
    if (PtInRect(localPt, &sAbsolutePositionRect)) {
        PushUndo();
        if (isMultiFrame) {
            bool nv = !gSelectedFrame->isAbsolutePosition;
            for (Frame* f : gSelectedFrames) f->isAbsolutePosition = nv;
        } else if (isMulti) {
            bool nv = !gSelectedShapes[0]->isAbsolutePosition;
            for (Shape* s : gSelectedShapes) s->isAbsolutePosition = nv;
        } else if (gSelectedShape) gSelectedShape->isAbsolutePosition = !gSelectedShape->isAbsolutePosition;
        else if (gSelectedFrame)   gSelectedFrame->isAbsolutePosition = !gSelectedFrame->isAbsolutePosition;
        InvalidateInspector();
        if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        return;
    }

    // Constraint dropdowns (H then V) — same PopUpMenuSelect pattern as Stroke alignment
    if (PtInRect(localPt, &sConstraintHRect) || PtInRect(localPt, &sConstraintVRect)) {
        bool isH = PtInRect(localPt, &sConstraintHRect);
        Rect& trigger = isH ? sConstraintHRect : sConstraintVRect;

        MenuRef popMenu = NewMenu(5004, "\p");
        if (isH) {
            AppendMenu(popMenu, "\pLeft");
            AppendMenu(popMenu, "\pCenter");
            AppendMenu(popMenu, "\pRight");
            AppendMenu(popMenu, "\pLeft & Right");
            AppendMenu(popMenu, "\pScale");
        } else {
            AppendMenu(popMenu, "\pTop");
            AppendMenu(popMenu, "\pCenter");
            AppendMenu(popMenu, "\pBottom");
            AppendMenu(popMenu, "\pTop & Bottom");
            AppendMenu(popMenu, "\pScale");
        }
        InsertMenu(popMenu, -1);

        ConstraintMode cur = isH
            ? (gSelectedShape ? gSelectedShape->constraintH : (gSelectedFrame ? gSelectedFrame->constraintH : ConstraintMode::Start))
            : (gSelectedShape ? gSelectedShape->constraintV : (gSelectedFrame ? gSelectedFrame->constraintV : ConstraintMode::Start));
        short curItem = 1;
        for (int i = 0; i < 5; ++i) if (kConstraintModes[i] == cur) { curItem = static_cast<short>(i+1); break; }

        Point popPt = { static_cast<short>(trigger.top - gInspectorScrollY), trigger.left };
        SetPortWindowPort(gInspectorWindow);
        LocalToGlobal(&popPt);

        long result = PopUpMenuSelect(popMenu, popPt.v, popPt.h, curItem);
        DeleteMenu(5004);
        DisposeMenu(popMenu);

        short item = static_cast<short>(result & 0xFFFF);
        if (item > 0) {
            ConstraintMode nv = kConstraintModes[item-1];
            PushUndo();
            if (isMultiFrame) {
                for (Frame* f : gSelectedFrames) { if (isH) f->constraintH = nv; else f->constraintV = nv; }
            } else if (isMulti) {
                for (Shape* s : gSelectedShapes) { if (isH) s->constraintH = nv; else s->constraintV = nv; }
            } else if (isH) {
                if (gSelectedShape)      gSelectedShape->constraintH = nv;
                else if (gSelectedFrame) gSelectedFrame->constraintH = nv;
            } else {
                if (gSelectedShape)      gSelectedShape->constraintV = nv;
                else if (gSelectedFrame) gSelectedFrame->constraintV = nv;
            }
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        }
        return;
    }

    // Fill color swatch
    if (PtInRect(localPt, &sFillSwatchRect)) {
        RGBColor cur = gSelectedShape ? gSelectedShape->fillColor : gSelectedFrame->backgroundColor;
        bool changed = false; RGBColor newColor = cur;
#ifdef USE_SYSTEM_COLOR_PICKER
        Str255 p; PStrC("Choose Color", p); Point where = {-1,-1};
        changed = GetColor(where, p, &cur, &newColor);
#else
        Rect swR = sFillSwatchRect;
        swR.top    -= gInspectorScrollY;
        swR.bottom -= gInspectorScrollY;
        changed = ShowColorSwatchPicker(swR, newColor);
#endif
        if (changed) {
            PushUndo();
            if (isMultiFrame) { for (Frame* f : gSelectedFrames) f->backgroundColor = newColor; }
            else if (isMulti) { for (Shape* s : gSelectedShapes) s->fillColor = newColor; }
            else if (gSelectedShape) gSelectedShape->fillColor = newColor;
            else                gSelectedFrame->backgroundColor = newColor;
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        }
        return;
    }

    // Stroke toggle
    if (PtInRect(localPt, &sStrokeToggleRect)) {
        PushUndo();
        if (isMultiFrame) {
            bool nv = !gSelectedFrame->hasStroke;
            for (Frame* f : gSelectedFrames) f->hasStroke = nv;
        } else if (isMulti) {
            bool newStroke = !gSelectedShapes[0]->hasStroke;
            for (Shape* s : gSelectedShapes) s->hasStroke = newStroke;
        } else if (gSelectedShape) gSelectedShape->hasStroke = !gSelectedShape->hasStroke;
        else                gSelectedFrame->hasStroke = !gSelectedFrame->hasStroke;
        InvalidateInspector();
        if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        return;
    }

    // Stroke color swatch
    if (PtInRect(localPt, &sStrokeSwatchRect)) {
        RGBColor cur = gSelectedShape ? gSelectedShape->strokeColor : gSelectedFrame->strokeColor;
        bool changed = false; RGBColor newColor = cur;
#ifdef USE_SYSTEM_COLOR_PICKER
        Str255 p; PStrC("Choose Stroke Color", p); Point where = {-1,-1};
        changed = GetColor(where, p, &cur, &newColor);
#else
        Rect ssR = sStrokeSwatchRect;
        ssR.top    -= gInspectorScrollY;
        ssR.bottom -= gInspectorScrollY;
        changed = ShowColorSwatchPicker(ssR, newColor);
#endif
        if (changed) {
            PushUndo();
            if (isMultiFrame) { for (Frame* f : gSelectedFrames) { f->strokeColor = newColor; f->hasStroke = true; } }
            else if (isMulti) { for (Shape* s : gSelectedShapes) { s->strokeColor = newColor; s->hasStroke = true; } }
            else if (gSelectedShape) gSelectedShape->strokeColor = newColor;
            else                gSelectedFrame->strokeColor = newColor;
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        }
        return;
    }

    // Stroke width decrease
    if (PtInRect(localPt, &sStrokeWidthDownRect)) {
        UInt16 sw = gSelectedShape ? gSelectedShape->strokeWidth : gSelectedFrame->strokeWidth;
        if (sw > 1) {
            PushUndo();
            UInt16 nv = static_cast<UInt16>(sw - 1);
            if (isMultiFrame) { for (Frame* f : gSelectedFrames) if (f->strokeWidth > 1) f->strokeWidth = nv; }
            else if (isMulti) { for (Shape* s : gSelectedShapes) if (s->strokeWidth > 1) s->strokeWidth = nv; }
            else if (gSelectedShape) gSelectedShape->strokeWidth = nv;
            else                gSelectedFrame->strokeWidth = nv;
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        }
        return;
    }

    // Stroke width increase
    if (PtInRect(localPt, &sStrokeWidthUpRect)) {
        UInt16 sw = gSelectedShape ? gSelectedShape->strokeWidth : gSelectedFrame->strokeWidth;
        if (sw < 20) {
            PushUndo();
            UInt16 nv = static_cast<UInt16>(sw + 1);
            if (isMultiFrame) { for (Frame* f : gSelectedFrames) if (f->strokeWidth < 20) f->strokeWidth = nv; }
            else if (isMulti) { for (Shape* s : gSelectedShapes) if (s->strokeWidth < 20) s->strokeWidth = nv; }
            else if (gSelectedShape) gSelectedShape->strokeWidth = nv;
            else                gSelectedFrame->strokeWidth = nv;
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        }
        return;
    }

    // Stroke alignment — popup menu dropdown
    if (PtInRect(localPt, &sStrokeAlignRect)) {
        MenuRef popMenu = NewMenu(5001, "\p");
        AppendMenu(popMenu, "\pOutside");
        AppendMenu(popMenu, "\pCenter");
        AppendMenu(popMenu, "\pInside");
        InsertMenu(popMenu, -1);  // insert as popup (not in menu bar)

        UInt8 curAlign = gSelectedShape ? gSelectedShape->strokeAlign : gSelectedFrame->strokeAlign;
        short popItem = (curAlign == 2) ? 1 : (curAlign == 1) ? 3 : 2;

        Point popPt = { static_cast<short>(sStrokeAlignRect.top - gInspectorScrollY), sStrokeAlignRect.left };
        SetPortWindowPort(gInspectorWindow);
        LocalToGlobal(&popPt);

        long result = PopUpMenuSelect(popMenu, popPt.v, popPt.h, popItem);
        DeleteMenu(5001);
        DisposeMenu(popMenu);

        short item = static_cast<short>(result & 0xFFFF);
        if (item > 0) {
            UInt8 newAlign = (item == 1) ? 2 : (item == 3) ? 1 : 0;
            PushUndo();
            if (isMulti) { for (Shape* s : gSelectedShapes) s->strokeAlign = newAlign; }
            else if (gSelectedShape) gSelectedShape->strokeAlign = newAlign;
            else                gSelectedFrame->strokeAlign = newAlign;
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        }
        return;
    }

    // Aa button — toggle Typography panel
    if (PtInRect(localPt, &sTypographyBtnRect)) {
        ToggleTypographyPanel();
        InvalidateInspector();
        return;
    }

    // Text-specific controls (only active when a TextShape is selected)
    if (gSelectedShape && gSelectedShape->GetType() == Shape::kText) {
        TextShape& ts = static_cast<TextShape&>(*gSelectedShape);
        if (PtInRect(localPt, &sFontSizeRect)) {
            StartEdit(kFieldFontSize, static_cast<SInt32>(ts.fontSize)); return;
        }
        if (PtInRect(localPt, &sBoldRect)) {
            PushUndo(); ts.fontFace ^= 1;
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow,&r); InvalWindowRect(gMainWindow,&r); }
            return;
        }
        if (PtInRect(localPt, &sItalicRect)) {
            PushUndo(); ts.fontFace ^= 2;
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow,&r); InvalWindowRect(gMainWindow,&r); }
            return;
        }
        // Text sizing mode buttons
        for (int i = 0; i < 3; ++i) {
            if (PtInRect(localPt, &sTextSizingRect[i])) {
                PushUndo();
                ts.textSizing = static_cast<TextSizing>(i);
                InvalidateInspector();
                if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow,&r); InvalWindowRect(gMainWindow,&r); }
                return;
            }
        }
    }

    // Auto Layout controls (frame only)
    if (!gSelectedShape && gSelectedFrame) {
        Frame* lf = gSelectedFrame;

        // Flow direction buttons
        for (int i = 0; i < 3; ++i) {
            if (PtInRect(localPt, &sLayoutModeRect[i])) {
                PushUndo();
                LayoutMode nm = static_cast<LayoutMode>(i);
                // Enabling Auto Layout must be non-destructive (matches Figma): infer
                // padding/gap from whatever's already in the frame so nothing jumps.
                // An empty frame keeps Fixed sizing too — Hug on an empty frame collapses
                // it to just its padding, which would resize it the instant it's toggled on.
                auto enable = [&](Frame* f) {
                    if (nm == LayoutMode::None || f->layoutMode != LayoutMode::None) return;
                    InferAutoLayoutSpacing(f, nm);
                    if (!f->children.empty() || !f->childFrames.empty()) {
                        f->widthSizing  = SizingMode::Hug;
                        f->heightSizing = SizingMode::Hug;
                    }
                };
                if (isMultiFrame) {
                    for (Frame* f : gSelectedFrames) { enable(f); f->layoutMode = nm; }
                } else {
                    enable(lf);
                    lf->layoutMode = nm;
                }
                InvalidateInspector();
                if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
                return;
            }
        }

        // Wrap toggle (only active when H or V layout is on)
        if (PtInRect(localPt, &sWrapRect) && lf->layoutMode != LayoutMode::None) {
            PushUndo();
            bool nw = !lf->layoutWrap;
            if (isMultiFrame) { for (Frame* f : gSelectedFrames) f->layoutWrap = nw; }
            else              { lf->layoutWrap = nw; }
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
            return;
        }

        // Settings icon → open Auto Layout Settings panel
        if (PtInRect(localPt, &sLayoutSettingsRect)) {
            Point anchor = { static_cast<short>(sLayoutSettingsRect.top - gInspectorScrollY), sLayoutSettingsRect.right };
            SetPortWindowPort(gInspectorWindow); LocalToGlobal(&anchor);
            OpenAutoLayoutSettingsPanel(anchor);
            return;
        }

        // 3×3 alignment grid — sets primary + cross in one click
        for (int i = 0; i < 9; ++i) {
            if (PtInRect(localPt, &sAlignCellRect[i])) {
                int row = i / 3, col = i % 3;
                PushUndo();
                auto applyAlign = [&](Frame* f) {
                    if (f->layoutMode == LayoutMode::Horizontal) {
                        f->primaryAlign = static_cast<PrimaryAlign>(col);
                        f->crossAlign   = static_cast<CrossAlign>(row);
                    } else {
                        f->primaryAlign = static_cast<PrimaryAlign>(row);
                        f->crossAlign   = static_cast<CrossAlign>(col);
                    }
                };
                if (isMultiFrame) { for (Frame* f : gSelectedFrames) applyAlign(f); }
                else              { applyAlign(lf); }
                InvalidateInspector();
                if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
                return;
            }
        }

        // Gap mode popup (Fixed / Auto)
        if (PtInRect(localPt, &sLayoutGapModeRect)) {
            MenuRef pm = NewMenu(6004, "\p");
            AppendMenu(pm, "\pFixed");
            AppendMenu(pm, "\pAuto");
            InsertMenu(pm, -1);
            bool isSB = (lf->primaryAlign == PrimaryAlign::SpaceBetween);
            short curItem = isSB ? 2 : 1;
            Point pt = { static_cast<short>(sLayoutGapModeRect.top - gInspectorScrollY), sLayoutGapModeRect.left };
            SetPortWindowPort(gInspectorWindow); LocalToGlobal(&pt);
            long result = PopUpMenuSelect(pm, pt.v, pt.h, curItem);
            DeleteMenu(6004); DisposeMenu(pm);
            short item = static_cast<short>(result & 0xFFFF);
            if (item > 0) {
                PushUndo();
                auto applyGapMode = [&](Frame* f) {
                    if (item == 2) {
                        f->primaryAlign = PrimaryAlign::SpaceBetween;
                    } else {
                        if (f->primaryAlign == PrimaryAlign::SpaceBetween)
                            f->primaryAlign = PrimaryAlign::Start;
                    }
                };
                if (isMultiFrame) { for (Frame* f : gSelectedFrames) applyGapMode(f); }
                else              { applyGapMode(lf); }
                InvalidateInspector();
                if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
            }
            return;
        }

        // Gap field
        if (PtInRect(localPt, &sLayoutGapRect)) {
            StartEdit(kFieldLayoutGap, static_cast<SInt32>(lf->layoutGap)); return;
        }
        // Counter gap mode popup (Fixed / Auto)
        if (PtInRect(localPt, &sLayoutCounterGapModeRect)) {
            MenuRef pm = NewMenu(6005, "\p");
            AppendMenu(pm, "\pFixed");
            AppendMenu(pm, "\pAuto");
            InsertMenu(pm, -1);
            short curItem = lf->layoutCounterGapAuto ? 2 : 1;
            Point pt = { static_cast<short>(sLayoutCounterGapModeRect.top - gInspectorScrollY), sLayoutCounterGapModeRect.left };
            SetPortWindowPort(gInspectorWindow); LocalToGlobal(&pt);
            long result = PopUpMenuSelect(pm, pt.v, pt.h, curItem);
            DeleteMenu(6005); DisposeMenu(pm);
            short item = static_cast<short>(result & 0xFFFF);
            if (item > 0) {
                PushUndo();
                bool ncga = (item == 2);
                if (isMultiFrame) { for (Frame* f : gSelectedFrames) f->layoutCounterGapAuto = ncga; }
                else              { lf->layoutCounterGapAuto = ncga; }
                InvalidateInspector();
                if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
            }
            return;
        }
        // Counter gap field (Wrap mode, Fixed only)
        if (PtInRect(localPt, &sLayoutCounterGapRect)) {
            StartEdit(kFieldCounterGap, static_cast<SInt32>(lf->layoutCounterGap)); return;
        }

        // Padding expand toggle
        if (PtInRect(localPt, &sPadMixedBtnRect)) {
            sMixedPadding = !sMixedPadding;
            CancelInspectorEdit();
            InvalidateInspector();
            return;
        }
        // Padding fields
        if (PtInRect(localPt, &sPadHRect)) {
            std::string s = padCompactStr(lf->paddingLeft, lf->paddingRight);
            StartEditStr(kFieldPadH, s.c_str()); return;
        }
        if (PtInRect(localPt, &sPadVRect)) {
            std::string s = padCompactStr(lf->paddingTop, lf->paddingBottom);
            StartEditStr(kFieldPadV, s.c_str()); return;
        }
        if (PtInRect(localPt, &sPadTopRect))   { StartEdit(kFieldPadTop,    lf->paddingTop);    return; }
        if (PtInRect(localPt, &sPadRightRect)) { StartEdit(kFieldPadRight,  lf->paddingRight);  return; }
        if (PtInRect(localPt, &sPadBottomRect)){ StartEdit(kFieldPadBottom, lf->paddingBottom); return; }
        if (PtInRect(localPt, &sPadLeftRect))  { StartEdit(kFieldPadLeft,   lf->paddingLeft);   return; }

        // Width sizing popup
        if (PtInRect(localPt, &sWidthSizingPopupRect)) {
            bool hasFill = (lf->parent && lf->parent->layoutMode != LayoutMode::None);
            Rect wSzR = sWidthSizingPopupRect; wSzR.top -= gInspectorScrollY; wSzR.bottom -= gInspectorScrollY;
            SizingMode nm = ShowSizingPopup(wSzR, lf->widthSizing, hasFill);
            if (nm != lf->widthSizing) {
                PushUndo(); lf->widthSizing = nm;
                InvalidateInspector();
                if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
            }
            return;
        }

        // Height sizing popup
        if (PtInRect(localPt, &sHeightSizingPopupRect)) {
            bool hasFill = (lf->parent && lf->parent->layoutMode != LayoutMode::None);
            Rect hSzR = sHeightSizingPopupRect; hSzR.top -= gInspectorScrollY; hSzR.bottom -= gInspectorScrollY;
            SizingMode nm = ShowSizingPopup(hSzR, lf->heightSizing, hasFill);
            if (nm != lf->heightSizing) {
                PushUndo(); lf->heightSizing = nm;
                InvalidateInspector();
                if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
            }
            return;
        }
    }

    // Shape W/H sizing buttons (Fixed / Fill within a layout frame)
    if (gSelectedShape) {
        auto setShapeSizing = [&](UInt8& field, UInt8 val) {
            if (field != val) { PushUndo(); field = val; InvalidateInspector();
                if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow,&r); InvalWindowRect(gMainWindow,&r); } }
        };
        if (PtInRect(localPt, &sShapeWFxRect)) { setShapeSizing(gSelectedShape->wSizing, 0); return; }
        if (PtInRect(localPt, &sShapeWFlRect)) { setShapeSizing(gSelectedShape->wSizing, 2); return; }
        if (PtInRect(localPt, &sShapeHFxRect)) { setShapeSizing(gSelectedShape->hSizing, 0); return; }
        if (PtInRect(localPt, &sShapeHFlRect)) { setShapeSizing(gSelectedShape->hSizing, 2); return; }
    }

    // Aspect ratio lock toggle
    if (PtInRect(localPt, &sAspectLockRect)) {
        sAspectLocked = !sAspectLocked;
        InvalidateInspector();
        return;
    }

    // Clip content toggle (frame only)
    if (gSelectedFrame && PtInRect(localPt, &sClipContentRect)) {
        PushUndo();
        gSelectedFrame->clipContent = !gSelectedFrame->clipContent;
        InvalidateInspector();
        if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        return;
    }

    // Editable numeric fields
    Bounds2 bounds = gSelectedShape ? gSelectedShape->bounds : gSelectedFrame->bounds;
    bool wMixed = isMultiFrame ? MixedW(gSelectedFrames) : (isMulti ? MixedW(gSelectedShapes) : false);
    bool hMixed = isMultiFrame ? MixedH(gSelectedFrames) : (isMulti ? MixedH(gSelectedShapes) : false);
    if (PtInRect(localPt, &sFieldXRect))  { StartEdit(kFieldX, bounds.x);  return; }
    if (PtInRect(localPt, &sFieldYRect))  { StartEdit(kFieldY, bounds.y);  return; }
    if (PtInRect(localPt, &sFieldWRect))  { if (wMixed) StartEditStr(kFieldW, ""); else StartEdit(kFieldW, bounds.w); return; }
    if (PtInRect(localPt, &sFieldHRect))  { if (hMixed) StartEditStr(kFieldH, ""); else StartEdit(kFieldH, bounds.h); return; }
    if (PtInRect(localPt, &sMinWRect)) {
        StartEditMinMax(kFieldMinW, gSelectedShape ? gSelectedShape->minWidth : gSelectedFrame->minWidth);
        return;
    }
    if (PtInRect(localPt, &sMaxWRect)) {
        StartEditMinMax(kFieldMaxW, gSelectedShape ? gSelectedShape->maxWidth : gSelectedFrame->maxWidth);
        return;
    }
    if (PtInRect(localPt, &sMinHRect)) {
        StartEditMinMax(kFieldMinH, gSelectedShape ? gSelectedShape->minHeight : gSelectedFrame->minHeight);
        return;
    }
    if (PtInRect(localPt, &sMaxHRect)) {
        StartEditMinMax(kFieldMaxH, gSelectedShape ? gSelectedShape->maxHeight : gSelectedFrame->maxHeight);
        return;
    }
    if (PtInRect(localPt, &sFieldSwRect)) {
        UInt16 sw = gSelectedShape ? gSelectedShape->strokeWidth : gSelectedFrame->strokeWidth;
        StartEdit(kFieldStrokeWidth, static_cast<SInt32>(sw));
        return;
    }
    if (PtInRect(localPt, &sCornerRadiusRect)) {
        SInt16 crv = 0;
        if (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
            crv = static_cast<RectShape&>(*gSelectedShape).cornerRadius;
        else if (gSelectedFrame)
            crv = gSelectedFrame->cornerRadius;
        StartEdit(kFieldCornerRadius, static_cast<SInt32>(crv));
        return;
    }

    // Individual corner toggle button
    if (PtInRect(localPt, &sCornerIndividualBtnRect)) {
        PushUndo();
        if (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle) {
            auto& rs = static_cast<RectShape&>(*gSelectedShape);
            rs.cornerIndividual = !rs.cornerIndividual;
            if (rs.cornerIndividual) rs.cornerTL = rs.cornerTR = rs.cornerBR = rs.cornerBL = rs.cornerRadius;
        } else if (gSelectedFrame) {
            gSelectedFrame->cornerIndividual = !gSelectedFrame->cornerIndividual;
            if (gSelectedFrame->cornerIndividual)
                gSelectedFrame->cornerTL = gSelectedFrame->cornerTR =
                gSelectedFrame->cornerBR = gSelectedFrame->cornerBL = gSelectedFrame->cornerRadius;
        }
        InvalidateInspector();
        if (gMainWindow) { Rect wr; GetWindowPortBounds(gMainWindow, &wr); InvalWindowRect(gMainWindow, &wr); }
        return;
    }

    // Individual corner fields
    if (PtInRect(localPt, &sCornerTLRect)) {
        SInt16 v = (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
            ? static_cast<RectShape&>(*gSelectedShape).cornerTL
            : (gSelectedFrame ? gSelectedFrame->cornerTL : 0);
        StartEdit(kFieldCornerTL, v); return;
    }
    if (PtInRect(localPt, &sCornerTRRect)) {
        SInt16 v = (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
            ? static_cast<RectShape&>(*gSelectedShape).cornerTR
            : (gSelectedFrame ? gSelectedFrame->cornerTR : 0);
        StartEdit(kFieldCornerTR, v); return;
    }
    if (PtInRect(localPt, &sCornerBRRect)) {
        SInt16 v = (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
            ? static_cast<RectShape&>(*gSelectedShape).cornerBR
            : (gSelectedFrame ? gSelectedFrame->cornerBR : 0);
        StartEdit(kFieldCornerBR, v); return;
    }
    if (PtInRect(localPt, &sCornerBLRect)) {
        SInt16 v = (gSelectedShape && gSelectedShape->GetType() == Shape::kRectangle)
            ? static_cast<RectShape&>(*gSelectedShape).cornerBL
            : (gSelectedFrame ? gSelectedFrame->cornerBL : 0);
        StartEdit(kFieldCornerBL, v); return;
    }
    if (PtInRect(localPt, &sOpacityRect)) {
        UInt8 v = gSelectedShape ? gSelectedShape->opacity
                : (gSelectedFrame ? gSelectedFrame->opacity : 100);
        StartEdit(kFieldOpacity, static_cast<SInt32>(v)); return;
    }
    if (PtInRect(localPt, &sRotationRect)) {
        SInt16 v = gSelectedShape ? gSelectedShape->rotation
                 : (gSelectedFrame ? gSelectedFrame->rotation : 0);
        StartEdit(kFieldRotation, static_cast<SInt32>(v)); return;
    }
}

void RefreshInspector() { InvalidateInspector(); }
