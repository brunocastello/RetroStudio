#include "LayersPanel.h"
#include "window.h"
#include "RenameDialog.h"
#include "InspectorPanel.h"
#include <algorithm>

WindowRef gLayersWindow = nullptr;

static ControlHandle     gLayersScrollCtrl = nullptr;
static short             gLayersScrollY    = 0;  // current scroll offset in pixels
static short             gLayersTotalH     = 0;  // total content height after last draw
static ControlActionUPP  gLayersScrollUPP  = nullptr;
static const short       kLayersSBW        = 16; // scroll bar width

static void InvalidateLayers();  // forward-declare so the action proc can call it

static void LayersScrollAction(ControlHandle ctrl, short part) {
    short v  = GetControlValue(ctrl);
    short mn = GetControlMinimum(ctrl);
    short mx = GetControlMaximum(ctrl);
    short delta = 0;
    if (part == inUpButton)   delta = -kLayerRowH;
    if (part == inDownButton) delta = +kLayerRowH;
    if (part == inPageUp)     delta = -60;
    if (part == inPageDown)   delta = +60;
    if (delta != 0) {
        v += delta;
        if (v < mn) v = mn;
        if (v > mx) v = mx;
        SetControlValue(ctrl, v);
        gLayersScrollY = v;
        InvalidateLayers();
    }
}

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

static const short kEyeColW  = 18;  // rightmost N pixels: eye toggle
static const short kLockColW = 18;  // next N pixels left of eye: lock toggle

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
                               documentProc, (WindowRef)-1L, true, 0);

    // Vertical scroll bar at right edge
    Rect sbRect = {0, static_cast<short>(kLayersPanelWidth - kLayersSBW),
                   kLayersPanelHeight, kLayersPanelWidth};
    gLayersScrollCtrl = NewControl(gLayersWindow, &sbRect, "\p", true, 0, 0, 0, scrollBarProc, 0);
    gLayersScrollUPP  = NewControlActionUPP(LayersScrollAction);
}

// --------------------------------------------------------------------------
// Row rendering
// --------------------------------------------------------------------------

static void DrawLock(short midX, short midY, bool locked, bool selected) {
    RGBColor clr = selected ? RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF }
                            : RGBColor{ 0x9999, 0x9999, 0x9999 };
    RGBForeColor(&clr);

    // Body — small filled rectangle
    Rect body = { static_cast<short>(midY),     static_cast<short>(midX - 4),
                  static_cast<short>(midY + 4), static_cast<short>(midX + 4) };
    PaintRect(&body);

    // Shackle — inverted U drawn with lines
    if (locked) {
        // Closed: shackle sits just above body
        MoveTo(static_cast<short>(midX - 3), midY);
        LineTo(static_cast<short>(midX - 3), static_cast<short>(midY - 3));
        LineTo(static_cast<short>(midX + 3), static_cast<short>(midY - 3));
        LineTo(static_cast<short>(midX + 3), midY);
    } else {
        // Open: right leg raised and disconnected
        MoveTo(static_cast<short>(midX - 3), midY);
        LineTo(static_cast<short>(midX - 3), static_cast<short>(midY - 3));
        LineTo(static_cast<short>(midX + 3), static_cast<short>(midY - 3));
        LineTo(static_cast<short>(midX + 3), static_cast<short>(midY - 5));
    }
}

static void DrawEye(short midX, short midY, bool visible, bool selected) {
    RGBColor clr = selected ? RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF }
                            : RGBColor{ 0x9999, 0x9999, 0x9999 };
    RGBForeColor(&clr);

    Rect oval = { static_cast<short>(midY - 3), static_cast<short>(midX - 5),
                  static_cast<short>(midY + 4), static_cast<short>(midX + 6) };

    if (visible) {
        FrameOval(&oval);
        Rect pupil = { static_cast<short>(midY - 1), static_cast<short>(midX - 1),
                       static_cast<short>(midY + 2), static_cast<short>(midX + 2) };
        PaintOval(&pupil);
    } else {
        // Closed eye: flattened arc suggestion + slash
        RGBColor dimClr = selected ? RGBColor{ 0xCCCC, 0xCCCC, 0xCCCC }
                                   : RGBColor{ 0xBBBB, 0xBBBB, 0xBBBB };
        RGBForeColor(&dimClr);
        FrameOval(&oval);
        RGBForeColor(&clr);
        MoveTo(static_cast<short>(midX - 5), midY);
        LineTo(static_cast<short>(midX + 5), midY);
    }
}

static short DrawRow(short y, short indent,
                     const std::string& label, bool selected,
                     Shape::Type shapeType, bool isFrame, bool visible, bool locked,
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

    short midY  = static_cast<short>(y + kLayerRowH / 2 - 1);
    short iconX = static_cast<short>(4 + indent);

    // Shape/frame icon — dimmed when invisible
    RGBColor ic = selected  ? RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF }
                : visible   ? RGBColor{ 0x5555, 0x5555, 0x5555 }
                            : RGBColor{ 0xBBBB, 0xBBBB, 0xBBBB };
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

    // Label — dimmed when invisible
    RGBColor tc = selected  ? RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF }
                : visible   ? RGBColor{ 0x1111, 0x1111, 0x1111 }
                            : RGBColor{ 0xAAAA, 0xAAAA, 0xAAAA };
    RGBForeColor(&tc);
    Str255 ps; PStr(label, ps);
    MoveTo(static_cast<short>(iconX + 12), static_cast<short>(y + kLayerRowH - 6));
    DrawString(ps);

    // Lock icon — second column from right
    short lockMidX = static_cast<short>(portRect.right - kEyeColW - kLockColW / 2);
    DrawLock(lockMidX, midY, locked, selected);

    // Eye icon — rightmost column
    short eyeMidX = static_cast<short>(portRect.right - kEyeColW / 2);
    DrawEye(eyeMidX, midY, visible, selected);

    return static_cast<short>(y + kLayerRowH);
}

// Recursively draw a frame and all its contents as rows.
static short DrawFrameRows(const Frame* frame, short y, short indent, const Rect& portRect) {
    bool fsel = (gSelectedFrame == frame && gSelectedShape == nullptr);
    y = DrawRow(y, indent, frame->name, fsel, Shape::kRectangle, true,
                frame->visible, frame->locked, portRect);

    for (auto it = frame->childFrames.rbegin(); it != frame->childFrames.rend(); ++it)
        y = DrawFrameRows(it->get(), y, static_cast<short>(indent + 10), portRect);

    for (auto it = frame->children.rbegin(); it != frame->children.rend(); ++it) {
        const Shape* s = it->get();
        bool ssel = (gSelectedShape == s) ||
                    std::find(gSelectedShapes.begin(), gSelectedShapes.end(), s) != gSelectedShapes.end();
        std::string lbl = s->name;
        if (lbl.empty()) lbl = (s->GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";
        y = DrawRow(y, static_cast<short>(indent + 10), lbl, ssel, s->GetType(), false,
                    s->visible, s->locked, portRect);
    }
    return y;
}

void DrawLayersPanel() {
    if (!gLayersWindow || !gDocument) return;
    SetPortWindowPort(gLayersWindow);

    Rect portRect;
    GetWindowPortBounds(gLayersWindow, &portRect);
    short panelW = portRect.right;
    short panelH = portRect.bottom;

    // Refit scroll bar to current window dimensions
    if (gLayersScrollCtrl) {
        MoveControl(gLayersScrollCtrl, static_cast<short>(panelW - kLayersSBW), 0);
        SizeControl(gLayersScrollCtrl, kLayersSBW, panelH);
    }

    // Clamp scroll offset
    short maxScroll = (gLayersTotalH > panelH) ? static_cast<short>(gLayersTotalH - panelH) : 0;
    if (gLayersScrollY > maxScroll) gLayersScrollY = maxScroll;
    if (gLayersScrollY < 0)         gLayersScrollY = 0;

    // Clear visible window area (window coords, before SetOrigin)
    RGBColor bg = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBBackColor(&bg);
    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);
    EraseRect(&portRect);

    // Content rect: exclude the scroll bar strip on the right
    Rect contentRect = portRect;
    contentRect.right = static_cast<short>(panelW - kLayersSBW);

    // Shift drawing coordinate system so rows scroll correctly
    SetOrigin(0, gLayersScrollY);

    TextFont(0); TextSize(11);
    short y = 2;

    for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it) {
        const Shape* s = it->get();
        bool sel = (gSelectedShape == s && gSelectedFrame == nullptr) ||
                   (gSelectedFrame == nullptr &&
                    std::find(gSelectedShapes.begin(), gSelectedShapes.end(), s) != gSelectedShapes.end());
        std::string lbl = s->name;
        if (lbl.empty()) lbl = (s->GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";
        y = DrawRow(y, 0, lbl, sel, s->GetType(), false, s->visible, s->locked, contentRect);
    }

    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it)
        y = DrawFrameRows(it->get(), y, 0, contentRect);

    gLayersTotalH = y;

    // Restore origin before drawing controls
    SetOrigin(0, 0);

    // Update and redraw scroll bar
    if (gLayersScrollCtrl) {
        maxScroll = (gLayersTotalH > panelH) ? static_cast<short>(gLayersTotalH - panelH) : 0;
        SetControlMaximum(gLayersScrollCtrl, maxScroll);
        SetControlValue(gLayersScrollCtrl, gLayersScrollY);
        HiliteControl(gLayersScrollCtrl, (maxScroll > 0) ? 0 : 255);
        DrawControls(gLayersWindow);
    }

    TextSize(12); PenNormal();
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBForeColor(&black); RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Hit-testing
// --------------------------------------------------------------------------

static short HitTestFrameRows(Frame* frame, short y, short indent,
                               Point pt, const Rect& portRect,
                               bool eyeZone, bool lockZone, UInt16 modifiers) {
    Rect row = { y, 2,
                 static_cast<short>(y + kLayerRowH - 1),
                 static_cast<short>(portRect.right - 2) };
    if (PtInRect(pt, &row)) {
        if (eyeZone) {
            PushUndo(); frame->visible = !frame->visible;
            InvalidateLayers(); InvalidateMain();
        } else if (lockZone) {
            PushUndo(); frame->locked = !frame->locked;
            InvalidateLayers(); InvalidateMain();
        } else {
            // Frames don't participate in shape multi-select; always single-select
            gSelectedShapes.clear();
            gSelectedFrame = frame; gSelectedShape = nullptr;
            InvalidateLayers(); InvalidateMain();
        }
        return -1;
    }
    y = static_cast<short>(y + kLayerRowH);

    for (auto it = frame->childFrames.rbegin(); it != frame->childFrames.rend(); ++it) {
        y = HitTestFrameRows(it->get(), y, static_cast<short>(indent + 10),
                             pt, portRect, eyeZone, lockZone, modifiers);
        if (y == -1) return -1;
    }
    for (auto it = frame->children.rbegin(); it != frame->children.rend(); ++it) {
        Shape* s = it->get();
        Rect sr = { y, 2,
                    static_cast<short>(y + kLayerRowH - 1),
                    static_cast<short>(portRect.right - 2) };
        if (PtInRect(pt, &sr)) {
            if (eyeZone) {
                PushUndo(); s->visible = !s->visible;
                InvalidateLayers(); InvalidateMain();
            } else if (lockZone) {
                PushUndo(); s->locked = !s->locked;
                InvalidateLayers(); InvalidateMain();
            } else if ((modifiers & shiftKey) && (gSelectedFrame == frame || gSelectedShapes.size() > 0)) {
                // Shift+click: toggle shape in multi-select (same parent frame required)
                if (gSelectedFrame == nullptr || gSelectedFrame == frame) {
                    gSelectedFrame = frame;
                    auto sit = std::find(gSelectedShapes.begin(), gSelectedShapes.end(), s);
                    if (sit != gSelectedShapes.end()) {
                        gSelectedShapes.erase(sit);
                        gSelectedShape = gSelectedShapes.empty() ? nullptr : gSelectedShapes.back();
                    } else {
                        if (gSelectedShape &&
                            std::find(gSelectedShapes.begin(), gSelectedShapes.end(), gSelectedShape) == gSelectedShapes.end()) {
                            gSelectedShapes.push_back(gSelectedShape);
                        }
                        gSelectedShapes.push_back(s);
                        gSelectedShape = s;
                    }
                    InvalidateLayers(); InvalidateMain();
                }
            } else {
                gSelectedShapes.clear();
                gSelectedFrame = frame; gSelectedShape = s;
                InvalidateLayers(); InvalidateMain();
            }
            return -1;
        }
        y = static_cast<short>(y + kLayerRowH);
    }
    return y;
}

void HandleLayersPanelClick(Point localPt, UInt16 modifiers) {
    if (!gDocument || !gLayersWindow) return;
    SetPortWindowPort(gLayersWindow);

    Rect portRect;
    GetWindowPortBounds(gLayersWindow, &portRect);

    // Check scroll bar before anything else (it lives in the rightmost strip)
    if (gLayersScrollCtrl) {
        ControlHandle hitCtrl = nullptr;
        short ctrlPart = FindControl(localPt, gLayersWindow, &hitCtrl);
        if (ctrlPart && hitCtrl == gLayersScrollCtrl) {
            TrackControl(hitCtrl, localPt, gLayersScrollUPP);
            gLayersScrollY = GetControlValue(hitCtrl);  // sync for thumb drag
            InvalidateLayers();
            return;
        }
    }

    // Adjust click y for scroll offset so hit-testing uses document coordinates
    localPt.v += gLayersScrollY;

    // Content rect excludes scroll bar strip
    Rect contentRect = portRect;
    contentRect.right = static_cast<short>(portRect.right - kLayersSBW);

    bool eyeZone  = (localPt.h >= contentRect.right - kEyeColW);
    bool lockZone = !eyeZone && (localPt.h >= contentRect.right - kEyeColW - kLockColW);

    // Double-click detection (only meaningful for selection clicks, not eye)
    static UInt32  sLastWhen  = 0;
    static Frame*  sLastFrame = nullptr;
    static Shape*  sLastShape = nullptr;

    short y = 2;

    // Root shapes
    for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it) {
        Shape* s = it->get();
        Rect row = { y, 2, static_cast<short>(y + kLayerRowH - 1),
                     static_cast<short>(contentRect.right - 2) };
        if (PtInRect(localPt, &row)) {
            if (eyeZone) {
                PushUndo(); s->visible = !s->visible;
                InvalidateLayers(); InvalidateMain(); return;
            } else if (lockZone) {
                PushUndo(); s->locked = !s->locked;
                InvalidateLayers(); InvalidateMain(); return;
            }
            if ((modifiers & shiftKey) && (gSelectedFrame == nullptr)) {
                // Shift+click root shape: toggle in multi-select
                auto sit = std::find(gSelectedShapes.begin(), gSelectedShapes.end(), s);
                if (sit != gSelectedShapes.end()) {
                    gSelectedShapes.erase(sit);
                    gSelectedShape = gSelectedShapes.empty() ? nullptr : gSelectedShapes.back();
                } else {
                    if (gSelectedShape &&
                        std::find(gSelectedShapes.begin(), gSelectedShapes.end(), gSelectedShape) == gSelectedShapes.end()) {
                        gSelectedShapes.push_back(gSelectedShape);
                    }
                    gSelectedShapes.push_back(s);
                    gSelectedShape = s;
                }
                InvalidateLayers(); InvalidateMain();
                return;
            }
            gSelectedShapes.clear();
            gSelectedFrame = nullptr; gSelectedShape = s;
            InvalidateLayers(); InvalidateMain();
            goto check_dbl;
        }
        y = static_cast<short>(y + kLayerRowH);
    }

    // Top-level frames
    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it) {
        y = HitTestFrameRows(it->get(), y, 0, localPt, contentRect, eyeZone, lockZone, modifiers);
        if (y == -1) {
            if (eyeZone) return;
            goto check_dbl;
        }
    }

    // Empty area — deselect
    gSelectedShapes.clear();
    gSelectedFrame = nullptr; gSelectedShape = nullptr;
    sLastWhen = 0; sLastFrame = nullptr; sLastShape = nullptr;
    InvalidateLayers(); InvalidateMain();
    return;

check_dbl: {
        UInt32 now = static_cast<UInt32>(TickCount());
        bool isDbl = (gSelectedFrame == sLastFrame && gSelectedShape == sLastShape
                      && (now - sLastWhen) <= static_cast<UInt32>(GetDblTime())
                      && (gSelectedFrame != nullptr || gSelectedShape != nullptr));
        sLastWhen  = now;
        sLastFrame = gSelectedFrame;
        sLastShape = gSelectedShape;

        if (isDbl) {
            std::string* targetName = gSelectedShape
                ? &gSelectedShape->name
                : &gSelectedFrame->name;

            // localPt is in document coords (v adjusted by +gLayersScrollY);
            // convert back to window-local before GlobalToLocal
            Point globalPt = { static_cast<short>(localPt.v - gLayersScrollY), localPt.h };
            SetPortWindowPort(gLayersWindow);
            LocalToGlobal(&globalPt);

            std::string newName = ShowRenameDialog(*targetName, globalPt);
            if (!newName.empty()) {
                PushUndo();
                *targetName = newName;
                InvalidateLayers(); InvalidateMain();
                RefreshInspector();
            }
            sLastWhen = 0;
        }
    }
}

void RefreshLayersPanel() { InvalidateLayers(); }
