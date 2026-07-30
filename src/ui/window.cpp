#include "window.h"

WindowRef  gMainWindow    = nullptr;
Boolean    gQuitFlag      = false;
Tool       gActiveTool    = Tool::Select;
Renderer*  gRenderer      = nullptr;
Document*  gDocument      = nullptr;
Frame*     gSelectedFrame = nullptr;
Shape*     gSelectedShape = nullptr;

static const short kZoomDocProc = 8;
static const short kFileMenuID  = 129;
static const short kEditMenuID  = 130;
static const short kFileNew     = 1;
static const short kFileOpen    = 2;
static const short kFileQuit    = 4;

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
    AppendMenu(fileMenu, "\pQuit");
    SetItemCmd(fileMenu, kFileQuit, 'Q');
    InsertMenu(fileMenu, 0);

    MenuRef editMenu = NewMenu(kEditMenuID, "\pEdit");
    AppendMenu(editMenu, "\pUndo");
    SetItemCmd(editMenu, 1, 'Z');
    AppendMenu(editMenu, "\p-");
    AppendMenu(editMenu, "\pCut");
    SetItemCmd(editMenu, 3, 'X');
    AppendMenu(editMenu, "\pCopy");
    SetItemCmd(editMenu, 4, 'C');
    AppendMenu(editMenu, "\pPaste");
    SetItemCmd(editMenu, 5, 'V');
    InsertMenu(editMenu, 0);

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
}

// --------------------------------------------------------------------------
// Rendering — direct QuickDraw into window port
// --------------------------------------------------------------------------

static void DrawShape(const Shape& shape) {
    if (!shape.visible) return;
    Rect r = ToMacRect(shape.bounds);
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
}

// Forward-declare so DrawFrame can call itself recursively
static void DrawFrame(const Frame& frame);

static void DrawFrame(const Frame& frame) {
    if (!frame.visible) return;
    Rect r = ToMacRect(frame.bounds);

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
    Str255 pn; pn[0] = 0;
    const char* s = frame.name.c_str();
    for (int i = 0; s[i] && i < 63; ++i) { pn[i+1] = (unsigned char)s[i]; pn[0]++; }
    MoveTo(r.left, static_cast<short>(r.top - 5));
    DrawString(pn);
    TextSize(12);
}

static void DrawSelectionHighlight() {
    if (!gSelectedFrame && !gSelectedShape) return;

    Rect r = gSelectedShape
        ? ToMacRect(gSelectedShape->bounds)
        : ToMacRect(gSelectedFrame->bounds);

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

// Move a frame and ALL its descendants (shapes + nested frames) by (dx,dy)
static void MoveFrameTree(Frame* f, short dx, short dy) {
    f->bounds.x += dx;
    f->bounds.y += dy;
    for (auto& c : f->children)     { c->bounds.x += dx; c->bounds.y += dy; }
    for (auto& cf : f->childFrames) { MoveFrameTree(cf.get(), dx, dy); }
}

// Hit-test result from recursive search
struct HitResult { Frame* frame = nullptr; Shape* shape = nullptr; bool found = false; };

// Recursively search inside `f`. Returns deepest match.
static HitResult HitTestFrame(Frame* f, Point pt) {
    Rect r = ToMacRect(f->bounds);
    if (!PtInRect(pt, &r)) return {};

    // Child frames first (last added = topmost z-order)
    for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
        HitResult res = HitTestFrame(it->get(), pt);
        if (res.found) return res;
    }
    // Shapes within this frame
    for (auto it = f->children.rbegin(); it != f->children.rend(); ++it) {
        Rect sr = ToMacRect((*it)->bounds);
        if (PtInRect(pt, &sr)) return { f, it->get(), true };
    }
    return { f, nullptr, true };  // hit frame body
}

// Find the most deeply nested frame that contains `pt`, skipping `exclude`
static Frame* DeepestInFrame(Frame* f, Point pt, Frame* skip) {
    if (f == skip) return nullptr;
    Rect r = ToMacRect(f->bounds);
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
// Select tool: click-to-select + drag-to-move + drop-to-reparent
// --------------------------------------------------------------------------

void HandleCanvasSelect(WindowRef win, Point startGlobal) {
    if (!gDocument) return;

    SetPortWindowPort(win);
    Point pt = startGlobal;
    GlobalToLocal(&pt);

    Frame* hitFrame = nullptr;
    Shape* hitShape = nullptr;
    bool   found    = false;

    // Search top-level frames recursively (last = topmost z-order)
    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend() && !found; ++it) {
        HitResult res = HitTestFrame(it->get(), pt);
        if (res.found) { hitFrame = res.frame; hitShape = res.shape; found = true; }
    }
    // Search root shapes
    if (!found) {
        for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it) {
            Rect r = ToMacRect((*it)->bounds);
            if (PtInRect(pt, &r)) { hitShape = it->get(); hitFrame = nullptr; found = true; break; }
        }
    }

    gSelectedFrame = hitFrame;
    gSelectedShape = hitShape;

    if (found) {
        Frame* origParent = hitFrame;  // remember parent before drag
        Point prevPt = pt, currPt = pt;

        while (Button()) {
            GetMouse(&currPt);
            if (currPt.h != prevPt.h || currPt.v != prevPt.v) {
                short dx = currPt.h - prevPt.h;
                short dy = currPt.v - prevPt.v;

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
        Bounds2 finalB = hitShape ? hitShape->bounds : hitFrame->bounds;
        Point center;
        center.h = static_cast<short>(finalB.x + finalB.w / 2);
        center.v = static_cast<short>(finalB.y + finalB.h / 2);

        if (hitShape) {
            Frame* newParent = DeepestFrameAt(center);
            if (newParent != origParent) {
                auto owned = ExtractShape(hitShape, origParent);
                if (owned) {
                    if (newParent) newParent->children.push_back(std::move(owned));
                    else           gDocument->rootShapes.push_back(std::move(owned));
                    gSelectedFrame = newParent;
                    // gSelectedShape raw ptr still valid — object lives on
                }
            }
        } else {
            // Moving a frame: check if it should become a child of another frame
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
        Bounds2 b;
        b.x = sMin(startPt.h, currPt.h);
        b.y = sMin(startPt.v, currPt.v);
        b.w = dw; b.h = dh;

        Point center;
        center.h = static_cast<short>(b.x + b.w / 2);
        center.v = static_cast<short>(b.y + b.h / 2);

        gSelectedShape = nullptr;
        gSelectedFrame = nullptr;

        if (gActiveTool == Tool::Frame) {
            static int sFrameN = 2;
            auto f = std::make_unique<Frame>();
            f->name           = "Frame " + istr(sFrameN++);
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
                r->name       = "Rectangle";
                r->bounds     = b;
                r->fillColor  = { 0xCCCC, 0xDDDD, 0xFFFF };
                r->hasFill    = true;
                r->hasStroke  = false;
                shape = std::move(r);
            } else {
                auto e        = std::make_unique<EllipseShape>();
                e->name       = "Ellipse";
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

        gActiveTool = Tool::Select;  // auto-switch so object is immediately moveable
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

void HandleMenuCommand(long menuResult) {
    short menuID   = static_cast<short>(menuResult >> 16);
    short menuItem = static_cast<short>(menuResult & 0xFFFF);
    if (menuID == kFileMenuID && menuItem == kFileQuit) gQuitFlag = true;
    HiliteMenu(0);
}
