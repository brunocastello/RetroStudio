#include "window.h"

WindowRef  gMainWindow = nullptr;
Boolean    gQuitFlag   = false;
Tool       gActiveTool = Tool::Select;
Renderer*  gRenderer   = nullptr;   // reserved for future GWorld optimisation
Document*  gDocument   = nullptr;

static const short kZoomDocProc = 8;

static const short kFileMenuID = 129;
static const short kEditMenuID = 130;

static const short kFileNew  = 1;
static const short kFileOpen = 2;
// item 3 = separator
static const short kFileQuit = 4;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static inline short sMin(short a, short b) { return a < b ? a : b; }
static inline short sMax(short a, short b) { return a > b ? a : b; }

// int → std::string without relying on sprintf or std::to_string
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
// Direct-to-window canvas rendering (no offscreen GWorld)
// Bypasses all GWorldPtr / CGrafPtr typedef quirks in Retro68 CarbonLib.
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

    // Drop shadow
    RGBColor shadow = { 0x6666, 0x6666, 0x6666 };
    RGBForeColor(&shadow);
    Rect shadowR = r;
    OffsetRect(&shadowR, 4, 4);
    PaintRect(&shadowR);

    // Frame fill
    RGBColor bg = frame.backgroundColor;
    RGBForeColor(&bg);
    PaintRect(&r);

    // Children
    for (const auto& shape : frame.children)
        DrawShape(*shape);

    // Border (drawn after children so it sits on top)
    RGBColor border = { 0xBBBB, 0xBBBB, 0xBBBB };
    RGBForeColor(&border);
    FrameRect(&r);

    // Frame name label — Figma-style, above top-left corner
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

void DrawWindowContent(WindowRef win) {
    SetPortWindowPort(win);

    Rect portRect;
    GetWindowPortBounds(win, &portRect);

    // Gray canvas workspace background
    RGBColor canvasBg = { 0xDDDD, 0xDDDD, 0xDDDD };
    RGBBackColor(&canvasBg);
    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);
    EraseRect(&portRect);

    if (!gDocument) return;

    for (const auto& frame : gDocument->frames)
        DrawFrame(*frame);

    // Restore default QuickDraw state
    PenNormal();
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBForeColor(&black);
    RGBBackColor(&white);
}

// --------------------------------------------------------------------------
// Drag-to-create: rubber-band new Frame / Rectangle / Ellipse on the canvas
// --------------------------------------------------------------------------

void HandleCanvasCreate(WindowRef win, Point startGlobal) {
    SetPortWindowPort(win);

    Point startPt = startGlobal;
    GlobalToLocal(&startPt);

    Point prevPt = startPt;
    Point currPt = startPt;

    // Blocking drag loop — cooperative multitasking friendly on Mac OS 9
    // because we yield implicitly via GetMouse polling (no tight CPU loop).
    while (Button()) {
        GetMouse(&currPt);

        if (currPt.h != prevPt.h || currPt.v != prevPt.v) {
            // Redraw full canvas then paint rubber-band rect on top
            DrawWindowContent(win);

            Rect rb;
            rb.top    = sMin(startPt.v, currPt.v);
            rb.left   = sMin(startPt.h, currPt.h);
            rb.bottom = sMax(startPt.v, currPt.v);
            rb.right  = sMax(startPt.h, currPt.h);

            if (rb.right > rb.left && rb.bottom > rb.top) {
                // Blue 1px outline — indicates the shape being created
                RGBColor blue = { 0x1177, 0x55AA, 0xFFFF };
                RGBForeColor(&blue);
                PenSize(1, 1);
                FrameRect(&rb);
                PenNormal();
            }
            prevPt = currPt;
        }
    }

    // Reject micro-drags (< 4 px in either axis)
    short dw = sMax(currPt.h, startPt.h) - sMin(currPt.h, startPt.h);
    short dh = sMax(currPt.v, startPt.v) - sMin(currPt.v, startPt.v);

    if (dw >= 4 && dh >= 4) {
        Bounds2 b;
        b.x = sMin(startPt.h, currPt.h);
        b.y = sMin(startPt.v, currPt.v);
        b.w = dw;
        b.h = dh;

        if (gActiveTool == Tool::Frame) {
            static int sFrameN = 2;
            auto f = std::make_unique<Frame>();
            f->name   = "Frame " + istr(sFrameN++);
            f->bounds = b;
            RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
            f->backgroundColor = white;
            gDocument->frames.push_back(std::move(f));

        } else if (gActiveTool == Tool::Rectangle) {
            if (gDocument && !gDocument->frames.empty()) {
                auto shape  = std::make_unique<RectShape>();
                shape->bounds = b;
                // Figma-style default: light blue fill, no stroke
                RGBColor fc = { 0xCCCC, 0xDDDD, 0xFFFF };
                shape->fillColor  = fc;
                shape->hasFill    = true;
                shape->hasStroke  = false;
                gDocument->frames.front()->children.push_back(std::move(shape));
            }

        } else if (gActiveTool == Tool::Ellipse) {
            if (gDocument && !gDocument->frames.empty()) {
                auto shape  = std::make_unique<EllipseShape>();
                shape->bounds = b;
                RGBColor fc = { 0xCCCC, 0xFFFF, 0xEEEE };
                shape->fillColor  = fc;
                shape->hasFill    = true;
                shape->hasStroke  = false;
                gDocument->frames.front()->children.push_back(std::move(shape));
            }
        }
    }

    // Invalidate window to trigger a clean updateEvt redraw
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
