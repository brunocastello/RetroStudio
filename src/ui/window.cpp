#include "window.h"
#include "LayersPanel.h"
#include "InspectorPanel.h"
#include "RenameDialog.h"
#include "../export/DocumentSerializer.h"
#include "../canvas/AutoLayout.h"
#include <algorithm>
#include <cstring>
#include <cmath>

WindowRef  gMainWindow    = nullptr;
WindowRef  gAboutWindow   = nullptr;
Boolean    gQuitFlag      = false;
Tool       gActiveTool    = Tool::Select;
Renderer*  gRenderer      = nullptr;
Document*  gDocument      = nullptr;
Frame*     gSelectedFrame = nullptr;
Shape*     gSelectedShape = nullptr;
std::vector<Shape*> gSelectedShapes;
std::vector<Frame*> gSelectedFrames;
Shape*              gLayoutDragShape   = nullptr;
Frame*              gLayoutDragFrame   = nullptr;
bool                gIsLayoutMultiDrag = false;
int        gNextFrameNum  = 2;
bool       gIsDoubleClick = false;
SInt32     gCanvasOffsetX = 0;
SInt32     gCanvasOffsetY = 0;
int        gCanvasZoom    = 100;
int        gNextRectNum    = 1;
int        gNextEllipseNum = 1;
int        gNextTextNum    = 1;

// In-memory clipboard
static std::vector<std::unique_ptr<Frame>> sClipFrames;
static std::vector<std::unique_ptr<Shape>> sClipShapes;
static int                                 sPasteOffset = 0;  // increments per paste, resets on copy
static Frame*                              sPasteParent = nullptr; // nullptr = root level

// Undo / redo stacks — each entry is a full document snapshot
static const int kMaxUndo = 50;
static std::vector<std::unique_ptr<Document>> sUndoStack;
static std::vector<std::unique_ptr<Document>> sRedoStack;

// Per-window document context — stores state for inactive windows
struct DocCtx {
    Document*    doc          = nullptr;
    WindowRef    win          = nullptr;
    Frame*       selFrame     = nullptr;
    Shape*       selShape     = nullptr;
    std::vector<Shape*> selShapes;
    std::vector<Frame*> selFrames;
    int nextFrameNum   = 2;
    int nextRectNum    = 1;
    int nextEllipseNum = 1;
    int nextTextNum    = 1;
    SInt32 offsetX = 0, offsetY = 0;
    int zoom = 100;
    std::vector<std::unique_ptr<Document>> undoStack;
    std::vector<std::unique_ptr<Document>> redoStack;
};
static std::vector<std::unique_ptr<DocCtx>> sDocWindows;

static const short kZoomDocProc    = 8;
static const short kNoGrowDocProc  = 4;  // title bar + close box, no grow/zoom
static const short kFileMenuID  = 129;
static const short kEditMenuID  = 130;
static const short kViewMenuID  = 131;
static const short kFileNew     = 1;
static const short kFileOpen    = 2;
static const short kFileClose   = 3;
static const short kFileSave    = 5;
static const short kFileQuit    = 7;
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
static const short kAppleMenuID = 1;   // System Apple menu (must be ID=1)
static const short kAppleAbout  = 1;   // "About RetroStudio" item

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
    if (!gMainWindow || !gDocument) return;
    std::string title = gDocument->name + " " + istr(gCanvasZoom) + "%";
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
// Multi-document helpers
// --------------------------------------------------------------------------

static void SaveGlobalsToCtx(DocCtx& ctx) {
    ctx.doc           = gDocument;
    ctx.selFrame      = gSelectedFrame;
    ctx.selShape      = gSelectedShape;
    ctx.selShapes     = gSelectedShapes;
    ctx.selFrames     = gSelectedFrames;
    ctx.nextFrameNum   = gNextFrameNum;
    ctx.nextRectNum    = gNextRectNum;
    ctx.nextEllipseNum = gNextEllipseNum;
    ctx.nextTextNum    = gNextTextNum;
    ctx.offsetX        = gCanvasOffsetX;
    ctx.offsetY        = gCanvasOffsetY;
    ctx.zoom           = gCanvasZoom;
    ctx.undoStack      = std::move(sUndoStack);
    ctx.redoStack      = std::move(sRedoStack);
}

static void LoadGlobalsFromCtx(DocCtx& ctx) {
    gDocument        = ctx.doc;
    gMainWindow      = ctx.win;
    gSelectedFrame   = ctx.selFrame;
    gSelectedShape   = ctx.selShape;
    gSelectedShapes  = ctx.selShapes;
    gSelectedFrames  = ctx.selFrames;
    gNextFrameNum    = ctx.nextFrameNum;
    gNextRectNum     = ctx.nextRectNum;
    gNextEllipseNum  = ctx.nextEllipseNum;
    gNextTextNum     = ctx.nextTextNum;
    gCanvasOffsetX   = ctx.offsetX;
    gCanvasOffsetY   = ctx.offsetY;
    gCanvasZoom      = ctx.zoom;
    sUndoStack       = std::move(ctx.undoStack);
    sRedoStack       = std::move(ctx.redoStack);
}

static WindowRef CreateDocumentWindow(Document* doc) {
    static short sWinOff = 0;
    short off = static_cast<short>((sWinOff % 8) * 22);
    sWinOff++;
    Rect bounds = { static_cast<short>(50 + off), static_cast<short>(80 + off),
                    static_cast<short>(580 + off), static_cast<short>(720 + off) };
    Str255 title; ToPStr(doc->name, title);
    return NewCWindow(nullptr, &bounds, title, true, kZoomDocProc, (WindowRef)-1L, true, 0);
}

// About window — non-modal document window, SimpleText-style.
// Created on demand; clicking close box disposes it (handled in main.cpp).
static void ShowAboutDialog() {
    if (gAboutWindow) { SelectWindow(gAboutWindow); return; }
    Rect bounds = { 130, 100, 340, 420 };   // 210 h × 320 w
    gAboutWindow = NewCWindow(nullptr, &bounds, "\pAbout RetroStudio",
                              true, kNoGrowDocProc, (WindowRef)-1L, true, 0L);
}

void DrawAboutWindow() {
    if (!gAboutWindow) return;
    SetPortWindowPort(gAboutWindow);
    Rect portRect;
    GetWindowPortBounds(gAboutWindow, &portRect);
    EraseRect(&portRect);

    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);

    short cx = static_cast<short>((portRect.left + portRect.right) / 2);

    // Helper: center a C string at vertical position y, with given size/face.
    // TextWidth/DrawText take non-const Ptr; the strings are read-only in practice.
    auto drawC = [&](short y, short sz, short face, const char* s) {
        short len = 0; while (s[len]) ++len;
        char* p = const_cast<char*>(s);
        TextFont(0); TextSize(sz); TextFace(face);
        short w = TextWidth(p, 0, len);
        MoveTo(static_cast<short>(cx - w / 2), y);
        DrawText(p, 0, len);
    };

    drawC(48,  24, bold,   "RetroStudio");
    drawC(70,  12, normal, "Version 1.0");
    drawC(106, 12, normal, "A vector design & prototyping tool");
    drawC(124, 12, normal, "for Classic Mac OS 9");
    drawC(158, 12, normal, "Bruno Castello");
    drawC(190, 10, normal, "\xA9 2026 Bruno Castello. All rights reserved.");
}

// Enable/disable menu items based on whether any document is open.
// Call after any transition that changes gDocument.
static void UpdateMenuState() {
    bool has = (gDocument != nullptr);
    MenuRef fm = GetMenuHandle(kFileMenuID);
    if (fm) {
        if (has) { EnableMenuItem(fm, kFileClose); EnableMenuItem(fm, kFileSave); }
        else     { DisableMenuItem(fm, kFileClose); DisableMenuItem(fm, kFileSave); }
    }
    MenuRef em = GetMenuHandle(kEditMenuID);
    if (em) { if (has) EnableMenuItem(em, 0); else DisableMenuItem(em, 0); }
    MenuRef vm = GetMenuHandle(kViewMenuID);
    if (vm) { if (has) EnableMenuItem(vm, 0); else DisableMenuItem(vm, 0); }
    DrawMenuBar();
}

// DLOG 129 = Save-confirmation dialog defined in RetroStudio.r
// Returns: 0 = Save, 1 = Don't Save, 2 = Cancel
static int ShowConfirmCloseDialog() {
    DialogPtr dlg = GetNewDialog(129, nullptr, (WindowPtr)-1L);
    if (!dlg) return 2;
    short item = 0;
    while (item < 1 || item > 3)
        ModalDialog(nullptr, &item);
    DisposeDialog(dlg);
    if (item == 1) return 0; // Save
    if (item == 2) return 1; // Don't Save
    return 2;                // Cancel
}

void SwitchActiveDocument(WindowRef win) {
    if (win == gMainWindow) return;
    for (auto& ctx : sDocWindows)
        if (ctx->win == gMainWindow) { SaveGlobalsToCtx(*ctx); break; }
    for (auto& ctx : sDocWindows)
        if (ctx->win == win) { LoadGlobalsFromCtx(*ctx); break; }
    SelectWindow(gMainWindow);
    UpdateWindowTitle();
    RefreshLayersPanel();
    RefreshInspector();
    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
}

bool IsDocumentCanvas(WindowRef win) {
    for (const auto& ctx : sDocWindows)
        if (ctx->win == win) return true;
    return false;
}

void CloseDocumentWindow(WindowRef win) {
    DocCtx* closingCtx = nullptr;
    for (auto& ctx : sDocWindows)
        if (ctx->win == win) { closingCtx = ctx.get(); break; }
    if (!closingCtx) return;

    bool wasActive = (win == gMainWindow);
    DocCtx* prevCtx = nullptr;
    if (!wasActive) {
        for (auto& ctx : sDocWindows)
            if (ctx->win == gMainWindow) { prevCtx = ctx.get(); SaveGlobalsToCtx(*prevCtx); break; }
        LoadGlobalsFromCtx(*closingCtx);
    }

    if (!sUndoStack.empty()) {
        int conf = ShowConfirmCloseDialog(); // 0=Save, 1=Don't Save, 2=Cancel
        if (conf == 2) {
            if (!wasActive && prevCtx) {
                SaveGlobalsToCtx(*closingCtx);
                LoadGlobalsFromCtx(*prevCtx);
                SelectWindow(gMainWindow);
                UpdateWindowTitle();
                Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
            }
            return;
        }
        if (conf == 0)
            SaveDocument(gDocument);
    }

    Document* docToDelete = nullptr;
    WindowRef winToDispose = win;
    for (auto it = sDocWindows.begin(); it != sDocWindows.end(); ++it) {
        if ((*it)->win == win) {
            docToDelete = (*it)->doc;
            sDocWindows.erase(it);
            break;
        }
    }
    delete docToDelete;
    DisposeWindow(winToDispose);

    if (sDocWindows.empty()) {
        // Last document closed — keep running, show panels in disabled state.
        // File > Quit (or Apple Event) is the only path that sets gQuitFlag.
        gDocument = nullptr; gMainWindow = nullptr;
        gSelectedFrame = nullptr; gSelectedShape = nullptr;
        gSelectedShapes.clear(); gSelectedFrames.clear();
        sUndoStack.clear(); sRedoStack.clear();
        RefreshLayersPanel();
        RefreshInspector();
        UpdateMenuState();
    } else {
        DocCtx* nextCtx = (!wasActive && prevCtx) ? prevCtx : sDocWindows.front().get();
        LoadGlobalsFromCtx(*nextCtx);
        SelectWindow(gMainWindow);
        UpdateWindowTitle();
        RefreshLayersPanel();
        RefreshInspector();
        Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
    }
}

// --------------------------------------------------------------------------
// Menus
// --------------------------------------------------------------------------

void SetupMenus() {
    MenuRef appleMenu = NewMenu(kAppleMenuID, "\p\024"); // 0x14 = Apple logo in MacRoman
    AppendMenu(appleMenu, "\pAbout RetroStudio\311");    // \311 = ellipsis (…)
    InsertMenu(appleMenu, 0);

    MenuRef fileMenu = NewMenu(kFileMenuID, "\pFile");
    AppendMenu(fileMenu, "\pNew");
    SetItemCmd(fileMenu, kFileNew, 'N');
    AppendMenu(fileMenu, "\pOpen...");
    SetItemCmd(fileMenu, kFileOpen, 'O');
    AppendMenu(fileMenu, "\pClose");
    SetItemCmd(fileMenu, kFileClose, 'W');
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

// NavLoad/NavUnload are in CarbonLib but may be absent from Multiversal Navigation.h
extern "C" OSErr NavLoad();

// Apple Event handlers — installed during init so tools like A-Dock can quit us
static pascal OSErr AEHandleOpenApp(const AppleEvent*, AppleEvent*, long) { return noErr; }
static pascal OSErr AEHandleQuit   (const AppleEvent*, AppleEvent*, long) { gQuitFlag = true; return noErr; }

void SetupWindow() {
    auto* doc = new Document();
    doc->name = "Untitled";
    auto frame = std::make_unique<Frame>();
    frame->name = "Frame 1";
    frame->bounds = { 40, 40, 390, 480 };
    frame->backgroundColor = { 0xFFFF, 0xFFFF, 0xFFFF };
    doc->frames.push_back(std::move(frame));
    doc->rootChildOrder.push_back({ true, 0 });

    WindowRef win = CreateDocumentWindow(doc);
    auto ctx = std::make_unique<DocCtx>();
    ctx->doc = doc;
    ctx->win = win;
    sDocWindows.push_back(std::move(ctx));
    LoadGlobalsFromCtx(*sDocWindows.back());
    UpdateWindowTitle();

    // Load Navigation Services before first dialog call (required on some Mac OS 9 configs)
    NavLoad();

    // Register Apple Event handlers so the Finder, A-Dock, etc. can quit us
    AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
                          NewAEEventHandlerUPP(AEHandleOpenApp), 0L, false);
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                          NewAEEventHandlerUPP(AEHandleQuit),    0L, false);
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

// Fill or frame a rect with four independent corner radii (already in screen pixels).
// Approach: start with full RectRgn; for each corner subtract the "waste" piece
// (cr×cr corner square minus the quarter of the inscribed oval that falls in it).
// sq = cr×cr at the actual rect corner; ov = 2cr×2cr oval bounding box.
// DiffRgn(sq, oval) = the corner square area that is NOT inside the circle → the
// waste to punch out. This is correct because the oval extends beyond sq, so only
// the quarter of the oval inside sq participates in the subtraction.
static void ApplyRoundRectCorners(const Rect& r,
                                   short tl, short tr, short br, short bl,
                                   bool doFill) {
    short x = r.left,  y = r.top;
    short w = static_cast<short>(r.right  - r.left);
    short h = static_cast<short>(r.bottom - r.top);
    short maxR = static_cast<short>((w < h ? w : h) / 2);
    if (tl > maxR) tl = maxR; if (tr > maxR) tr = maxR;
    if (br > maxR) br = maxR; if (bl > maxR) bl = maxR;
    if (tl < 0) tl = 0; if (tr < 0) tr = 0;
    if (br < 0) br = 0; if (bl < 0) bl = 0;

    RgnHandle rgn     = NewRgn();
    RgnHandle sqRgn   = NewRgn();
    RgnHandle ovalRgn = NewRgn();
    RgnHandle cutRgn  = NewRgn();

    RectRgn(rgn, &r);

    auto cutCorner = [&](Rect sq, Rect ov) {
        RectRgn(sqRgn, &sq);
        OpenRgn(); FrameOval(&ov); CloseRgn(ovalRgn);
        DiffRgn(sqRgn, ovalRgn, cutRgn);
        DiffRgn(rgn, cutRgn, rgn);
    };

    // sq = cr×cr at the rect corner; ov = 2cr×2cr arc bounding box for that corner.
    if (tl > 0) cutCorner(
        { y,                          x,                          static_cast<short>(y+tl),    static_cast<short>(x+tl)    },
        { y,                          x,                          static_cast<short>(y+2*tl),  static_cast<short>(x+2*tl)  });
    if (tr > 0) cutCorner(
        { y,                          static_cast<short>(x+w-tr), static_cast<short>(y+tr),    static_cast<short>(x+w)     },
        { y,                          static_cast<short>(x+w-2*tr), static_cast<short>(y+2*tr), static_cast<short>(x+w)   });
    if (br > 0) cutCorner(
        { static_cast<short>(y+h-br), static_cast<short>(x+w-br), static_cast<short>(y+h),    static_cast<short>(x+w)     },
        { static_cast<short>(y+h-2*br), static_cast<short>(x+w-2*br), static_cast<short>(y+h), static_cast<short>(x+w)   });
    if (bl > 0) cutCorner(
        { static_cast<short>(y+h-bl), x,                          static_cast<short>(y+h),    static_cast<short>(x+bl)    },
        { static_cast<short>(y+h-2*bl), x,                        static_cast<short>(y+h),    static_cast<short>(x+2*bl)  });

    if (doFill) PaintRgn(rgn); else FrameRgn(rgn);

    DisposeRgn(cutRgn);
    DisposeRgn(ovalRgn);
    DisposeRgn(sqRgn);
    DisposeRgn(rgn);
}

// Scale a canvas-pixel corner radius to screen pixels (clamped to 16383 so 2× fits in short).
static short ScaleCornerRadius(SInt16 cr) {
    if (cr <= 0) return 0;
    SInt32 v = SInt32(cr) * gCanvasZoom / 100;
    if (v > 16383) v = 16383;
    if (v < 1) v = 1;
    return static_cast<short>(v);
}

// Rotate 4 rect corners and draw as a filled/stroked polygon.
// angleDeg is clockwise in screen coordinates (Y-down).
static void DrawRotatedRect(const Rect& r, short angleDeg,
                             bool doFill, const RGBColor& fillC,
                             bool doStroke, const RGBColor& strokeC, short sw) {
    double cx = (r.left + r.right)  * 0.5;
    double cy = (r.top  + r.bottom) * 0.5;
    double hw = (r.right  - r.left) * 0.5;
    double hh = (r.bottom - r.top)  * 0.5;
    double rad = angleDeg * 3.14159265358979323846 / 180.0;
    double cosA = std::cos(rad), sinA = std::sin(rad);
    // 4 corners: TL TR BR BL
    double lx[4] = { -hw,  hw,  hw, -hw };
    double ly[4] = { -hh, -hh,  hh,  hh };
    Point pts[4];
    for (int i = 0; i < 4; ++i) {
        pts[i].h = static_cast<short>(cx + lx[i]*cosA - ly[i]*sinA + 0.5);
        pts[i].v = static_cast<short>(cy + lx[i]*sinA + ly[i]*cosA + 0.5);
    }
    PolyHandle poly = OpenPoly();
    MoveTo(pts[0].h, pts[0].v);
    LineTo(pts[1].h, pts[1].v); LineTo(pts[2].h, pts[2].v);
    LineTo(pts[3].h, pts[3].v); LineTo(pts[0].h, pts[0].v);
    ClosePoly();
    if (doFill)   { RGBColor c = fillC;   RGBForeColor(&c); PaintPoly(poly); }
    if (doStroke) { RGBColor c = strokeC; RGBForeColor(&c); PenSize(sw,sw); FramePoly(poly); PenSize(1,1); }
    KillPoly(poly);
}

// Approximate a rotated ellipse as a 36-gon polygon.
static void DrawRotatedEllipse(const Rect& r, short angleDeg,
                                bool doFill, const RGBColor& fillC,
                                bool doStroke, const RGBColor& strokeC, short sw) {
    double cx = (r.left + r.right)  * 0.5;
    double cy = (r.top  + r.bottom) * 0.5;
    double hw = (r.right  - r.left) * 0.5;
    double hh = (r.bottom - r.top)  * 0.5;
    double rotRad = angleDeg * 3.14159265358979323846 / 180.0;
    double cosR = std::cos(rotRad), sinR = std::sin(rotRad);
    const int N = 36;
    PolyHandle poly = OpenPoly();
    for (int i = 0; i <= N; ++i) {
        double t   = 2.0 * 3.14159265358979323846 * i / N;
        double ex  = hw * std::cos(t);
        double ey  = hh * std::sin(t);
        short ph   = static_cast<short>(cx + ex*cosR - ey*sinR + 0.5);
        short pv   = static_cast<short>(cy + ex*sinR + ey*cosR + 0.5);
        if (i == 0) MoveTo(ph, pv); else LineTo(ph, pv);
    }
    ClosePoly();
    if (doFill)   { RGBColor c = fillC;   RGBForeColor(&c); PaintPoly(poly); }
    if (doStroke) { RGBColor c = strokeC; RGBForeColor(&c); PenSize(sw,sw); FramePoly(poly); PenSize(1,1); }
    KillPoly(poly);
}

// Returns true if screen point `pt` is inside `bounds` rotated by `angleDeg` clockwise.
static bool HitTestRotated(const Bounds2& bounds, short angleDeg, Point pt) {
    Rect r = CanvasRect(bounds);
    double cx = (r.left + r.right)  * 0.5;
    double cy = (r.top  + r.bottom) * 0.5;
    double hw = (r.right  - r.left) * 0.5;
    double hh = (r.bottom - r.top)  * 0.5;
    double rad = -angleDeg * 3.14159265358979323846 / 180.0;  // inverse rotation
    double dx = pt.h - cx, dy = pt.v - cy;
    double rx = dx * std::cos(rad) - dy * std::sin(rad);
    double ry = dx * std::sin(rad) + dy * std::cos(rad);
    return (rx >= -hw && rx <= hw && ry >= -hh && ry <= hh);
}

static void DrawShape(const Shape& shape) {
    if (!shape.visible) return;
    Rect r = CanvasRect(shape.bounds);
    bool shapeOp = (shape.opacity < 100);
    if (shapeOp) {
        UInt16 w = static_cast<UInt16>((UInt32)shape.opacity * 65535 / 100);
        RGBColor oc = { w, w, w };
        OpColor(&oc); PenMode(blend);
    }
    switch (shape.GetType()) {
        case Shape::kRectangle:
        case Shape::kLine: {
            if (shape.rotation != 0) {
                // Rotated rect: polygon path, corner radius ignored
                short sw = static_cast<short>(shape.strokeWidth);
                DrawRotatedRect(r, shape.rotation,
                                shape.hasFill, shape.fillColor,
                                shape.hasStroke, shape.strokeColor, sw);
                break;
            }
            if (shape.GetType() == Shape::kRectangle &&
                static_cast<const RectShape&>(shape).cornerIndividual) {
                const auto& rs = static_cast<const RectShape&>(shape);
                short itl = ScaleCornerRadius(rs.cornerTL);
                short itr = ScaleCornerRadius(rs.cornerTR);
                short ibr = ScaleCornerRadius(rs.cornerBR);
                short ibl = ScaleCornerRadius(rs.cornerBL);
                if (shape.hasFill) {
                    RGBColor c = shape.fillColor; RGBForeColor(&c);
                    ApplyRoundRectCorners(r, itl, itr, ibr, ibl, true);
                }
                if (shape.hasStroke) {
                    RGBColor c = shape.strokeColor; RGBForeColor(&c);
                    short sw = static_cast<short>(shape.strokeWidth);
                    Rect sr = r;
                    if (shape.strokeAlign == 2) { sr.top-=sw; sr.left-=sw; sr.bottom+=sw; sr.right+=sw; }
                    else if (shape.strokeAlign == 0) { short e=sw/2; sr.top-=e; sr.left-=e; sr.bottom+=e; sr.right+=e; }
                    PenSize(sw, sw);
                    ApplyRoundRectCorners(sr, itl, itr, ibr, ibl, false);
                    PenSize(1, 1);
                }
            } else {
                SInt16 cr = (shape.GetType() == Shape::kRectangle)
                            ? static_cast<const RectShape&>(shape).cornerRadius : 0;
                SInt32 ovL = (cr > 0) ? (SInt32(cr) * 2 * gCanvasZoom / 100) : 0;
                if (ovL > 32767) ovL = 32767;
                short ov = static_cast<short>(ovL);
                if (ov < 2 && cr > 0) ov = 2;
                // Clamp oval to rect dimensions so large radii give a pill, not an ellipse.
                { short rw=static_cast<short>(r.right-r.left), rh=static_cast<short>(r.bottom-r.top);
                  short mx=static_cast<short>(rw<rh?rw:rh); if(ov>mx) ov=mx; }
                if (shape.hasFill) {
                    RGBColor c = shape.fillColor; RGBForeColor(&c);
                    if (ov > 0) PaintRoundRect(&r, ov, ov); else PaintRect(&r);
                }
                if (shape.hasStroke) {
                    RGBColor c = shape.strokeColor; RGBForeColor(&c);
                    short sw = static_cast<short>(shape.strokeWidth);
                    Rect sr = r;
                    if (shape.strokeAlign == 2) { sr.top-=sw; sr.left-=sw; sr.bottom+=sw; sr.right+=sw; }
                    else if (shape.strokeAlign == 0) { short e=sw/2; sr.top-=e; sr.left-=e; sr.bottom+=e; sr.right+=e; }
                    PenSize(sw, sw);
                    if (ov > 0) FrameRoundRect(&sr, ov, ov); else FrameRect(&sr);
                    PenSize(1, 1);
                }
            }
            break;
        }
        case Shape::kEllipse:
            if (shape.rotation != 0) {
                short sw = static_cast<short>(shape.strokeWidth);
                DrawRotatedEllipse(r, shape.rotation,
                                   shape.hasFill, shape.fillColor,
                                   shape.hasStroke, shape.strokeColor, sw);
            } else {
                if (shape.hasFill) {
                    RGBColor c = shape.fillColor; RGBForeColor(&c); PaintOval(&r);
                }
                if (shape.hasStroke) {
                    RGBColor c = shape.strokeColor; RGBForeColor(&c);
                    short sw = static_cast<short>(shape.strokeWidth);
                    Rect sr = r;
                    if (shape.strokeAlign == 2) { sr.top-=sw; sr.left-=sw; sr.bottom+=sw; sr.right+=sw; }
                    else if (shape.strokeAlign == 0) { short e=sw/2; sr.top-=e; sr.left-=e; sr.bottom+=e; sr.right+=e; }
                    PenSize(sw, sw); FrameOval(&sr); PenSize(1, 1);
                }
            }
            break;
        case Shape::kText: {
            const TextShape& t = static_cast<const TextShape&>(shape);
            short scaledSize = static_cast<short>(SInt32(t.fontSize) * gCanvasZoom / 100);
            if (scaledSize < 4)   scaledSize = 4;
            if (scaledSize > 127) scaledSize = 127;

            // Resolve font family → QuickDraw font ID
            short fontID = 0;
            if (!t.fontFamily.empty()) {
                Str255 fname; fname[0] = 0;
                for (int i = 0; i < (int)t.fontFamily.size() && i < 63; ++i) {
                    fname[i+1] = static_cast<unsigned char>(t.fontFamily[i]); fname[0]++;
                }
                GetFNum(fname, &fontID);
            }
            TextFont(fontID); TextSize(scaledSize);

            // Stroke renders as QuickDraw outline on glyphs (backColor=fill, foreColor=stroke)
            // so the outline follows letter shapes rather than a bounding rectangle.
            if (shape.hasStroke) {
                RGBColor fc = shape.hasFill ? shape.fillColor : RGBColor{0xFFFF,0xFFFF,0xFFFF};
                RGBBackColor(&fc);
                RGBColor sc = shape.strokeColor; RGBForeColor(&sc);
                TextFace(static_cast<short>(t.fontFace | 8));  // QuickDraw outline bit
            } else if (shape.hasFill) {
                RGBColor tc = shape.fillColor; RGBForeColor(&tc);
                RGBColor wh = {0xFFFF,0xFFFF,0xFFFF}; RGBBackColor(&wh);
                TextFace(t.fontFace);
            } else {
                TextFont(0); TextSize(12); break;  // nothing to draw
            }

            const std::string& str = t.text;
            short lineH = static_cast<short>(SInt32(scaledSize) * t.lineHeight / 100);
            if (lineH < 1) lineH = 1;
            short drawY   = static_cast<short>(r.top + scaledSize);
            short boxW    = static_cast<short>(r.right - r.left);
            short lsxPx   = static_cast<short>(SInt32(t.letterSpacing) * gCanvasZoom / 100);

            if (!str.empty()) {
                size_t pos = 0;
                do {
                    size_t nl  = str.find('\n', pos);
                    size_t len = (nl == std::string::npos) ? str.size() - pos : nl - pos;
                    if (len > 0) {
                        Str255 pline; pline[0] = 0;
                        for (size_t ci = 0; ci < len && ci < 63; ++ci) {
                            pline[ci+1] = static_cast<unsigned char>(str[pos+ci]); pline[0]++;
                        }
                        // Line width for alignment
                        short lw;
                        if (lsxPx == 0) {
                            lw = StringWidth(pline);
                        } else {
                            lw = 0;
                            for (int ci = 1; ci <= pline[0]; ++ci)
                                lw = static_cast<short>(lw + CharWidth((char)pline[ci]) + lsxPx);
                        }
                        // Alignment → start X
                        short sx;
                        if (t.textAlign == 1)      sx = static_cast<short>(r.left + (boxW - lw) / 2);
                        else if (t.textAlign == 2) sx = static_cast<short>(r.right - lw);
                        else                       sx = r.left;
                        // Draw line
                        if (lsxPx == 0) {
                            MoveTo(sx, drawY); DrawString(pline);
                        } else {
                            short px = sx;
                            for (int ci = 1; ci <= pline[0]; ++ci) {
                                Str255 sc; sc[0]=1; sc[1]=pline[ci];
                                MoveTo(px, drawY); DrawString(sc);
                                px = static_cast<short>(px + CharWidth((char)pline[ci]) + lsxPx);
                            }
                        }
                    }
                    if (nl == std::string::npos) break;
                    pos   = nl + 1;
                    drawY = static_cast<short>(drawY + lineH);
                } while (pos < str.size());
            }
            TextFace(0); TextSize(12); TextFont(0);
            RGBColor wh = {0xFFFF,0xFFFF,0xFFFF}; RGBBackColor(&wh);
            break;
        }
        default: break;
    }
    if (shapeOp) PenNormal();
}

// Forward-declare so DrawFrame can call itself recursively
static void DrawFrame(const Frame& frame);

static void DrawFrame(const Frame& frame) {
    if (!frame.visible) return;
    Rect r = CanvasRect(frame.bounds);

    // Compute corner rendering params — individual per-corner or uniform
    bool fIndiv = frame.cornerIndividual;
    short fitl = 0, fitr = 0, fibr = 0, fibl = 0;
    short fov = 0;
    if (fIndiv) {
        fitl = ScaleCornerRadius(frame.cornerTL);
        fitr = ScaleCornerRadius(frame.cornerTR);
        fibr = ScaleCornerRadius(frame.cornerBR);
        fibl = ScaleCornerRadius(frame.cornerBL);
    } else {
        SInt32 fovL = (frame.cornerRadius > 0)
                      ? (SInt32(frame.cornerRadius) * 2 * gCanvasZoom / 100) : 0;
        if (fovL > 32767) fovL = 32767;
        fov = static_cast<short>(fovL);
        if (fov < 2 && frame.cornerRadius > 0) fov = 2;
        // Clamp oval to rect dimensions — Figma caps effective radius at min(w,h)/2.
        { short rw=static_cast<short>(r.right-r.left), rh=static_cast<short>(r.bottom-r.top);
          short mx=static_cast<short>(rw<rh?rw:rh); if(fov>mx) fov=mx; }
    }

    // Fill
    bool frameOp = (frame.opacity < 100);
    if (frameOp) {
        UInt16 fw = static_cast<UInt16>((UInt32)frame.opacity * 65535 / 100);
        RGBColor oc = { fw, fw, fw };
        OpColor(&oc); PenMode(blend);
    }
    RGBColor bg = frame.backgroundColor;
    RGBForeColor(&bg);
    if (fIndiv) ApplyRoundRectCorners(r, fitl, fitr, fibr, fibl, true);
    else if (fov > 0) PaintRoundRect(&r, fov, fov); else PaintRect(&r);
    if (frameOp) PenNormal();

    // Draw children, optionally clipped and optionally in reverse z-order.
    auto drawChildren = [&]() {
        if (frame.childOrder.empty()) {
            // Legacy / newly-created frames without explicit childOrder: shapes first, then frames
            if (frame.canvasStackReverse) {
                for (auto it = frame.children.rbegin();    it != frame.children.rend();    ++it) DrawShape(**it);
                for (auto it = frame.childFrames.rbegin(); it != frame.childFrames.rend(); ++it) DrawFrame(**it);
            } else {
                for (const auto& s  : frame.children)    DrawShape(*s);
                for (const auto& cf : frame.childFrames)  DrawFrame(*cf);
            }
        } else {
            if (frame.canvasStackReverse) {
                for (auto it = frame.childOrder.rbegin(); it != frame.childOrder.rend(); ++it) {
                    if (it->isFrame) DrawFrame(*frame.childFrames[it->idx]);
                    else             DrawShape(*frame.children[it->idx]);
                }
            } else {
                for (const auto& cr : frame.childOrder) {
                    if (cr.isFrame) DrawFrame(*frame.childFrames[cr.idx]);
                    else            DrawShape(*frame.children[cr.idx]);
                }
            }
        }
    };
    if (frame.clipContent) {
        RgnHandle savedClip = NewRgn();
        GetClip(savedClip);
        ClipRect(&r);
        drawChildren();
        SetClip(savedClip);
        DisposeRgn(savedClip);
    } else {
        drawChildren();
    }

    // Stroke or default thin border
    if (frameOp) {
        UInt16 fw = static_cast<UInt16>((UInt32)frame.opacity * 65535 / 100);
        RGBColor oc = { fw, fw, fw };
        OpColor(&oc); PenMode(blend);
    }
    if (frame.hasStroke) {
        RGBColor c = frame.strokeColor; RGBForeColor(&c);
        short sw = static_cast<short>(frame.strokeWidth);
        Rect sr = r;
        if (frame.strokeAlign == 2) { sr.top-=sw; sr.left-=sw; sr.bottom+=sw; sr.right+=sw; }
        else if (frame.strokeAlign == 0) { short e=sw/2; sr.top-=e; sr.left-=e; sr.bottom+=e; sr.right+=e; }
        PenSize(sw, sw);
        if (fIndiv) ApplyRoundRectCorners(sr, fitl, fitr, fibr, fibl, false);
        else if (fov > 0) FrameRoundRect(&sr, fov, fov); else FrameRect(&sr);
        PenSize(1, 1);
    } else {
        RGBColor border = { 0xBBBB, 0xBBBB, 0xBBBB };
        RGBForeColor(&border);
        if (fIndiv) ApplyRoundRectCorners(r, fitl, fitr, fibr, fibl, false);
        else if (fov > 0) FrameRoundRect(&r, fov, fov); else FrameRect(&r);
    }
    if (frameOp) PenNormal();

    // Name label — only on top-level frames (no parent)
    if (frame.parent == nullptr) {
        RGBColor lc = { 0x4444, 0x4444, 0x4444 };
        RGBForeColor(&lc);
        TextSize(10);
        Str255 pn; ToPStr(frame.name, pn);
        MoveTo(r.left, static_cast<short>(r.top - 5));
        DrawString(pn);
        TextSize(12);
    }
}

static void DrawSelectionHighlight() {
    RGBColor selBlue = { 0x1177, 0x55AA, 0xFFFF };
    RGBColor white   = { 0xFFFF, 0xFFFF, 0xFFFF };
    static const short kHW = 4;

    // Axis-aligned border + square handles at 8 positions
    auto drawHandles = [&](const Rect& r) {
        short cx = static_cast<short>((r.left + r.right)  / 2);
        short cy = static_cast<short>((r.top  + r.bottom) / 2);
        const short hx[8] = { r.left, cx, r.right, r.right,  r.right,  cx,     r.left,  r.left  };
        const short hy[8] = { r.top,  r.top, r.top, cy,      r.bottom, r.bottom, r.bottom, cy    };
        for (int i = 0; i < 8; ++i) {
            Rect h = {
                static_cast<short>(hy[i]-kHW), static_cast<short>(hx[i]-kHW),
                static_cast<short>(hy[i]+kHW), static_cast<short>(hx[i]+kHW)
            };
            RGBForeColor(&white); PaintRect(&h);
            RGBForeColor(&selBlue); FrameRect(&h);
        }
    };

    auto drawItem = [&](const Rect& r) {
        RGBForeColor(&selBlue);
        PenSize(2, 2); FrameRect(&r); PenSize(1, 1);
        drawHandles(r);
    };

    // Rotated border + handles placed at rotated corner and edge-midpoint positions
    auto drawRotatedItem = [&](const Bounds2& bounds, short angleDeg) {
        Rect r = CanvasRect(bounds);
        double cx = (r.left + r.right)  * 0.5;
        double cy = (r.top  + r.bottom) * 0.5;
        double hw = (r.right  - r.left) * 0.5;
        double hh = (r.bottom - r.top)  * 0.5;
        double rad = angleDeg * 3.14159265358979323846 / 180.0;
        double cosA = std::cos(rad), sinA = std::sin(rad);

        double lx[4] = { -hw,  hw,  hw, -hw };
        double ly[4] = { -hh, -hh,  hh,  hh };
        short px[4], py[4];
        for (int i = 0; i < 4; ++i) {
            px[i] = static_cast<short>(cx + lx[i]*cosA - ly[i]*sinA + 0.5);
            py[i] = static_cast<short>(cy + lx[i]*sinA + ly[i]*cosA + 0.5);
        }

        // Rotated border
        RGBForeColor(&selBlue);
        PenSize(2, 2);
        MoveTo(px[0], py[0]);
        LineTo(px[1], py[1]); LineTo(px[2], py[2]);
        LineTo(px[3], py[3]); LineTo(px[0], py[0]);
        PenSize(1, 1);

        // 8 handle positions: 4 corners then 4 edge midpoints
        short hpx[8], hpy[8];
        for (int i = 0; i < 4; ++i) { hpx[i] = px[i]; hpy[i] = py[i]; }
        hpx[4] = static_cast<short>((px[0]+px[1])/2); hpy[4] = static_cast<short>((py[0]+py[1])/2);
        hpx[5] = static_cast<short>((px[1]+px[2])/2); hpy[5] = static_cast<short>((py[1]+py[2])/2);
        hpx[6] = static_cast<short>((px[2]+px[3])/2); hpy[6] = static_cast<short>((py[2]+py[3])/2);
        hpx[7] = static_cast<short>((px[3]+px[0])/2); hpy[7] = static_cast<short>((py[3]+py[0])/2);
        for (int i = 0; i < 8; ++i) {
            Rect h = {
                static_cast<short>(hpy[i]-kHW), static_cast<short>(hpx[i]-kHW),
                static_cast<short>(hpy[i]+kHW), static_cast<short>(hpx[i]+kHW)
            };
            RGBForeColor(&white); PaintRect(&h);
            RGBForeColor(&selBlue); FrameRect(&h);
        }
    };

    for (Shape* s : gSelectedShapes) {
        if (s->rotation != 0) drawRotatedItem(s->bounds, s->rotation);
        else                  drawItem(CanvasRect(s->bounds));
    }
    for (Frame* f : gSelectedFrames) drawItem(CanvasRect(f->bounds));

    // Primary single-select item (skip if already drawn as part of multi-select)
    bool drawnAsShape = gSelectedShape &&
        std::find(gSelectedShapes.begin(), gSelectedShapes.end(), gSelectedShape) != gSelectedShapes.end();
    bool drawnAsFrame = gSelectedFrame &&
        std::find(gSelectedFrames.begin(), gSelectedFrames.end(), gSelectedFrame) != gSelectedFrames.end();

    if (!drawnAsShape && !drawnAsFrame) {
        if (!gSelectedShape && !gSelectedFrame) { PenNormal(); return; }
        if (gSelectedShape && gSelectedShape->rotation != 0) {
            drawRotatedItem(gSelectedShape->bounds, gSelectedShape->rotation);
        } else {
            Rect r = gSelectedShape ? CanvasRect(gSelectedShape->bounds)
                                    : CanvasRect(gSelectedFrame->bounds);
            drawItem(r);
        }
    }

    PenNormal();
}

// Update bounds of a single text shape based on its TextSizing mode.
// Must be called with a valid QuickDraw port (gMainWindow) already set.
static void UpdateTextShapeBounds(TextShape& ts) {
    short fontID = 0;
    if (!ts.fontFamily.empty()) {
        Str255 fname; fname[0] = 0;
        for (int i = 0; i < (int)ts.fontFamily.size() && i < 63; ++i) {
            fname[i+1] = static_cast<unsigned char>(ts.fontFamily[i]); fname[0]++;
        }
        GetFNum(fname, &fontID);
    }
    short fs = ts.fontSize;
    if (fs < 4)   fs = 4;
    if (fs > 127) fs = 127;
    TextFont(fontID); TextSize(fs); TextFace(ts.fontFace);

    // Measure each explicit line; count lines
    const std::string& str = ts.text;
    short maxW    = 8;   // minimum 8px
    int   nLines  = 0;
    size_t pos    = 0;
    do {
        size_t nl  = str.find('\n', pos);
        size_t len = (nl == std::string::npos) ? str.size() - pos : nl - pos;
        Str255 pl; pl[0] = 0;
        for (size_t ci = 0; ci < len && ci < 63; ++ci) {
            pl[ci+1] = static_cast<unsigned char>(str[pos+ci]); pl[0]++;
        }
        short lw;
        if (ts.letterSpacing == 0) {
            lw = StringWidth(pl);
        } else {
            lw = 0;
            for (int ci = 1; ci <= pl[0]; ++ci)
                lw = static_cast<short>(lw + CharWidth(static_cast<char>(pl[ci])) + ts.letterSpacing);
        }
        if (lw > maxW) maxW = lw;
        ++nLines;
        if (nl == std::string::npos) break;
        pos = nl + 1;
    } while (pos < str.size());
    if (nLines == 0) nLines = 1;

    short lineH = static_cast<short>(SInt32(fs) * ts.lineHeight / 100);
    if (lineH < 1) lineH = fs;
    short totalH = static_cast<short>(lineH * nLines + 4);

    TextFont(0); TextSize(12); TextFace(0);

    if (ts.textSizing == TextSizing::AutoWidth) {
        ts.bounds.w = static_cast<SInt32>(maxW + 8);
        ts.bounds.h = static_cast<SInt32>(totalH);
    } else if (ts.textSizing == TextSizing::AutoHeight) {
        ts.bounds.h = static_cast<SInt32>(totalH);
    }
    // Fixed: leave w and h unchanged
}

static void UpdateTextShapeBoundsInFrame(Frame& f) {
    for (auto& s : f.children)
        if (s->GetType() == Shape::kText)
            UpdateTextShapeBounds(static_cast<TextShape&>(*s));
    for (auto& cf : f.childFrames)
        UpdateTextShapeBoundsInFrame(*cf);
}

static void UpdateAllTextShapeBounds(Document* doc) {
    if (!doc || !gMainWindow) return;
    SetPortWindowPort(gMainWindow);
    for (auto& s : doc->rootShapes)
        if (s->GetType() == Shape::kText)
            UpdateTextShapeBounds(static_cast<TextShape&>(*s));
    for (auto& f : doc->frames)
        UpdateTextShapeBoundsInFrame(*f);
}

void DrawWindowContent(WindowRef win) {
    // Update text shape bounds first (auto-sizing), then run layout.
    UpdateAllTextShapeBounds(gDocument);
    RunDocumentLayout(gDocument);

    SetPortWindowPort(win);
    Rect portRect;
    GetWindowPortBounds(win, &portRect);

    RGBColor canvasBg = { 0xDDDD, 0xDDDD, 0xDDDD };
    RGBBackColor(&canvasBg);
    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);
    EraseRect(&portRect);

    if (gDocument) {
        // Render root-level items in rootChildOrder z-order.
        // rootChildOrder[0] = top of layers panel = frontmost; iterate rbegin so it is drawn last.
        if (!gDocument->rootChildOrder.empty()) {
            for (auto it = gDocument->rootChildOrder.rbegin(); it != gDocument->rootChildOrder.rend(); ++it) {
                if (it->isFrame) {
                    if (it->idx < (int)gDocument->frames.size())
                        DrawFrame(*gDocument->frames[it->idx]);
                } else {
                    if (it->idx < (int)gDocument->rootShapes.size()) {
                        const auto& shape = gDocument->rootShapes[it->idx];
                        DrawShape(*shape);
                        if (shape->visible) DrawShapeNameLabel(*shape);
                    }
                }
            }
        } else {
            // Legacy fallback (document predates rootChildOrder)
            for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it)
                DrawFrame(**it);
            for (const auto& shape : gDocument->rootShapes) {
                DrawShape(*shape);
                if (shape->visible) DrawShapeNameLabel(*shape);
            }
        }
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

    if (!f->childOrder.empty()) {
        // Iterate in reverse childOrder (topmost first for correct z-order hit-testing)
        for (auto it = f->childOrder.rbegin(); it != f->childOrder.rend(); ++it) {
            if (it->isFrame) {
                HitResult res = HitTestFrame(f->childFrames[it->idx].get(), pt);
                if (res.found) return res;
            } else {
                const auto& s = f->children[it->idx];
                Rect sr1 = CanvasRect(s->bounds);
                bool hit = (s->rotation != 0)
                    ? HitTestRotated(s->bounds, s->rotation, pt)
                    : PtInRect(pt, &sr1) != 0;
                if (hit) return { f, s.get(), true };
            }
        }
    } else {
        // Legacy fallback: child frames then shapes (both in reverse for topmost-first)
        for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
            HitResult res = HitTestFrame(it->get(), pt);
            if (res.found) return res;
        }
        for (auto it = f->children.rbegin(); it != f->children.rend(); ++it) {
            const auto& s = *it;
            Rect sr2 = CanvasRect(s->bounds);
            bool hit = (s->rotation != 0)
                ? HitTestRotated(s->bounds, s->rotation, pt)
                : PtInRect(pt, &sr2) != 0;
            if (hit) return { f, s.get(), true };
        }
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

// DeepestInFrame variant that skips any frame in an exclusion set
static Frame* DeepestInFrameExcl(Frame* f, Point pt, const std::vector<Frame*>& excl) {
    if (std::find(excl.begin(), excl.end(), f) != excl.end()) return nullptr;
    Rect r = CanvasRect(f->bounds);
    if (!PtInRect(pt, &r)) return nullptr;
    for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
        Frame* deeper = DeepestInFrameExcl(it->get(), pt, excl);
        if (deeper) return deeper;
    }
    return f;
}
static Frame* DeepestFrameAtExcl(Point pt, const std::vector<Frame*>& excl) {
    Frame* result = nullptr;
    for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it) {
        Frame* f = DeepestInFrameExcl(it->get(), pt, excl);
        if (f) result = f;
    }
    return result;
}

// Recursively collect frames intersecting (l,t,r,b) in canvas space.
// If the band fully contains a frame, select that frame (not its children).
// If the band only partially overlaps a frame, recurse into children.
// A leaf frame (no child frames) is always selected on any intersection.
static void CollectAllBandFrames(Frame* frm, SInt32 l, SInt32 t, SInt32 r, SInt32 b,
                                  std::vector<Frame*>& out) {
    const Bounds2& bnd = frm->bounds;
    bool intersects = bnd.x < r && (bnd.x + bnd.w) > l && bnd.y < b && (bnd.y + bnd.h) > t;
    if (!intersects) return;
    bool contained  = (l <= bnd.x && (bnd.x + bnd.w) <= r &&
                       t <= bnd.y && (bnd.y + bnd.h) <= b);
    if (contained || frm->childFrames.empty()) {
        out.push_back(frm);
    } else {
        for (auto& cf : frm->childFrames)
            CollectAllBandFrames(cf.get(), l, t, r, b, out);
    }
}

// Forward declarations — defined later in this file (after undo infrastructure)
static std::unique_ptr<Frame> CloneFrame(const Frame* src, Frame* newParent);
static std::string NextAvailableName(const std::string& name);
static Frame* LocateShapeParent(Shape* s);

// --------------------------------------------------------------------------
// rootChildOrder helpers
// --------------------------------------------------------------------------

// Remove the rootChildOrder entry for (isFrame, typedIdx) and fix up higher indices.
static void RootOrderErase(bool isFrame, int typedIdx) {
    auto& ord = gDocument->rootChildOrder;
    for (auto it = ord.begin(); it != ord.end(); ) {
        if (it->isFrame == isFrame && it->idx == typedIdx) { it = ord.erase(it); continue; }
        if (it->isFrame == isFrame && it->idx > typedIdx) --it->idx;
        ++it;
    }
}

// Insert a new rootChildOrder entry at orderPos, incrementing higher typed indices.
static void RootOrderInsert(int orderPos, bool isFrame, int typedIdx) {
    auto& ord = gDocument->rootChildOrder;
    for (auto& ref : ord)
        if (ref.isFrame == isFrame && ref.idx >= typedIdx) ++ref.idx;
    if (orderPos < 0) orderPos = 0;
    if (orderPos > (int)ord.size()) orderPos = (int)ord.size();
    ord.insert(ord.begin() + orderPos, { isFrame, typedIdx });
}

// Build rootChildOrder from current frames+rootShapes (legacy panel order).
static void InitRootChildOrder(Document* doc) {
    doc->rootChildOrder.clear();
    for (int i = 0; i < (int)doc->rootShapes.size(); ++i)
        doc->rootChildOrder.push_back({ false, i });
    for (int i = 0; i < (int)doc->frames.size(); ++i)
        doc->rootChildOrder.push_back({ true, i });
}

// --------------------------------------------------------------------------
// Extract a Shape unique_ptr from its current owner (Frame or rootShapes).
// Also removes and reindexes the entry in parent->childOrder or rootChildOrder.
static std::unique_ptr<Shape> ExtractShape(Shape* s, Frame* parent) {
    auto& vec = parent ? parent->children : gDocument->rootShapes;
    for (int i = 0; i < (int)vec.size(); ++i) {
        if (vec[i].get() == s) {
            auto o = std::move(vec[i]);
            vec.erase(vec.begin() + i);
            if (parent) {
                for (auto it = parent->childOrder.begin(); it != parent->childOrder.end(); ) {
                    if (!it->isFrame && it->idx == i) { it = parent->childOrder.erase(it); continue; }
                    if (!it->isFrame && it->idx > i) --it->idx;
                    ++it;
                }
            } else {
                RootOrderErase(false, i);
            }
            return o;
        }
    }
    return nullptr;
}

// Extract a Frame unique_ptr from its current owner.
// Also removes and reindexes the entry in parent->childOrder or rootChildOrder.
static std::unique_ptr<Frame> ExtractFrame(Frame* f) {
    Frame* par = f->parent;
    auto& vec = par ? par->childFrames : gDocument->frames;
    for (int i = 0; i < (int)vec.size(); ++i) {
        if (vec[i].get() == f) {
            auto o = std::move(vec[i]);
            vec.erase(vec.begin() + i);
            if (par) {
                for (auto it = par->childOrder.begin(); it != par->childOrder.end(); ) {
                    if (it->isFrame && it->idx == i) { it = par->childOrder.erase(it); continue; }
                    if (it->isFrame && it->idx > i) --it->idx;
                    ++it;
                }
            } else {
                RootOrderErase(true, i);
            }
            return o;
        }
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
static void HandleResizeDrag(WindowRef win, int hi, Point startPt, UInt16 startMods = 0) {
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
    bool isCorner   = (hi == 0 || hi == 2 || hi == 4 || hi == 6);
    Bounds2 origB   = *b;   // snapshot for absolute delta calculation
    Point prev = startPt, curr = startPt;
    bool pushedUndo = false;

    while (Button()) {
        GetMouse(&curr);
        if (curr.h != prev.h || curr.v != prev.v) {
            if (!pushedUndo) { PushUndo(); pushedUndo = true; }

            // Compute total delta from drag start (avoids AR accumulation error)
            SInt32 totalDX = SInt32(curr.h - startPt.h) * 100 / gCanvasZoom;
            SInt32 totalDY = SInt32(curr.v - startPt.v) * 100 / gCanvasZoom;

            *b = origB;
            if (bL[hi]) { b->x = origB.x + totalDX; b->w = origB.w - totalDX; }
            if (bT[hi]) { b->y = origB.y + totalDY; b->h = origB.h - totalDY; }
            if (bR[hi])   b->w = origB.w + totalDX;
            if (bB[hi])   b->h = origB.h + totalDY;

            // Aspect ratio lock: inspector button OR Shift held at drag start
            bool lockAR = isCorner && (IsAspectLocked() || (startMods & shiftKey));
            if (lockAR && origB.w > 0 && origB.h > 0) {
                SInt32 newH = b->w * origB.h / origB.w;
                if (bT[hi]) { SInt32 bot = origB.y + origB.h; b->h = newH; b->y = bot - newH; }
                else          b->h = newH;
            }

            if (b->w < kMin) { if (bL[hi]) b->x += (b->w - kMin); b->w = kMin; }
            if (b->h < kMin) { if (bT[hi]) b->y += (b->h - kMin); b->h = kMin; }

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
        if (label.empty()) {
            if      (s->GetType() == Shape::kEllipse) label = "Ellipse";
            else if (s->GetType() == Shape::kText)    label = "Text";
            else                                      label = "Rectangle";
        }
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

// Returns the shape and its immediate parent frame when the click hits a shape
// name label, enabling drag-from-label (same as frames already support).
struct ShapeLabelHit { Shape* shape = nullptr; Frame* parent = nullptr; };
static ShapeLabelHit HitTestShapeLabelInFrame(Frame* f, Point pt) {
    for (auto it = f->children.rbegin(); it != f->children.rend(); ++it) {
        Shape* s = it->get();
        Rect r = CanvasRect(s->bounds);
        std::string lbl = s->name;
        if (lbl.empty()) {
            if      (s->GetType() == Shape::kEllipse) lbl = "Ellipse";
            else if (s->GetType() == Shape::kText)    lbl = "Text";
            else                                      lbl = "Rectangle";
        }
        Str255 pn; ToPStr(lbl, pn);
        TextSize(10); short tw = StringWidth(pn); TextSize(12);
        Rect lr = { static_cast<short>(r.top-16), r.left,
                    static_cast<short>(r.top-1),  static_cast<short>(r.left+tw+4) };
        if (PtInRect(pt, &lr)) return { s, f };
    }
    for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
        auto res = HitTestShapeLabelInFrame(it->get(), pt);
        if (res.shape) return res;
    }
    return {};
}

// --------------------------------------------------------------------------
// Rubber-band helpers
// --------------------------------------------------------------------------

struct BandShape { Shape* shape; Frame* parent; };

static void CollectBandShapes(Frame* frm, SInt32 l, SInt32 t, SInt32 r, SInt32 b,
                               std::vector<BandShape>& out) {
    for (auto& sp : frm->children) {
        if (!sp->visible) continue;
        const Bounds2& bnd = sp->bounds;
        if (bnd.x < r && (bnd.x + bnd.w) > l && bnd.y < b && (bnd.y + bnd.h) > t)
            out.push_back({ sp.get(), frm });
    }
    for (auto& cf : frm->childFrames)
        CollectBandShapes(cf.get(), l, t, r, b, out);
}

// --------------------------------------------------------------------------
// Select tool: resize handles → name labels → body hit-test → move + reparent
// --------------------------------------------------------------------------

void HandleCanvasSelect(WindowRef win, Point startGlobal, UInt16 modifiers) {
    if (!gDocument) return;

    SetPortWindowPort(win);
    Point pt = startGlobal;
    GlobalToLocal(&pt);

    // ---- 1. Resize handle (only when something is already selected) ----
    int handleIdx = HitTestHandles(pt);
    if (handleIdx >= 0) {
        bool selLocked = gSelectedShape ? gSelectedShape->locked
                                        : (gSelectedFrame ? gSelectedFrame->locked : false);
        if (!selLocked) HandleResizeDrag(win, handleIdx, pt, modifiers);
        return;
    }

    Frame* hitFrame = nullptr;
    Shape* hitShape = nullptr;
    bool   found    = false;

    // ---- 2. Name label: shapes first (enables drag-from-label), then frames ----
    if (!found) {
        for (auto it = gDocument->frames.begin(); it != gDocument->frames.end() && !found; ++it) {
            auto res = HitTestShapeLabelInFrame(it->get(), pt);
            if (res.shape) { hitShape = res.shape; hitFrame = res.parent; found = true; }
        }
    }
    if (!found) {
        // rootShapes name labels
        for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it) {
            Shape* s = it->get();
            Rect sr = CanvasRect(s->bounds);
            std::string lbl = s->name;
            if (lbl.empty()) {
                if      (s->GetType() == Shape::kEllipse) lbl = "Ellipse";
                else if (s->GetType() == Shape::kText)    lbl = "Text";
                else                                      lbl = "Rectangle";
            }
            Str255 pn; ToPStr(lbl, pn);
            TextSize(10); short tw = StringWidth(pn); TextSize(12);
            Rect lr = { static_cast<short>(sr.top-16), sr.left,
                        static_cast<short>(sr.top-1),  static_cast<short>(sr.left+tw+4) };
            if (PtInRect(pt, &lr)) { hitShape = s; hitFrame = nullptr; found = true; break; }
        }
    }
    if (!found) {
        for (auto it = gDocument->frames.begin(); it != gDocument->frames.end() && !found; ++it) {
            Frame* lf = HitTestFrameLabel(it->get(), pt);
            if (lf) { hitFrame = lf; found = true; }
        }
    }

    // ---- 3. Regular body hit-test (respects rootChildOrder z-order) ----
    if (!found) {
        if (!gDocument->rootChildOrder.empty()) {
            // rootChildOrder[0] = frontmost; iterate forward so topmost item wins.
            for (const auto& ref : gDocument->rootChildOrder) {
                if (found) break;
                if (ref.isFrame) {
                    if (ref.idx < (int)gDocument->frames.size()) {
                        HitResult res = HitTestFrame(gDocument->frames[ref.idx].get(), pt);
                        if (res.found) { hitFrame = res.frame; hitShape = res.shape; found = true; }
                    }
                } else {
                    if (ref.idx < (int)gDocument->rootShapes.size()) {
                        Shape* s = gDocument->rootShapes[ref.idx].get();
                        Rect sr3 = CanvasRect(s->bounds);
                        bool hit = (s->rotation != 0)
                            ? HitTestRotated(s->bounds, s->rotation, pt)
                            : PtInRect(pt, &sr3) != 0;
                        if (hit) { hitShape = s; hitFrame = nullptr; found = true; }
                    }
                }
            }
        } else {
            // Legacy fallback
            for (auto it = gDocument->frames.begin(); it != gDocument->frames.end() && !found; ++it) {
                HitResult res = HitTestFrame(it->get(), pt);
                if (res.found) { hitFrame = res.frame; hitShape = res.shape; found = true; }
            }
            if (!found) {
                for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it) {
                    Shape* s = it->get();
                    Rect sr4 = CanvasRect(s->bounds);
                    bool hit = (s->rotation != 0)
                        ? HitTestRotated(s->bounds, s->rotation, pt)
                        : PtInRect(pt, &sr4) != 0;
                    if (hit) { hitShape = s; hitFrame = nullptr; found = true; break; }
                }
            }
        }
    }

    // ---- Shift+click: toggle shape in multi-select ----
    if (found && (modifiers & shiftKey) && hitShape) {
        // Allow adding when: no current context, same-frame context, gSelectedShapes already populated,
        // OR mixing with frame selections that share the same parent as this shape.
        Frame* selFramesParent = nullptr;
        if (!gSelectedFrames.empty())
            selFramesParent = gSelectedFrames[0]->parent;
        else if (gSelectedFrame && gSelectedShape == nullptr)
            selFramesParent = gSelectedFrame->parent;
        bool mixOK = (selFramesParent != nullptr) && (selFramesParent == hitFrame);
        bool canAdd = mixOK
                   || (gSelectedFrame == nullptr)
                   || (hitFrame == gSelectedFrame)
                   || (!gSelectedShapes.empty());
        if (canAdd) {
            if (mixOK) {
                if (gSelectedFrame && gSelectedShape == nullptr &&
                    std::find(gSelectedFrames.begin(), gSelectedFrames.end(), gSelectedFrame) == gSelectedFrames.end())
                    gSelectedFrames.push_back(gSelectedFrame);
            }
            gSelectedFrame = hitFrame;
            auto it = std::find(gSelectedShapes.begin(), gSelectedShapes.end(), hitShape);
            if (it != gSelectedShapes.end()) {
                // Already selected — deselect it
                gSelectedShapes.erase(it);
                gSelectedShape = gSelectedShapes.empty() ? nullptr : gSelectedShapes.back();
            } else {
                // Add existing single selection to pool first
                if (gSelectedShape &&
                    std::find(gSelectedShapes.begin(), gSelectedShapes.end(), gSelectedShape) == gSelectedShapes.end()) {
                    gSelectedShapes.push_back(gSelectedShape);
                }
                gSelectedShapes.push_back(hitShape);
                gSelectedShape = hitShape;
            }
            Rect portRect2; GetWindowPortBounds(win, &portRect2); InvalWindowRect(win, &portRect2);
            RefreshLayersPanel();
            RefreshInspector();
        }
        return;
    }

    // ---- Shift+click on a frame (no shape hit): toggle frame in gSelectedFrames ----
    if (found && (modifiers & shiftKey) && !hitShape && hitFrame) {
        // Allow mixing with shapes when the clicked frame's parent == gSelectedFrame (shape context).
        bool shapesPresent = (gSelectedShape != nullptr || !gSelectedShapes.empty());
        bool mixOK = shapesPresent && (hitFrame->parent == gSelectedFrame);
        if (!mixOK) {
            gSelectedShapes.clear(); gSelectedShape = nullptr;
        } else {
            if (gSelectedShape &&
                std::find(gSelectedShapes.begin(), gSelectedShapes.end(), gSelectedShape) == gSelectedShapes.end())
                gSelectedShapes.push_back(gSelectedShape);
        }
        auto fit = std::find(gSelectedFrames.begin(), gSelectedFrames.end(), hitFrame);
        if (fit != gSelectedFrames.end()) {
            gSelectedFrames.erase(fit);
            if (!mixOK) gSelectedFrame = gSelectedFrames.empty() ? nullptr : gSelectedFrames.back();
        } else {
            if (!mixOK && gSelectedFrame && !gSelectedShape &&
                std::find(gSelectedFrames.begin(), gSelectedFrames.end(), gSelectedFrame) == gSelectedFrames.end())
                gSelectedFrames.push_back(gSelectedFrame);
            gSelectedFrames.push_back(hitFrame);
            if (!mixOK) gSelectedFrame = hitFrame;
            // In mixed mode, gSelectedFrame stays as the parent context.
        }
        Rect portRect2; GetWindowPortBounds(win, &portRect2); InvalWindowRect(win, &portRect2);
        RefreshLayersPanel();
        RefreshInspector();
        return;
    }

    // If clicking on an object already in the multi-select (including mixed), preserve selection for drag.
    bool isMixedSelect = (!gSelectedFrames.empty() && !gSelectedShapes.empty());

    // True if hitFrame, or any ancestor of hitFrame, is in gSelectedFrames.
    // Needed so clicking a shape (or sub-frame) *inside* a multi-selected frame still drags the group.
    auto frameOrAncestorSelected = [&]() -> bool {
        for (Frame* cur = hitFrame; cur; cur = cur->parent)
            if (std::find(gSelectedFrames.begin(), gSelectedFrames.end(), cur) != gSelectedFrames.end())
                return true;
        return false;
    };

    bool hitInSelection =
        (hitShape && gSelectedShapes.size() > 1 &&
         std::find(gSelectedShapes.begin(), gSelectedShapes.end(), hitShape) != gSelectedShapes.end())
        ||
        (!hitShape && hitFrame && gSelectedFrames.size() > 1 &&
         std::find(gSelectedFrames.begin(), gSelectedFrames.end(), hitFrame) != gSelectedFrames.end())
        ||
        (isMixedSelect && hitShape &&
         std::find(gSelectedShapes.begin(), gSelectedShapes.end(), hitShape) != gSelectedShapes.end())
        ||
        (isMixedSelect && !hitShape && hitFrame &&
         std::find(gSelectedFrames.begin(), gSelectedFrames.end(), hitFrame) != gSelectedFrames.end())
        ||
        // Clicking a child shape or sub-frame inside one of the multi-selected frames.
        // Only applies when 2+ frames are selected so single-frame clicks still enter the frame normally.
        (gSelectedFrames.size() > 1 && hitFrame && frameOrAncestorSelected());

    if (hitInSelection) {
        if (hitShape) gSelectedShape = hitShape;
        else          gSelectedFrame = hitFrame;
    } else {
        // Normal click — clear all multi-select, single selection
        gSelectedShapes.clear();
        gSelectedFrames.clear();
        gSelectedFrame = hitFrame;
        gSelectedShape = hitShape;
    }

    // ---- Double-click: rename the hit object ----
    if (found && gIsDoubleClick) {
        // Determine which object's name to edit.
        // Priority: shape label > frame label > any body hit
        std::string* targetName = nullptr;

        // Check shape labels across all frames first
        for (auto it = gDocument->frames.begin(); it != gDocument->frames.end() && !targetName; ++it) {
            Shape* sl = HitTestShapeLabel(it->get(), pt);
            if (sl) { targetName = &sl->name; }
        }
        // Check frame labels
        if (!targetName) {
            for (auto it = gDocument->frames.begin(); it != gDocument->frames.end() && !targetName; ++it) {
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

        // ── Option+drag: duplicate selection in-place, then drag the copy ────────
        if (modifiers & optionKey) {
            PushUndo();
            pushedUndo = true;

            bool isMultiSel = (gSelectedShapes.size() > 1 || gSelectedFrames.size() > 1)
                           || (!gSelectedFrames.empty() && !gSelectedShapes.empty());

            if (isMultiSel) {
                // Resolve the shapes' parent context (same logic as the multi-drag reparent path)
                Frame* shapeCtx = (std::find(gSelectedFrames.begin(), gSelectedFrames.end(),
                                             gSelectedFrame) != gSelectedFrames.end())
                                  ? (gSelectedFrames.empty() ? nullptr : gSelectedFrames[0]->parent)
                                  : gSelectedFrame;
                // Clone shapes
                std::vector<Shape*> newShapes;
                for (Shape* s : gSelectedShapes) {
                    auto clone = s->Clone();
                    clone->name = NextAvailableName(clone->name);
                    Shape* cp = clone.get();
                    newShapes.push_back(cp);
                    if (shapeCtx) {
                        shapeCtx->childOrder.push_back({ false, (int)shapeCtx->children.size() });
                        shapeCtx->children.push_back(std::move(clone));
                    } else {
                        RootOrderInsert(0, false, (int)gDocument->rootShapes.size());
                        gDocument->rootShapes.push_back(std::move(clone));
                    }
                }
                // Clone frames — skip frames whose ancestor is also selected (already cloned inside parent)
                auto hasSelAncestor = [&](Frame* f) -> bool {
                    for (Frame* cur = f->parent; cur; cur = cur->parent)
                        if (std::find(gSelectedFrames.begin(), gSelectedFrames.end(), cur) != gSelectedFrames.end())
                            return true;
                    return false;
                };
                std::vector<Frame*> newFrames;
                for (Frame* f : gSelectedFrames) {
                    if (hasSelAncestor(f)) continue;
                    Frame* par = f->parent;
                    auto clone = CloneFrame(f, par);
                    clone->name = NextAvailableName(clone->name);
                    Frame* cp = clone.get();
                    newFrames.push_back(cp);
                    if (par) {
                        par->childOrder.push_back({ true, (int)par->childFrames.size() });
                        par->childFrames.push_back(std::move(clone));
                    } else {
                        RootOrderInsert(0, true, (int)gDocument->frames.size());
                        gDocument->frames.push_back(std::move(clone));
                    }
                }
                gSelectedShapes  = newShapes;
                gSelectedShape   = newShapes.empty() ? nullptr : newShapes.back();
                gSelectedFrames  = newFrames;
                gSelectedFrame   = newFrames.empty() ? shapeCtx : newFrames.back();
                if (hitShape && !newShapes.empty()) hitShape = newShapes[0];
                if (!hitShape && !newFrames.empty()) hitFrame = newFrames[0];
            } else if (gSelectedShape) {
                auto clone = gSelectedShape->Clone();
                clone->name = NextAvailableName(clone->name);
                Shape* cp = clone.get();
                if (origParent) {
                    origParent->childOrder.push_back({ false, (int)origParent->children.size() });
                    origParent->children.push_back(std::move(clone));
                } else {
                    RootOrderInsert(0, false, (int)gDocument->rootShapes.size());
                    gDocument->rootShapes.push_back(std::move(clone));
                }
                gSelectedShape = cp;
                hitShape = cp;
            } else if (gSelectedFrame) {
                Frame* par = gSelectedFrame->parent;
                auto clone = CloneFrame(gSelectedFrame, par);
                clone->name = NextAvailableName(clone->name);
                Frame* cp = clone.get();
                if (par) {
                    par->childOrder.push_back({ true, (int)par->childFrames.size() });
                    par->childFrames.push_back(std::move(clone));
                } else {
                    RootOrderInsert(0, true, (int)gDocument->frames.size());
                    gDocument->frames.push_back(std::move(clone));
                }
                gSelectedFrame = cp;
                hitFrame = cp;
            }

            DrawWindowContent(win);
            RefreshLayersPanel();
            RefreshInspector();
        }

        // Exclude dragged shape(s)/frame from layout so they move freely.
        // Single-select: use gLayoutDragShape / gLayoutDragFrame; multi-select: gIsLayoutMultiDrag.
        bool isMultiDrag  = (gSelectedShapes.size() > 1 || gSelectedFrames.size() > 1)
                         || (!gSelectedFrames.empty() && !gSelectedShapes.empty()); // mixed
        bool isLayoutDrag = (!isMultiDrag && hitShape && hitFrame &&
                             hitFrame->layoutMode != LayoutMode::None);
        // Child frame being dragged within a layout parent
        bool isLayoutFrameDrag = (!isMultiDrag && !hitShape && hitFrame && hitFrame->parent &&
                                  hitFrame->parent->layoutMode != LayoutMode::None);
        if (isLayoutDrag)      gLayoutDragShape = hitShape;
        if (isLayoutFrameDrag) gLayoutDragFrame = hitFrame;
        if (isMultiDrag && hitFrame && hitFrame->layoutMode != LayoutMode::None)
            gIsLayoutMultiDrag = true;

        while (Button()) {
            GetMouse(&currPt);
            if (currPt.h != prevPt.h || currPt.v != prevPt.v) {
                if (!pushedUndo) { PushUndo(); pushedUndo = true; }
                // Convert screen pixel delta → canvas pixel delta
                SInt32 dx = SInt32(currPt.h - prevPt.h) * 100 / gCanvasZoom;
                SInt32 dy = SInt32(currPt.v - prevPt.v) * 100 / gCanvasZoom;

                // Helper: skip a frame if any of its ancestors is also in gSelectedFrames
                auto hasSelectedAncestor = [&](Frame* f) -> bool {
                    for (Frame* cur = f->parent; cur; cur = cur->parent)
                        if (std::find(gSelectedFrames.begin(), gSelectedFrames.end(), cur) != gSelectedFrames.end())
                            return true;
                    return false;
                };

                if (!gSelectedFrames.empty() && !gSelectedShapes.empty()) {
                    // gSelectedFrame may be one of the selected child frames (e.g. rubber-band mixed
                    // drag started by clicking a child frame). Resolve the shapes' actual context:
                    // if gSelectedFrame is itself selected, the shapes live in its parent.
                    Frame* shapesCtx = (std::find(gSelectedFrames.begin(), gSelectedFrames.end(),
                                                  gSelectedFrame) != gSelectedFrames.end())
                                       ? gSelectedFrames[0]->parent
                                       : gSelectedFrame;
                    bool parentMoved = shapesCtx &&
                        std::find(gSelectedFrames.begin(), gSelectedFrames.end(), shapesCtx) != gSelectedFrames.end();
                    if (!parentMoved)
                        for (Shape* s : gSelectedShapes) { s->bounds.x += dx; s->bounds.y += dy; }
                    for (Frame* f : gSelectedFrames)
                        if (!hasSelectedAncestor(f)) MoveFrameTree(f, dx, dy);
                } else if (gSelectedShapes.size() > 1) {
                    for (Shape* s : gSelectedShapes) {
                        s->bounds.x += dx;
                        s->bounds.y += dy;
                    }
                } else if (gSelectedFrames.size() > 1) {
                    for (Frame* f : gSelectedFrames)
                        if (!hasSelectedAncestor(f)) MoveFrameTree(f, dx, dy);
                } else if (hitShape) {
                    hitShape->bounds.x += dx;
                    hitShape->bounds.y += dy;
                } else {
                    MoveFrameTree(hitFrame, dx, dy);
                }

                DrawWindowContent(win);
                prevPt = currPt;
            }
        }

        gLayoutDragShape   = nullptr;
        gLayoutDragFrame   = nullptr;
        gIsLayoutMultiDrag = false;

        // ---- Re-parent on drop ----
        // Unified multi-select reparent: move ALL selected shapes AND frames to the drop destination.
        if (isMultiDrag) {
            // Use mouse-up position to determine reparent destination.
            Frame* newShapeParent = DeepestFrameAt(currPt);

            // Resolve shapes' actual parent: in a rubber-band mixed drag gSelectedFrame is set
            // to one of the selected child frames, not the containing parent frame.
            Frame* origShapeParent = (std::find(gSelectedFrames.begin(), gSelectedFrames.end(),
                                                 gSelectedFrame) != gSelectedFrames.end())
                                     ? gSelectedFrames[0]->parent
                                     : gSelectedFrame;

            // Reparent all selected shapes only if an actual drag occurred.
            if (!gSelectedShapes.empty() && newShapeParent != origShapeParent && pushedUndo) {
                for (Shape* target : gSelectedShapes) {
                    auto owned = ExtractShape(target, origShapeParent);
                    if (owned) {
                        if (newShapeParent) {
                            newShapeParent->childOrder.push_back({ false, (int)newShapeParent->children.size() });
                            newShapeParent->children.push_back(std::move(owned));
                        } else {
                            // Insert in rootChildOrder before the original parent's entry
                            int newIdx = (int)gDocument->rootShapes.size();
                            int parentTypedIdx = -1;
                            for (int pi = 0; pi < (int)gDocument->frames.size(); ++pi)
                                if (gDocument->frames[pi].get() == origShapeParent) { parentTypedIdx = pi; break; }
                            int orderPos = (int)gDocument->rootChildOrder.size();
                            if (parentTypedIdx >= 0) {
                                for (int oi = 0; oi < (int)gDocument->rootChildOrder.size(); ++oi)
                                    if (gDocument->rootChildOrder[oi].isFrame && gDocument->rootChildOrder[oi].idx == parentTypedIdx)
                                        { orderPos = oi; break; }
                            }
                            gDocument->rootChildOrder.insert(gDocument->rootChildOrder.begin() + orderPos, { false, newIdx });
                            gDocument->rootShapes.push_back(std::move(owned));
                        }
                    }
                }
                gSelectedFrame = newShapeParent;
            }

            // Reparent each selected frame to where its center landed (excluding selected frames).
            // When dropping to root level, insert BEFORE the original parent so the extracted
            // frames appear above the parent in the layers panel.
            for (Frame* f : gSelectedFrames) {
                Bounds2 fb = f->bounds;
                Point fc;
                fc.h = static_cast<short>(SInt32(fb.x + fb.w/2) * gCanvasZoom / 100 + gCanvasOffsetX);
                fc.v = static_cast<short>(SInt32(fb.y + fb.h/2) * gCanvasZoom / 100 + gCanvasOffsetY);
                Frame* origFrameParent = f->parent;
                Frame* newFrameParent = DeepestFrameAtExcl(fc, gSelectedFrames);
                if (newFrameParent != origFrameParent) {
                    auto owned = ExtractFrame(f);
                    if (owned) {
                        if (newFrameParent) {
                            owned->parent = newFrameParent;
                            newFrameParent->childOrder.push_back({ true, (int)newFrameParent->childFrames.size() });
                            newFrameParent->childFrames.push_back(std::move(owned));
                        } else {
                            owned->parent = nullptr;
                            // Find where the original parent sits in gDocument->frames and
                            // insert before it, so extracted frames appear above the parent.
                            int insertPos = (int)gDocument->frames.size();
                            if (origFrameParent) {
                                for (int i = 0; i < (int)gDocument->frames.size(); ++i) {
                                    if (gDocument->frames[i].get() == origFrameParent) {
                                        insertPos = i; break;
                                    }
                                }
                            }
                            // Update rootChildOrder: insert before the parent's entry.
                            int rootOrderPos = (int)gDocument->rootChildOrder.size();
                            for (int oi = 0; oi < (int)gDocument->rootChildOrder.size(); ++oi)
                                if (gDocument->rootChildOrder[oi].isFrame && gDocument->rootChildOrder[oi].idx == insertPos)
                                    { rootOrderPos = oi; break; }
                            RootOrderInsert(rootOrderPos, true, insertPos);
                            gDocument->frames.insert(gDocument->frames.begin() + insertPos,
                                                     std::move(owned));
                        }
                    }
                }
            }
            if (!gSelectedFrames.empty())
                gSelectedFrame = gSelectedFrames.back();

            Rect portRect; GetWindowPortBounds(win, &portRect); InvalWindowRect(win, &portRect);
            RefreshLayersPanel();
            RefreshInspector();
            return;
        }
        Bounds2 finalB = hitShape ? hitShape->bounds : hitFrame->bounds;
        Point center;
        center.h = static_cast<short>(SInt32(finalB.x + finalB.w / 2) * gCanvasZoom / 100 + gCanvasOffsetX);
        center.v = static_cast<short>(SInt32(finalB.y + finalB.h / 2) * gCanvasZoom / 100 + gCanvasOffsetY);

        if (hitShape) {
            Frame* newParent = DeepestFrameAt(center);

            if (isLayoutDrag && newParent == hitFrame && pushedUndo) {
                // Reorder by mouse-position insertion: remove the dragged shape's ChildRef,
                // find where the mouse falls among remaining laid-out items, re-insert there.
                // This preserves relative order of non-dragged items and avoids false
                // reordering from the sort-by-position approach.
                bool isHoriz = (hitFrame->layoutMode == LayoutMode::Horizontal);
                bool isWrap  = hitFrame->layoutWrap;
                SInt32 mCanX = (SInt32(currPt.h) - gCanvasOffsetX) * 100 / SInt32(gCanvasZoom);
                SInt32 mCanY = (SInt32(currPt.v) - gCanvasOffsetY) * 100 / SInt32(gCanvasZoom);
                if (!hitFrame->childOrder.empty()) {
                    int dragSrcIdx = -1;
                    ChildRef draggedCR = { false, 0 };
                    for (int i = 0; i < (int)hitFrame->childOrder.size(); ++i) {
                        const ChildRef& cr = hitFrame->childOrder[i];
                        if (!cr.isFrame && hitFrame->children[cr.idx].get() == hitShape) {
                            draggedCR = cr; dragSrcIdx = i; break;
                        }
                    }
                    if (dragSrcIdx >= 0) {
                        hitFrame->childOrder.erase(hitFrame->childOrder.begin() + dragSrcIdx);
                        int insertPos = 0;
                        for (int i = 0; i < (int)hitFrame->childOrder.size(); ++i) {
                            const ChildRef& cr = hitFrame->childOrder[i];
                            const Bounds2& bnd = cr.isFrame
                                ? hitFrame->childFrames[cr.idx]->bounds
                                : hitFrame->children[cr.idx]->bounds;
                            SInt32 cx = bnd.x + bnd.w / 2;
                            SInt32 cy = bnd.y + bnd.h / 2;
                            if (isWrap) {
                                SInt32 cross   = isHoriz ? cy : cx;
                                SInt32 primary = isHoriz ? cx : cy;
                                SInt32 mCross  = isHoriz ? mCanY : mCanX;
                                SInt32 mPri    = isHoriz ? mCanX : mCanY;
                                if (cross < mCross || (cross == mCross && primary < mPri))
                                    insertPos = i + 1;
                            } else {
                                if ((isHoriz ? cx : cy) < (isHoriz ? mCanX : mCanY))
                                    insertPos = i + 1;
                            }
                        }
                        hitFrame->childOrder.insert(hitFrame->childOrder.begin() + insertPos, draggedCR);
                    }
                }
            } else if (newParent != origParent) {
                auto owned = ExtractShape(hitShape, origParent);
                if (owned) {
                    if (newParent) {
                        newParent->childOrder.push_back({ false, (int)newParent->children.size() });
                        newParent->children.push_back(std::move(owned));
                    } else {
                        RootOrderInsert(0, false, (int)gDocument->rootShapes.size());
                        gDocument->rootShapes.push_back(std::move(owned));
                    }
                    gSelectedFrame = newParent;
                }
            }
        } else {
            Frame* newParent = DeepestFrameAt(center, hitFrame);
            if (isLayoutFrameDrag && newParent == hitFrame->parent && hitFrame->parent && pushedUndo) {
                // Reorder by mouse-position insertion (same logic as shape case above).
                Frame* layoutParent = hitFrame->parent;
                bool isHoriz = (layoutParent->layoutMode == LayoutMode::Horizontal);
                bool isWrap  = layoutParent->layoutWrap;
                SInt32 mCanX = (SInt32(currPt.h) - gCanvasOffsetX) * 100 / SInt32(gCanvasZoom);
                SInt32 mCanY = (SInt32(currPt.v) - gCanvasOffsetY) * 100 / SInt32(gCanvasZoom);
                if (!layoutParent->childOrder.empty()) {
                    int dragSrcIdx = -1;
                    ChildRef draggedCR = { true, 0 };
                    for (int i = 0; i < (int)layoutParent->childOrder.size(); ++i) {
                        const ChildRef& cr = layoutParent->childOrder[i];
                        if (cr.isFrame && layoutParent->childFrames[cr.idx].get() == hitFrame) {
                            draggedCR = cr; dragSrcIdx = i; break;
                        }
                    }
                    if (dragSrcIdx >= 0) {
                        layoutParent->childOrder.erase(layoutParent->childOrder.begin() + dragSrcIdx);
                        int insertPos = 0;
                        for (int i = 0; i < (int)layoutParent->childOrder.size(); ++i) {
                            const ChildRef& cr = layoutParent->childOrder[i];
                            const Bounds2& bnd = cr.isFrame
                                ? layoutParent->childFrames[cr.idx]->bounds
                                : layoutParent->children[cr.idx]->bounds;
                            SInt32 cx = bnd.x + bnd.w / 2;
                            SInt32 cy = bnd.y + bnd.h / 2;
                            if (isWrap) {
                                SInt32 cross   = isHoriz ? cy : cx;
                                SInt32 primary = isHoriz ? cx : cy;
                                SInt32 mCross  = isHoriz ? mCanY : mCanX;
                                SInt32 mPri    = isHoriz ? mCanX : mCanY;
                                if (cross < mCross || (cross == mCross && primary < mPri))
                                    insertPos = i + 1;
                            } else {
                                if ((isHoriz ? cx : cy) < (isHoriz ? mCanX : mCanY))
                                    insertPos = i + 1;
                            }
                        }
                        layoutParent->childOrder.insert(layoutParent->childOrder.begin() + insertPos, draggedCR);
                    }
                }
            } else if (newParent != hitFrame->parent) {
                auto owned = ExtractFrame(hitFrame);
                if (owned) {
                    Frame* raw = owned.get();
                    if (newParent) {
                        owned->parent = newParent;
                        newParent->childOrder.push_back({ true, (int)newParent->childFrames.size() });
                        newParent->childFrames.push_back(std::move(owned));
                    } else {
                        owned->parent = nullptr;
                        RootOrderInsert(0, true, (int)gDocument->frames.size());
                        gDocument->frames.push_back(std::move(owned));
                    }
                    gSelectedFrame = raw;
                }
            }
        }
    } else {
        // No hit — rubber-band marquee selection (Finder-style).
        // Use XOR pen to draw/erase the selection rect without redrawing canvas.
        Point anchorPt  = pt;
        Point prevEndPt = pt;
        bool  tracking  = false;
        bool  didDrag   = false;

        Pattern blkPat; memset(&blkPat, 0xFF, sizeof(blkPat));
        PenMode(patXor);
        PenPat(&blkPat);
        PenSize(1, 1);

        while (Button()) {
            Point loopPt; GetMouse(&loopPt);
            if (!tracking || loopPt.h != prevEndPt.h || loopPt.v != prevEndPt.v) {
                if (tracking) {
                    Rect prev;
                    prev.top    = static_cast<short>(std::min((int)anchorPt.v, (int)prevEndPt.v));
                    prev.left   = static_cast<short>(std::min((int)anchorPt.h, (int)prevEndPt.h));
                    prev.bottom = static_cast<short>(std::max((int)anchorPt.v, (int)prevEndPt.v) + 1);
                    prev.right  = static_cast<short>(std::max((int)anchorPt.h, (int)prevEndPt.h) + 1);
                    FrameRect(&prev);
                }
                Rect band;
                band.top    = static_cast<short>(std::min((int)anchorPt.v, (int)loopPt.v));
                band.left   = static_cast<short>(std::min((int)anchorPt.h, (int)loopPt.h));
                band.bottom = static_cast<short>(std::max((int)anchorPt.v, (int)loopPt.v) + 1);
                band.right  = static_cast<short>(std::max((int)anchorPt.h, (int)loopPt.h) + 1);
                FrameRect(&band);
                tracking = true;
                didDrag  = true;
                prevEndPt = loopPt;
            }
        }

        // Erase final rubber-band
        if (tracking) {
            Rect finalBand;
            finalBand.top    = static_cast<short>(std::min((int)anchorPt.v, (int)prevEndPt.v));
            finalBand.left   = static_cast<short>(std::min((int)anchorPt.h, (int)prevEndPt.h));
            finalBand.bottom = static_cast<short>(std::max((int)anchorPt.v, (int)prevEndPt.v) + 1);
            finalBand.right  = static_cast<short>(std::max((int)anchorPt.h, (int)prevEndPt.h) + 1);
            FrameRect(&finalBand);
        }
        PenNormal();

        if (didDrag) {
            // Convert rubber-band corners to canvas space
            Point cAnchor = ScreenToCanvas(anchorPt);
            Point cEnd    = ScreenToCanvas(prevEndPt);
            SInt32 selL = std::min((SInt32)cAnchor.h, (SInt32)cEnd.h);
            SInt32 selT = std::min((SInt32)cAnchor.v, (SInt32)cEnd.v);
            SInt32 selR = std::max((SInt32)cAnchor.h, (SInt32)cEnd.h);
            SInt32 selB = std::max((SInt32)cAnchor.v, (SInt32)cEnd.v);

            gSelectedShapes.clear();
            gSelectedFrames.clear();
            gSelectedShape = nullptr;
            gSelectedFrame = nullptr;

            // Collect shapes (recursively through nested frames) that intersect the band
            std::vector<BandShape> band;
            for (auto& frm : gDocument->frames)
                CollectBandShapes(frm.get(), selL, selT, selR, selB, band);
            for (auto& sp : gDocument->rootShapes) {
                if (!sp->visible) continue;
                const Bounds2& bnd = sp->bounds;
                if (bnd.x < selR && (bnd.x + bnd.w) > selL &&
                    bnd.y < selB && (bnd.y + bnd.h) > selT)
                    band.push_back({ sp.get(), nullptr });
            }

            // Collect frames: fully-contained frames selected directly;
            // partially-overlapped frames recurse into children.
            std::vector<Frame*> bandFrames;
            for (auto& frm : gDocument->frames)
                CollectAllBandFrames(frm.get(), selL, selT, selR, selB, bandFrames);

            // Remove shapes whose parent frame is already captured as a whole frame.
            {
                std::vector<BandShape> filtered;
                for (auto& bs : band) {
                    bool parentInBand = bs.parent &&
                        std::find(bandFrames.begin(), bandFrames.end(), bs.parent) != bandFrames.end();
                    if (!parentInBand) filtered.push_back(bs);
                }
                band = std::move(filtered);
            }

            if (!band.empty() && !bandFrames.empty()) {
                // Mixed: loose shapes + whole frames both selected
                for (auto& bs : band) gSelectedShapes.push_back(bs.shape);
                gSelectedShape  = gSelectedShapes.back();
                gSelectedFrames = bandFrames;
                gSelectedFrame  = gSelectedFrames.back();
            } else if (band.size() == 1) {
                gSelectedShape = band[0].shape;
                gSelectedFrame = band[0].parent;
            } else if (band.size() > 1) {
                for (auto& bs : band) gSelectedShapes.push_back(bs.shape);
                gSelectedShape = gSelectedShapes.back();
            } else if (bandFrames.size() == 1) {
                gSelectedFrame = bandFrames[0];
            } else if (bandFrames.size() > 1) {
                gSelectedFrames = bandFrames;
                gSelectedFrame  = gSelectedFrames.back();
            }

            RefreshLayersPanel();
            RefreshInspector();
        }
    }

    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    InvalWindowRect(win, &portRect);
}

// --------------------------------------------------------------------------
// Text placement: click-to-place with inline TENew popup
// --------------------------------------------------------------------------

static void HandleTextPlace(WindowRef win, Point localPt, Point globalPt) {
    std::string text = ShowRenameDialog("", globalPt);
    if (text.empty()) return;

    PushUndo();
    Point cPt = ScreenToCanvas(localPt);

    auto t       = std::make_unique<TextShape>();
    t->name      = "Text " + istr(gNextTextNum++);
    t->text      = text;
    t->fontSize  = 14;
    t->fontFace  = 0;
    SInt32 tw = static_cast<SInt32>(text.size()) * 7 + 16;
    if (tw < 40) tw = 40;
    t->bounds    = { cPt.h, cPt.v, tw, 20 };
    t->fillColor = { 0, 0, 0 };  // black text color
    t->hasFill   = true;
    t->hasStroke = false;

    Frame* target  = DeepestFrameAt(localPt);
    gSelectedShape = t.get();
    gSelectedFrame = target;
    if (target) {
        target->childOrder.push_back({ false, (int)target->children.size() });
        target->children.push_back(std::move(t));
    } else {
        RootOrderInsert(0, false, (int)gDocument->rootShapes.size());
        gDocument->rootShapes.push_back(std::move(t));
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

    // Text tool: click-to-place (no rubber-band)
    if (gActiveTool == Tool::Text) {
        HandleTextPlace(win, startPt, startGlobal);
        return;
    }

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
                parent->childOrder.push_back({ true, (int)parent->childFrames.size() });
                parent->childFrames.push_back(std::move(f));
            } else {
                f->parent = nullptr;
                RootOrderInsert(0, true, (int)gDocument->frames.size());
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

            if (target) {
                target->childOrder.push_back({ false, (int)target->children.size() });
                target->children.push_back(std::move(shape));
            } else {
                RootOrderInsert(0, false, (int)gDocument->rootShapes.size());
                gDocument->rootShapes.push_back(std::move(shape));
            }
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
    Rect c;
    if (win == gLayersWindow || win == gInspectorWindow)
        c = { 80, 130, 2000, 600 };   // panels: small minimum, generous maximum
    else
        c = { 300, 400, 2000, 4000 }; // main canvas
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
    // Save current window context before creating new window
    for (auto& ctx : sDocWindows)
        if (ctx->win == gMainWindow) { SaveGlobalsToCtx(*ctx); break; }

    auto* doc = new Document();
    doc->name = "Untitled";
    auto frame = std::make_unique<Frame>();
    frame->name = "Frame 1";
    frame->bounds = { 40, 40, 390, 480 };
    frame->backgroundColor = { 0xFFFF, 0xFFFF, 0xFFFF };
    doc->frames.push_back(std::move(frame));
    doc->rootChildOrder.push_back({ true, 0 });

    WindowRef win = CreateDocumentWindow(doc);
    auto ctx = std::make_unique<DocCtx>();
    ctx->doc = doc;
    ctx->win = win;
    sDocWindows.push_back(std::move(ctx));
    LoadGlobalsFromCtx(*sDocWindows.back());
    sPasteParent = nullptr;
    UpdateWindowTitle();
    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
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
    f->hasStroke       = src->hasStroke;
    f->strokeColor     = src->strokeColor;
    f->strokeWidth     = src->strokeWidth;
    f->strokeAlign     = src->strokeAlign;
    f->visible         = src->visible;
    f->locked          = src->locked;
    f->clipContent     = src->clipContent;
    f->layoutMode           = src->layoutMode;
    f->layoutWrap           = src->layoutWrap;
    f->strokesInLayout      = src->strokesInLayout;
    f->canvasStackReverse   = src->canvasStackReverse;
    f->alignTextBaseline    = src->alignTextBaseline;
    f->layoutGap            = src->layoutGap;
    f->layoutCounterGap     = src->layoutCounterGap;
    f->layoutCounterGapAuto = src->layoutCounterGapAuto;
    f->paddingTop      = src->paddingTop;
    f->paddingRight    = src->paddingRight;
    f->paddingBottom   = src->paddingBottom;
    f->paddingLeft     = src->paddingLeft;
    f->primaryAlign    = src->primaryAlign;
    f->crossAlign      = src->crossAlign;
    f->widthSizing     = src->widthSizing;
    f->heightSizing    = src->heightSizing;
    f->parent          = newParent;
    for (const auto& s : src->children)
        f->children.push_back(s->Clone());
    for (const auto& cf : src->childFrames)
        f->childFrames.push_back(CloneFrame(cf.get(), f.get()));
    // Clone childOrder (indices are the same in the cloned vectors)
    for (const auto& cr : src->childOrder)
        f->childOrder.push_back(cr);
    return f;
}

static std::unique_ptr<Document> CloneDocument(const Document* src) {
    auto d = std::make_unique<Document>();
    d->name = src->name;
    for (const auto& s : src->rootShapes)
        d->rootShapes.push_back(s->Clone());
    for (const auto& f : src->frames)
        d->frames.push_back(CloneFrame(f.get(), nullptr));
    d->rootChildOrder = src->rootChildOrder;
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
    gSelectedShapes.clear();
    gSelectedFrames.clear();
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
    gSelectedShapes.clear();
    gSelectedFrames.clear();
    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
    RefreshLayersPanel();
    RefreshInspector();
}

void CopySelected() {
    sClipFrames.clear();
    sClipShapes.clear();
    sPasteOffset  = 0;
    sPasteParent  = nullptr;
    bool parentSet = false;

    // Track common parent of everything being copied; nullptr if items span multiple parents.
    auto mergeParent = [&](Frame* p) {
        if (!parentSet) { sPasteParent = p; parentSet = true; }
        else if (sPasteParent != p) sPasteParent = nullptr;
    };

    for (Shape* s : gSelectedShapes) {
        mergeParent(LocateShapeParent(s));
        sClipShapes.push_back(s->Clone());
    }
    for (Frame* f : gSelectedFrames) {
        mergeParent(f->parent);
        sClipFrames.push_back(CloneFrame(f, nullptr));
    }
    // Single select fallback
    if (sClipShapes.empty() && sClipFrames.empty()) {
        if (gSelectedShape) {
            mergeParent(LocateShapeParent(gSelectedShape));
            sClipShapes.push_back(gSelectedShape->Clone());
        } else if (gSelectedFrame) {
            mergeParent(gSelectedFrame->parent);
            sClipFrames.push_back(CloneFrame(gSelectedFrame, nullptr));
        }
    }
}

void PasteClipboard() {
    if (!gDocument) return;
    if (sClipShapes.empty() && sClipFrames.empty()) return;

    PushUndo();
    ++sPasteOffset;
    SInt32 off = SInt32(sPasteOffset) * 10;

    // Validate sPasteParent still lives in the document (may have been deleted since copy).
    Frame* pasteParent = sPasteParent;
    if (pasteParent) {
        bool found = false;
        std::vector<Frame*> stk;
        for (auto& f : gDocument->frames) stk.push_back(f.get());
        while (!stk.empty()) {
            Frame* f = stk.back(); stk.pop_back();
            if (f == pasteParent) { found = true; break; }
            for (auto& cf : f->childFrames) stk.push_back(cf.get());
        }
        if (!found) pasteParent = nullptr;
    }

    gSelectedShapes.clear();
    gSelectedFrames.clear();
    gSelectedShape = nullptr;
    gSelectedFrame = nullptr;

    for (const auto& src : sClipShapes) {
        auto copy = src->Clone();
        copy->name     = NextAvailableName(copy->name);
        copy->bounds.x = src->bounds.x + off;
        copy->bounds.y = src->bounds.y + off;
        Shape* raw = copy.get();
        if (pasteParent) {
            pasteParent->childOrder.push_back({ false, (int)pasteParent->children.size() });
            pasteParent->children.push_back(std::move(copy));
        } else {
            RootOrderInsert(0, false, (int)gDocument->rootShapes.size());
            gDocument->rootShapes.push_back(std::move(copy));
        }
        gSelectedShapes.push_back(raw);
        gSelectedShape = raw;
    }

    for (const auto& src : sClipFrames) {
        auto copy = CloneFrame(src.get(), pasteParent);
        copy->name = NextAvailableName(copy->name);
        MoveFrameTree(copy.get(), off, off);
        Frame* raw = copy.get();
        if (pasteParent) {
            pasteParent->childOrder.push_back({ true, (int)pasteParent->childFrames.size() });
            pasteParent->childFrames.push_back(std::move(copy));
        } else {
            RootOrderInsert(0, true, (int)gDocument->frames.size());
            gDocument->frames.push_back(std::move(copy));
        }
        gSelectedFrames.push_back(raw);
        gSelectedFrame = raw;
    }

    // If only shapes pasted, set gSelectedFrame to the parent for context.
    if (!gSelectedShapes.empty() && gSelectedFrames.empty())
        gSelectedFrame = pasteParent;

    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
    RefreshLayersPanel();
    RefreshInspector();
}

// Find the direct parent Frame of a shape by searching the whole document.
// Returns nullptr if the shape lives in gDocument->rootShapes.
static Frame* LocateShapeParent(Shape* s) {
    for (const auto& sp : gDocument->rootShapes)
        if (sp.get() == s) return nullptr;
    std::vector<Frame*> stack;
    for (auto& f : gDocument->frames) stack.push_back(f.get());
    while (!stack.empty()) {
        Frame* f = stack.back(); stack.pop_back();
        for (const auto& ch : f->children)
            if (ch.get() == s) return f;
        for (const auto& cf : f->childFrames)
            stack.push_back(cf.get());
    }
    return nullptr;
}

void DeleteSelected() {
    if (!gDocument) return;
    if (!gSelectedShape && !gSelectedFrame && gSelectedShapes.empty() && gSelectedFrames.empty()) return;
    PushUndo();
    bool changed = false;

    if (!gSelectedFrames.empty() && !gSelectedShapes.empty()) {
        // Mixed selection: delete frames first, then shapes (shapes may be in extracted frames,
        // in which case LocateShapeParent returns nullptr and ExtractShape is a no-op — that's fine).
        for (Frame* target : gSelectedFrames) {
            auto owned = ExtractFrame(target);
            if (owned) changed = true;
        }
        gSelectedFrames.clear();
        for (Shape* target : gSelectedShapes) {
            auto owned = ExtractShape(target, LocateShapeParent(target));
            if (owned) changed = true;
        }
        gSelectedShapes.clear();
        gSelectedShape  = nullptr;
        gSelectedFrame  = nullptr;
    } else if (gSelectedFrames.size() > 1) {
        for (Frame* target : gSelectedFrames) {
            auto owned = ExtractFrame(target);
            if (owned) changed = true;
        }
        gSelectedFrames.clear();
        gSelectedFrame  = nullptr;
        gSelectedShape  = nullptr;
    } else if (gSelectedShapes.size() > 1) {
        for (Shape* target : gSelectedShapes) {
            auto owned = ExtractShape(target, LocateShapeParent(target));
            if (owned) changed = true;
        }
        gSelectedShapes.clear();
        gSelectedShape = nullptr;
    } else if (gSelectedShape) {
        // Works for both in-frame children and floating rootShapes
        auto owned = ExtractShape(gSelectedShape, gSelectedFrame);
        if (owned) changed = true;
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

    if (menuID == kAppleMenuID) {
        if (menuItem == kAppleAbout) ShowAboutDialog();
    } else if (menuID == kFileMenuID) {
        switch (menuItem) {
            case kFileNew:
                NewDocument();
                RefreshLayersPanel();
                RefreshInspector();
                break;
            case kFileOpen: {
                // Save current context before showing open dialog
                for (auto& ctx : sDocWindows)
                    if (ctx->win == gMainWindow) { SaveGlobalsToCtx(*ctx); break; }
                Document* newDoc = nullptr;
                if (LoadDocument(newDoc)) {
                    if (newDoc->rootChildOrder.empty()) InitRootChildOrder(newDoc);
                    WindowRef win = CreateDocumentWindow(newDoc);
                    auto ctx = std::make_unique<DocCtx>();
                    ctx->doc = newDoc;
                    ctx->win = win;
                    ctx->nextFrameNum = static_cast<int>(newDoc->frames.size()) + 2;
                    sDocWindows.push_back(std::move(ctx));
                    LoadGlobalsFromCtx(*sDocWindows.back());
                    sPasteParent = nullptr;
                    UpdateWindowTitle();
                    Rect r; GetWindowPortBounds(gMainWindow, &r); InvalWindowRect(gMainWindow, &r);
                    RefreshLayersPanel();
                    RefreshInspector();
                } else {
                    // User cancelled — restore previous context
                    for (auto& c : sDocWindows)
                        if (c->win == gMainWindow) { LoadGlobalsFromCtx(*c); break; }
                }
                break;
            }
            case kFileClose:
                CloseDocumentWindow(gMainWindow);
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
    UpdateMenuState();
    HiliteMenu(0);
}
