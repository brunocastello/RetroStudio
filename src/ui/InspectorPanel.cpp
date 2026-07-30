#include "InspectorPanel.h"
#include "LayersPanel.h"
#include "window.h"
#include "../core/Shape.h"
#include <string>

WindowRef gInspectorWindow = nullptr;

// Hit-test rect for the fill/background color swatch — rebuilt each draw
static Rect sFillSwatchRect = {0, 0, 0, 0};

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
// Setup
// --------------------------------------------------------------------------

void SetupInspectorPanel() {
    if (!gLayersWindow) return;

    // Find global position of the layers panel content area
    Rect lb;
    GetWindowPortBounds(gLayersWindow, &lb);
    Point topLeft = {0, 0};
    SetPortWindowPort(gLayersWindow);
    LocalToGlobal(&topLeft);

    // Inspector content rect: starts below the layers window.
    // Add ~24px for the inspector's own title bar so it appears fully below layers.
    short layersContentBottom = static_cast<short>(topLeft.v + lb.bottom);

    Rect pr;
    pr.top    = static_cast<short>(layersContentBottom + 24);
    pr.left   = topLeft.h;
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
        PStrC("Select an object", ps);  MoveTo(8, 28); DrawString(ps);
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

    // Color swatch (36 × 18 px)
    sFillSwatchRect = { y, 6, static_cast<short>(y + 18), 42 };
    RGBForeColor(&fillColor);
    PaintRect(&sFillSwatchRect);
    RGBColor border = { 0x7777, 0x7777, 0x7777 };
    RGBForeColor(&border);
    FrameRect(&sFillSwatchRect);

    // "Click to change" hint text
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

        RGBColor newColor;
        Point where = {-1, -1};   // -1,-1 = center the dialog on screen
        if (GetColor(where, "\pChoose Color", &currentColor, &newColor)) {
            if (gSelectedShape)
                gSelectedShape->fillColor = newColor;
            else
                gSelectedFrame->backgroundColor = newColor;

            // Redraw inspector and main canvas to reflect the new color
            InvalidateInspector();
            if (gMainWindow) {
                Rect r; GetWindowPortBounds(gMainWindow, &r);
                InvalWindowRect(gMainWindow, &r);
            }
        }
    }
}

void RefreshInspector() { InvalidateInspector(); }
