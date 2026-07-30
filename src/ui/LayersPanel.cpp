#include "LayersPanel.h"
#include "window.h"   // gDocument, gMainWindow, gSelectedFrame, gSelectedShape

WindowRef gLayersWindow = nullptr;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static void PStr(const std::string& src, Str255& dst) {
    dst[0] = 0;
    for (int i = 0; src[i] && i < 63; ++i) {
        dst[i + 1] = static_cast<unsigned char>(src[i]);
        dst[0]++;
    }
}

static void InvalidateMain() {
    if (!gMainWindow) return;
    Rect r;
    GetWindowPortBounds(gMainWindow, &r);
    InvalWindowRect(gMainWindow, &r);
}

static void InvalidateLayers() {
    if (!gLayersWindow) return;
    Rect r;
    GetWindowPortBounds(gLayersWindow, &r);
    InvalWindowRect(gLayersWindow, &r);
}

// --------------------------------------------------------------------------
// Setup
// --------------------------------------------------------------------------

void SetupLayersPanel() {
    // Position panel to the right of the main window content area
    Rect mainBounds;
    GetWindowPortBounds(gMainWindow, &mainBounds);

    Point topRight = { mainBounds.top, mainBounds.right };
    SetPortWindowPort(gMainWindow);
    LocalToGlobal(&topRight);

    Rect panelRect;
    panelRect.top    = topRight.v;
    panelRect.left   = static_cast<short>(topRight.h + 4);
    panelRect.right  = static_cast<short>(panelRect.left + kLayersPanelWidth);
    panelRect.bottom = static_cast<short>(panelRect.top + (mainBounds.bottom - mainBounds.top));

    gLayersWindow = NewCWindow(
        nullptr,
        &panelRect,
        "\pLayers",
        true,
        noGrowDocProc,
        (WindowRef)-1L,
        true,
        0
    );
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

// Draw a single row at vertical offset y inside the layers panel.
// Returns the updated y after the row.
static short DrawRow(short y, short indent,
                     const std::string& label, bool selected,
                     Shape::Type shapeType, bool isFrame,
                     const Rect& portRect) {

    Rect row = {
        y,
        2,
        static_cast<short>(y + kLayerRowH - 1),
        static_cast<short>(portRect.right - 2)
    };

    // Background
    if (selected) {
        RGBColor selBg = { 0x3399, 0x6666, 0xCCCC };
        RGBForeColor(&selBg);
        PaintRect(&row);
    } else if (isFrame) {
        // Frame rows get a slightly darker header-style background
        RGBColor frameBg = { 0xE6E6, 0xE6E6, 0xE6E6 };
        RGBForeColor(&frameBg);
        PaintRect(&row);
    }

    // Thin bottom separator
    RGBColor sep = { 0xD0D0, 0xD0D0, 0xD0D0 };
    RGBForeColor(&sep);
    MoveTo(row.left, static_cast<short>(row.bottom));
    LineTo(row.right, static_cast<short>(row.bottom));

    // Icon — tiny QuickDraw shape at left of row
    short midY = static_cast<short>(y + kLayerRowH / 2 - 1);
    short iconX = static_cast<short>(4 + indent);

    RGBColor iconColor = selected
        ? RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF }
        : RGBColor{ 0x5555, 0x5555, 0x5555 };
    RGBForeColor(&iconColor);

    if (isFrame) {
        // Hollow rectangle icon (frame / artboard)
        Rect ir = {
            static_cast<short>(midY - 3), iconX,
            static_cast<short>(midY + 4), static_cast<short>(iconX + 8)
        };
        FrameRect(&ir);
    } else if (shapeType == Shape::kEllipse) {
        // Hollow oval icon
        Rect ir = {
            static_cast<short>(midY - 3), iconX,
            static_cast<short>(midY + 4), static_cast<short>(iconX + 7)
        };
        FrameOval(&ir);
    } else {
        // Filled rect icon (rectangle shape)
        Rect ir = {
            static_cast<short>(midY - 2), iconX,
            static_cast<short>(midY + 4), static_cast<short>(iconX + 7)
        };
        PaintRect(&ir);
    }

    // Label text
    RGBColor textColor = selected
        ? RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF }
        : RGBColor{ 0x1111, 0x1111, 0x1111 };
    RGBForeColor(&textColor);

    Str255 ps;
    PStr(label, ps);
    MoveTo(static_cast<short>(iconX + 12),
           static_cast<short>(y + kLayerRowH - 6));
    DrawString(ps);

    return static_cast<short>(y + kLayerRowH);
}

void DrawLayersPanel() {
    if (!gLayersWindow || !gDocument) return;

    SetPortWindowPort(gLayersWindow);

    Rect portRect;
    GetWindowPortBounds(gLayersWindow, &portRect);

    // White panel background
    RGBColor panelBg = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBBackColor(&panelBg);
    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);
    EraseRect(&portRect);

    TextFont(0);  // System font
    TextSize(11);

    short y = 2;

    // Topmost frame in the visual stack = last in the vector (drawn last = on top).
    // The layers panel lists topmost first, so iterate in reverse.
    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it) {
        const Frame* frame = it->get();
        bool frameSel = (gSelectedFrame == frame && gSelectedShape == nullptr);

        y = DrawRow(y, 0, frame->name, frameSel,
                    Shape::kRectangle /*unused for frames*/, true, portRect);

        // Children — also reversed so topmost shape = first row
        for (auto sit = frame->children.rbegin(); sit != frame->children.rend(); ++sit) {
            const Shape* shape = sit->get();
            bool shapeSel = (gSelectedShape == shape);

            std::string label = shape->name;
            if (label.empty())
                label = (shape->GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";

            y = DrawRow(y, 10, label, shapeSel,
                        shape->GetType(), false, portRect);
        }
    }

    TextSize(12);
    PenNormal();
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBForeColor(&black);
    RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Interaction
// --------------------------------------------------------------------------

void HandleLayersPanelClick(Point localPt) {
    if (!gDocument || !gLayersWindow) return;

    SetPortWindowPort(gLayersWindow);
    Rect portRect;
    GetWindowPortBounds(gLayersWindow, &portRect);

    short y = 2;

    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it) {
        Frame* frame = it->get();

        Rect row = {
            y, 2,
            static_cast<short>(y + kLayerRowH - 1),
            static_cast<short>(portRect.right - 2)
        };
        if (PtInRect(localPt, &row)) {
            gSelectedFrame = frame;
            gSelectedShape = nullptr;
            InvalidateLayers();
            InvalidateMain();
            return;
        }
        y = static_cast<short>(y + kLayerRowH);

        for (auto sit = frame->children.rbegin(); sit != frame->children.rend(); ++sit) {
            Shape* shape = sit->get();

            Rect srow = {
                y, 2,
                static_cast<short>(y + kLayerRowH - 1),
                static_cast<short>(portRect.right - 2)
            };
            if (PtInRect(localPt, &srow)) {
                gSelectedFrame = frame;
                gSelectedShape = shape;
                InvalidateLayers();
                InvalidateMain();
                return;
            }
            y = static_cast<short>(y + kLayerRowH);
        }
    }

    // Empty area — deselect
    gSelectedFrame = nullptr;
    gSelectedShape = nullptr;
    InvalidateLayers();
    InvalidateMain();
}

void RefreshLayersPanel() {
    InvalidateLayers();
}
