// Define USE_SYSTEM_COLOR_PICKER to use the Mac OS 9 GetColor() dialog instead
// of the built-in swatch picker.  Disabled by default: the Color Picker
// extension crashes on some emulators (UTM/QEMU) when switching picker panels.
// On a real PowerPC Mac or a stable emulator, define this flag in CMakeLists.txt:
//   target_compile_definitions(RetroStudio PRIVATE USE_SYSTEM_COLOR_PICKER)
// #define USE_SYSTEM_COLOR_PICKER

#include "InspectorPanel.h"
#include "LayersPanel.h"
#include "window.h"
#include "../core/Shape.h"
#include <string>

WindowRef gInspectorWindow = nullptr;

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

// Inline text-edit state for numeric fields
enum EditField { kNoField, kFieldX, kFieldY, kFieldW, kFieldH, kFieldStrokeWidth };
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
    sActiveField = field;
    sEditLen = 0;
    std::string s = numStr(val);
    for (int i = 0; i < (int)s.size() && i < 11; ++i)
        sEditBuf[sEditLen++] = s[i];
    sEditBuf[sEditLen] = '\0';
    InvalidateInspector();
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
    pr.bottom = static_cast<short>(pr.top + 260);

    gInspectorWindow = NewCWindow(nullptr, &pr, "\pInspector", true,
                                  noGrowDocProc, (WindowRef)-1L, true, 0);
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

void DrawInspectorPanel() {
    if (!gInspectorWindow || !gDocument) return;
    SetPortWindowPort(gInspectorWindow);

    Rect portRect; GetWindowPortBounds(gInspectorWindow, &portRect);
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBColor black = { 0, 0, 0 };
    RGBBackColor(&white); RGBForeColor(&black);
    EraseRect(&portRect);
    TextFont(0); TextSize(11);

    // Reset all hit-test rects each frame
    sFillSwatchRect = sStrokeToggleRect = sStrokeSwatchRect = {0,0,0,0};
    sStrokeWidthDownRect = sStrokeWidthUpRect = {0,0,0,0};
    sStrokeAlignRect = {0,0,0,0};
    sFieldXRect = sFieldYRect = sFieldWRect = sFieldHRect = sFieldSwRect = {0,0,0,0};

    if (!gSelectedFrame && !gSelectedShape) {
        RGBColor gray = { 0x9999, 0x9999, 0x9999 }; RGBForeColor(&gray); TextSize(10);
        Str255 ps;
        PStrC("Select an object", ps);       MoveTo(8, 28); DrawString(ps);
        PStrC("to view its properties.", ps); MoveTo(8, 44); DrawString(ps);
        TextSize(12); PenNormal(); RGBForeColor(&black); RGBBackColor(&white);
        return;
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
        if (objName.empty())
            objName = (gSelectedShape->GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";
    } else {
        fillColor   = gSelectedFrame->backgroundColor;
        hasStroke   = gSelectedFrame->hasStroke;
        strokeColor = gSelectedFrame->strokeColor;
        strokeWidth = gSelectedFrame->strokeWidth;
        strokeAlign = gSelectedFrame->strokeAlign;
        bounds      = gSelectedFrame->bounds;
        objName     = gSelectedFrame->name;
    }

    Str255 ps;
    RGBColor labelClr = { 0x6666, 0x6666, 0x6666 };
    RGBColor valueClr = { 0x1111, 0x1111, 0x1111 };
    RGBColor hint     = { 0x9999, 0x9999, 0x9999 };
    short y = 4;

    // ---------------------------------------------------------------- NAME --
    y = DrawSectionHeader(y, "NAME", portRect);
    y = static_cast<short>(y + 6);
    RGBForeColor(&valueClr);
    PStr(objName, ps); MoveTo(6, static_cast<short>(y + 12)); DrawString(ps);
    y = static_cast<short>(y + 22);

    // ---------------------------------------------------------------- FILL --
    y = DrawSectionHeader(y, "FILL", portRect);
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

    // ---------------------------------------------------------- POSITION --
    y = DrawSectionHeader(y, "POSITION", portRect);
    y = static_cast<short>(y + 6);

    RGBForeColor(&labelClr); PStrC("X", ps); MoveTo(6,  static_cast<short>(y+12)); DrawString(ps);
    DrawNumField(20, static_cast<short>(y+12), 64, kFieldX, bounds.x, sFieldXRect);

    RGBForeColor(&labelClr); PStrC("Y", ps); MoveTo(94, static_cast<short>(y+12)); DrawString(ps);
    DrawNumField(106, static_cast<short>(y+12), 62, kFieldY, bounds.y, sFieldYRect);

    y = static_cast<short>(y + 22);

    // -------------------------------------------------------------- SIZE --
    y = DrawSectionHeader(y, "SIZE", portRect);
    y = static_cast<short>(y + 6);

    RGBForeColor(&labelClr); PStrC("W", ps); MoveTo(6,  static_cast<short>(y+12)); DrawString(ps);
    DrawNumField(20, static_cast<short>(y+12), 64, kFieldW, bounds.w, sFieldWRect);

    RGBForeColor(&labelClr); PStrC("H", ps); MoveTo(94, static_cast<short>(y+12)); DrawString(ps);
    DrawNumField(106, static_cast<short>(y+12), 62, kFieldH, bounds.h, sFieldHRect);

    TextSize(12); PenNormal(); RGBForeColor(&black); RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Inline numeric field key handling
// --------------------------------------------------------------------------

bool HandleInspectorKey(char key) {
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

    if (key == 0x0D || key == 0x03) {  // Return or Enter — apply
        sEditBuf[sEditLen] = '\0';
        SInt32 val = 0; int i = 0; bool neg = false;
        if (sEditLen > 0 && sEditBuf[0] == '-') { neg = true; i = 1; }
        for (; i < sEditLen; ++i)
            if (sEditBuf[i] >= '0' && sEditBuf[i] <= '9')
                val = val * 10 + (sEditBuf[i] - '0');
        if (neg) val = -val;

        Bounds2* b  = gSelectedShape ? &gSelectedShape->bounds
                                     : (gSelectedFrame ? &gSelectedFrame->bounds : nullptr);
        UInt16*  sw = gSelectedShape ? &gSelectedShape->strokeWidth
                                     : (gSelectedFrame ? &gSelectedFrame->strokeWidth : nullptr);

        bool changed = false;
        if (b) {
            switch (sActiveField) {
                case kFieldX: if (val!=b->x){PushUndo(); b->x=val;      changed=true;} break;
                case kFieldY: if (val!=b->y){PushUndo(); b->y=val;      changed=true;} break;
                case kFieldW: if (val<1) val=1; if (val!=b->w){PushUndo(); b->w=val; changed=true;} break;
                case kFieldH: if (val<1) val=1; if (val!=b->h){PushUndo(); b->h=val; changed=true;} break;
                default: break;
            }
        }
        if (sw && sActiveField == kFieldStrokeWidth) {
            if (val < 1) val = 1; if (val > 20) val = 20;
            UInt16 nv = static_cast<UInt16>(val);
            if (nv != *sw) { PushUndo(); *sw = nv; changed = true; }
        }

        sActiveField = kNoField; sEditLen = 0; sEditBuf[0] = '\0';
        InvalidateInspector();
        if (changed && gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        return true;
    }

    if (key == 0x08) {  // Backspace
        if (sEditLen > 0) sEditBuf[--sEditLen] = '\0';
        InvalidateInspector();
        return true;
    }

    if (key >= '0' && key <= '9' && sEditLen < 7) {
        sEditBuf[sEditLen++] = key; sEditBuf[sEditLen] = '\0';
        InvalidateInspector();
        return true;
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

void CancelInspectorEdit() {
    if (sActiveField != kNoField) {
        sActiveField = kNoField; sEditLen = 0; sEditBuf[0] = '\0';
        InvalidateInspector();
    }
}

// --------------------------------------------------------------------------
// Interaction
// --------------------------------------------------------------------------

void HandleInspectorClick(Point localPt) {
    if (!gDocument) return;
    if (!gSelectedFrame && !gSelectedShape) return;

    // Locked objects are read-only — no inspector edits allowed
    bool isLocked = gSelectedShape ? gSelectedShape->locked
                                   : gSelectedFrame->locked;
    if (isLocked) return;

    // Cancel any active edit when clicking elsewhere in the inspector
    CancelInspectorEdit();

    // Fill color swatch
    if (PtInRect(localPt, &sFillSwatchRect)) {
        RGBColor cur = gSelectedShape ? gSelectedShape->fillColor : gSelectedFrame->backgroundColor;
        bool changed = false; RGBColor newColor = cur;
#ifdef USE_SYSTEM_COLOR_PICKER
        Str255 p; PStrC("Choose Color", p); Point where = {-1,-1};
        changed = GetColor(where, p, &cur, &newColor);
#else
        changed = ShowColorSwatchPicker(sFillSwatchRect, newColor);
#endif
        if (changed) {
            PushUndo();
            if (gSelectedShape) gSelectedShape->fillColor = newColor;
            else                gSelectedFrame->backgroundColor = newColor;
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        }
        return;
    }

    // Stroke toggle
    if (PtInRect(localPt, &sStrokeToggleRect)) {
        PushUndo();
        if (gSelectedShape) gSelectedShape->hasStroke = !gSelectedShape->hasStroke;
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
        changed = ShowColorSwatchPicker(sStrokeSwatchRect, newColor);
#endif
        if (changed) {
            PushUndo();
            if (gSelectedShape) gSelectedShape->strokeColor = newColor;
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
            if (gSelectedShape) gSelectedShape->strokeWidth = nv;
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
            if (gSelectedShape) gSelectedShape->strokeWidth = nv;
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

        Point popPt = { sStrokeAlignRect.top, sStrokeAlignRect.left };
        SetPortWindowPort(gInspectorWindow);
        LocalToGlobal(&popPt);

        long result = PopUpMenuSelect(popMenu, popPt.v, popPt.h, popItem);
        DeleteMenu(5001);
        DisposeMenu(popMenu);

        short item = static_cast<short>(result & 0xFFFF);
        if (item > 0) {
            UInt8 newAlign = (item == 1) ? 2 : (item == 3) ? 1 : 0;
            PushUndo();
            if (gSelectedShape) gSelectedShape->strokeAlign = newAlign;
            else                gSelectedFrame->strokeAlign = newAlign;
            InvalidateInspector();
            if (gMainWindow) { Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r); }
        }
        return;
    }

    // Editable numeric fields
    Bounds2 bounds = gSelectedShape ? gSelectedShape->bounds : gSelectedFrame->bounds;
    if (PtInRect(localPt, &sFieldXRect))  { StartEdit(kFieldX, bounds.x);  return; }
    if (PtInRect(localPt, &sFieldYRect))  { StartEdit(kFieldY, bounds.y);  return; }
    if (PtInRect(localPt, &sFieldWRect))  { StartEdit(kFieldW, bounds.w);  return; }
    if (PtInRect(localPt, &sFieldHRect))  { StartEdit(kFieldH, bounds.h);  return; }
    if (PtInRect(localPt, &sFieldSwRect)) {
        UInt16 sw = gSelectedShape ? gSelectedShape->strokeWidth : gSelectedFrame->strokeWidth;
        StartEdit(kFieldStrokeWidth, static_cast<SInt32>(sw));
        return;
    }
}

void RefreshInspector() { InvalidateInspector(); }
