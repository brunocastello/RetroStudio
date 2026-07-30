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

// Hit-test rect for the fill/background color swatch — rebuilt each draw
static Rect sFillSwatchRect = {0, 0, 0, 0};

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

// Draw a gray section header bar and return the new y (y + bar height)
static short DrawSectionHeader(short y, const char* title, const Rect& pr) {
    RGBColor bg  = { 0xEEEE, 0xEEEE, 0xEEEE };
    Rect hdr = { y, 0, static_cast<short>(y + 16), pr.right };
    RGBForeColor(&bg); PaintRect(&hdr);

    RGBColor sep = { 0xCCCC, 0xCCCC, 0xCCCC };
    RGBForeColor(&sep);
    MoveTo(0, y); LineTo(pr.right, y);
    MoveTo(0, static_cast<short>(y + 15)); LineTo(pr.right, static_cast<short>(y + 15));

    RGBColor tc = { 0x5555, 0x5555, 0x5555 };
    RGBForeColor(&tc);
    TextSize(9);
    Str255 ps; PStrC(title, ps);
    MoveTo(6, static_cast<short>(y + 12));
    DrawString(ps);

    return static_cast<short>(y + 16);
}

// --------------------------------------------------------------------------
// Color swatch picker — replaces the system GetColor dialog.
// Uses only basic QuickDraw, no Color Picker extensions.
// --------------------------------------------------------------------------

// 24 preset colors: 6 columns x 4 rows
static const RGBColor kSwatchColors[24] = {
    // Row 1 — neutrals
    {      0,      0,      0 },  // Black
    { 0xFFFF, 0xFFFF, 0xFFFF },  // White
    { 0xCCCC, 0xCCCC, 0xCCCC },  // Light Gray
    { 0x8888, 0x8888, 0x8888 },  // Medium Gray
    { 0x4444, 0x4444, 0x4444 },  // Dark Gray
    { 0xFFFF, 0xFFFF, 0xDDDD },  // Cream
    // Row 2 — reds / oranges / yellows
    { 0xFFFF,      0,      0 },  // Red
    { 0xFFFF, 0x7777,      0 },  // Orange
    { 0xFFFF, 0xFFFF,      0 },  // Yellow
    { 0xFFFF, 0xFFFF, 0x9999 },  // Light Yellow
    { 0xFFFF, 0x9999, 0x9999 },  // Salmon
    { 0x8888,      0,      0 },  // Dark Red
    // Row 3 — greens
    {      0, 0xFFFF,      0 },  // Green
    {      0, 0x8888,      0 },  // Dark Green
    { 0x9999, 0xFFFF, 0x9999 },  // Light Green
    { 0xAAAA, 0xFFFF, 0xCCCC },  // Mint
    {      0, 0x8888, 0x8888 },  // Teal
    {      0, 0xFFFF, 0xFFFF },  // Cyan
    // Row 4 — blues / purples
    {      0,      0, 0xFFFF },  // Blue
    {      0,      0, 0x8888 },  // Dark Blue
    { 0x9999, 0xCCCC, 0xFFFF },  // Sky Blue
    { 0xCCCC, 0xCCCC, 0xFFFF },  // Lavender (app default)
    { 0x8888,      0, 0x8888 },  // Purple
    { 0xFFFF,      0, 0xFFFF },  // Magenta
};

static const short kSwCellSize = 22;
static const short kSwCellGap  =  2;
static const short kSwPad      =  6;
static const short kSwCols     =  6;
static const short kSwRows     =  4;
// Total content width / height of the picker window
static const short kSwWidth  = kSwPad * 2 + kSwCols * (kSwCellSize + kSwCellGap) - kSwCellGap;
static const short kSwHeight = kSwPad * 2 + kSwRows * (kSwCellSize + kSwCellGap) - kSwCellGap;

static void DrawSwatchPicker(WindowRef win) {
    SetPortWindowPort(win);
    Rect pr; GetWindowPortBounds(win, &pr);

    RGBColor bg = { 0xEEEE, 0xEEEE, 0xEEEE };
    RGBForeColor(&bg); PaintRect(&pr);

    for (int i = 0; i < 24; ++i) {
        int col = i % kSwCols;
        int row = i / kSwCols;
        short x = static_cast<short>(kSwPad + col * (kSwCellSize + kSwCellGap));
        short y = static_cast<short>(kSwPad + row * (kSwCellSize + kSwCellGap));
        Rect cell = { y, x,
                      static_cast<short>(y + kSwCellSize),
                      static_cast<short>(x + kSwCellSize) };
        RGBColor c = kSwatchColors[i];
        RGBForeColor(&c); PaintRect(&cell);
        RGBColor border = { 0x6666, 0x6666, 0x6666 };
        RGBForeColor(&border); FrameRect(&cell);
    }

    RGBColor black = {0,0,0};
    RGBForeColor(&black);
}

// Returns true and fills outColor if the user clicked a swatch; false = cancelled.
static bool ShowColorSwatchPicker(RGBColor& outColor) {
    // Anchor picker near the color swatch in the Inspector
    Point anchor = { sFillSwatchRect.bottom, sFillSwatchRect.right };
    SetPortWindowPort(gInspectorWindow);
    LocalToGlobal(&anchor);

    Rect wr;
    wr.top    = static_cast<short>(anchor.v + 4);
    wr.left   = static_cast<short>(anchor.h - kSwWidth);
    wr.right  = static_cast<short>(wr.left + kSwWidth);
    wr.bottom = static_cast<short>(wr.top  + kSwHeight);

    // Keep on screen (1024 x 768 assumption)
    if (wr.right  > 1020) { short d = static_cast<short>(wr.right - 1020); wr.left -= d; wr.right -= d; }
    if (wr.bottom > 744)  { short d = static_cast<short>(wr.bottom - 744); wr.top  -= d; wr.bottom -= d; }

    WindowRef pickerWin = NewCWindow(nullptr, &wr, "\p", true,
                                     kDBoxProc, (WindowRef)-1L, false, 0);
    if (!pickerWin) return false;

    DrawSwatchPicker(pickerWin);

    // Wait for the triggering mouse-down press to be released before listening
    while (Button()) { /* yield */ }

    bool picked = false;
    bool done   = false;
    EventRecord evt;

    while (!done) {
        if (WaitNextEvent(everyEvent, &evt, 5, nullptr)) {
            switch (evt.what) {
                case mouseDown: {
                    WindowRef clickWin;
                    short part = FindWindow(evt.where, &clickWin);
                    if (part == inContent && clickWin == pickerWin) {
                        Point local = evt.where;
                        SetPortWindowPort(pickerWin);
                        GlobalToLocal(&local);
                        for (int i = 0; i < 24; ++i) {
                            int col = i % kSwCols;
                            int row = i / kSwCols;
                            short x = static_cast<short>(kSwPad + col * (kSwCellSize + kSwCellGap));
                            short y = static_cast<short>(kSwPad + row * (kSwCellSize + kSwCellGap));
                            Rect cell = { y, x,
                                          static_cast<short>(y + kSwCellSize),
                                          static_cast<short>(x + kSwCellSize) };
                            if (PtInRect(local, &cell)) {
                                outColor = kSwatchColors[i];
                                picked = true;
                                break;
                            }
                        }
                    }
                    done = true;  // any click (inside or outside) dismisses
                    break;
                }
                case keyDown: {
                    char key = static_cast<char>(evt.message & charCodeMask);
                    if (key == 0x1B) done = true;  // Escape cancels
                    break;
                }
                case updateEvt: {
                    WindowRef upWin = reinterpret_cast<WindowRef>(evt.message);
                    BeginUpdate(upWin);
                    if (upWin == pickerWin) DrawSwatchPicker(pickerWin);
                    EndUpdate(upWin);
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

    // Mirror the same anchor SetupLayersPanel uses: the main window's top-right
    // corner in global coordinates.  Inspector content starts below the Layers
    // panel content area (kLayersPanelHeight) plus ~24 px for the Inspector's
    // own title bar chrome, so the two windows never overlap.
    Rect mb;
    GetWindowPortBounds(gMainWindow, &mb);
    Point tr = { mb.top, mb.right };
    SetPortWindowPort(gMainWindow);
    LocalToGlobal(&tr);

    Rect pr;
    pr.top    = static_cast<short>(tr.v + kLayersPanelHeight + 24);
    pr.left   = static_cast<short>(tr.h + 4);
    pr.right  = static_cast<short>(pr.left + kInspectorWidth);
    pr.bottom = static_cast<short>(pr.top + 180);

    gInspectorWindow = NewCWindow(nullptr, &pr, "\pInspector", true,
                                  noGrowDocProc, (WindowRef)-1L, true, 0);
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

void DrawInspectorPanel() {
    if (!gInspectorWindow || !gDocument) return;
    SetPortWindowPort(gInspectorWindow);

    Rect portRect;
    GetWindowPortBounds(gInspectorWindow, &portRect);

    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBColor black = { 0, 0, 0 };
    RGBBackColor(&white); RGBForeColor(&black);
    EraseRect(&portRect);

    TextFont(0); TextSize(11);

    if (!gSelectedFrame && !gSelectedShape) {
        RGBColor gray = { 0x9999, 0x9999, 0x9999 };
        RGBForeColor(&gray);
        TextSize(10);
        Str255 ps;
        PStrC("Select an object", ps);       MoveTo(8, 28); DrawString(ps);
        PStrC("to view its properties.", ps); MoveTo(8, 44); DrawString(ps);
        TextSize(12); PenNormal();
        RGBForeColor(&black); RGBBackColor(&white);
        return;
    }

    // Gather data from the selected object
    RGBColor fillColor = { 0xCCCC, 0xCCCC, 0xCCCC };
    Bounds2  bounds    = { 0, 0, 100, 100 };
    std::string objName;

    if (gSelectedShape) {
        fillColor = gSelectedShape->fillColor;
        bounds    = gSelectedShape->bounds;
        objName   = gSelectedShape->name;
        if (objName.empty())
            objName = (gSelectedShape->GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";
    } else {
        fillColor = gSelectedFrame->backgroundColor;
        bounds    = gSelectedFrame->bounds;
        objName   = gSelectedFrame->name;
    }

    short y = 4;

    // ---------------------------------------------------------------- FILL --
    y = DrawSectionHeader(y, "FILL", portRect);
    y = static_cast<short>(y + 6);

    // Color swatch (36 x 18 px) — clicking opens the swatch picker popup
    sFillSwatchRect = { y, 6, static_cast<short>(y + 18), 42 };
    RGBForeColor(&fillColor);
    PaintRect(&sFillSwatchRect);
    RGBColor border = { 0x7777, 0x7777, 0x7777 };
    RGBForeColor(&border);
    FrameRect(&sFillSwatchRect);

    // Hint text
    RGBColor hint = { 0x9999, 0x9999, 0x9999 };
    RGBForeColor(&hint);
    TextSize(9);
    Str255 ps; PStrC("Click to change", ps);
    MoveTo(48, static_cast<short>(y + 13));
    DrawString(ps);
    TextSize(11);

    y = static_cast<short>(y + 26);

    // ---------------------------------------------------------- POSITION --
    y = DrawSectionHeader(y, "POSITION", portRect);
    y = static_cast<short>(y + 6);

    RGBColor labelClr = { 0x6666, 0x6666, 0x6666 };
    RGBColor valueClr = { 0x1111, 0x1111, 0x1111 };

    RGBForeColor(&labelClr); PStrC("X", ps); MoveTo(6,   static_cast<short>(y + 12)); DrawString(ps);
    RGBForeColor(&valueClr); PStr(numStr(bounds.x), ps); MoveTo(20,  static_cast<short>(y + 12)); DrawString(ps);
    RGBForeColor(&labelClr); PStrC("Y", ps); MoveTo(92,  static_cast<short>(y + 12)); DrawString(ps);
    RGBForeColor(&valueClr); PStr(numStr(bounds.y), ps); MoveTo(106, static_cast<short>(y + 12)); DrawString(ps);

    y = static_cast<short>(y + 22);

    // -------------------------------------------------------------- SIZE --
    y = DrawSectionHeader(y, "SIZE", portRect);
    y = static_cast<short>(y + 6);

    RGBForeColor(&labelClr); PStrC("W", ps); MoveTo(6,   static_cast<short>(y + 12)); DrawString(ps);
    RGBForeColor(&valueClr); PStr(numStr(bounds.w), ps); MoveTo(20,  static_cast<short>(y + 12)); DrawString(ps);
    RGBForeColor(&labelClr); PStrC("H", ps); MoveTo(92,  static_cast<short>(y + 12)); DrawString(ps);
    RGBForeColor(&valueClr); PStr(numStr(bounds.h), ps); MoveTo(106, static_cast<short>(y + 12)); DrawString(ps);

    y = static_cast<short>(y + 22);

    // -------------------------------------------------------------- NAME --
    y = DrawSectionHeader(y, "NAME", portRect);
    y = static_cast<short>(y + 6);

    RGBForeColor(&valueClr);
    PStr(objName, ps);
    MoveTo(6, static_cast<short>(y + 12));
    DrawString(ps);

    // Reset drawing state
    TextSize(12); PenNormal();
    RGBForeColor(&black); RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Interaction
// --------------------------------------------------------------------------

void HandleInspectorClick(Point localPt) {
    if (!gDocument) return;
    if (!gSelectedFrame && !gSelectedShape) return;

    if (PtInRect(localPt, &sFillSwatchRect)) {
        RGBColor currentColor = gSelectedShape
            ? gSelectedShape->fillColor
            : gSelectedFrame->backgroundColor;

        bool       changed  = false;
        RGBColor   newColor = currentColor;

#ifdef USE_SYSTEM_COLOR_PICKER
        // Native Mac OS 9 Color Picker — works on real hardware, may crash
        // under some emulators (UTM/QEMU) when switching picker panels.
        Str255 prompt; PStrC("Choose Color", prompt);
        Point  where  = {-1, -1};   // -1,-1 = center dialog on screen
        changed = GetColor(where, prompt, &currentColor, &newColor);
#else
        // Emulator-safe built-in swatch picker (24 presets, no extensions)
        changed = ShowColorSwatchPicker(newColor);
#endif

        if (changed) {
            if (gSelectedShape)
                gSelectedShape->fillColor = newColor;
            else
                gSelectedFrame->backgroundColor = newColor;

            InvalidateInspector();
            if (gMainWindow) {
                Rect r; GetWindowPortBounds(gMainWindow, &r);
                InvalWindowRect(gMainWindow, &r);
            }
        }
    }
}

void RefreshInspector() { InvalidateInspector(); }
