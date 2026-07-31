#include "window.h"
#include "LayersPanel.h"
#include "InspectorPanel.h"
#include "RenameDialog.h"
#include "../export/DocumentSerializer.h"

WindowRef  gMainWindow    = nullptr;
Boolean    gQuitFlag      = false;
Tool       gActiveTool    = Tool::Select;
Renderer*  gRenderer      = nullptr;
Document*  gDocument      = nullptr;
Frame*     gSelectedFrame = nullptr;
Shape*     gSelectedShape = nullptr;
int        gNextFrameNum  = 2;
bool       gIsDoubleClick = false;
SInt32     gCanvasOffsetX = 0;
SInt32     gCanvasOffsetY = 0;
int        gCanvasZoom    = 100;
int        gNextRectNum   = 1;
int        gNextEllipseNum = 1;

// In-memory clipboard (one item — either a frame or a shape, never both)
static std::unique_ptr<Frame> sClipFrame;
static std::unique_ptr<Shape> sClipShape;
static int                    sPasteOffset = 0;  // increments per paste, resets on copy

// Undo / redo stacks — each entry is a full document snapshot
static const int kMaxUndo = 50;
static std::vector<std::unique_ptr<Document>> sUndoStack;
static std::vector<std::unique_ptr<Document>> sRedoStack;

static const short kZoomDocProc = 8;
static const short kFileMenuID  = 129;
static const short kEditMenuID  = 130;
static const short kViewMenuID  = 131;
static const short kFileNew     = 1;
static const short kFileOpen    = 2;
static const short kFileSave    = 4;
static const short kFileQuit    = 6;
static const short kViewZoomIn  = 1;
static const short kViewZoomOut = 2;
// item 3 = separator
static const short kViewZoomFit = 4;
static const short kViewZoom100 = 5;
// Edit menu items
static const short kEditUndo    = 1;
static const short kEditRedo    = 2;
// item 3 = separator
// item 4 = Cut
static const short kEditCopy    = 5;
static const short kEditPaste   = 6;

// --------------------------------------------------------------------------
// Small helpers
// --------------------------------------------------------------------------

static inline short sMin(short a, short b) { return a < b ? a : b; }
static inline short sMax(short a, short b) { return a > b ? a : b; }

static std::string istr(int n) {
    if (n == 0) return std::string("0");
    char buf[12]; int i = 11; buf[i] = '\0';
    while (n > 0) { buf[--i] = static_cast<char>('0' + n % 10); n /= 10; }
    return std::string(&buf[i]);
}

// Forward declaration — ToPStr is defined in the rendering section below
static void ToPStr(const std::string& src, Str255& dst);

// --------------------------------------------------------------------------
// Canvas coordinate transforms
// --------------------------------------------------------------------------

Rect CanvasRect(const Bounds2& b) {
    Rect r;
    r.left   = static_cast<short>(SInt32(b.x)         * gCanvasZoom / 100 + gCanvasOffsetX);
    r.top    = static_cast<short>(SInt32(b.y)         * gCanvasZoom / 100 + gCanvasOffsetY);
    r.right  = static_cast<short>(SInt32(b.x + b.w)  * gCanvasZoom / 100 + gCanvasOffsetX);
    r.bottom = static_cast<short>(SInt32(b.y + b.h)  * gCanvasZoom / 100 + gCanvasOffsetY);
    return r;
}

Point ScreenToCanvas(Point screenPt) {
    Point p;
    p.h = static_cast<short>((SInt32(screenPt.h) - gCanvasOffsetX) * 100 / gCanvasZoom);
    p.v = static_cast<short>((SInt32(screenPt.v) - gCanvasOffsetY) * 100 / gCanvasZoom);
    return p;
}

static void UpdateWindowTitle() {
    std::string title = "RetroStudio " + istr(gCanvasZoom) + "%";
    Str255 pt; ToPStr(title, pt);
    SetWTitle(gMainWindow, pt);
}

static const int kZoomTable[] = { 10, 25, 50, 75, 100, 150, 200, 300, 400 };
static const int kNumZoom = 9;

static void ZoomTo(int newZoom) {
    if (!gMainWindow) return;
    Rect portRect; GetWindowPortBounds(gMainWindow, &portRect);
    short cx = static_cast<short>((portRect.left + portRect.right)  / 2);
    short cy = static_cast<short>((portRect.top  + portRect.bottom) / 2);
    SInt32 canvasX = (SInt32(cx) - gCanvasOffsetX) * 100 / gCanvasZoom;
    SInt32 canvasY = (SInt32(cy) - gCanvasOffsetY) * 100 / gCanvasZoom;
    gCanvasZoom    = newZoom;
    gCanvasOffsetX = SInt32(cx) - canvasX * gCanvasZoom / 100;
    gCanvasOffsetY = SInt32(cy) - canvasY * gCanvasZoom / 100;
    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
    UpdateWindowTitle();
}

void StepZoom(int dir) {
    int idx = kNumZoom - 1;
    for (int i = 0; i < kNumZoom; ++i) {
        if (kZoomTable[i] >= gCanvasZoom) { idx = i; break; }
    }
    int newIdx = idx + dir;
    if (newIdx < 0) newIdx = 0;
    if (newIdx >= kNumZoom) newIdx = kNumZoom - 1;
    ZoomTo(kZoomTable[newIdx]);
}

void ZoomToFit() {
    if (!gDocument || gDocument->frames.empty()) return;
    SInt32 minX = gDocument->frames[0]->bounds.x;
    SInt32 minY = gDocument->frames[0]->bounds.y;
    SInt32 maxX = minX + gDocument->frames[0]->bounds.w;
    SInt32 maxY = minY + gDocument->frames[0]->bounds.h;
    for (const auto& f : gDocument->frames) {
        if (f->bounds.x           < minX) minX = f->bounds.x;
        if (f->bounds.y           < minY) minY = f->bounds.y;
        if (f->bounds.x + f->bounds.w > maxX) maxX = f->bounds.x + f->bounds.w;
        if (f->bounds.y + f->bounds.h > maxY) maxY = f->bounds.y + f->bounds.h;
    }
    Rect portRect; GetWindowPortBounds(gMainWindow, &portRect);
    SInt32 vpW = portRect.right - portRect.left;
    SInt32 vpH = portRect.bottom - portRect.top;
    SInt32 cW  = maxX - minX;
    SInt32 cH  = maxY - minY;
    if (cW <= 0 || cH <= 0) return;
    SInt32 fzW = (vpW - 40) * 100 / cW;
    SInt32 fzH = (vpH - 40) * 100 / cH;
    int fz = static_cast<int>(fzW < fzH ? fzW : fzH);
    if (fz > 400) fz = 400;
    if (fz < 10)  fz = 10;
    gCanvasZoom    = fz;
    SInt32 scaledW = cW * fz / 100;
    SInt32 scaledH = cH * fz / 100;
    gCanvasOffsetX = (vpW - scaledW) / 2 - minX * fz / 100;
    gCanvasOffsetY = (vpH - scaledH) / 2 - minY * fz / 100;
    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
    UpdateWindowTitle();
}

void HandleCanvasPan(WindowRef win, Point startGlobal) {
    SetPortWindowPort(win);
    Point prevPt = startGlobal; GlobalToLocal(&prevPt);
    Point currPt = prevPt;
    while (Button()) {
        GetMouse(&currPt);
        if (currPt.h != prevPt.h || currPt.v != prevPt.v) {
            gCanvasOffsetX += currPt.h - prevPt.h;
            gCanvasOffsetY += currPt.v - prevPt.v;
            DrawWindowContent(win);
            prevPt = currPt;
        }
    }
    Rect r; GetWindowPortBounds(win, &r); InvalWindowRect(win, &r);
}

// --------------------------------------------------------------------------
// Menus
// --------------------------------------------------------------------------

void SetupMenus() {
    MenuRef fileMenu = NewMenu(kFileMenuID, "\pFile");
    AppendMenu(fileMenu, "\pNew");
    SetItemCmd(fileMenu, kFileNew, 'N');
    AppendMenu(fileMenu, "\pOpen...");
    SetItemCmd(fileMenu, kFileOpen, 'O');
    AppendMenu(fileMenu, "\p-");
    AppendMenu(fileMenu, "\pSave...");
    SetItemCmd(fileMenu, kFileSave, 'S');
    AppendMenu(fileMenu, "\p-");
    AppendMenu(fileMenu, "\pQuit");
    SetItemCmd(fileMenu, kFileQuit, 'Q');
    InsertMenu(fileMenu, 0);

    MenuRef editMenu = NewMenu(kEditMenuID, "\pEdit");
    AppendMenu(editMenu, "\pUndo");
    SetItemCmd(editMenu, kEditUndo, 'Z');
    AppendMenu(editMenu, "\pRedo");
    AppendMenu(editMenu, "\p-");
    AppendMenu(editMenu, "\pCut");
    SetItemCmd(editMenu, 4, 'X');
    AppendMenu(editMenu, "\pCopy");
    SetItemCmd(editMenu, kEditCopy, 'C');
    AppendMenu(editMenu, "\pPaste");
    SetItemCmd(editMenu, kEditPaste, 'V');
    InsertMenu(editMenu, 0);

    MenuRef viewMenu = NewMenu(kViewMenuID, "\pView");
    AppendMenu(viewMenu, "\pZoom In");
    SetItemCmd(viewMenu, kViewZoomIn, '=');
    AppendMenu(viewMenu, "\pZoom Out");
    SetItemCmd(viewMenu, kViewZoomOut, '-');
    AppendMenu(viewMenu, "\p-");
    AppendMenu(viewMenu, "\pZoom to Fit");
    SetItemCmd(viewMenu, kViewZoomFit, '0');
    AppendMenu(viewMenu, "\pActual Size");
    SetItemCmd(viewMenu, kViewZoom100, '1');
    InsertMenu(viewMenu, 0);

    DrawMenuBar();
}

// --------------------------------------------------------------------------
// Window + document bootstrap
// --------------------------------------------------------------------------

void SetupWindow() {
    Rect bounds = { 50, 80, 580, 720 };
    gMainWindow = NewCWindow(
        nullptr, &bounds, "\pRetroStudio",
        true, kZoomDocProc, (WindowRef)-1L, true, 0);

    gDocument = new Document();
    gDocument->name = "Untitled";

    auto frame           = std::make_unique<Frame>();
    frame->name          = "Screen 1";
    frame->bounds        = { 40, 40, 390, 480 };
    frame->backgroundColor = { 0xFFFF, 0xFFFF, 0xFFFF };
    gDocument->frames.push_back(std::move(frame));

    UpdateWindowTitle();
}

// --------------------------------------------------------------------------
// Rendering — direct QuickDraw into window port
// --------------------------------------------------------------------------

// Build a Pascal string from a C++ string (max 63 visible chars)
static void ToPStr(const std::string& src, Str255& dst) {
    dst[0] = 0;
    for (int i = 0; src[i] && i < 63; ++i) {
        dst[i + 1] = static_cast<unsigned char>(src[i]); dst[0]++;
    }
}

static void DrawShapeNameLabel(const Shape& shape) {
    Rect r = CanvasRect(shape.bounds);
    std::string label = shape.name;
    if (label.empty())
        label = (shape.GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";
    Str255 pn; ToPStr(label, pn);
    TextSize(10);
    RGBColor lc = { 0x8888, 0x8888, 0x8888 };
    RGBForeColor(&lc);
    MoveTo(r.left, static_cast<short>(r.top - 5));
    DrawString(pn);
    TextSize(12);
}

static void DrawShape(const Shape& shape) {
    if (!shape.visible) return;
    Rect r = CanvasRect(shape.bounds);
    switch (shape.GetType()) {
        case Shape::kRectangle:
        case Shape::kLine:
            if (shape.hasFill) {
                RGBColor c = shape.fillColor; RGBForeColor(&c); PaintRect(&r);
            }
            if (shape.hasStroke) {
                RGBColor c = shape.strokeColor; RGBForeColor(&c);
                PenSize(shape.strokeWidth, shape.strokeWidth);
                FrameRect(&r); PenSize(1,1);
            }
            break;
        case Shape::kEllipse:
            if (shape.hasFill) {
                RGBColor c = shape.fillColor; RGBForeColor(&c); PaintOval(&r);
            }
            if (shape.hasStroke) {
                RGBColor c = shape.strokeColor; RGBForeColor(&c);
                PenSize(shape.strokeWidth, shape.strokeWidth);
                FrameOval(&r); PenSize(1,1);
            }
            break;
        default: break;
    }
    DrawShapeNameLabel(shape);
}

// Forward-declare so DrawFrame can call itself recursively
static void DrawFrame(const Frame& frame);

static void DrawFrame(const Frame& frame) {
    if (!frame.visible) return;
    Rect r = CanvasRect(frame.bounds);

    // Fill
    RGBColor bg = frame.backgroundColor;
    RGBForeColor(&bg);
    PaintRect(&r);

    // Shape children
    for (const auto& s : frame.children)
        DrawShape(*s);

    // Nested child frames (drawn on top of shapes)
    for (const auto& cf : frame.childFrames)
        DrawFrame(*cf);

    // Thin border (on top of everything in this frame)
    RGBColor border = { 0xBBBB, 0xBBBB, 0xBBBB };
    RGBForeColor(&border);
    FrameRect(&r);

    // Name label above top-left corner
    RGBColor lc = { 0x4444, 0x4444, 0x4444 };
    RGBForeColor(&lc);
    TextSize(10);
    Str255 pn; ToPStr(frame.name, pn);
    MoveTo(r.left, static_cast<short>(r.top - 5));
    DrawString(pn);
    TextSize(12);
}

static void DrawSelectionHighlight() {
    if (!gSelectedFrame && !gSelectedShape) return;

    Rect r = gSelectedShape
        ? CanvasRect(gSelectedShape->bounds)
        : CanvasRect(gSelectedFrame->bounds);

    RGBColor selBlue = { 0x1177, 0x55AA, 0xFFFF };
    RGBForeColor(&selBlue);
    PenSize(2, 2);
    FrameRect(&r);
    PenSize(1, 1);

    static const short kHW = 4;
    short cx = static_cast<short>((r.left + r.right)  / 2);
    short cy = static_cast<short>((r.top  + r.bottom) / 2);
    const short hx[8] = { r.left, cx, r.right, r.right,  r.right,  cx,     r.left,  r.left };
    const short hy[8] = { r.top,  r.top, r.top, cy,      r.bottom, r.bottom, r.bottom, cy   };
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    for (int i = 0; i < 8; ++i) {
        Rect h = {
            static_cast<short>(hy[i]-kHW), static_cast<short>(hx[i]-kHW),
            static_cast<short>(hy[i]+kHW), static_cast<short>(hx[i]+kHW)
        };
        RGBForeColor(&white); PaintRect(&h);
        RGBForeColor(&selBlue); FrameRect(&h);
    }
    PenNormal();
}

void DrawWindowContent(WindowRef win) {
    SetPortWindowPort(win);
    Rect portRect;
    GetWindowPortBounds(win, &portRect);

    RGBColor canvasBg = { 0xDDDD, 0xDDDD, 0xDDDD };
    RGBBackColor(&canvasBg);
    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);
    EraseRect(&portRect);

    if (gDocument) {
        for (const auto& frame : gDocument->frames)
            DrawFrame(*frame);

        // Shapes floating at canvas root (outside every frame)
        for (const auto& shape : gDocument->rootShapes)
            DrawShape(*shape);
    }

    DrawSelectionHighlight();

    PenNormal();
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBForeColor(&black);
    RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Hierarchy helpers
// --------------------------------------------------------------------------

// Move a frame and ALL its descendants (shapes + nested frames) by (dx,dy) in canvas pixels
static void MoveFrameTree(Frame* f, SInt32 dx, SInt32 dy) {
    f->bounds.x += dx;
    f->bounds.y += dy;
    for (auto& c : f->children)     { c->bounds.x += dx; c->bounds.y += dy; }
    for (auto& cf : f->childFrames) { MoveFrameTree(cf.get(), dx, dy); }
}

// Hit-test result from recursive search
struct HitResult { Frame* frame = nullptr; Shape* shape = nullptr; bool found = false; };

// Recursively search inside `f`. Returns deepest match.
static HitResult HitTestFrame(Frame* f, Point pt) {
    Rect r = CanvasRect(f->bounds);
    if (!PtInRect(pt, &r)) return {};

    // Child frames first (last added = topmost z-order)
    for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
        HitResult res = HitTestFrame(it->get(), pt);
        if (res.found) return res;
    }
    // Shapes within this frame
    for (auto it = f->children.rbegin(); it != f->children.rend(); ++it) {
        Rect sr = CanvasRect((*it)->bounds);
        if (PtInRect(pt, &sr)) return { f, it->get(), true };
    }
    return { f, nullptr, true };  // hit frame body
}

// Find the most deeply nested frame that contains `pt`, skipping `exclude`
static Frame* DeepestInFrame(Frame* f, Point pt, Frame* skip) {
    if (f == skip) return nullptr;
    Rect r = CanvasRect(f->bounds);
    if (!PtInRect(pt, &r)) return nullptr;
    for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
        Frame* deeper = DeepestInFrame(it->get(), pt, skip);
        if (deeper) return deeper;
    }
    return f;
}

static Frame* DeepestFrameAt(Point pt, Frame* skip = nullptr) {
    Frame* result = nullptr;
    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it) {
        Frame* f = DeepestInFrame(it->get(), pt, skip);
        if (f) result = f;
    }
    return result;
}

// Extract a Shape unique_ptr from its current owner (Frame or rootShapes)
static std::unique_ptr<Shape> ExtractShape(Shape* s, Frame* parent) {
    auto& vec = parent ? parent->children : gDocument->rootShapes;
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (it->get() == s) { auto o = std::move(*it); vec.erase(it); return o; }
    }
    return nullptr;
}

// Extract a Frame unique_ptr from its current owner
static std::unique_ptr<Frame> ExtractFrame(Frame* f) {
    auto& vec = f->parent ? f->parent->childFrames : gDocument->frames;
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (it->get() == f) { auto o = std::move(*it); vec.erase(it); return o; }
    }
    return nullptr;
}

// --------------------------------------------------------------------------
// Resize handle hit-test + drag
// --------------------------------------------------------------------------

// Returns the handle index (0-7) if pt lands on one of the 8 selection
// handles drawn by DrawSelectionHighlight, or -1 if nothing selected / miss.
// Handle order: 0=TL 1=TC 2=TR 3=MR 4=BR 5=BC 6=BL 7=ML
static int HitTestHandles(Point pt) {
    if (!gSelectedFrame && !gSelectedShape) return -1;
    Rect r = gSelectedShape
        ? CanvasRect(gSelectedShape->bounds)
        : CanvasRect(gSelectedFrame->bounds);

    static const short kHW = 4;
    short cx = static_cast<short>((r.left + r.right)  / 2);
    short cy = static_cast<short>((r.top  + r.bottom) / 2);
    const short hx[8] = { r.left, cx, r.right, r.right,  r.right,  cx,     r.left,  r.left };
    const short hy[8] = { r.top,  r.top, r.top, cy,      r.bottom, r.bottom, r.bottom, cy   };

    for (int i = 0; i < 8; ++i) {
        Rect h = {
            static_cast<short>(hy[i] - kHW), static_cast<short>(hx[i] - kHW),
            static_cast<short>(hy[i] + kHW), static_cast<short>(hx[i] + kHW)
        };
        if (PtInRect(pt, &h)) return i;
    }
    return -1;
}

// Drag the selected object's bounds by moving only the edge(s) implied by
// handleIdx, then redraw live.  Minimum dimension: 10px.
static void HandleResizeDrag(WindowRef win, int hi, Point startPt) {
    // Which edges each handle moves
    // Index:           0      1      2      3      4      5      6      7
    static const bool bL[8]={ true,  false, false, false, false, false, true,  true  };
    static const bool bT[8]={ true,  true,  true,  false, false, false, false, false };
    static const bool bR[8]={ false, false, true,  true,  true,  false, false, false };
    static const bool bB[8]={ false, false, false, false, true,  true,  true,  false };

    Bounds2* b = gSelectedShape
        ? &gSelectedShape->bounds
        : &gSelectedFrame->bounds;

    static const SInt32 kMin = 10;
    Point prev = startPt, curr = startPt;
    bool pushedUndo = false;

    while (Button()) {
        GetMouse(&curr);
        if (curr.h != prev.h || curr.v != prev.v) {
            if (!pushedUndo) { PushUndo(); pushedUndo = true; }
            // Convert screen pixel delta → canvas pixel delta
            SInt32 dx = SInt32(curr.h - prev.h) * 100 / gCanvasZoom;
            SInt32 dy = SInt32(curr.v - prev.v) * 100 / gCanvasZoom;

            if (bL[hi]) { b->x += dx; b->w -= dx; }
            if (bT[hi]) { b->y += dy; b->h -= dy; }
            if (bR[hi])   b->w += dx;
            if (bB[hi])   b->h += dy;

            if (b->w < kMin) { if (bL[hi]) b->x -= (kMin - b->w); b->w = kMin; }
            if (b->h < kMin) { if (bT[hi]) b->y -= (kMin - b->h); b->h = kMin; }

            DrawWindowContent(win);
            prev = curr;
        }
    }
    Rect pr; GetWindowPortBounds(win, &pr); InvalWindowRect(win, &pr);
}

// --------------------------------------------------------------------------
// Name-label hit-tests (canvas coordinate space, port = main window)
// --------------------------------------------------------------------------

// Returns the Shape whose name label contains pt, searching recursively
// through the given frame and its children.
static Shape* HitTestShapeLabel(const Frame* f, Point pt) {
    for (auto it = f->children.rbegin(); it != f->children.rend(); ++it) {
        Shape* s = it->get();
        Rect r = CanvasRect(s->bounds);
        std::string label = s->name;
        if (label.empty())
            label = (s->GetType() == Shape::kEllipse) ? "Ellipse" : "Rectangle";
        Str255 pn; ToPStr(label, pn);
        TextSize(10);
        short tw = StringWidth(pn);
        TextSize(12);
        Rect lr = {
            static_cast<short>(r.top  - 16), r.left,
            static_cast<short>(r.top  -  1), static_cast<short>(r.left + tw + 4)
        };
        if (PtInRect(pt, &lr)) return s;
    }
    for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
        Shape* s = HitTestShapeLabel(it->get(), pt);
        if (s) return s;
    }
    return nullptr;
}

// Returns the innermost Frame whose name label (rendered above its top-left
// corner) contains pt, or nullptr.  Port must already be set to main window.
static Frame* HitTestFrameLabel(Frame* f, Point pt) {
    Rect fr = CanvasRect(f->bounds);

    Str255 pn; pn[0] = 0;
    const char* nm = f->name.c_str();
    for (int i = 0; nm[i] && i < 63; ++i) { pn[i+1] = (unsigned char)nm[i]; pn[0]++; }

    TextSize(10);
    short tw = StringWidth(pn);
    TextSize(12);

    Rect label = {
        static_cast<short>(fr.top - 16), fr.left,
        static_cast<short>(fr.top -  1), static_cast<short>(fr.left + tw + 4)
    };
    if (PtInRect(pt, &label)) return f;

    for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
        Frame* res = HitTestFrameLabel(it->get(), pt);
        if (res) return res;
    }
    return nullptr;
}

// --------------------------------------------------------------------------
// Select tool: resize handles → name labels → body hit-test → move + reparent
// --------------------------------------------------------------------------

void HandleCanvasSelect(WindowRef win, Point startGlobal) {
    if (!gDocument) return;

    SetPortWindowPort(win);
    Point pt = startGlobal;
    GlobalToLocal(&pt);

    // ---- 1. Resize handle (only when something is already selected) ----
    int handleIdx = HitTestHandles(pt);
    if (handleIdx >= 0) {
        HandleResizeDrag(win, handleIdx, pt);
        return;  // selection unchanged; no re-parent after resize
    }

    Frame* hitFrame = nullptr;
    Shape* hitShape = nullptr;
    bool   found    = false;

    // ---- 2. Name label (frames only — labels are visible on canvas) ----
    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend() && !found; ++it) {
        Frame* lf = HitTestFrameLabel(it->get(), pt);
        if (lf) { hitFrame = lf; found = true; }
    }

    // ---- 3. Regular body hit-test ----
    if (!found) {
        for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend() && !found; ++it) {
            HitResult res = HitTestFrame(it->get(), pt);
            if (res.found) { hitFrame = res.frame; hitShape = res.shape; found = true; }
        }
    }
    if (!found) {
        for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it) {
            Rect r = CanvasRect((*it)->bounds);
            if (PtInRect(pt, &r)) { hitShape = it->get(); hitFrame = nullptr; found = true; break; }
        }
    }

    gSelectedFrame = hitFrame;
    gSelectedShape = hitShape;

    // ---- Double-click: rename the hit object ----
    if (found && gIsDoubleClick) {
        // Determine which object's name to edit.
        // Priority: shape label > frame label > any body hit
        std::string* targetName = nullptr;

        // Check shape labels across all frames first
        for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend() && !targetName; ++it) {
            Shape* sl = HitTestShapeLabel(it->get(), pt);
            if (sl) { targetName = &sl->name; }
        }
        // Check frame labels
        if (!targetName) {
            for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend() && !targetName; ++it) {
                Frame* fl = HitTestFrameLabel(it->get(), pt);
                if (fl) { targetName = &fl->name; }
            }
        }
        // Fall back to whatever body was hit
        if (!targetName) {
            if (gSelectedShape) targetName = &gSelectedShape->name;
            else if (gSelectedFrame) targetName = &gSelectedFrame->name;
        }

        if (targetName) {
            Point globalPt = pt;
            LocalToGlobal(&globalPt);
            std::string newName = ShowRenameDialog(*targetName, globalPt);
            if (!newName.empty()) { PushUndo(); *targetName = newName; }
            Rect pr; GetWindowPortBounds(win, &pr); InvalWindowRect(win, &pr);
            RefreshLayersPanel();
            RefreshInspector();
        }
        gIsDoubleClick = false;
        return;
    }
    gIsDoubleClick = false;

    if (found) {
        Frame* origParent = hitFrame;
        Point prevPt = pt, currPt = pt;
        bool pushedUndo = false;

        while (Button()) {
            GetMouse(&currPt);
            if (currPt.h != prevPt.h || currPt.v != prevPt.v) {
                if (!pushedUndo) { PushUndo(); pushedUndo = true; }
                // Convert screen pixel delta → canvas pixel delta
                SInt32 dx = SInt32(currPt.h - prevPt.h) * 100 / gCanvasZoom;
                SInt32 dy = SInt32(currPt.v - prevPt.v) * 100 / gCanvasZoom;

                if (hitShape) {
                    hitShape->bounds.x += dx;
                    hitShape->bounds.y += dy;
                } else {
                    MoveFrameTree(hitFrame, dx, dy);
                }

                DrawWindowContent(win);
                prevPt = currPt;
            }
        }

        // ---- Re-parent on drop ----
        // Center in screen space (DeepestFrameAt uses CanvasRect = screen space rects)
        Bounds2 finalB = hitShape ? hitShape->bounds : hitFrame->bounds;
        Point center;
        center.h = static_cast<short>(SInt32(finalB.x + finalB.w / 2) * gCanvasZoom / 100 + gCanvasOffsetX);
        center.v = static_cast<short>(SInt32(finalB.y + finalB.h / 2) * gCanvasZoom / 100 + gCanvasOffsetY);

        if (hitShape) {
            Frame* newParent = DeepestFrameAt(center);
            if (newParent != origParent) {
                auto owned = ExtractShape(hitShape, origParent);
                if (owned) {
                    if (newParent) newParent->children.push_back(std::move(owned));
                    else           gDocument->rootShapes.push_back(std::move(owned));
                    gSelectedFrame = newParent;
                }
            }
        } else {
            Frame* newParent = DeepestFrameAt(center, hitFrame);
            if (newParent != hitFrame->parent) {
                auto owned = ExtractFrame(hitFrame);
                if (owned) {
                    Frame* raw = owned.get();
                    if (newParent) {
                        owned->parent = newParent;
                        newParent->childFrames.push_back(std::move(owned));
                    } else {
                        owned->parent = nullptr;
                        gDocument->frames.push_back(std::move(owned));
                    }
                    gSelectedFrame = raw;
                }
            }
        }
    }

    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    InvalWindowRect(win, &portRect);
}

// --------------------------------------------------------------------------
// Shape / Frame creation: rubber-band drag with active tool
// --------------------------------------------------------------------------

void HandleCanvasCreate(WindowRef win, Point startGlobal) {
    if (!gDocument) return;

    SetPortWindowPort(win);
    Point startPt = startGlobal;
    GlobalToLocal(&startPt);

    Point prevPt = startPt, currPt = startPt;

    while (Button()) {
        GetMouse(&currPt);
        if (currPt.h != prevPt.h || currPt.v != prevPt.v) {
            DrawWindowContent(win);
            Rect rb = {
                sMin(startPt.v, currPt.v), sMin(startPt.h, currPt.h),
                sMax(startPt.v, currPt.v), sMax(startPt.h, currPt.h)
            };
            if (rb.right > rb.left && rb.bottom > rb.top) {
                RGBColor blue = { 0x1177, 0x55AA, 0xFFFF };
                RGBForeColor(&blue); PenSize(1,1); FrameRect(&rb); PenNormal();
            }
            prevPt = currPt;
        }
    }

    short dw = sMax(currPt.h, startPt.h) - sMin(currPt.h, startPt.h);
    short dh = sMax(currPt.v, startPt.v) - sMin(currPt.v, startPt.v);

    if (dw >= 4 && dh >= 4) {
        PushUndo();
        // Convert rubber-band screen corners to canvas coordinates
        Point cStart = ScreenToCanvas(startPt);
        Point cEnd   = ScreenToCanvas(currPt);

        Bounds2 b;
        b.x = sMin(cStart.h, cEnd.h);
        b.y = sMin(cStart.v, cEnd.v);
        b.w = sMax(cStart.h, cEnd.h) - b.x;
        b.h = sMax(cStart.v, cEnd.v) - b.y;

        // Center in screen space for DeepestFrameAt (uses CanvasRect = screen rects)
        Point center;
        center.h = static_cast<short>((sMin(startPt.h, currPt.h) + sMax(startPt.h, currPt.h)) / 2);
        center.v = static_cast<short>((sMin(startPt.v, currPt.v) + sMax(startPt.v, currPt.v)) / 2);

        gSelectedShape = nullptr;
        gSelectedFrame = nullptr;

        if (gActiveTool == Tool::Frame) {
            auto f = std::make_unique<Frame>();
            f->name           = "Frame " + istr(gNextFrameNum++);
            f->bounds         = b;
            f->backgroundColor = { 0xFFFF, 0xFFFF, 0xFFFF };

            Frame* parent = DeepestFrameAt(center);  // nest inside containing frame if any
            Frame* raw    = f.get();
            if (parent) {
                f->parent = parent;
                parent->childFrames.push_back(std::move(f));
            } else {
                f->parent = nullptr;
                gDocument->frames.push_back(std::move(f));
            }
            gSelectedFrame = raw;

        } else {
            // Rectangle or Ellipse — place inside deepest containing frame,
            // or at canvas root if drawn on bare canvas.
            Frame* target = DeepestFrameAt(center);

            std::unique_ptr<Shape> shape;
            if (gActiveTool == Tool::Rectangle) {
                auto r        = std::make_unique<RectShape>();
                r->name       = "Rectangle " + istr(gNextRectNum++);
                r->bounds     = b;
                r->fillColor  = { 0xCCCC, 0xDDDD, 0xFFFF };
                r->hasFill    = true;
                r->hasStroke  = false;
                shape = std::move(r);
            } else {
                auto e        = std::make_unique<EllipseShape>();
                e->name       = "Ellipse " + istr(gNextEllipseNum++);
                e->bounds     = b;
                e->fillColor  = { 0xCCCC, 0xFFFF, 0xEEEE };
                e->hasFill    = true;
                e->hasStroke  = false;
                shape = std::move(e);
            }

            gSelectedShape = shape.get();
            gSelectedFrame = target;

            if (target) target->children.push_back(std::move(shape));
            else        gDocument->rootShapes.push_back(std::move(shape));
        }

    }

    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    InvalWindowRect(win, &portRect);
}

// --------------------------------------------------------------------------
// Window grow
// --------------------------------------------------------------------------

void HandleWindowGrow(WindowRef win, Point where) {
    Rect c = { 300, 400, 2000, 4000 };
    long sz = GrowWindow(win, where, &c);
    if (!sz) return;
    SizeWindow(win, static_cast<SInt16>(sz & 0xFFFF),
                    static_cast<SInt16>((sz >> 16) & 0xFFFF), true);
    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    InvalWindowRect(win, &portRect);
}

// --------------------------------------------------------------------------
// Menu commands
// --------------------------------------------------------------------------

static void NewDocument() {
    delete gDocument;
    gDocument     = new Document();
    gDocument->name = "Untitled";

    auto frame           = std::make_unique<Frame>();
    frame->name          = "Screen 1";
    frame->bounds        = { 40, 40, 390, 480 };
    frame->backgroundColor = { 0xFFFF, 0xFFFF, 0xFFFF };
    gDocument->frames.push_back(std::move(frame));

    gSelectedFrame  = nullptr;
    gSelectedShape  = nullptr;
    gNextFrameNum   = 2;
    gNextRectNum    = 1;
    gNextEllipseNum = 1;
    gCanvasOffsetX  = 0;
    gCanvasOffsetY  = 0;
    gCanvasZoom     = 100;
    sUndoStack.clear();
    sRedoStack.clear();

    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
    UpdateWindowTitle();
}

// Collect every name currently used in the document (all frames + shapes, recursive)
static void CollectNamesFromFrame(const Frame* f, std::vector<std::string>& out) {
    out.push_back(f->name);
    for (const auto& s : f->children)     out.push_back(s->name);
    for (const auto& cf : f->childFrames) CollectNamesFromFrame(cf.get(), out);
}
static void CollectAllNames(std::vector<std::string>& out) {
    if (!gDocument) return;
    for (const auto& s : gDocument->rootShapes) out.push_back(s->name);
    for (const auto& f : gDocument->frames)     CollectNamesFromFrame(f.get(), out);
}

// If name ends with " <N>", return the same base with the lowest M > N
// that doesn't already exist in the document. Otherwise return name unchanged.
static std::string NextAvailableName(const std::string& name) {
    int i = static_cast<int>(name.size()) - 1;
    while (i >= 0 && name[i] >= '0' && name[i] <= '9') --i;
    if (i < 0 || i >= static_cast<int>(name.size()) - 1 || name[i] != ' ')
        return name;  // no trailing " N" — keep as-is

    std::string base = name.substr(0, static_cast<size_t>(i + 1)); // e.g. "Frame "
    int num = 0;
    for (int j = i + 1; j < static_cast<int>(name.size()); ++j)
        num = num * 10 + (name[j] - '0');

    std::vector<std::string> existing;
    CollectAllNames(existing);

    // If the original name is now free (e.g. original was deleted), reuse it
    bool originalTaken = false;
    for (const auto& n : existing) { if (n == name) { originalTaken = true; break; } }
    if (!originalTaken) return name;

    // Otherwise find the lowest available number above the current one
    int candidate = num + 1;
    while (candidate < 10000) {
        std::string cn = base + istr(candidate);
        bool taken = false;
        for (const auto& n : existing) { if (n == cn) { taken = true; break; } }
        if (!taken) return cn;
        ++candidate;
    }
    return base + istr(candidate);
}

// Deep-copy a frame tree (children have absolute canvas coords, so no coord fixup needed)
static std::unique_ptr<Frame> CloneFrame(const Frame* src, Frame* newParent) {
    auto f = std::make_unique<Frame>();
    f->name            = src->name;
    f->bounds          = src->bounds;
    f->backgroundColor = src->backgroundColor;
    f->visible         = src->visible;
    f->clipContent     = src->clipContent;
    f->parent          = newParent;
    for (const auto& s : src->children)
        f->children.push_back(s->Clone());
    for (const auto& cf : src->childFrames)
        f->childFrames.push_back(CloneFrame(cf.get(), f.get()));
    return f;
}

static std::unique_ptr<Document> CloneDocument(const Document* src) {
    auto d = std::make_unique<Document>();
    d->name = src->name;
    for (const auto& s : src->rootShapes)
        d->rootShapes.push_back(s->Clone());
    for (const auto& f : src->frames)
        d->frames.push_back(CloneFrame(f.get(), nullptr));
    return d;
}

void PushUndo() {
    if (!gDocument) return;
    sRedoStack.clear();
    sUndoStack.push_back(CloneDocument(gDocument));
    if (static_cast<int>(sUndoStack.size()) > kMaxUndo)
        sUndoStack.erase(sUndoStack.begin());
}

void PerformUndo() {
    if (sUndoStack.empty()) return;
    sRedoStack.push_back(CloneDocument(gDocument));
    delete gDocument;
    gDocument = sUndoStack.back().release();
    sUndoStack.pop_back();
    gSelectedFrame = nullptr;
    gSelectedShape = nullptr;
    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
    RefreshLayersPanel();
    RefreshInspector();
}

void PerformRedo() {
    if (sRedoStack.empty()) return;
    sUndoStack.push_back(CloneDocument(gDocument));
    delete gDocument;
    gDocument = sRedoStack.back().release();
    sRedoStack.pop_back();
    gSelectedFrame = nullptr;
    gSelectedShape = nullptr;
    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
    RefreshLayersPanel();
    RefreshInspector();
}

void CopySelected() {
    sClipFrame.reset();
    sClipShape.reset();
    sPasteOffset = 0;
    if (gSelectedShape)
        sClipShape = gSelectedShape->Clone();
    else if (gSelectedFrame)
        sClipFrame = CloneFrame(gSelectedFrame, nullptr);
}

void PasteClipboard() {
    if (!gDocument) return;
    if (!sClipShape && !sClipFrame) return;

    PushUndo();
    ++sPasteOffset;
    SInt32 off = SInt32(sPasteOffset) * 10;

    if (sClipShape) {
        auto copy = sClipShape->Clone();
        copy->name     = NextAvailableName(copy->name);
        copy->bounds.x = sClipShape->bounds.x + off;
        copy->bounds.y = sClipShape->bounds.y + off;
        Shape* raw = copy.get();
        if (gSelectedFrame) gSelectedFrame->children.push_back(std::move(copy));
        else                gDocument->rootShapes.push_back(std::move(copy));
        gSelectedShape = raw;
    } else {
        auto copy = CloneFrame(sClipFrame.get(), nullptr);
        copy->name = NextAvailableName(copy->name);
        MoveFrameTree(copy.get(), off, off);  // shifts frame + all children
        Frame* raw = copy.get();
        gDocument->frames.push_back(std::move(copy));
        gSelectedFrame = raw;
        gSelectedShape = nullptr;
    }

    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
    RefreshLayersPanel();
    RefreshInspector();
}

void DeleteSelected() {
    if (!gDocument) return;
    if (!gSelectedShape && !gSelectedFrame) return;
    PushUndo();
    bool changed = false;

    if (gSelectedShape) {
        // Works for both in-frame children and floating rootShapes
        auto& vec = gSelectedFrame ? gSelectedFrame->children : gDocument->rootShapes;
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it->get() == gSelectedShape) { vec.erase(it); changed = true; break; }
        }
        gSelectedShape = nullptr;
    } else if (gSelectedFrame) {
        // ExtractFrame handles both top-level and nested (child) frames
        auto owned = ExtractFrame(gSelectedFrame);
        if (owned) changed = true;
        gSelectedFrame = nullptr;
    }

    if (changed) {
        Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
        RefreshLayersPanel();
        RefreshInspector();
    }
}

void HandleMenuCommand(long menuResult) {
    short menuID   = static_cast<short>(menuResult >> 16);
    short menuItem = static_cast<short>(menuResult & 0xFFFF);

    if (menuID == kFileMenuID) {
        switch (menuItem) {
            case kFileNew:
                NewDocument();
                RefreshLayersPanel();
                RefreshInspector();
                break;
            case kFileOpen:
                if (LoadDocument(gDocument)) {
                    gSelectedFrame  = nullptr;
                    gSelectedShape  = nullptr;
                    gNextFrameNum   = static_cast<int>(gDocument->frames.size()) + 2;
                    gNextRectNum    = 1;
                    gNextEllipseNum = 1;
                    gCanvasOffsetX  = 0;
                    gCanvasOffsetY  = 0;
                    gCanvasZoom     = 100;
                    sUndoStack.clear();
                    sRedoStack.clear();
                    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
                    UpdateWindowTitle();
                    RefreshLayersPanel();
                    RefreshInspector();
                }
                break;
            case kFileSave:
                SaveDocument(gDocument);
                break;
            case kFileQuit:
                gQuitFlag = true;
                break;
        }
    } else if (menuID == kEditMenuID) {
        switch (menuItem) {
            case kEditUndo:  PerformUndo();    break;
            case kEditRedo:  PerformRedo();    break;
            case kEditCopy:  CopySelected();   break;
            case kEditPaste: PasteClipboard(); break;
        }
    } else if (menuID == kViewMenuID) {
        switch (menuItem) {
            case kViewZoomIn:  StepZoom(+1); break;
            case kViewZoomOut: StepZoom(-1); break;
            case kViewZoomFit: ZoomToFit();  break;
            case kViewZoom100: ZoomTo(100);  break;
        }
    }
    HiliteMenu(0);
}
