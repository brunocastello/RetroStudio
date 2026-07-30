#include "LayersPanel.h"
#include "window.h"

WindowRef gLayersWindow = nullptr;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static void PStr(const std::string& src, Str255& dst) {
    dst[0] = 0;
    for (int i = 0; src[i] && i < 63; ++i) {
        dst[i+1] = static_cast<unsigned char>(src[i]); dst[0]++;
    }
}

static void InvalidateMain() {
    if (!gMainWindow) return;
    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
}
static void InvalidateLayers() {
    if (!gLayersWindow) return;
    Rect r; GetWindowPortBounds(gLayersWindow, &r); InvalWindowRect(gLayersWindow, &r);
}

// --------------------------------------------------------------------------
// Setup
// --------------------------------------------------------------------------

void SetupLayersPanel() {
    Rect mb;
    GetWindowPortBounds(gMainWindow, &mb);
    Point tr = { mb.top, mb.right };
    SetPortWindowPort(gMainWindow);
    LocalToGlobal(&tr);

    Rect pr;
    pr.top    = tr.v;
    pr.left   = static_cast<short>(tr.h + 4);
    pr.right  = static_cast<short>(pr.left + kLayersPanelWidth);
    pr.bottom = static_cast<short>(pr.top + kLayersPanelHeight);

    gLayersWindow = NewCWindow(nullptr, &pr, "\pLayers", true,
                               noGrowDocProc, (WindowRef)-1L, true, 0);
}

// --------------------------------------------------------------------------
// Row rendering
// --------------------------------------------------------------------------

static short DrawRow(short y, short indent,
                     const std::string& label, bool selected,
                     Shape::Type shapeType, bool isFrame,
                     const Rect& portRect) {
    Rect row = { y, 2,
                 static_cast<short>(y + kLayerRowH - 1),
                 static_cast<short>(portRect.right - 2) };

    if (selected) {
        RGBColor b = { 0x3399, 0x6666, 0xCCCC }; RGBForeColor(&b); PaintRect(&row);
    } else if (isFrame) {
        RGBColor b = { 0xE6E6, 0xE6E6, 0xE6E6 }; RGBForeColor(&b); PaintRect(&row);
    }

    RGBColor sep = { 0xD0D0, 0xD0D0, 0xD0D0 };
    RGBForeColor(&sep);
    MoveTo(row.left, static_cast<short>(row.bottom));
    LineTo(row.right, static_cast<short>(row.bottom));

    // Tiny icon
    short midY   = static_cast<short>(y + kLayerRowH / 2 - 1);
    short iconX  = static_cast<short>(4 + indent);
    RGBColor ic  = selected
        ? RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF }
        : RGBColor{ 0x5555, 0x5555, 0x5555 };
    RGBForeColor(&ic);

    if (isFrame) {
        Rect ir = { static_cast<short>(midY-3), iconX,
                    static_cast<short>(midY+4), static_cast<short>(iconX+8) };
        FrameRect(&ir);
    } else if (shapeType == Shape::kEllipse) {
        Rect ir = { static_cast<short>(midY-3), iconX,
                    static_cast<short>(midY+4), static_cast<short>(iconX+7) };
        FrameOval(&ir);
    } else {
        Rect ir = { static_cast<short>(midY-2), iconX,
                    static_cast<short>(midY+4), static_cast<short>(iconX+7) };
        PaintRect(&ir);
    }

    // Label
    RGBColor tc = selected
        ? RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF }
        : RGBColor{ 0x1111, 0x1111, 0x1111 };
    RGBForeColor(&tc);
    Str255 ps; PStr(label, ps);
    MoveTo(static_cast<short>(iconX + 12), static_cast<short>(y + kLayerRowH - 6));
    DrawString(ps);

    return static_cast<short>(y + kLayerRowH);
}

// Recursively draw a frame and all its contents as rows.
// Returns updated y offset.
static short DrawFrameRows(const Frame* frame, short y, short indent, const Rect& portRect) {
    bool fsel = (gSelectedFrame == frame && gSelectedShape == nullptr);
    y = DrawRow(y, indent, frame->name, fsel, Shape::kRectangle, true, portRect);

    // Child frames first (reverse = topmost first in panel)
    for (auto it = frame->childFrames.rbegin(); it != frame->childFrames.rend(); ++it)
        y = DrawFrameRows(it->get(), y, static_cast<short>(indent + 10), portRect);

    // Shapes (reverse = topmost first)
    for (auto it = frame->children.rbegin(); it != frame->children.rend(); ++it) {
        const Shape* s = it->get();
        bool ssel = (gSelectedShape == s);
        std::string lbl = s->name;
        if (lbl.empty()) lbl = (s->GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";
        y = DrawRow(y, static_cast<short>(indent + 10), lbl, ssel, s->GetType(), false, portRect);
    }
    return y;
}

void DrawLayersPanel() {
    if (!gLayersWindow || !gDocument) return;
    SetPortWindowPort(gLayersWindow);

    Rect portRect;
    GetWindowPortBounds(gLayersWindow, &portRect);

    RGBColor bg = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBBackColor(&bg);
    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);
    EraseRect(&portRect);

    TextFont(0); TextSize(11);
    short y = 2;

    // Root shapes (floating on canvas, no parent frame) — top of list
    for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it) {
        const Shape* s = it->get();
        bool sel = (gSelectedShape == s && gSelectedFrame == nullptr);
        std::string lbl = s->name;
        if (lbl.empty()) lbl = (s->GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";
        y = DrawRow(y, 0, lbl, sel, s->GetType(), false, portRect);
    }

    // Top-level frames (reverse = topmost first)
    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it)
        y = DrawFrameRows(it->get(), y, 0, portRect);

    TextSize(12); PenNormal();
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBForeColor(&black); RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Hit-testing
// --------------------------------------------------------------------------

// Recursively test rows for a frame. Returns updated y, or -1 if hit found.
static short HitTestFrameRows(Frame* frame, short y, short indent,
                               Point pt, const Rect& portRect) {
    Rect row = { y, 2,
                 static_cast<short>(y + kLayerRowH - 1),
                 static_cast<short>(portRect.right - 2) };
    if (PtInRect(pt, &row)) {
        gSelectedFrame = frame; gSelectedShape = nullptr;
        InvalidateLayers(); InvalidateMain();
        return -1;
    }
    y = static_cast<short>(y + kLayerRowH);

    for (auto it = frame->childFrames.rbegin(); it != frame->childFrames.rend(); ++it) {
        y = HitTestFrameRows(it->get(), y, static_cast<short>(indent + 10), pt, portRect);
        if (y == -1) return -1;
    }
    for (auto it = frame->children.rbegin(); it != frame->children.rend(); ++it) {
        Shape* s = it->get();
        Rect sr = { y, 2,
                    static_cast<short>(y + kLayerRowH - 1),
                    static_cast<short>(portRect.right - 2) };
        if (PtInRect(pt, &sr)) {
            gSelectedFrame = frame; gSelectedShape = s;
            InvalidateLayers(); InvalidateMain();
            return -1;
        }
        y = static_cast<short>(y + kLayerRowH);
    }
    return y;
}

void HandleLayersPanelClick(Point localPt) {
    if (!gDocument || !gLayersWindow) return;
    SetPortWindowPort(gLayersWindow);

    Rect portRect;
    GetWindowPortBounds(gLayersWindow, &portRect);

    short y = 2;

    // Root shapes
    for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it) {
        Shape* s = it->get();
        Rect row = { y, 2, static_cast<short>(y + kLayerRowH - 1),
                     static_cast<short>(portRect.right - 2) };
        if (PtInRect(localPt, &row)) {
            gSelectedFrame = nullptr; gSelectedShape = s;
            InvalidateLayers(); InvalidateMain(); return;
        }
        y = static_cast<short>(y + kLayerRowH);
    }

    // Top-level frames
    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it) {
        y = HitTestFrameRows(it->get(), y, 0, localPt, portRect);
        if (y == -1) return;
    }

    // Empty area — deselect
    gSelectedFrame = nullptr; gSelectedShape = nullptr;
    InvalidateLayers(); InvalidateMain();
}

void RefreshLayersPanel() { InvalidateLayers(); }
