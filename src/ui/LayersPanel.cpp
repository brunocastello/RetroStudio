#include "LayersPanel.h"
#include "window.h"
#include "RenameDialog.h"
#include "InspectorPanel.h"
#include <algorithm>
#include <cstring>

WindowRef gLayersWindow = nullptr;

static ControlHandle     gLayersScrollCtrl = nullptr;
static short             gLayersScrollY    = 0;  // current scroll offset in pixels
static short             gLayersTotalH     = 0;  // total content height after last draw
static ControlActionUPP  gLayersScrollUPP  = nullptr;
static const short       kLayersSBW        = 16; // scroll bar width
static short             gLayersPrevW      = 0;  // last known panel width (for resize detection)
static short             gLayersPrevH      = 0;

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

// Flat row list rebuilt each draw; used for drag-reorder hit-testing
struct LayerRow {
    bool   isFrame;
    Frame* frame;   // non-null when isFrame
    Shape* shape;   // non-null when !isFrame
    Frame* owner;   // parent frame (nullptr = doc root)
    short  rowTop;  // document-coordinate Y of the row top
    int    vecIdx;  // index in owner's vector (childFrames or children / frames or rootShapes)
};
static std::vector<LayerRow> sLayerRows;

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

// Recursively draw a frame and all its contents as rows; populates sLayerRows.
static short DrawFrameRows(const Frame* frame, short y, short indent,
                           const Rect& portRect, int myVecIdx) {
    sLayerRows.push_back({ true, const_cast<Frame*>(frame), nullptr,
                           frame->parent, y, myVecIdx });
    bool fsel = (gSelectedFrame == frame && gSelectedShape == nullptr)
             || std::find(gSelectedFrames.begin(), gSelectedFrames.end(), frame) != gSelectedFrames.end();
    y = DrawRow(y, indent, frame->name, fsel, Shape::kRectangle, true,
                frame->visible, frame->locked, portRect);

    int cfIdx = static_cast<int>(frame->childFrames.size()) - 1;
    for (auto it = frame->childFrames.rbegin(); it != frame->childFrames.rend(); ++it, --cfIdx)
        y = DrawFrameRows(it->get(), y, static_cast<short>(indent + 10), portRect, cfIdx);

    int chIdx = static_cast<int>(frame->children.size()) - 1;
    for (auto it = frame->children.rbegin(); it != frame->children.rend(); ++it, --chIdx) {
        const Shape* s = it->get();
        sLayerRows.push_back({ false, nullptr, const_cast<Shape*>(s),
                               const_cast<Frame*>(frame), y, chIdx });
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

    // Refit scroll bar only when the window was actually resized.
    // MoveControl/SizeControl call HideControl/ShowControl which internally
    // call InvalWindowRect — doing this every draw creates an infinite update loop.
    if (gLayersScrollCtrl && (panelW != gLayersPrevW || panelH != gLayersPrevH)) {
        gLayersPrevW = panelW;
        gLayersPrevH = panelH;
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

    sLayerRows.clear();

    int rsIdx = static_cast<int>(gDocument->rootShapes.size()) - 1;
    for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it, --rsIdx) {
        const Shape* s = it->get();
        sLayerRows.push_back({ false, nullptr, const_cast<Shape*>(s), nullptr, y, rsIdx });
        bool sel = (gSelectedShape == s && gSelectedFrame == nullptr) ||
                   (gSelectedFrame == nullptr &&
                    std::find(gSelectedShapes.begin(), gSelectedShapes.end(), s) != gSelectedShapes.end());
        std::string lbl = s->name;
        if (lbl.empty()) lbl = (s->GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";
        y = DrawRow(y, 0, lbl, sel, s->GetType(), false, s->visible, s->locked, contentRect);
    }

    int fIdx = static_cast<int>(gDocument->frames.size()) - 1;
    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it, --fIdx)
        y = DrawFrameRows(it->get(), y, 0, contentRect, fIdx);

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
        } else if (modifiers & shiftKey) {
            // Shift+click: toggle frame in gSelectedFrames
            gSelectedShapes.clear();
            gSelectedShape = nullptr;
            auto fit = std::find(gSelectedFrames.begin(), gSelectedFrames.end(), frame);
            if (fit != gSelectedFrames.end()) {
                gSelectedFrames.erase(fit);
                gSelectedFrame = gSelectedFrames.empty() ? nullptr : gSelectedFrames.back();
            } else {
                if (gSelectedFrame &&
                    std::find(gSelectedFrames.begin(), gSelectedFrames.end(), gSelectedFrame) == gSelectedFrames.end())
                    gSelectedFrames.push_back(gSelectedFrame);
                gSelectedFrames.push_back(frame);
                gSelectedFrame = frame;
            }
            InvalidateLayers(); InvalidateMain();
        } else {
            // Normal click: if already in multi-select, keep the group (enables group drag).
            // Otherwise single-select.
            bool alreadyInMulti = !gSelectedFrames.empty() &&
                std::find(gSelectedFrames.begin(), gSelectedFrames.end(), frame) != gSelectedFrames.end();
            if (!alreadyInMulti) {
                gSelectedShapes.clear();
                gSelectedFrames.clear();
            }
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
            } else if (modifiers & shiftKey) {
                // Shift+click: toggle shape in multi-select.
                // Allow when: no context, same frame, or shapes already multi-selected (gSelectedFrame==null).
                bool canAdd = (gSelectedFrame == nullptr || gSelectedFrame == frame || !gSelectedShapes.empty());
                if (canAdd) {
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
                bool alreadyInMulti = !gSelectedShapes.empty() &&
                    std::find(gSelectedShapes.begin(), gSelectedShapes.end(), s) != gSelectedShapes.end();
                if (!alreadyInMulti) {
                    gSelectedShapes.clear();
                    gSelectedFrame = frame;
                }
                gSelectedShape = s;
                InvalidateLayers(); InvalidateMain();
            }
            return -1;
        }
        y = static_cast<short>(y + kLayerRowH);
    }
    return y;
}

// --------------------------------------------------------------------------
// Layer drag-reorder
// --------------------------------------------------------------------------

// Returns true if potentialAncestor is an ancestor-or-equal of node (frame parentage chain).
static bool IsFrameAncestorOrSelf(Frame* potentialAncestor, Frame* node) {
    Frame* cur = node;
    while (cur) {
        if (cur == potentialAncestor) return true;
        cur = cur->parent;
    }
    return false;
}

// Track mouse during a drag-reorder operation.
// srcIdx     — index of the clicked row in sLayerRows.
// startDocPt — click point in document coords (v already offset by gLayersScrollY).
//
// Features:
//  • Multi-select drag: all frames in gSelectedFrames (or shapes in gSelectedShapes)
//    are moved together, provided they share the same parent.
//  • Three drop zones per row (top-third / middle-third / bottom-third):
//      top    → gap-line ABOVE that row
//      middle → "into" that frame (XOR highlight rect); only for frame rows
//      bottom → gap-line BELOW that row
//  • Cross-parent drag: items can move out of their current parent.
static void TrackLayerDrag(int srcIdx, Point startDocPt) {
    if (srcIdx < 0 || srcIdx >= (int)sLayerRows.size()) return;
    const LayerRow& srcRow = sLayerRows[srcIdx];
    bool isSrcFrame = srcRow.isFrame;

    // ── Collect all items to drag ─────────────────────────────────────────────
    struct SrcItem { bool isFrame; Frame* frame; Shape* shape; Frame* owner; int vecIdx; };
    std::vector<SrcItem> srcItems;

    auto gatherFrames = [&]() {
        if (gSelectedFrames.size() > 1) {
            Frame* commonOwner = srcRow.owner;
            std::vector<SrcItem> cand;
            for (Frame* f : gSelectedFrames) {
                for (int i = 0; i < (int)sLayerRows.size(); ++i) {
                    if (sLayerRows[i].isFrame && sLayerRows[i].frame == f) {
                        if (sLayerRows[i].owner != commonOwner) { cand.clear(); return; }
                        cand.push_back({ true, f, nullptr, sLayerRows[i].owner, sLayerRows[i].vecIdx });
                        break;
                    }
                }
            }
            srcItems = cand;
        }
    };
    auto gatherShapes = [&]() {
        if (gSelectedShapes.size() > 1) {
            Frame* commonOwner = srcRow.owner;
            std::vector<SrcItem> cand;
            for (Shape* s : gSelectedShapes) {
                for (int i = 0; i < (int)sLayerRows.size(); ++i) {
                    if (!sLayerRows[i].isFrame && sLayerRows[i].shape == s) {
                        if (sLayerRows[i].owner != commonOwner) { cand.clear(); return; }
                        cand.push_back({ false, nullptr, s, sLayerRows[i].owner, sLayerRows[i].vecIdx });
                        break;
                    }
                }
            }
            srcItems = cand;
        }
    };

    if (isSrcFrame)  gatherFrames();
    else             gatherShapes();

    if (srcItems.empty())
        srcItems.push_back({ srcRow.isFrame, srcRow.frame, srcRow.shape, srcRow.owner, srcRow.vecIdx });

    // Sort ascending vecIdx (stable relative order for insertion)
    std::sort(srcItems.begin(), srcItems.end(), [](const SrcItem& a, const SrcItem& b){
        return a.vecIdx < b.vecIdx;
    });

    // Forbidden drop targets: self or any descendant of a dragged frame
    auto isForbidden = [&](Frame* f) -> bool {
        for (const SrcItem& si : srcItems)
            if (si.isFrame && IsFrameAncestorOrSelf(si.frame, f)) return true;
        return false;
    };

    // ── Setup ─────────────────────────────────────────────────────────────────
    SetPortWindowPort(gLayersWindow);
    Rect portRect; GetWindowPortBounds(gLayersWindow, &portRect);
    short panelW = static_cast<short>(portRect.right - kLayersSBW);

    // All rows of same type for hit-testing
    std::vector<int> allIdxs;
    for (int i = 0; i < (int)sLayerRows.size(); ++i)
        if (sLayerRows[i].isFrame == isSrcFrame)
            allIdxs.push_back(i);
    int N = static_cast<int>(allIdxs.size());
    if (N < 2) return;

    // ── Tracking loop ─────────────────────────────────────────────────────────
    bool  isDragging   = false;
    bool  prevIsInto   = false;
    int   prevDropPos  = -2;   // -2 = "unknown initial"
    short prevWinY     = -1;   // valid only when !prevIsInto

    while (StillDown()) {
        Point rawPt; GetMouse(&rawPt);
        short docV = static_cast<short>(rawPt.v + gLayersScrollY);
        short dy   = static_cast<short>(docV - startDocPt.v);
        if (!isDragging && (dy > 4 || dy < -4)) isDragging = true;
        if (!isDragging) continue;

        // Determine drop target using three zones per row
        bool curIsInto = false;
        int  curPos    = N; // default: gap after all rows

        for (int i = 0; i < N; ++i) {
            short rowT = sLayerRows[allIdxs[i]].rowTop;
            short rowB = static_cast<short>(rowT + kLayerRowH);
            if (docV < rowT) { curPos = i; break; }   // above row i → gap before it
            if (docV < rowB) {
                short z1 = static_cast<short>(rowT + kLayerRowH / 3);
                short z2 = static_cast<short>(rowT + 2 * kLayerRowH / 3);
                if (docV < z1) {
                    curPos = i;             // top zone → gap before
                } else if (docV >= z2) {
                    curPos = i + 1;         // bottom zone → gap after
                } else if (isSrcFrame && !isForbidden(sLayerRows[allIdxs[i]].frame)) {
                    curIsInto = true;       // middle zone of a safe frame → drop into
                    curPos    = i;
                } else {
                    curPos = i;             // middle of non-droppable row → gap before
                }
                break;
            }
        }

        if (curIsInto == prevIsInto && curPos == prevDropPos) continue;

        Pattern blk; memset(&blk, 0xFF, sizeof(blk));

        // Erase previous indicator
        if (prevDropPos != -2) {
            if (prevIsInto && prevDropPos >= 0 && prevDropPos < N) {
                short rowT = static_cast<short>(sLayerRows[allIdxs[prevDropPos]].rowTop - gLayersScrollY);
                Rect hr = { rowT, 2, static_cast<short>(rowT + kLayerRowH - 1), static_cast<short>(panelW - 2) };
                PenMode(patXor); PenSize(2, 2); PenPat(&blk); FrameRect(&hr); PenNormal();
            } else if (!prevIsInto && prevWinY >= 0) {
                PenMode(patXor); PenSize(2, 2); PenPat(&blk);
                MoveTo(4, prevWinY); LineTo(static_cast<short>(panelW - 4), prevWinY);
                PenNormal();
            }
        }

        // Draw new indicator
        if (curIsInto) {
            short rowT = static_cast<short>(sLayerRows[allIdxs[curPos]].rowTop - gLayersScrollY);
            Rect hr = { rowT, 2, static_cast<short>(rowT + kLayerRowH - 1), static_cast<short>(panelW - 2) };
            PenMode(patXor); PenSize(2, 2); PenPat(&blk); FrameRect(&hr); PenNormal();
            prevWinY = -1;
        } else {
            short indicDocY;
            if (curPos <= 0)
                indicDocY = sLayerRows[allIdxs[0]].rowTop;
            else if (curPos >= N)
                indicDocY = static_cast<short>(sLayerRows[allIdxs[N-1]].rowTop + kLayerRowH);
            else
                indicDocY = sLayerRows[allIdxs[curPos]].rowTop;
            short winY = static_cast<short>(indicDocY - gLayersScrollY);
            PenMode(patXor); PenSize(2, 2); PenPat(&blk);
            MoveTo(4, winY); LineTo(static_cast<short>(panelW - 4), winY);
            PenNormal();
            prevWinY = winY;
        }
        prevIsInto  = curIsInto;
        prevDropPos = curPos;
    }

    // Erase final indicator
    {
        Pattern blk; memset(&blk, 0xFF, sizeof(blk));
        if (prevIsInto && prevDropPos >= 0 && prevDropPos < N) {
            short rowT = static_cast<short>(sLayerRows[allIdxs[prevDropPos]].rowTop - gLayersScrollY);
            Rect hr = { rowT, 2, static_cast<short>(rowT + kLayerRowH - 1), static_cast<short>(panelW - 2) };
            PenMode(patXor); PenSize(2, 2); PenPat(&blk); FrameRect(&hr); PenNormal();
        } else if (!prevIsInto && prevWinY >= 0) {
            PenMode(patXor); PenSize(2, 2); PenPat(&blk);
            MoveTo(4, prevWinY); LineTo(static_cast<short>(portRect.right - kLayersSBW - 4), prevWinY);
            PenNormal();
        }
    }

    if (!isDragging || prevDropPos < 0) return;

    bool isInto = prevIsInto;
    int  dropPos = prevDropPos;

    // ── Determine destination ─────────────────────────────────────────────────
    Frame* dstOwner;
    int    insertAt;

    if (isInto) {
        // Drop into the frame at allIdxs[dropPos] — it becomes the new parent
        dstOwner = sLayerRows[allIdxs[dropPos]].frame;
        // Append at top (highest z) of dstOwner's children
        insertAt = isSrcFrame
            ? static_cast<int>(dstOwner->childFrames.size())
            : static_cast<int>(dstOwner->children.size());
    } else {
        // Gap drop — owner comes from the reference row
        int refIdx = (dropPos <= 0) ? 0 : (dropPos >= N ? N-1 : dropPos);
        dstOwner = sLayerRows[allIdxs[refIdx]].owner;

        if (dropPos <= 0) {
            insertAt = isSrcFrame
                ? static_cast<int>((dstOwner ? dstOwner->childFrames : gDocument->frames).size())
                : static_cast<int>((dstOwner ? dstOwner->children    : gDocument->rootShapes).size());
        } else if (dropPos >= N) {
            insertAt = 0;
        } else {
            insertAt = sLayerRows[allIdxs[dropPos]].vecIdx + 1;
        }
    }

    // Cycle check
    for (const SrcItem& si : srcItems)
        if (si.isFrame && dstOwner && IsFrameAncestorOrSelf(si.frame, dstOwner))
            return;

    // Check something actually changes (single-item, same owner, same position)
    bool sameOwner = (srcItems[0].owner == dstOwner);
    if (srcItems.size() == 1 && !isInto && sameOwner && srcItems[0].vecIdx == insertAt - 1)
        return;

    PushUndo();

    // ── Extract all src items (high vecIdx first to avoid index shifts in same vec) ──
    std::vector<std::unique_ptr<Frame>> movedFrames;
    std::vector<std::unique_ptr<Shape>> movedShapes;
    std::vector<Frame*> movedFramePtrs; // raw ptrs before move, ascending vecIdx order

    // Sort descending for extraction
    std::vector<SrcItem> extractOrder = srcItems;
    std::sort(extractOrder.begin(), extractOrder.end(), [](const SrcItem& a, const SrcItem& b){
        return a.vecIdx > b.vecIdx;
    });

    if (isSrcFrame) {
        for (const SrcItem& si : extractOrder) {
            auto& srcVec = si.owner ? si.owner->childFrames : gDocument->frames;
            movedFramePtrs.push_back(srcVec[si.vecIdx].get());
            movedFrames.insert(movedFrames.begin(), std::move(srcVec[si.vecIdx]));
            srcVec.erase(srcVec.begin() + si.vecIdx);
            // Adjust insertAt for same-vector extraction
            if (sameOwner && si.vecIdx < insertAt) --insertAt;
        }
        for (auto& f : movedFrames) f->parent = dstOwner;
    } else {
        for (const SrcItem& si : extractOrder) {
            auto& srcVec = si.owner ? si.owner->children : gDocument->rootShapes;
            movedShapes.insert(movedShapes.begin(), std::move(srcVec[si.vecIdx]));
            srcVec.erase(srcVec.begin() + si.vecIdx);
            if (sameOwner && si.vecIdx < insertAt) --insertAt;
        }
    }

    // ── Insert into destination ───────────────────────────────────────────────
    if (isSrcFrame) {
        auto& dstVec = dstOwner ? dstOwner->childFrames : gDocument->frames;
        if (insertAt < 0) insertAt = 0;
        if (insertAt > (int)dstVec.size()) insertAt = static_cast<int>(dstVec.size());
        int firstAt = insertAt;
        for (auto& f : movedFrames) {
            dstVec.insert(dstVec.begin() + insertAt, std::move(f));
            ++insertAt;
        }
        // Update selection
        gSelectedFrames.clear();
        for (int i = firstAt; i < (int)dstVec.size() && i < firstAt + (int)movedFramePtrs.size(); ++i)
            gSelectedFrames.push_back(dstVec[i].get());
        gSelectedFrame = gSelectedFrames.empty() ? nullptr : gSelectedFrames.back();
        gSelectedShape = nullptr;
    } else {
        auto& dstVec = dstOwner ? dstOwner->children : gDocument->rootShapes;
        if (insertAt < 0) insertAt = 0;
        if (insertAt > (int)dstVec.size()) insertAt = static_cast<int>(dstVec.size());
        int firstAt = insertAt;
        for (auto& s : movedShapes) {
            dstVec.insert(dstVec.begin() + insertAt, std::move(s));
            ++insertAt;
        }
        gSelectedShapes.clear();
        for (int i = firstAt; i < (int)dstVec.size() && i < firstAt + (int)movedShapes.size(); ++i)
            gSelectedShapes.push_back(dstVec[i].get());
        gSelectedShape = gSelectedShapes.empty() ? nullptr : gSelectedShapes.back();
        gSelectedFrame = dstOwner;
    }

    InvalidateLayers(); InvalidateMain();
    RefreshInspector();
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
            if ((modifiers & shiftKey) && (gSelectedFrame == nullptr || !gSelectedShapes.empty())) {
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
            bool alreadyInMulti = !gSelectedShapes.empty() &&
                std::find(gSelectedShapes.begin(), gSelectedShapes.end(), s) != gSelectedShapes.end();
            if (!alreadyInMulti) {
                gSelectedShapes.clear();
                gSelectedFrame = nullptr;
            }
            gSelectedShape = s;
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
    gSelectedFrames.clear();
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
        } else if (!eyeZone && !lockZone && !(modifiers & shiftKey)) {
            // Drag-reorder: find the just-selected item in sLayerRows and track
            int srcIdx = -1;
            for (int i = 0; i < (int)sLayerRows.size(); ++i) {
                const LayerRow& r = sLayerRows[i];
                if (r.isFrame && r.frame == gSelectedFrame && gSelectedShape == nullptr) {
                    srcIdx = i; break;
                }
                if (!r.isFrame && r.shape == gSelectedShape) {
                    srcIdx = i; break;
                }
            }
            if (srcIdx >= 0) TrackLayerDrag(srcIdx, localPt);
        }
    }
}

void RefreshLayersPanel() { InvalidateLayers(); }
