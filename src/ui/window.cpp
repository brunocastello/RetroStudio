#include "window.h"

WindowRef  gMainWindow    = nullptr;
Boolean    gQuitFlag      = false;
Tool       gActiveTool    = Tool::Select;
Renderer*  gRenderer      = nullptr;
Document*  gDocument      = nullptr;
Frame*     gSelectedFrame = nullptr;
Shape*     gSelectedShape = nullptr;

static const short kZoomDocProc = 8;

static const short kFileMenuID = 129;
static const short kEditMenuID = 130;

static const short kFileNew  = 1;
static const short kFileOpen = 2;
static const short kFileQuit = 4;

// --------------------------------------------------------------------------
// Helpers
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
    Rect bounds = { 50, 60, 580, 960 };
    gMainWindow = NewCWindow(
        nullptr,
        &bounds,
        "\pRetroStudio",
        true,
        kZoomDocProc,
        (WindowRef)-1L,
        true,
        0
    );

    gDocument = new Document();
    gDocument->name = "Untitled";

    auto frame      = std::make_unique<Frame>();
    frame->name     = "Screen 1";
    frame->bounds   = { 40, 40, 390, 480 };
    RGBColor white  = { 0xFFFF, 0xFFFF, 0xFFFF };
    frame->backgroundColor = white;
    gDocument->frames.push_back(std::move(frame));
}

// --------------------------------------------------------------------------
// Canvas rendering — direct QuickDraw to window port (no GWorld)
// --------------------------------------------------------------------------

static void DrawShape(const Shape& shape) {
    if (!shape.visible) return;
    Rect r = ToMacRect(shape.bounds);

    switch (shape.GetType()) {
        case Shape::kRectangle:
        case Shape::kLine: {
            if (shape.hasFill) {
                RGBColor c = shape.fillColor;
                RGBForeColor(&c);
                PaintRect(&r);
            }
            if (shape.hasStroke) {
                RGBColor c = shape.strokeColor;
                RGBForeColor(&c);
                PenSize(shape.strokeWidth, shape.strokeWidth);
                FrameRect(&r);
                PenSize(1, 1);
            }
            break;
        }
        case Shape::kEllipse: {
            if (shape.hasFill) {
                RGBColor c = shape.fillColor;
                RGBForeColor(&c);
                PaintOval(&r);
            }
            if (shape.hasStroke) {
                RGBColor c = shape.strokeColor;
                RGBForeColor(&c);
                PenSize(shape.strokeWidth, shape.strokeWidth);
                FrameOval(&r);
                PenSize(1, 1);
            }
            break;
        }
        default:
            break;
    }
}

static void DrawFrame(const Frame& frame) {
    if (!frame.visible) return;
    Rect r = ToMacRect(frame.bounds);

    // Frame fill — no drop shadow (Figma frames are flat)
    RGBColor bg = frame.backgroundColor;
    RGBForeColor(&bg);
    PaintRect(&r);

    // Children rendered on top of fill
    for (const auto& shape : frame.children)
        DrawShape(*shape);

    // Thin border so frame boundaries are visible on the gray canvas
    RGBColor border = { 0xBBBB, 0xBBBB, 0xBBBB };
    RGBForeColor(&border);
    FrameRect(&r);

    // Name label above top-left corner (Figma-style)
    RGBColor label = { 0x4444, 0x4444, 0x4444 };
    RGBForeColor(&label);
    TextSize(10);
    Str255 pname; pname[0] = 0;
    const char* s = frame.name.c_str();
    for (int i = 0; s[i] && i < 63; ++i) {
        pname[i + 1] = static_cast<unsigned char>(s[i]);
        pname[0]++;
    }
    MoveTo(r.left, static_cast<short>(r.top - 5));
    DrawString(pname);
    TextSize(12);
}

// Figma-style selection highlight: 2 px blue border + 8 handles
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

    // 8 resize handles (corners + edge midpoints)
    static const short kHW = 4;
    short cx = static_cast<short>((r.left + r.right)  / 2);
    short cy = static_cast<short>((r.top  + r.bottom) / 2);
    const short hx[8] = { r.left, cx, r.right, r.right,  r.right,  cx,     r.left,  r.left };
    const short hy[8] = { r.top,  r.top, r.top, cy,       r.bottom, r.bottom, r.bottom, cy   };

    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    for (int i = 0; i < 8; ++i) {
        Rect h = {
            static_cast<short>(hy[i] - kHW),
            static_cast<short>(hx[i] - kHW),
            static_cast<short>(hy[i] + kHW),
            static_cast<short>(hx[i] + kHW)
        };
        RGBForeColor(&white);
        PaintRect(&h);
        RGBForeColor(&selBlue);
        FrameRect(&h);
    }

    PenNormal();
}

void DrawWindowContent(WindowRef win) {
    SetPortWindowPort(win);

    Rect portRect;
    GetWindowPortBounds(win, &portRect);

    // Gray canvas workspace
    RGBColor canvasBg = { 0xDDDD, 0xDDDD, 0xDDDD };
    RGBBackColor(&canvasBg);
    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);
    EraseRect(&portRect);

    if (gDocument) {
        for (const auto& frame : gDocument->frames)
            DrawFrame(*frame);
    }

    DrawSelectionHighlight();

    // Restore default QuickDraw state
    PenNormal();
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBForeColor(&black);
    RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Select tool: click to select, drag to move
// --------------------------------------------------------------------------

void HandleCanvasSelect(WindowRef win, Point startGlobal) {
    if (!gDocument) return;

    SetPortWindowPort(win);
    Point pt = startGlobal;
    GlobalToLocal(&pt);

    // Hit-test in reverse draw order so topmost object wins
    Frame* hitFrame = nullptr;
    Shape* hitShape = nullptr;
    bool   found    = false;

    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend() && !found; ++it) {
        Frame* frame = it->get();

        // Check shapes within frame (reverse = topmost drawn first)
        for (auto sit = frame->children.rbegin(); sit != frame->children.rend(); ++sit) {
            Rect r = ToMacRect((*sit)->bounds);
            if (PtInRect(pt, &r)) {
                hitShape = sit->get();
                hitFrame = frame;
                found    = true;
                break;
            }
        }

        if (!found) {
            Rect r = ToMacRect(frame->bounds);
            if (PtInRect(pt, &r)) {
                hitFrame = frame;
                found    = true;
            }
        }
    }

    gSelectedFrame = hitFrame;
    gSelectedShape = hitShape;

    // Move-drag if something was hit
    if (found) {
        Point prevPt = pt;
        Point currPt = pt;

        while (Button()) {
            GetMouse(&currPt);
            if (currPt.h != prevPt.h || currPt.v != prevPt.v) {
                short dx = currPt.h - prevPt.h;
                short dy = currPt.v - prevPt.v;

                Bounds2& b = hitShape ? hitShape->bounds : hitFrame->bounds;
                b.x += dx;
                b.y += dy;

                DrawWindowContent(win);  // also draws selection highlight
                prevPt = currPt;
            }
        }
    }

    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    InvalWindowRect(win, &portRect);
}

// --------------------------------------------------------------------------
// Shape/Frame creation: rubber-band drag with active tool
// --------------------------------------------------------------------------

// Return the frame whose bounds contain point pt (last / topmost wins).
// Returns null if pt is not inside any frame.
static Frame* FrameAtPoint(Point pt) {
    Frame* result = nullptr;
    for (const auto& frame : gDocument->frames) {
        Rect r = ToMacRect(frame->bounds);
        if (PtInRect(pt, &r))
            result = frame.get();
    }
    return result;
}

void HandleCanvasCreate(WindowRef win, Point startGlobal) {
    if (!gDocument) return;

    SetPortWindowPort(win);

    Point startPt = startGlobal;
    GlobalToLocal(&startPt);

    Point prevPt = startPt;
    Point currPt = startPt;

    while (Button()) {
        GetMouse(&currPt);

        if (currPt.h != prevPt.h || currPt.v != prevPt.v) {
            DrawWindowContent(win);

            Rect rb;
            rb.top    = sMin(startPt.v, currPt.v);
            rb.left   = sMin(startPt.h, currPt.h);
            rb.bottom = sMax(startPt.v, currPt.v);
            rb.right  = sMax(startPt.h, currPt.h);

            if (rb.right > rb.left && rb.bottom > rb.top) {
                RGBColor blue = { 0x1177, 0x55AA, 0xFFFF };
                RGBForeColor(&blue);
                PenSize(1, 1);
                FrameRect(&rb);
                PenNormal();
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
        b.w = dw;
        b.h = dh;

        // Point at center of drawn rect — used to find parent frame
        Point centerPt;
        centerPt.h = static_cast<short>(b.x + b.w / 2);
        centerPt.v = static_cast<short>(b.y + b.h / 2);

        gSelectedShape = nullptr;
        gSelectedFrame = nullptr;

        if (gActiveTool == Tool::Frame) {
            static int sFrameN = 2;
            auto f = std::make_unique<Frame>();
            f->name   = "Frame " + istr(sFrameN++);
            f->bounds = b;
            RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
            f->backgroundColor = white;
            gSelectedFrame = f.get();
            gDocument->frames.push_back(std::move(f));

        } else if (gActiveTool == Tool::Rectangle) {
            // Add to the frame that contains the center of the drawn rect
            Frame* target = FrameAtPoint(centerPt);
            if (!target && !gDocument->frames.empty())
                target = gDocument->frames.front().get();
            if (target) {
                auto shape      = std::make_unique<RectShape>();
                shape->bounds   = b;
                RGBColor fc     = { 0xCCCC, 0xDDDD, 0xFFFF };
                shape->fillColor  = fc;
                shape->hasFill    = true;
                shape->hasStroke  = false;
                gSelectedShape  = shape.get();
                gSelectedFrame  = target;
                target->children.push_back(std::move(shape));
            }

        } else if (gActiveTool == Tool::Ellipse) {
            Frame* target = FrameAtPoint(centerPt);
            if (!target && !gDocument->frames.empty())
                target = gDocument->frames.front().get();
            if (target) {
                auto shape      = std::make_unique<EllipseShape>();
                shape->bounds   = b;
                RGBColor fc     = { 0xCCCC, 0xFFFF, 0xEEEE };
                shape->fillColor  = fc;
                shape->hasFill    = true;
                shape->hasStroke  = false;
                gSelectedShape  = shape.get();
                gSelectedFrame  = target;
                target->children.push_back(std::move(shape));
            }
        }

        // Auto-switch to Select so the user can immediately move what they drew
        gActiveTool = Tool::Select;
    }

    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    InvalWindowRect(win, &portRect);
}

// --------------------------------------------------------------------------
// Window grow
// --------------------------------------------------------------------------

void HandleWindowGrow(WindowRef win, Point where) {
    Rect sizeConstraints = { 300, 400, 2000, 4000 };
    long newSize = GrowWindow(win, where, &sizeConstraints);
    if (newSize == 0) return;

    SInt16 newW = static_cast<SInt16>(newSize & 0xFFFF);
    SInt16 newH = static_cast<SInt16>((newSize >> 16) & 0xFFFF);
    SizeWindow(win, newW, newH, true);

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

    switch (menuID) {
        case kFileMenuID:
            switch (menuItem) {
                case kFileQuit:
                    gQuitFlag = true;
                    break;
            }
            break;
    }

    HiliteMenu(0);
}
