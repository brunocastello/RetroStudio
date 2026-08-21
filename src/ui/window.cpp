#include "window.h"
#include "LayersPanel.h"
#include "InspectorPanel.h"
#include "RenameDialog.h"
#include "../export/DocumentSerializer.h"
#include "../canvas/AutoLayout.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <map>

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

// Non-null while a text shape is being edited in place (EditTextInPlace) —
// DrawShape skips rendering it normally so the live TextEdit overlay shows
// through instead.
static TextShape* gEditingTextShape = nullptr;
// The current document window's local content rect, refreshed at the top of
// DrawWindowContent and EditTextInPlace -- the only two places that call
// GetWindowPortBounds with the real, actual WindowRef in hand. Rotated-text
// clipping reads this instead of re-deriving a WindowRef from GetPort()'s
// GrafPtr (a cast that assumes a classic-Mac pointer equivalence Carbon
// doesn't actually guarantee) or reading portRect directly off the raw
// CGrafPort struct (assumes an unverified struct layout) -- both were tried
// and both silently produced a wrong-sized rect at least once.
static Rect gActivePortBounds = {0, 0, 0, 0};

// ---- Cursor management -------------------------------------------------------
// Helper: compute 8-connected dilation mask from a 16-row bitmap
static void ComputeCursorMask(Cursor& cur, const unsigned short kData[16]) {
    for (int i = 0; i < 16; ++i) {
        unsigned short d  = kData[i];
        unsigned short dN = (i > 0)  ? kData[i-1] : 0u;
        unsigned short dS = (i < 15) ? kData[i+1] : 0u;
        unsigned short m  = d | dN | dS
            | static_cast<unsigned short>(d  << 1) | static_cast<unsigned short>(d  >> 1)
            | static_cast<unsigned short>(dN << 1) | static_cast<unsigned short>(dN >> 1)
            | static_cast<unsigned short>(dS << 1) | static_cast<unsigned short>(dS >> 1);
        cur.data[i] = static_cast<short>(d);
        cur.mask[i] = static_cast<short>(m);
    }
}

// Handle geometry shared by cursor selection and hit-testing.
static const short kHandleHW   = 4;   // handle square half-width (grab target)
static const short kRotateZone = 18;  // extra px beyond handle for rotate zone (was 10 — too thin to regrab)

// ---- Direction-accurate resize cursors: EW, NS, and both diagonals ----
// (double-headed arrows; source bitmaps below, NWSE derived from NESW by mirroring)
static const unsigned short kResizeNESW[16] = {
    0x000F, 0x0001, 0x0005, 0x0009, 0x0010, 0x0020,
    0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x4800,
    0x5000, 0x4000, 0x7800, 0x0000
};
static const unsigned short kResizeNWSE[16] = {
    0xF000, 0x8000, 0xA000, 0x9000, 0x0800, 0x0400,
    0x0200, 0x0100, 0x0080, 0x0040, 0x0020, 0x0012,
    0x000A, 0x0002, 0x001E, 0x0000
};
static const unsigned short kResizeEW[16] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1002,
    0x3003, 0x7FFF, 0x7FFF, 0x3003, 0x1002, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000
};
static const unsigned short kResizeNS[16] = {
    0x0000, 0x0180, 0x03C0, 0x07E0, 0x0180, 0x0180,
    0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0180, 0x07E0, 0x03C0
};
// Indexed by direction family (bucket % 4): 0=E/W, 1=SE/NW, 2=S/N, 3=SW/NE
static const unsigned short* const kResizeFamilies[4] = {
    kResizeEW, kResizeNWSE, kResizeNS, kResizeNESW
};
static bool   sResizeCursorInited[4] = { false, false, false, false };
static Cursor sResizeCursor[4];

// ---- Direction-accurate rotate cursors: 8 buckets, 45 deg apart ----
// bucket 0=E, 1=SE, 2=S, 3=SW, 4=W, 5=NW, 6=N, 7=NE (screen angle = bucket*45 deg,
// where 0 deg = +x/east, 90 deg = +y/south — matches atan2(dy,dx) used for rotation drag)
static const unsigned short kRotateData[8][16] = {
    /*E */ { 0x0000, 0x0040, 0x00E0, 0x00F0, 0x0018, 0x0008, 0x0008, 0x0008,
              0x0008, 0x0008, 0x0018, 0x00F0, 0x00E0, 0x0040, 0x0000, 0x0000 },
    /*SE*/ { 0x0000, 0x0000, 0x0000, 0x0000, 0x0008, 0x0018, 0x0008, 0x0008,
              0x0008, 0x0008, 0x0018, 0x0430, 0x0FE0, 0x0000, 0x0000, 0x0000 },
    /*S */ { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
              0x3018, 0x701C, 0x3018, 0x1830, 0x0FE0, 0x0000, 0x0000, 0x0000 },
    /*SW*/ { 0x0000, 0x0000, 0x0000, 0x0000, 0x2000, 0x3000, 0x2000, 0x2000,
              0x2000, 0x2000, 0x3000, 0x1840, 0x0FE0, 0x0000, 0x0000, 0x0000 },
    /*W */ { 0x0000, 0x0400, 0x0E00, 0x1E00, 0x3000, 0x2000, 0x2000, 0x2000,
              0x2000, 0x2000, 0x3000, 0x1E00, 0x0E00, 0x0400, 0x0000, 0x0000 },
    /*NW*/ { 0x0000, 0x0000, 0x0FE0, 0x1840, 0x3000, 0x2000, 0x2000, 0x2000,
              0x2000, 0x3000, 0x2000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
    /*N */ { 0x0000, 0x0000, 0x0FE0, 0x1830, 0x3018, 0x701C, 0x3018, 0x0000,
              0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
    /*NE*/ { 0x0000, 0x0000, 0x0FE0, 0x0430, 0x0018, 0x0008, 0x0008, 0x0008,
              0x0008, 0x0018, 0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
};
static bool   sRotateCursorInited[8] = { false,false,false,false,false,false,false,false };
static Cursor sRotateCursor[8];

// Maps a screen-space angle (deg, atan2 convention) to one of 8 cursor buckets 45 deg apart.
static int AngleToBucket(double angleDeg) {
    angleDeg = std::fmod(angleDeg, 360.0);
    if (angleDeg < 0) angleDeg += 360.0;
    return static_cast<int>(angleDeg / 45.0 + 0.5) % 8;
}

// handleIdx: 0=TL,1=N,2=TR,3=E,4=BR,5=S,6=BL,7=W (unrotated). shapeRotationDeg
// is the selected shape's current rotation (0 for frames/unrotated shapes).
static int HandleBucket(int handleIdx, double shapeRotationDeg) {
    double baseAngle = handleIdx * 45.0 + 225.0;
    return AngleToBucket(baseAngle + shapeRotationDeg);
}

static Cursor* GetResizeCursor(int bucket) {
    int family = bucket % 4;
    if (!sResizeCursorInited[family]) {
        ComputeCursorMask(sResizeCursor[family], kResizeFamilies[family]);
        sResizeCursor[family].hotSpot.v = 7;
        sResizeCursor[family].hotSpot.h = 7;
        sResizeCursorInited[family] = true;
    }
    return &sResizeCursor[family];
}

static Cursor* GetRotateCursor(int bucket) {
    bucket = ((bucket % 8) + 8) % 8;
    if (!sRotateCursorInited[bucket]) {
        ComputeCursorMask(sRotateCursor[bucket], kRotateData[bucket]);
        sRotateCursor[bucket].hotSpot.v = 7;
        sRotateCursor[bucket].hotSpot.h = 7;
        sRotateCursorInited[bucket] = true;
    }
    return &sRotateCursor[bucket];
}

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

// ---- Ancestor rotation chain -------------------------------------------------
// A frame's rotation must carry its whole subtree along as a rigid body. Bounds
// are always stored "local" (as if every ancestor frame had rotation 0); a
// RotStep records one ancestor's own rotation + its screen-space center (both
// computed from ITS local bounds, i.e. before that ancestor's own step is
// applied). A RotChain is the ordered list of such steps from the immediate
// parent's own rotation (applied first) up through the root (applied last) —
// exactly mirroring nested "rotate around my own center" transforms.
struct RotStep { double angleDeg; double cx, cy; };
using RotChain = std::vector<RotStep>;

static void ApplyRotChain(const RotChain& chain, double x, double y, double& ox, double& oy) {
    ox = x; oy = y;
    for (const auto& step : chain) {
        if (step.angleDeg == 0.0) continue;
        double rad = step.angleDeg * 3.14159265358979323846 / 180.0;
        double ca = std::cos(rad), sa = std::sin(rad);
        double dx = ox - step.cx, dy = oy - step.cy;
        ox = step.cx + dx*ca - dy*sa;
        oy = step.cy + dx*sa + dy*ca;
    }
}

// Inverse of ApplyRotChain: maps a final screen point back into the local space
// the chain was built from (steps undone in reverse order).
static void ApplyRotChainInverse(const RotChain& chain, double x, double y, double& ox, double& oy) {
    ox = x; oy = y;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (it->angleDeg == 0.0) continue;
        double rad = -it->angleDeg * 3.14159265358979323846 / 180.0;
        double ca = std::cos(rad), sa = std::sin(rad);
        double dx = ox - it->cx, dy = oy - it->cy;
        ox = it->cx + dx*ca - dy*sa;
        oy = it->cy + dx*sa + dy*ca;
    }
}

static Point ToQDPoint(double x, double y) {
    Point p;
    p.h = static_cast<short>(x + (x >= 0 ? 0.5 : -0.5));
    p.v = static_cast<short>(y + (y >= 0 ? 0.5 : -0.5));
    return p;
}

// Rotate 4 rect corners around their own center by angleDeg, then carry them
// through `ambient` (the enclosing rotated frames, if any), and draw as a
// filled/stroked polygon. angleDeg is clockwise in screen coords (Y-down).
static void DrawRotatedRect(const Rect& r, short angleDeg,
                             bool doFill, const RGBColor& fillC,
                             bool doStroke, const RGBColor& strokeC, short sw,
                             const RotChain& ambient = {}) {
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
        double px = cx + lx[i]*cosA - ly[i]*sinA;
        double py = cy + lx[i]*sinA + ly[i]*cosA;
        double fx, fy;
        ApplyRotChain(ambient, px, py, fx, fy);
        pts[i] = ToQDPoint(fx, fy);
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

// Approximate a rotated ellipse as a 36-gon polygon, then carry it through `ambient`.
static void DrawRotatedEllipse(const Rect& r, short angleDeg,
                                bool doFill, const RGBColor& fillC,
                                bool doStroke, const RGBColor& strokeC, short sw,
                                const RotChain& ambient = {}) {
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
        double px  = cx + ex*cosR - ey*sinR;
        double py  = cy + ex*sinR + ey*cosR;
        double fx, fy;
        ApplyRotChain(ambient, px, py, fx, fy);
        Point p = ToQDPoint(fx, fy);
        if (i == 0) MoveTo(p.h, p.v); else LineTo(p.h, p.v);
    }
    ClosePoly();
    if (doFill)   { RGBColor c = fillC;   RGBForeColor(&c); PaintPoly(poly); }
    if (doStroke) { RGBColor c = strokeC; RGBForeColor(&c); PenSize(sw,sw); FramePoly(poly); PenSize(1,1); }
    KillPoly(poly);
}

// Returns true if screen point `pt` is inside `bounds` rotated by `angleDeg`
// clockwise, after first un-rotating `pt` through `ambient` (enclosing rotated
// frames, if any) so it lands in the same local space `bounds` is expressed in.
static bool HitTestRotated(const Bounds2& bounds, short angleDeg, Point pt,
                            const RotChain& ambient = {}) {
    double lx0, ly0;
    ApplyRotChainInverse(ambient, pt.h, pt.v, lx0, ly0);
    Rect r = CanvasRect(bounds);
    double cx = (r.left + r.right)  * 0.5;
    double cy = (r.top  + r.bottom) * 0.5;
    double hw = (r.right  - r.left) * 0.5;
    double hh = (r.bottom - r.top)  * 0.5;
    double rad = -angleDeg * 3.14159265358979323846 / 180.0;  // inverse rotation
    double dx = lx0 - cx, dy = ly0 - cy;
    double rx = dx * std::cos(rad) - dy * std::sin(rad);
    double ry = dx * std::sin(rad) + dy * std::cos(rad);
    return (rx >= -hw && rx <= hw && ry >= -hh && ry <= hh);
}

// Axis-aligned bounds check in local space, after un-rotating `pt` through `ambient`.
static bool PtInLocalRect(const Bounds2& bounds, Point pt, const RotChain& ambient) {
    if (ambient.empty()) {
        Rect r = CanvasRect(bounds);
        Point p = pt;
        return PtInRect(p, &r);
    }
    double lx, ly;
    ApplyRotChainInverse(ambient, pt.h, pt.v, lx, ly);
    Rect r = CanvasRect(bounds);
    Point p = ToQDPoint(lx, ly);
    return PtInRect(p, &r);
}

// Traces a (possibly per-corner) rounded-rect boundary as a closed point path:
// TL arc -> top edge -> TR arc -> right edge -> BR arc -> bottom edge -> BL arc
// -> left edge -> (back to start). Radii of 0 collapse an arc to its single
// corner point, so this also covers the plain sharp-corner case. Each point is
// rotated by the shape/frame's own rotation around its own local center, then
// carried through `ambient` (enclosing rotated frames, if any).
static std::vector<Point> TraceRoundedRectPoints(const Rect& r, short tl, short tr, short br, short bl,
                                                   double angleDeg, const RotChain& ambient) {
    short x = r.left, y = r.top;
    short w = static_cast<short>(r.right - r.left);
    short h = static_cast<short>(r.bottom - r.top);
    short maxR = static_cast<short>((w < h ? w : h) / 2);
    if (tl > maxR) tl = maxR; if (tr > maxR) tr = maxR;
    if (br > maxR) br = maxR; if (bl > maxR) bl = maxR;
    if (tl < 0) tl = 0; if (tr < 0) tr = 0; if (br < 0) br = 0; if (bl < 0) bl = 0;

    double cx0 = (r.left + r.right) * 0.5, cy0 = (r.top + r.bottom) * 0.5;
    double rad = angleDeg * 3.14159265358979323846 / 180.0;
    double cosA = std::cos(rad), sinA = std::sin(rad);

    std::vector<Point> out;
    auto addPoint = [&](double lx, double ly) {
        double dx = lx - cx0, dy = ly - cy0;
        double ox = cx0 + dx*cosA - dy*sinA;
        double oy = cy0 + dx*sinA + dy*cosA;
        double fx, fy;
        ApplyRotChain(ambient, ox, oy, fx, fy);
        out.push_back(ToQDPoint(fx, fy));
    };
    auto addArc = [&](double ccx, double ccy, double radius, double a0Deg, double a1Deg) {
        if (radius <= 0) { addPoint(ccx, ccy); return; }
        const int steps = 8;
        for (int i = 0; i <= steps; ++i) {
            double a = (a0Deg + (a1Deg - a0Deg) * i / steps) * 3.14159265358979323846 / 180.0;
            addPoint(ccx + radius*std::cos(a), ccy + radius*std::sin(a));
        }
    };

    addArc(x+tl,   y+tl,   tl, 180, 270);
    addArc(x+w-tr, y+tr,   tr, 270, 360);
    addArc(x+w-br, y+h-br, br, 0,   90);
    addArc(x+bl,   y+h-bl, bl, 90,  180);
    return out;
}

// Traces `pts` via MoveTo/LineTo (closing back to the first point) — shared by
// callers that need the same path both as a fillable/strokeable PolyHandle and
// as an OpenRgn/CloseRgn clip region.
static void TracePointPath(const std::vector<Point>& pts) {
    if (pts.empty()) return;
    MoveTo(pts[0].h, pts[0].v);
    for (size_t i = 1; i < pts.size(); ++i) LineTo(pts[i].h, pts[i].v);
    LineTo(pts[0].h, pts[0].v);
}

// Per-TextShape cache of its upright-rendered glyph pixels, used by
// DrawShape's kText rotation path. What the glyphs look like only depends
// on text/font/color/box-size — not on rotation angle — so while a text
// shape (or its parent frame) is being live-dragged around its own
// rotation, the angle changes every frame but the cache key doesn't,
// meaning the expensive multi-pass GetCPixel/SetCPixel capture only runs
// once, not on every mouse-move. This is what makes live rotation cheap
// enough to run during an active drag instead of only on the settled
// redraw after mouse-up.
struct TextGlyphCacheKey {
    std::string text;
    short fontID = 0, size = 0, face = 0, lineH = 0, lsx = 0, align = 0;
    bool hasFill = false, hasStroke = false;
    RGBColor fillColor{}, strokeColor{};
    short w = 0, h = 0;
    bool operator==(const TextGlyphCacheKey& o) const {
        return text == o.text && fontID == o.fontID && size == o.size && face == o.face &&
               lineH == o.lineH && lsx == o.lsx && align == o.align &&
               hasFill == o.hasFill && hasStroke == o.hasStroke && w == o.w && h == o.h &&
               fillColor.red == o.fillColor.red && fillColor.green == o.fillColor.green &&
               fillColor.blue == o.fillColor.blue &&
               strokeColor.red == o.strokeColor.red && strokeColor.green == o.strokeColor.green &&
               strokeColor.blue == o.strokeColor.blue;
    }
};
struct TextGlyphCacheEntry {
    TextGlyphCacheKey key;
    std::vector<RGBColor> pixels;  // key.w * key.h
    // Precomputed once alongside `pixels`: true where the captured glyph
    // pixel actually differs from what was underneath it (real ink), false
    // where it's just background repeated. The inverse-map repaint skips
    // SetCPixel entirely on false entries — for ordinary text, most of the
    // bounding box is background, so this cuts the per-frame Toolbox call
    // count (the actual cost driver during a live drag) well below the
    // full box area instead of writing every pixel in it every frame.
    std::vector<bool> ink;
};
// Keyed by raw TextShape pointer. A deleted shape's entry is simply never
// looked up again; if its memory address is later reused by an unrelated
// TextShape with byte-identical cache-key fields, that new shape could in
// theory reuse a stale cached bitmap — a real but extremely low-probability
// edge case, not worth a full deletion-hook lifecycle for right now.
static std::map<const TextShape*, TextGlyphCacheEntry> gTextGlyphCache;

// Direct pixel-buffer access to the CURRENT PORT's own onscreen pixmap
// (never a GWorld) for the text-rotation hot path. SetCPixel is a full
// QuickDraw Toolbox trap call per pixel; called thousands of times per
// mouse-move during a live drag, the call overhead itself is what causes
// visible flicker/lag, independent of how many of those pixels are
// actually "ink" (see the `ink` mask above). CGrafPort exposes its
// PixMapHandle directly as `portPixMap`, and PixMap directly exposes
// `baseAddr`/`rowBytes`/`pixelSize` — confirmed against this toolchain's
// actual Multiversal header (not assumed), same reinterpret_cast-across-
// the-Carbon-opacity-boundary pattern already used for SetGWorld elsewhere
// in this codebase. Handles only the common 32-bit-depth case; anything
// else falls back to SetCPixel, so this is a pure speed-up with no
// correctness regression on other depths.
struct FastPixelWriter {
    Ptr base = nullptr;
    SInt32 rowBytes = 0;
    short pixelSize = 0;
    Rect bounds = {0, 0, 0, 0};
    bool Ready() const { return base != nullptr && (pixelSize == 32 || pixelSize == 16); }
    void Set(short h, short v, const RGBColor& c) const {
        SInt32 lx = h - bounds.left, ly = v - bounds.top;
        if (lx < 0 || ly < 0 || lx >= (bounds.right - bounds.left) || ly >= (bounds.bottom - bounds.top)) return;
        UInt8* row = reinterpret_cast<UInt8*>(base) + static_cast<SInt32>(ly) * rowBytes;
        if (pixelSize == 32) {
            UInt8* p = row + lx * 4;
            p[1] = static_cast<UInt8>(c.red   >> 8);
            p[2] = static_cast<UInt8>(c.green >> 8);
            p[3] = static_cast<UInt8>(c.blue  >> 8);
        } else {
            // 16-bit QuickDraw pixel: 1 unused bit + 5-5-5 RGB, stored as a
            // single big-endian 16-bit word — this build targets PowerPC
            // (big-endian), so a direct UInt16 store already lands in the
            // right byte order with no manual swap needed.
            UInt16 r5 = static_cast<UInt16>(c.red   >> 11);
            UInt16 g5 = static_cast<UInt16>(c.green >> 11);
            UInt16 b5 = static_cast<UInt16>(c.blue  >> 11);
            UInt16 pixel = static_cast<UInt16>((r5 << 10) | (g5 << 5) | b5);
            *reinterpret_cast<UInt16*>(row + lx * 2) = pixel;
        }
    }
    // Mirrors Set(): reads a pixel back out of the same raw buffer instead
    // of via GetCPixel. Needed for the text-glyph capture path too, not
    // just the rotated repaint — during a live resize the box dimensions
    // change every frame, which invalidates the glyph cache every frame,
    // so capture (normally a rare cache-miss event) becomes a hot path
    // for the duration of the drag and needs to be just as fast.
    RGBColor Get(short h, short v) const {
        SInt32 lx = h - bounds.left, ly = v - bounds.top;
        UInt8* row = reinterpret_cast<UInt8*>(base) + static_cast<SInt32>(ly) * rowBytes;
        if (pixelSize == 32) {
            UInt8* p = row + lx * 4;
            // Bit-replicate 8 -> 16 bits ((v<<8)|v) so a round-tripped pure
            // white/black channel comes back as exactly 0xFFFF/0x0000, not
            // 0xFF00/0x0000 -- callers compare captured pixels against exact
            // RGBColor constants (e.g. live-edit's white-background ink
            // check), and a truncated round-trip made every pixel compare
            // unequal to white, so every pixel looked like "ink".
            UInt16 r = p[1], g = p[2], b = p[3];
            return RGBColor{ static_cast<UInt16>((r << 8) | r), static_cast<UInt16>((g << 8) | g),
                              static_cast<UInt16>((b << 8) | b) };
        }
        UInt16 pixel = *reinterpret_cast<UInt16*>(row + lx * 2);
        UInt16 r5 = (pixel >> 10) & 0x1F, g5 = (pixel >> 5) & 0x1F, b5 = pixel & 0x1F;
        // Same bit-replication idea, 5 -> 16 bits: (v<<11)|(v<<6)|(v<<1)|(v>>4)
        // maps 31 -> exactly 0xFFFF instead of 0xF800.
        auto expand5 = [](UInt16 v) -> UInt16 {
            return static_cast<UInt16>((v << 11) | (v << 6) | (v << 1) | (v >> 4));
        };
        return RGBColor{ expand5(r5), expand5(g5), expand5(b5) };
    }
};

static FastPixelWriter GetFastPixelWriter() {
    FastPixelWriter w;
    GrafPtr gp;
    GetPort(&gp);
    CGrafPort* cgp = reinterpret_cast<CGrafPort*>(gp);
    PixMapHandle pmH = cgp->portPixMap;
    if (pmH && *pmH) {
        PixMapPtr pmp = *pmH;
        w.base      = pmp->baseAddr;
        w.rowBytes  = pmp->rowBytes & 0x3FFF;
        w.pixelSize = pmp->pixelSize;
        w.bounds    = pmp->bounds;
    }
    return w;
}

// The current document window's local content rect. Rotated text
// destinations are computed purely from the shape's geometry and can swing
// well outside the window (a wide box rotated near 90 degrees can extend
// far above its own position) -- neither FastPixelWriter's bounds (the
// pixmap's own bounds, not necessarily the window's visible content area)
// nor SetCPixel reliably stop that from landing on the title bar, menu bar,
// or desktop, so every rotated-text destination pixel must be explicitly
// clipped to this rect before it's ever written. See gActivePortBounds for
// why this reads a cached value instead of re-deriving it here.
static Rect CurrentPortBounds() {
    return gActivePortBounds;
}

// Paints a captured, unrotated `srcW`x`srcH` pixel block (read from
// `srcRect` on the real port) into its rotated destination on screen —
// same inverse-mapped affine-stepping technique as DrawShape's kText case
// (see the comments there for the derivation), factored out here so the
// live text-edit overlay (EditTextInPlace) can reuse it too, not just the
// static/settled rendering. `ink`, if non-null, skips repainting pixels
// where it's false (background); pass nullptr to paint every pixel
// unconditionally (used for the edit overlay, where content changes too
// often per keystroke for a meaningful ink cache to be worth building).
static void PaintRotatedPixelBlock(const std::vector<RGBColor>& pixels, const std::vector<bool>* ink,
                                    short srcW, short srcH, Rect srcRect, const RotChain& full,
                                    FastPixelWriter& fastW, bool useFast) {
    if (srcW <= 0 || srcH <= 0) return;
    Point c0, c1, c2, c3;
    { double fx,fy;
      ApplyRotChain(full, srcRect.left,  srcRect.top,    fx,fy); c0 = ToQDPoint(fx,fy);
      ApplyRotChain(full, srcRect.right, srcRect.top,    fx,fy); c1 = ToQDPoint(fx,fy);
      ApplyRotChain(full, srcRect.right, srcRect.bottom, fx,fy); c2 = ToQDPoint(fx,fy);
      ApplyRotChain(full, srcRect.left,  srcRect.bottom, fx,fy); c3 = ToQDPoint(fx,fy); }
    short minX = std::min(std::min(c0.h,c1.h), std::min(c2.h,c3.h));
    short maxX = std::max(std::max(c0.h,c1.h), std::max(c2.h,c3.h));
    short minY = std::min(std::min(c0.v,c1.v), std::min(c2.v,c3.v));
    short maxY = std::max(std::max(c0.v,c1.v), std::max(c2.v,c3.v));

    // Clip the destination AABB to the window's own content rect -- see
    // CurrentPortBounds() for why this can't be skipped.
    Rect winBounds = CurrentPortBounds();
    minX = std::max(minX, winBounds.left);
    maxX = std::min(maxX, static_cast<short>(winBounds.right - 1));
    minY = std::max(minY, winBounds.top);
    maxY = std::min(maxY, static_cast<short>(winBounds.bottom - 1));

    SInt32 dstW = (SInt32)maxX - minX + 1, dstH = (SInt32)maxY - minY + 1;
    // Cap raised from 300000: the destination is already clipped to the
    // window, so a window close to full-screen-sized (640x530 = 339200
    // alone exceeds the old cap) needs headroom above typical window
    // dimensions, not a guard tighter than a single ordinary window.
    if (dstW <= 0 || dstH <= 0 || dstW * dstH > 2000000) return;

    double baseOx, baseOy, stepXOx, stepXOy, stepYOx, stepYOy;
    ApplyRotChainInverse(full, minX+0.5, minY+0.5, baseOx,  baseOy);
    ApplyRotChainInverse(full, minX+1.5, minY+0.5, stepXOx, stepXOy);
    ApplyRotChainInverse(full, minX+0.5, minY+1.5, stepYOx, stepYOy);
    stepXOx -= baseOx; stepXOy -= baseOy;
    stepYOx -= baseOx; stepYOy -= baseOy;

    auto ClipAxis = [](double a, double b, double lo, double hi, double& pxLo, double& pxHi) {
        if (b == 0.0) { if (a < lo || a >= hi) pxHi = pxLo - 1.0; return; }
        double t0 = (lo - a) / b, t1 = (hi - a) / b;
        if (b > 0) { pxLo = std::max(pxLo, t0); pxHi = std::min(pxHi, t1); }
        else       { pxLo = std::max(pxLo, t1); pxHi = std::min(pxHi, t0); }
    };

    double rowOx = baseOx, rowOy = baseOy;
    for (SInt32 py = 0; py < dstH; ++py) {
        double pxLo = 0.0, pxHi = static_cast<double>(dstW);
        ClipAxis(rowOx, stepXOx, srcRect.left, srcRect.right,  pxLo, pxHi);
        ClipAxis(rowOy, stepXOy, srcRect.top,  srcRect.bottom, pxLo, pxHi);
        SInt32 pxStart = std::max<SInt32>(0, static_cast<SInt32>(std::ceil(pxLo)));
        SInt32 pxEnd   = std::min<SInt32>(dstW, static_cast<SInt32>(std::ceil(pxHi)));

        double ox = rowOx + pxStart * stepXOx;
        double oy = rowOy + pxStart * stepXOy;
        for (SInt32 px = pxStart; px < pxEnd; ++px) {
            SInt32 sxi = static_cast<SInt32>(std::floor(ox)) - srcRect.left;
            SInt32 syi = static_cast<SInt32>(std::floor(oy)) - srcRect.top;
            if (sxi >= 0 && sxi < srcW && syi >= 0 && syi < srcH) {
                size_t si = static_cast<size_t>(syi) * srcW + sxi;
                if (!ink || (*ink)[si]) {
                    short dh = static_cast<short>(minX+px), dv = static_cast<short>(minY+py);
                    if (useFast) fastW.Set(dh, dv, pixels[si]);
                    else         SetCPixel(dh, dv, const_cast<RGBColor*>(&pixels[si]));
                }
            }
            ox += stepXOx; oy += stepXOy;
        }
        rowOx += stepYOx; rowOy += stepYOy;
    }
}

static void DrawShape(const Shape& shape, const RotChain& ambient = {}) {
    if (!shape.visible) return;
    if (gEditingTextShape && &shape == static_cast<const Shape*>(gEditingTextShape)) return;
    Rect r = CanvasRect(shape.bounds);
    bool anyRotation = (shape.rotation != 0) || !ambient.empty();
    bool shapeOp = (shape.opacity < 100);
    if (shapeOp) {
        UInt16 w = static_cast<UInt16>((UInt32)shape.opacity * 65535 / 100);
        RGBColor oc = { w, w, w };
        OpColor(&oc); PenMode(blend);
    }
    switch (shape.GetType()) {
        case Shape::kRectangle:
        case Shape::kLine: {
            if (anyRotation) {
                // Rotated rect (own rotation and/or inherited from a rotated
                // ancestor frame): polygon path, corners rounded via TraceRoundedRectPoints.
                short sw = static_cast<short>(shape.strokeWidth);
                short rtl = 0, rtr = 0, rbr = 0, rbl = 0;
                if (shape.GetType() == Shape::kRectangle) {
                    const auto& rs = static_cast<const RectShape&>(shape);
                    if (rs.cornerIndividual) {
                        rtl = ScaleCornerRadius(rs.cornerTL); rtr = ScaleCornerRadius(rs.cornerTR);
                        rbr = ScaleCornerRadius(rs.cornerBR); rbl = ScaleCornerRadius(rs.cornerBL);
                    } else {
                        short uniform = ScaleCornerRadius(rs.cornerRadius);
                        rtl = rtr = rbr = rbl = uniform;
                    }
                }
                if (rtl || rtr || rbr || rbl) {
                    std::vector<Point> pts = TraceRoundedRectPoints(r, rtl, rtr, rbr, rbl, shape.rotation, ambient);
                    PolyHandle poly = OpenPoly();
                    TracePointPath(pts);
                    ClosePoly();
                    if (shape.hasFill)   { RGBColor c = shape.fillColor;   RGBForeColor(&c); PaintPoly(poly); }
                    if (shape.hasStroke) { RGBColor c = shape.strokeColor; RGBForeColor(&c); PenSize(sw,sw); FramePoly(poly); PenSize(1,1); }
                    KillPoly(poly);
                } else {
                    DrawRotatedRect(r, shape.rotation,
                                    shape.hasFill, shape.fillColor,
                                    shape.hasStroke, shape.strokeColor, sw, ambient);
                }
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
            if (anyRotation) {
                short sw = static_cast<short>(shape.strokeWidth);
                DrawRotatedEllipse(r, shape.rotation,
                                   shape.hasFill, shape.fillColor,
                                   shape.hasStroke, shape.strokeColor, sw, ambient);
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
            if (!shape.hasStroke && !shape.hasFill) { break; }  // nothing to draw

            const std::string& str = t.text;
            short lineH = static_cast<short>(SInt32(scaledSize) * t.lineHeight / 100);
            if (lineH < 1) lineH = 1;
            short lsxPx = static_cast<short>(SInt32(t.letterSpacing) * gCanvasZoom / 100);

            // Sets the current port's font/face/color state for text drawing. Must be
            // re-called after switching ports (offscreen GWorld path below), since
            // that state lives on the GrafPort, not globally.
            auto setTextDrawState = [&]() {
                TextFont(fontID); TextSize(scaledSize);
                // Stroke renders as QuickDraw outline on glyphs (backColor=fill,
                // foreColor=stroke) so the outline follows letter shapes rather than
                // a bounding rectangle.
                if (shape.hasStroke) {
                    RGBColor fc = shape.hasFill ? shape.fillColor : RGBColor{0xFFFF,0xFFFF,0xFFFF};
                    RGBBackColor(&fc);
                    RGBColor sc = shape.strokeColor; RGBForeColor(&sc);
                    TextFace(static_cast<short>(t.fontFace | 8));  // QuickDraw outline bit
                } else {
                    RGBColor tc = shape.fillColor; RGBForeColor(&tc);
                    RGBColor wh = {0xFFFF,0xFFFF,0xFFFF}; RGBBackColor(&wh);
                    TextFace(t.fontFace);
                }
            };

            // Draws all lines into the current port at `rect` (font/color state
            // must already be set via setTextDrawState).
            auto drawLines = [&](Rect rect) {
                if (str.empty()) return;
                // Baseline offset from the box's top: the font's real
                // ascent, not the raw point size. TextEdit (used during
                // live editing) positions the first line's baseline using
                // the font's actual ascent metric internally -- ascent is
                // smaller than the full point size for virtually every
                // font, so approximating it with scaledSize placed the
                // settled/committed text measurably lower than the same
                // text looked while being edited, showing up as a
                // position jump on every add or edit commit regardless of
                // auto layout or rotation.
                FontInfo fi; GetFontInfo(&fi);
                short baselineOffset = fi.ascent > 0 ? fi.ascent : scaledSize;
                short drawY = static_cast<short>(rect.top + baselineOffset);
                short boxW  = static_cast<short>(rect.right - rect.left);
                size_t pos = 0;
                do {
                    size_t nl  = str.find('\n', pos);
                    size_t len = (nl == std::string::npos) ? str.size() - pos : nl - pos;
                    if (len > 0) {
                        Str255 pline; pline[0] = 0;
                        for (size_t ci = 0; ci < len && ci < 255; ++ci) {
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
                        if (t.textAlign == 1)      sx = static_cast<short>(rect.left + (boxW - lw) / 2);
                        else if (t.textAlign == 2) sx = static_cast<short>(rect.right - lw);
                        else                       sx = rect.left;
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
            };

            // True glyph rotation, done entirely on the REAL window port —
            // never an offscreen GWorld (three earlier GWorld techniques all
            // corrupted the shared screen palette; see project memory:
            // CopyBits screen corruption). Draw the text upright into its
            // own unrotated rect, capture those exact pixels with
            // GetCPixel, restore what was underneath, then inverse-map the
            // rotated destination bounding box back into that captured
            // block and paint it with SetCPixel (inverse mapping so the
            // result has no holes).
            //
            // What the upright glyphs look like doesn't depend on rotation
            // angle, so the capture is cached per TextShape (see
            // TextGlyphCache above DrawShape) and only redone when text,
            // font, color, or box size actually change. A live rotate drag
            // changes only the angle every frame, so after the first frame
            // this is a cache hit — just the inverse-map repaint below runs
            // every mouse-move, which is cheap enough not to flicker. Only
            // a cold cache (first frame after an edit, or a box too large
            // to cache/repaint cheaply) pays the full multi-pass cost.
            double ownRot = static_cast<double>(shape.rotation);
            double cx0 = (r.left + r.right) * 0.5, cy0 = (r.top + r.bottom) * 0.5;
            RotChain full;
            if (ownRot != 0.0) full.push_back({ownRot, cx0, cy0});
            full.insert(full.end(), ambient.begin(), ambient.end());

            short srcW = static_cast<short>(r.right - r.left);
            short srcH = static_cast<short>(r.bottom - r.top);
            bool didPixelRotate = false;

            // r (the shape's own box) can be far wider/taller than the
            // window (a long AutoWidth sentence) -- matching Figma means
            // that content simply extends past the visible viewport, not
            // that it falls back to upright once it no longer fits. A box
            // that fits inside the window uses the fast, cached, rotation-
            // ANGLE-INDEPENDENT technique below (the whole point of the
            // cache: a live rotate drag changes only the angle, so it's a
            // cache hit every frame). A box wider than the window instead
            // only ever captures the slice that maps into the visible,
            // window-clipped destination -- which depends on the rotation
            // angle, so it can't be cached the same way and is handled in
            // its own uncached branch further below.
            Rect fitCheckBounds = CurrentPortBounds();
            bool fitsWindow = srcW <= (fitCheckBounds.right - fitCheckBounds.left) &&
                               srcH <= (fitCheckBounds.bottom - fitCheckBounds.top);

            if (anyRotation && !str.empty() && srcW > 0 && srcH > 0 &&
                (fitsWindow ? ((SInt32)srcW * (SInt32)srcH <= 150000) : true)) {
                Point c0, c1, c2, c3;
                { double fx,fy;
                  ApplyRotChain(full, r.left,  r.top,    fx,fy); c0 = ToQDPoint(fx,fy);
                  ApplyRotChain(full, r.right, r.top,    fx,fy); c1 = ToQDPoint(fx,fy);
                  ApplyRotChain(full, r.right, r.bottom, fx,fy); c2 = ToQDPoint(fx,fy);
                  ApplyRotChain(full, r.left,  r.bottom, fx,fy); c3 = ToQDPoint(fx,fy); }
                short minX = std::min(std::min(c0.h,c1.h), std::min(c2.h,c3.h));
                short maxX = std::max(std::max(c0.h,c1.h), std::max(c2.h,c3.h));
                short minY = std::min(std::min(c0.v,c1.v), std::min(c2.v,c3.v));
                short maxY = std::max(std::max(c0.v,c1.v), std::max(c2.v,c3.v));

                // Clip the destination AABB to the window's own content rect --
                // see CurrentPortBounds() for why this can't be skipped.
                Rect winBounds = CurrentPortBounds();
                minX = std::max(minX, winBounds.left);
                maxX = std::min(maxX, static_cast<short>(winBounds.right - 1));
                minY = std::max(minY, winBounds.top);
                maxY = std::min(maxY, static_cast<short>(winBounds.bottom - 1));

                SInt32 dstW = (SInt32)maxX - minX + 1, dstH = (SInt32)maxY - minY + 1;

                // Uncached path for a box wider/taller than the window:
                // inverse-map the (already window-clipped) destination
                // corners back into r's own coordinate space to find
                // exactly which slice of the box is actually visible, then
                // stage/capture just that -- same technique as the
                // live-edit overlay's needR. This is angle-dependent (so
                // not cached) but the slice is always bounded to roughly
                // the window's own size, so it's cheap regardless of how
                // wide r itself has grown.
                std::vector<RGBColor> uncachedGlyph;
                std::vector<bool> uncachedInk;
                Rect paintSrcRect = r;
                short paintSrcW = srcW, paintSrcH = srcH;
                const std::vector<RGBColor>* glyphPtr = nullptr;
                const std::vector<bool>* inkPtr = nullptr;
                FastPixelWriter fastW;
                bool useFast = false;

                if (dstW > 0 && dstH > 0 && dstW * dstH <= 2000000 && !fitsWindow) {
                    double nx0,ny0, nx1,ny1, nx2,ny2, nx3,ny3;
                    ApplyRotChainInverse(full, minX, minY, nx0, ny0);
                    ApplyRotChainInverse(full, maxX, minY, nx1, ny1);
                    ApplyRotChainInverse(full, maxX, maxY, nx2, ny2);
                    ApplyRotChainInverse(full, minX, maxY, nx3, ny3);
                    double needMinXd = std::min(std::min(nx0,nx1), std::min(nx2,nx3));
                    double needMaxXd = std::max(std::max(nx0,nx1), std::max(nx2,nx3));
                    double needMinYd = std::min(std::min(ny0,ny1), std::min(ny2,ny3));
                    double needMaxYd = std::max(std::max(ny0,ny1), std::max(ny2,ny3));

                    // Generous margin (not just +-1px): the AABB of the
                    // inverse-mapped clipped-destination corners is a
                    // mathematically conservative bound already, but this
                    // is exactly the boundary that's been the repeated
                    // source of "text near the edge gets cut/garbled"
                    // reports all session -- pad it well beyond the bare
                    // minimum so any small mismatch elsewhere (rounding,
                    // TE's own glyph metrics vs measured width, etc.)
                    // can't manifest as a visible cut right at this edge.
                    // Not capped to the window's size -- see the striping
                    // loop below for why capping needR itself was the bug,
                    // not the fix (the STAGING draw fundamentally needs
                    // source-width screen space regardless of how compact
                    // the final rotated result is).
                    short needPad = 60;
                    short winW = static_cast<short>(winBounds.right - winBounds.left);
                    short winH = static_cast<short>(winBounds.bottom - winBounds.top);
                    Rect needR;
                    needR.left   = std::max(r.left,   static_cast<short>(std::floor(needMinXd) - needPad));
                    needR.right  = std::min(r.right,  static_cast<short>(std::ceil(needMaxXd)  + needPad));
                    needR.top    = std::max(r.top,    static_cast<short>(std::floor(needMinYd) - needPad));
                    needR.bottom = std::min(r.bottom, static_cast<short>(std::ceil(needMaxYd)  + needPad));
                    short needSrcW = static_cast<short>(needR.right - needR.left);
                    short needSrcH = static_cast<short>(needR.bottom - needR.top);

                    if (needR.right > needR.left && needR.bottom > needR.top &&
                        (SInt32)needSrcW * needSrcH <= 2000000) {
                        fastW = GetFastPixelWriter();
                        useFast = fastW.Ready();
                        auto getPxU = [&](short px, short py) -> RGBColor {
                            if (useFast) return fastW.Get(px, py);
                            RGBColor c; GetCPixel(px, py, &c); return c;
                        };
                        auto setPxU = [&](short px, short py, const RGBColor& c) {
                            if (useFast) fastW.Set(px, py, c);
                            else         SetCPixel(px, py, const_cast<RGBColor*>(&c));
                        };

                        uncachedGlyph.assign(static_cast<size_t>(needSrcW) * needSrcH, RGBColor{0,0,0});
                        uncachedInk.assign(static_cast<size_t>(needSrcW) * needSrcH, false);

                        // Capping needR only ever moved the cutoff point
                        // around: the STAGING draw needs source-width
                        // screen space no matter how compact the rotated
                        // result is (rotation compacts the footprint;
                        // staging happens before rotation is applied).
                        // Capture in successive window-sized STRIPS
                        // instead, each shifted into the window
                        // independently and stitched into the full
                        // buffers at the right offset.
                        short stripMaxW = static_cast<short>(std::max<short>(8, winW - 4));
                        for (short stripLeft = 0; stripLeft < needSrcW; stripLeft = static_cast<short>(stripLeft + stripMaxW)) {
                            short stripW = static_cast<short>(std::min<short>(stripMaxW, needSrcW - stripLeft));
                            Rect stripR = { needR.top, static_cast<short>(needR.left + stripLeft),
                                             needR.bottom, static_cast<short>(needR.left + stripLeft + stripW) };

                            Rect stageR = stripR;
                            short stageShiftX = 0, stageShiftY = 0;
                            if (stageR.right > winBounds.right) stageShiftX = static_cast<short>(winBounds.right - stageR.right);
                            if (static_cast<short>(stageR.left + stageShiftX) < winBounds.left)
                                stageShiftX = static_cast<short>(winBounds.left - stageR.left);
                            if (stageR.bottom > winBounds.bottom) stageShiftY = static_cast<short>(winBounds.bottom - stageR.bottom);
                            if (static_cast<short>(stageR.top + stageShiftY) < winBounds.top)
                                stageShiftY = static_cast<short>(winBounds.top - stageR.top);
                            stageR.left   = static_cast<short>(stageR.left   + stageShiftX);
                            stageR.right  = static_cast<short>(stageR.right  + stageShiftX);
                            stageR.top    = static_cast<short>(stageR.top    + stageShiftY);
                            stageR.bottom = static_cast<short>(stageR.bottom + stageShiftY);

                            std::vector<RGBColor> under(static_cast<size_t>(stripW) * needSrcH);
                            for (short y = 0; y < needSrcH; ++y)
                                for (short x = 0; x < stripW; ++x)
                                    under[static_cast<size_t>(y)*stripW + x] =
                                        getPxU(static_cast<short>(stageR.left+x), static_cast<short>(stageR.top+y));

                            RgnHandle savedClipU = NewRgn();
                            GetClip(savedClipU);
                            { Rect cr = stageR; ClipRect(&cr); }
                            setTextDrawState();
                            // drawLines aligns/positions text using the
                            // rect's own left/right, which must stay r's
                            // full real extent (shifted by this strip's
                            // own offset), not just the strip's own
                            // narrower width, or a centered/right-aligned
                            // line would land in the wrong spot; the clip
                            // above still confines the actual pixels
                            // touched to this strip's stageR.
                            short shift = static_cast<short>(stageR.left - stripR.left);
                            Rect alignR = r;
                            alignR.left  = static_cast<short>(r.left  + shift);
                            alignR.right = static_cast<short>(r.right + shift);
                            alignR.top   = stageR.top; alignR.bottom = stageR.bottom;
                            drawLines(alignR);
                            SetClip(savedClipU);
                            DisposeRgn(savedClipU);

                            // Per-pixel comparison against this strip's own
                            // captured background (not a single sampled
                            // color): the settled renderer draws directly
                            // on top of whatever was already there rather
                            // than erasing first, so the real background
                            // can vary pixel-to-pixel (e.g. partly inside
                            // a filled frame, partly on plain canvas).
                            for (short y = 0; y < needSrcH; ++y) {
                                for (short x = 0; x < stripW; ++x) {
                                    RGBColor c = getPxU(static_cast<short>(stageR.left+x), static_cast<short>(stageR.top+y));
                                    const RGBColor& u = under[static_cast<size_t>(y)*stripW + x];
                                    size_t di = static_cast<size_t>(y)*needSrcW + static_cast<size_t>(stripLeft+x);
                                    uncachedGlyph[di] = c;
                                    uncachedInk[di] = c.red != u.red || c.green != u.green || c.blue != u.blue;
                                }
                            }

                            for (short y = 0; y < needSrcH; ++y)
                                for (short x = 0; x < stripW; ++x)
                                    setPxU(static_cast<short>(stageR.left+x), static_cast<short>(stageR.top+y),
                                           under[static_cast<size_t>(y)*stripW + x]);
                        }

                        paintSrcRect = needR;
                        paintSrcW = needSrcW; paintSrcH = needSrcH;
                        glyphPtr = &uncachedGlyph;
                        inkPtr = &uncachedInk;
                    }
                }

                if (fitsWindow && dstW > 0 && dstH > 0 && dstW * dstH <= 2000000) {
                    TextGlyphCacheKey key;
                    key.text = str; key.fontID = fontID; key.size = scaledSize;
                    key.face = static_cast<short>(t.fontFace | (shape.hasStroke ? 8 : 0));
                    key.lineH = lineH; key.lsx = lsxPx; key.align = t.textAlign;
                    key.hasFill = shape.hasFill; key.hasStroke = shape.hasStroke;
                    key.fillColor = shape.fillColor; key.strokeColor = shape.strokeColor;
                    key.w = srcW; key.h = srcH;

                    TextGlyphCacheEntry& entry = gTextGlyphCache[&t];
                    fastW = GetFastPixelWriter();
                    useFast = fastW.Ready();
                    bool wasCacheHit = (entry.key == key) && entry.pixels.size() == static_cast<size_t>(srcW) * srcH;
                    if (!wasCacheHit) {
                        // The capture below stages its draw at a real screen
                        // location before reading it back -- but r (the
                        // shape's own box) can sit partly or fully outside
                        // the window (e.g. dragged off the left edge), and
                        // any part of that staging draw landing outside the
                        // window reads/writes undefined pixels, baking
                        // corruption into the cached glyph bitmap right at
                        // the window edge. Shift (never resize) a staging
                        // copy of r so it fits inside the window and draw/
                        // capture there instead -- same fix as the live-edit
                        // overlay's stageR. r itself stays untouched for the
                        // rotation transform below.
                        Rect stageWinBounds = CurrentPortBounds();
                        Rect stageR = r;
                        short stageShiftX = 0, stageShiftY = 0;
                        if (stageR.right > stageWinBounds.right) stageShiftX = static_cast<short>(stageWinBounds.right - stageR.right);
                        if (static_cast<short>(stageR.left + stageShiftX) < stageWinBounds.left)
                            stageShiftX = static_cast<short>(stageWinBounds.left - stageR.left);
                        if (stageR.bottom > stageWinBounds.bottom) stageShiftY = static_cast<short>(stageWinBounds.bottom - stageR.bottom);
                        if (static_cast<short>(stageR.top + stageShiftY) < stageWinBounds.top)
                            stageShiftY = static_cast<short>(stageWinBounds.top - stageR.top);
                        stageR.left   = static_cast<short>(stageR.left   + stageShiftX);
                        stageR.right  = static_cast<short>(stageR.right  + stageShiftX);
                        stageR.top    = static_cast<short>(stageR.top    + stageShiftY);
                        stageR.bottom = static_cast<short>(stageR.bottom + stageShiftY);

                        // A live resize changes srcW/srcH on every mouse-move,
                        // which invalidates this cache every frame -- capture
                        // (normally rare) becomes the hot path for the whole
                        // drag, so it needs the same fast raw-buffer access as
                        // the repaint loop below, not just GetCPixel/SetCPixel.
                        std::vector<RGBColor> under(static_cast<size_t>(srcW) * srcH);
                        for (short y = 0; y < srcH; ++y)
                            for (short x = 0; x < srcW; ++x) {
                                short px = static_cast<short>(stageR.left+x), py = static_cast<short>(stageR.top+y);
                                under[static_cast<size_t>(y)*srcW + x] =
                                    useFast ? fastW.Get(px, py) : ([&]{ RGBColor c; GetCPixel(px, py, &c); return c; }());
                            }

                        // Clip the capture-phase draw to stageR: drawLines has
                        // no real word-wrap (only explicit newlines break a
                        // line), so text wider than the box draws straight
                        // past its right edge onto the real screen. The
                        // restore step below only restores pixels inside
                        // [0,srcW)x[0,srcH), so any unclipped overflow here
                        // would never get erased -- a permanent upright ghost
                        // of the overflowing text left behind.
                        RgnHandle savedClip = NewRgn();
                        GetClip(savedClip);
                        { Rect cr = stageR; ClipRect(&cr); }
                        setTextDrawState();
                        drawLines(stageR);
                        SetClip(savedClip);
                        DisposeRgn(savedClip);

                        std::vector<RGBColor> glyph(static_cast<size_t>(srcW) * srcH);
                        for (short y = 0; y < srcH; ++y)
                            for (short x = 0; x < srcW; ++x) {
                                short px = static_cast<short>(stageR.left+x), py = static_cast<short>(stageR.top+y);
                                glyph[static_cast<size_t>(y)*srcW + x] =
                                    useFast ? fastW.Get(px, py) : ([&]{ RGBColor c; GetCPixel(px, py, &c); return c; }());
                            }

                        for (short y = 0; y < srcH; ++y)
                            for (short x = 0; x < srcW; ++x) {
                                short px = static_cast<short>(stageR.left+x), py = static_cast<short>(stageR.top+y);
                                const RGBColor& u = under[static_cast<size_t>(y)*srcW + x];
                                if (useFast) fastW.Set(px, py, u);
                                else         SetCPixel(px, py, const_cast<RGBColor*>(&u));
                            }

                        std::vector<bool> ink(static_cast<size_t>(srcW) * srcH);
                        for (size_t i = 0; i < ink.size(); ++i) {
                            ink[i] = glyph[i].red != under[i].red ||
                                     glyph[i].green != under[i].green ||
                                     glyph[i].blue != under[i].blue;
                        }

                        entry.key    = key;
                        entry.pixels = std::move(glyph);
                        entry.ink    = std::move(ink);
                    }

                    glyphPtr = &entry.pixels;
                    inkPtr   = &entry.ink;
                }

                // Shared repaint: paints whichever source (the cached full
                // box, or the uncached window-clipped slice) either branch
                // above produced into the rotated destination.
                if (glyphPtr && inkPtr && dstW > 0 && dstH > 0) {
                    const std::vector<RGBColor>& glyph = *glyphPtr;
                    const std::vector<bool>&     ink   = *inkPtr;

                    // The inverse mapping from destination pixel to source
                    // pixel is a fixed affine transform (rotation + translation,
                    // never scale/shear) for this entire repaint, so instead of
                    // re-deriving it per pixel (even the precomputed-cos/sin
                    // version above still did several multiplies per pixel),
                    // sample it at 3 points to extract that affine transform's
                    // coefficients once, then step through every pixel with
                    // plain addition: exact same math, no per-pixel trig or
                    // multiplication at all.
                    double baseOx, baseOy, stepXOx, stepXOy, stepYOx, stepYOy;
                    ApplyRotChainInverse(full, minX+0.5,   minY+0.5,   baseOx,  baseOy);
                    ApplyRotChainInverse(full, minX+1.5,   minY+0.5,   stepXOx, stepXOy);
                    ApplyRotChainInverse(full, minX+0.5,   minY+1.5,   stepYOx, stepYOy);
                    stepXOx -= baseOx; stepXOy -= baseOy;  // change in (ox,oy) per +1 dest X
                    stepYOx -= baseOx; stepYOy -= baseOy;  // change in (ox,oy) per +1 dest Y

                    // Most of the destination AABB is empty space around the
                    // (thin, rotated) actual rectangle -- worse at some
                    // angles than others, which is exactly the "flickers
                    // more at certain angles while resizing" symptom. Since
                    // ox(px)/oy(px) are linear in px for a fixed row, the
                    // valid px range per row (where the source rect contains
                    // the sample) is a single contiguous interval, solvable
                    // directly instead of testing every px and discarding
                    // most of them.
                    auto ClipAxis = [](double a, double b, double lo, double hi,
                                        double& pxLo, double& pxHi) {
                        if (b == 0.0) { if (a < lo || a >= hi) pxHi = pxLo - 1.0; return; }
                        double t0 = (lo - a) / b, t1 = (hi - a) / b;
                        if (b > 0) { pxLo = std::max(pxLo, t0); pxHi = std::min(pxHi, t1); }
                        else       { pxLo = std::max(pxLo, t1); pxHi = std::min(pxHi, t0); }
                    };

                    double rowOx = baseOx, rowOy = baseOy;
                    for (SInt32 py = 0; py < dstH; ++py) {
                        double pxLo = 0.0, pxHi = static_cast<double>(dstW);
                        ClipAxis(rowOx, stepXOx, paintSrcRect.left, paintSrcRect.right, pxLo, pxHi);
                        ClipAxis(rowOy, stepXOy, paintSrcRect.top,  paintSrcRect.bottom, pxLo, pxHi);
                        SInt32 pxStart = std::max<SInt32>(0, static_cast<SInt32>(std::ceil(pxLo)));
                        SInt32 pxEnd   = std::min<SInt32>(dstW, static_cast<SInt32>(std::ceil(pxHi)));

                        double ox = rowOx + pxStart * stepXOx;
                        double oy = rowOy + pxStart * stepXOy;
                        for (SInt32 px = pxStart; px < pxEnd; ++px) {
                            SInt32 sxi = static_cast<SInt32>(std::floor(ox)) - paintSrcRect.left;
                            SInt32 syi = static_cast<SInt32>(std::floor(oy)) - paintSrcRect.top;
                            if (sxi >= 0 && sxi < paintSrcW && syi >= 0 && syi < paintSrcH) {
                                size_t si = static_cast<size_t>(syi) * paintSrcW + sxi;
                                if (ink[si]) {
                                    short dh = static_cast<short>(minX+px), dv = static_cast<short>(minY+py);
                                    if (useFast) fastW.Set(dh, dv, glyph[si]);
                                    else         SetCPixel(dh, dv, const_cast<RGBColor*>(&glyph[si]));
                                }
                            }
                            ox += stepXOx; oy += stepXOy;
                        }
                        rowOx += stepYOx; rowOy += stepYOy;
                    }

                    didPixelRotate = true;
                }
            }

            if (!didPixelRotate) {
                // Empty text, or box too large to pixel-rotate cheaply:
                // position tracks the ambient chain but glyphs stay upright.
                Rect rr = r;
                if (!ambient.empty()) {
                    double fcx, fcy;
                    ApplyRotChain(ambient, cx0, cy0, fcx, fcy);
                    short dx = static_cast<short>((fcx - cx0) + (fcx >= cx0 ? 0.5 : -0.5));
                    short dy = static_cast<short>((fcy - cy0) + (fcy >= cy0 ? 0.5 : -0.5));
                    rr.left = static_cast<short>(rr.left + dx); rr.right  = static_cast<short>(rr.right  + dx);
                    rr.top  = static_cast<short>(rr.top  + dy); rr.bottom = static_cast<short>(rr.bottom + dy);
                }
                // Clip to the shape's own box: drawLines only ever breaks on
                // explicit newlines, never on width, so a Fixed/Auto Height
                // box narrower than its content would otherwise let text
                // spill out past its edges instead of being hidden like a
                // normal text box.
                RgnHandle savedClip = NewRgn();
                GetClip(savedClip);
                { Rect cr = rr; ClipRect(&cr); }
                setTextDrawState();
                drawLines(rr);
                SetClip(savedClip);
                DisposeRgn(savedClip);
            }
            TextFace(0); TextSize(12); TextFont(0);
            RGBColor wh = {0xFFFF,0xFFFF,0xFFFF}; RGBBackColor(&wh);
            break;
        }
        default: break;
    }
    if (shapeOp) PenNormal();
}

// Draws a small label rigidly attached just above r's own LOCAL top-left
// corner, rotated along with r around r's own center (same pivot/angle as
// the owning shape or frame's own border) -- so the label keeps a
// constant gap from its owner at any rotation angle, tilting together
// with it, instead of either staying upright or being recomputed in
// screen space every frame (which let the gap balloon or shrink as the
// angle changed, and made the anchor jump between corners). Uses the
// same capture/rotate/paint technique already proven for rotated text
// shapes, uncached and single-pass since a name label is always short
// enough to stage in one shot. Falls back to a plain upright draw when
// unrotated, or in the rare case the staging area can't fit on screen.
static void DrawRotatedLabel(const unsigned char* pn, const Rect& r, double ownRotDeg, const RotChain& ambient) {
    bool anyRotation = (ownRotDeg != 0.0) || !ambient.empty();
    if (!anyRotation) {
        MoveTo(r.left, static_cast<short>(r.top - 5));
        DrawString(pn);
        return;
    }

    FontInfo fi; GetFontInfo(&fi);
    short labelW = static_cast<short>(StringWidth(pn) + 4);
    short labelH = static_cast<short>(fi.ascent + fi.descent + 2);

    // Logical (unrotated, LOCAL) position -- this is what gets rotated,
    // not a screen-space anchor recomputed per-angle.
    short labelBottom = static_cast<short>(r.top - 5);
    short labelTop    = static_cast<short>(labelBottom - labelH);
    Rect srcR = { labelTop, r.left, labelBottom, static_cast<short>(r.left + labelW) };

    double cx = (r.left + r.right) * 0.5, cy = (r.top + r.bottom) * 0.5;
    RotChain full;
    if (ownRotDeg != 0.0) full.push_back({ownRotDeg, cx, cy});
    full.insert(full.end(), ambient.begin(), ambient.end());

    // srcR's own LOGICAL position may not correspond to any real
    // on-screen location once rotated, so the upright staging draw can't
    // just happen there directly -- shift a staging copy into the window
    // first, same technique used for wide rotated text boxes. A label is
    // always small, so this always fits.
    Rect winBoundsLbl = CurrentPortBounds();
    Rect stageR = srcR;
    short shiftX = 0, shiftY = 0;
    if (stageR.right > winBoundsLbl.right) shiftX = static_cast<short>(winBoundsLbl.right - stageR.right);
    if (static_cast<short>(stageR.left + shiftX) < winBoundsLbl.left)
        shiftX = static_cast<short>(winBoundsLbl.left - stageR.left);
    if (stageR.bottom > winBoundsLbl.bottom) shiftY = static_cast<short>(winBoundsLbl.bottom - stageR.bottom);
    if (static_cast<short>(stageR.top + shiftY) < winBoundsLbl.top)
        shiftY = static_cast<short>(winBoundsLbl.top - stageR.top);
    stageR.left   = static_cast<short>(stageR.left   + shiftX);
    stageR.right  = static_cast<short>(stageR.right  + shiftX);
    stageR.top    = static_cast<short>(stageR.top    + shiftY);
    stageR.bottom = static_cast<short>(stageR.bottom + shiftY);
    if (stageR.right <= stageR.left || stageR.bottom <= stageR.top) return;

    short lsW = static_cast<short>(stageR.right - stageR.left);
    short lsH = static_cast<short>(stageR.bottom - stageR.top);
    FastPixelWriter lfw = GetFastPixelWriter();
    bool lUseFast = lfw.Ready();
    auto lGetPx = [&](short px, short py) -> RGBColor {
        if (lUseFast) return lfw.Get(px, py);
        RGBColor c; GetCPixel(px, py, &c); return c;
    };
    auto lSetPx = [&](short px, short py, const RGBColor& c) {
        if (lUseFast) lfw.Set(px, py, c);
        else          SetCPixel(px, py, const_cast<RGBColor*>(&c));
    };

    std::vector<RGBColor> lUnder(static_cast<size_t>(lsW) * lsH);
    for (short y = 0; y < lsH; ++y)
        for (short x = 0; x < lsW; ++x)
            lUnder[static_cast<size_t>(y)*lsW+x] =
                lGetPx(static_cast<short>(stageR.left+x), static_cast<short>(stageR.top+y));

    MoveTo(stageR.left, static_cast<short>(stageR.bottom - fi.descent));
    DrawString(pn);

    std::vector<RGBColor> lGlyph(static_cast<size_t>(lsW) * lsH);
    for (short y = 0; y < lsH; ++y)
        for (short x = 0; x < lsW; ++x)
            lGlyph[static_cast<size_t>(y)*lsW+x] =
                lGetPx(static_cast<short>(stageR.left+x), static_cast<short>(stageR.top+y));

    for (short y = 0; y < lsH; ++y)
        for (short x = 0; x < lsW; ++x)
            lSetPx(static_cast<short>(stageR.left+x), static_cast<short>(stageR.top+y),
                   lUnder[static_cast<size_t>(y)*lsW+x]);

    std::vector<bool> lInk(static_cast<size_t>(lsW) * lsH);
    for (size_t i = 0; i < lInk.size(); ++i)
        lInk[i] = lGlyph[i].red   != lUnder[i].red ||
                  lGlyph[i].green != lUnder[i].green ||
                  lGlyph[i].blue  != lUnder[i].blue;

    HideCursor();
    PaintRotatedPixelBlock(lGlyph, &lInk, lsW, lsH, srcR, full, lfw, lUseFast);
    ShowCursor();
}

// Root-level shapes get a small name label above them (nested shapes don't —
// see DrawFrame).
static void DrawShapeNameLabel(const Shape& shape) {
    Rect r = CanvasRect(shape.bounds);
    std::string label = shape.name;
    if (label.empty()) {
        if      (shape.GetType() == Shape::kEllipse) label = "Ellipse";
        else if (shape.GetType() == Shape::kText)     label = "Text";
        else                                           label = "Rectangle";
    }
    Str255 pn; ToPStr(label, pn);
    RGBColor lc = { 0x8888, 0x8888, 0x8888 };
    RGBForeColor(&lc);
    TextSize(10);
    // Root-level shapes have no rotated ancestor frame (nested shapes
    // don't get a label at all), so their own rotation is the only input.
    DrawRotatedLabel(pn, r, static_cast<double>(shape.rotation), {});
    TextSize(12);
}

// Forward-declare so DrawFrame can call itself recursively
static void DrawFrame(const Frame& frame, const RotChain& ambient = {});

static void DrawFrame(const Frame& frame, const RotChain& ambient) {
    if (!frame.visible) return;
    Rect r = CanvasRect(frame.bounds);
    bool anyRotation = (frame.rotation != 0) || !ambient.empty();

    // Compute corner rendering params — individual per-corner or uniform.
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

    std::vector<Point> corners;
    PolyHandle framePoly = nullptr;
    if (anyRotation) {
        // TraceRoundedRectPoints wants a RADIUS per corner; fov above is a
        // DIAMETER (PaintRoundRect's own convention, ×2 the radius) — reusing
        // it directly here would render the rotated radius twice too large.
        short runiform = fIndiv ? 0 : ScaleCornerRadius(frame.cornerRadius);
        short rtl = fIndiv ? fitl : runiform, rtr = fIndiv ? fitr : runiform;
        short rbr = fIndiv ? fibr : runiform, rbl = fIndiv ? fibl : runiform;
        corners = TraceRoundedRectPoints(r, rtl, rtr, rbr, rbl, frame.rotation, ambient);
        framePoly = OpenPoly();
        TracePointPath(corners);
        ClosePoly();
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
    if (anyRotation)      PaintPoly(framePoly);
    else if (fIndiv)      ApplyRoundRectCorners(r, fitl, fitr, fibr, fibl, true);
    else if (fov > 0)     PaintRoundRect(&r, fov, fov);
    else                  PaintRect(&r);
    if (frameOp) PenNormal();

    // Ambient transform to hand down to this frame's own children: this
    // frame's own rotation (about its own local center) is applied first
    // (innermost — children are local to this frame), then whatever ancestor
    // chain came in, in its existing order. Chain order is always
    // [nearest ancestor ... root]; a step must be prepended here, not appended.
    RotChain childChain;
    if (frame.rotation != 0) {
        double cx = (r.left + r.right) * 0.5, cy = (r.top + r.bottom) * 0.5;
        childChain.push_back({ static_cast<double>(frame.rotation), cx, cy });
    }
    childChain.insert(childChain.end(), ambient.begin(), ambient.end());

    // Draw children, optionally clipped and optionally in reverse z-order.
    auto drawChildren = [&]() {
        if (frame.childOrder.empty()) {
            // Legacy / newly-created frames without explicit childOrder: shapes first, then frames
            if (frame.canvasStackReverse) {
                for (auto it = frame.children.rbegin();    it != frame.children.rend();    ++it) DrawShape(**it, childChain);
                for (auto it = frame.childFrames.rbegin(); it != frame.childFrames.rend(); ++it) DrawFrame(**it, childChain);
            } else {
                for (const auto& s  : frame.children)    DrawShape(*s, childChain);
                for (const auto& cf : frame.childFrames)  DrawFrame(*cf, childChain);
            }
        } else {
            if (frame.canvasStackReverse) {
                for (auto it = frame.childOrder.rbegin(); it != frame.childOrder.rend(); ++it) {
                    if (it->isFrame) DrawFrame(*frame.childFrames[it->idx], childChain);
                    else             DrawShape(*frame.children[it->idx], childChain);
                }
            } else {
                for (const auto& cr : frame.childOrder) {
                    if (cr.isFrame) DrawFrame(*frame.childFrames[cr.idx], childChain);
                    else            DrawShape(*frame.children[cr.idx], childChain);
                }
            }
        }
    };
    if (frame.clipContent) {
        RgnHandle savedClip = NewRgn();
        GetClip(savedClip);
        if (anyRotation) {
            RgnHandle newClip = NewRgn();
            OpenRgn();
            TracePointPath(corners);
            CloseRgn(newClip);
            SetClip(newClip);
            DisposeRgn(newClip);
        } else {
            ClipRect(&r);
        }
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
        PenSize(sw, sw);
        if (anyRotation) {
            FramePoly(framePoly);
        } else {
            Rect sr = r;
            if (frame.strokeAlign == 2) { sr.top-=sw; sr.left-=sw; sr.bottom+=sw; sr.right+=sw; }
            else if (frame.strokeAlign == 0) { short e=sw/2; sr.top-=e; sr.left-=e; sr.bottom+=e; sr.right+=e; }
            if (fIndiv) ApplyRoundRectCorners(sr, fitl, fitr, fibr, fibl, false);
            else if (fov > 0) FrameRoundRect(&sr, fov, fov); else FrameRect(&sr);
        }
        PenSize(1, 1);
    } else {
        RGBColor border = { 0xBBBB, 0xBBBB, 0xBBBB };
        RGBForeColor(&border);
        if (anyRotation)      FramePoly(framePoly);
        else if (fIndiv)      ApplyRoundRectCorners(r, fitl, fitr, fibr, fibl, false);
        else if (fov > 0)     FrameRoundRect(&r, fov, fov);
        else                  FrameRect(&r);
    }
    if (frameOp) PenNormal();
    if (framePoly) KillPoly(framePoly);

    // Name label — only on top-level frames (no parent). Top-level frames are
    // always drawn with an empty incoming `ambient`, so only the frame's own
    // rotation matters here.
    if (frame.parent == nullptr) {
        RGBColor lc = { 0x4444, 0x4444, 0x4444 };
        RGBForeColor(&lc);
        TextSize(10);
        Str255 pn; ToPStr(frame.name, pn);
        DrawRotatedLabel(pn, r, static_cast<double>(frame.rotation), {});
        TextSize(12);
    }
}

// Forward declarations — full definitions live further down, near HitTestHandles.
static bool ComputeSelectionHandles(short hx[8], short hy[8]);
static double SelectedOwnRotation();
static RotChain SelectedAmbientChain();
static short EffectiveRotateZone(const short hx[8], const short hy[8], int cornerIdx);

void UpdateCanvasCursor(Point globalPt) {
    // Only for the select tool with a single selection over the canvas
    if (gActiveTool != Tool::Select || !gMainWindow) { InitCursor(); return; }
    bool hasSingle = (gSelectedShape && gSelectedShapes.size() <= 1 && gSelectedFrames.empty())
                  || (gSelectedFrame && gSelectedFrames.size() <= 1 && !gSelectedShape
                      && gSelectedShapes.empty());
    if (!hasSingle) { InitCursor(); return; }

    WindowRef hitWin = nullptr;
    short part = FindWindow(globalPt, &hitWin);
    if (hitWin != gMainWindow || part != inContent) { InitCursor(); return; }

    Point localPt = globalPt;
    SetPortWindowPort(gMainWindow);
    GlobalToLocal(&localPt);

    short hx[8], hy[8];
    if (!ComputeSelectionHandles(hx, hy)) { InitCursor(); return; }

    // Net rotation (own + every rotated ancestor) picks which of the 8/4 preset
    // cursor bitmaps looks right — matches HandleBucket's screen-angle convention.
    double shapeRot = SelectedOwnRotation();
    for (const auto& step : SelectedAmbientChain()) shapeRot += step.angleDeg;

    // Corner handles: indices 0,2,4,6 — inner rect = resize, outer ring = rotate
    static const int kCorner[4] = {0, 2, 4, 6};
    int rotateCorner = -1;
    for (int ci = 0; ci < 4; ++ci) {
        int i = kCorner[ci];
        short adx = static_cast<short>(localPt.h - hx[i]);
        short ady = static_cast<short>(localPt.v - hy[i]);
        if (adx < 0) adx = -adx;
        if (ady < 0) ady = -ady;
        short dist = adx > ady ? adx : ady;  // Chebyshev distance
        if (dist <= kHandleHW) {
            SetCursor(GetResizeCursor(HandleBucket(i, shapeRot)));
            return;
        }
        if (dist <= kHandleHW + EffectiveRotateZone(hx, hy, i)) rotateCorner = i;
    }
    if (rotateCorner >= 0) {
        SetCursor(GetRotateCursor(HandleBucket(rotateCorner, shapeRot)));
        return;
    }

    // Edge handles: indices 1,3,5,7 → resize cursor
    for (int i = 1; i < 8; i += 2) {
        short adx = static_cast<short>(localPt.h - hx[i]);
        short ady = static_cast<short>(localPt.v - hy[i]);
        if (adx < 0) adx = -adx;
        if (ady < 0) ady = -ady;
        if (adx <= kHandleHW && ady <= kHandleHW) {
            SetCursor(GetResizeCursor(HandleBucket(i, shapeRot)));
            return;
        }
    }

    InitCursor();
}

// Forward declarations — full definitions live further down, near ComputeSelectionHandles.
static Frame* LocateShapeParent(Shape* s);
static RotChain AncestorChainFor(Frame* startFrame);

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

    // Rotated border + handles placed at rotated corner and edge-midpoint positions.
    // `angleDeg` is the object's own rotation; `ambient` carries it through any
    // rotated ancestor frames, same convention as DrawFrame/ComputeSelectionHandles.
    auto drawRotatedItem = [&](const Bounds2& bounds, double angleDeg, const RotChain& ambient) {
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
            double ox = cx + lx[i]*cosA - ly[i]*sinA;
            double oy = cy + lx[i]*sinA + ly[i]*cosA;
            double fx, fy;
            ApplyRotChain(ambient, ox, oy, fx, fy);
            Point p = ToQDPoint(fx, fy);
            px[i] = p.h; py[i] = p.v;
        }

        // Rotated border
        RGBForeColor(&selBlue);
        PenSize(2, 2);
        MoveTo(px[0], py[0]);
        LineTo(px[1], py[1]); LineTo(px[2], py[2]);
        LineTo(px[3], py[3]); LineTo(px[0], py[0]);
        PenSize(1, 1);

        // 8 handle positions in LOCAL (pre-rotation) space, corners
        // interleaved with edge midpoints — same 0=TL,1=N,2=TR,3=E,4=BR,
        // 5=S,6=BL,7=W convention as ComputeSelectionHandles.
        double hlx[8] = { -hw, 0,  hw,  hw,  hw,  0, -hw, -hw };
        double hly[8] = { -hh, -hh, -hh, 0,  hh, hh,  hh,  0 };
        for (int i = 0; i < 8; ++i) {
            // Each handle is drawn as a small square rotated the same way
            // as the object itself, not a plain axis-aligned box: build
            // its 4 corners in local space (centered on the handle's own
            // local position), then run them through the exact same
            // own-rotation + ambient transform as the shape's own corners
            // above, instead of just rotating the handle's CENTER position
            // and leaving its square shape upright.
            double sx[4] = { hlx[i]-kHW, hlx[i]+kHW, hlx[i]+kHW, hlx[i]-kHW };
            double sy[4] = { hly[i]-kHW, hly[i]-kHW, hly[i]+kHW, hly[i]+kHW };
            Point hp[4];
            for (int k = 0; k < 4; ++k) {
                double ox = cx + sx[k]*cosA - sy[k]*sinA;
                double oy = cy + sx[k]*sinA + sy[k]*cosA;
                double fx, fy;
                ApplyRotChain(ambient, ox, oy, fx, fy);
                hp[k] = ToQDPoint(fx, fy);
            }
            PolyHandle hpoly = OpenPoly();
            MoveTo(hp[0].h, hp[0].v);
            LineTo(hp[1].h, hp[1].v); LineTo(hp[2].h, hp[2].v);
            LineTo(hp[3].h, hp[3].v); LineTo(hp[0].h, hp[0].v);
            ClosePoly();
            RGBForeColor(&white); PaintPoly(hpoly);
            RGBForeColor(&selBlue); FramePoly(hpoly);
            KillPoly(hpoly);
        }
    };

    for (Shape* s : gSelectedShapes) {
        if (s == static_cast<Shape*>(gEditingTextShape)) continue;
        RotChain ambient = AncestorChainFor(LocateShapeParent(s));
        if (s->rotation != 0 || !ambient.empty()) drawRotatedItem(s->bounds, s->rotation, ambient);
        else                                      drawItem(CanvasRect(s->bounds));
    }
    for (Frame* f : gSelectedFrames) {
        RotChain ambient = AncestorChainFor(f->parent);
        if (f->rotation != 0 || !ambient.empty()) drawRotatedItem(f->bounds, f->rotation, ambient);
        else                                      drawItem(CanvasRect(f->bounds));
    }

    // Primary single-select item (skip if already drawn as part of multi-select)
    bool drawnAsShape = gSelectedShape &&
        std::find(gSelectedShapes.begin(), gSelectedShapes.end(), gSelectedShape) != gSelectedShapes.end();
    bool drawnAsFrame = gSelectedFrame &&
        std::find(gSelectedFrames.begin(), gSelectedFrames.end(), gSelectedFrame) != gSelectedFrames.end();

    if (!drawnAsShape && !drawnAsFrame) {
        if (!gSelectedShape && !gSelectedFrame) { PenNormal(); return; }
        // The text-editing overlay (EditTextInPlace) draws its own plain
        // axis-aligned frame around the (necessarily unrotated — TextEdit
        // can't rotate) edit box; the normal rotated selection border/
        // handles would otherwise draw on top of it, unrelated to where
        // the edit box actually is.
        if (gEditingTextShape && gSelectedShape == static_cast<Shape*>(gEditingTextShape)) { PenNormal(); return; }
        double ownRot = SelectedOwnRotation();
        RotChain ambient = SelectedAmbientChain();
        if (ownRot != 0.0 || !ambient.empty()) {
            const Bounds2& bounds = gSelectedShape ? gSelectedShape->bounds : gSelectedFrame->bounds;
            drawRotatedItem(bounds, ownRot, ambient);
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
        for (size_t ci = 0; ci < len && ci < 255; ++ci) {
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
        if (s->GetType() == Shape::kText && s.get() != static_cast<Shape*>(gEditingTextShape))
            UpdateTextShapeBounds(static_cast<TextShape&>(*s));
    for (auto& cf : f.childFrames)
        UpdateTextShapeBoundsInFrame(*cf);
}

// Skips whichever shape EditTextInPlace currently has open, same as
// DrawShape already skips drawing it: ts->text isn't updated with the live
// typed content until the edit session commits, so recomputing bounds from
// the stale ts->text mid-edit is meaningless at best. Worse, if a parent
// frame uses Auto Layout, RunDocumentLayout (called right after this, every
// single redraw during editing) can then reflow/reposition the shape using
// that stale size -- observed as the shape's on-screen position drifting or
// even jumping outside its frame while actively typing. Freezing bounds for
// the duration of editing keeps ts->bounds (and therefore layout) stable
// until the real, final size is known at commit.
static void UpdateAllTextShapeBounds(Document* doc) {
    if (!doc || !gMainWindow) return;
    SetPortWindowPort(gMainWindow);
    for (auto& s : doc->rootShapes)
        if (s->GetType() == Shape::kText && s.get() != static_cast<Shape*>(gEditingTextShape))
            UpdateTextShapeBounds(static_cast<TextShape&>(*s));
    for (auto& f : doc->frames)
        UpdateTextShapeBoundsInFrame(*f);
}

// Draws the full canvas (background + frames/shapes + selection) into whatever
// port is currently active, erasing `eraseRect` first. Factored out of
// DrawWindowContent so it can target either the offscreen double-buffer or,
// as a low-memory fallback, the window directly.
static void DrawCanvasInto(const Rect& eraseRect) {
    RGBColor canvasBg = { 0xDDDD, 0xDDDD, 0xDDDD };
    RGBBackColor(&canvasBg);
    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);
    EraseRect(&eraseRect);

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

void DrawWindowContent(WindowRef win, const Rect* clipTo) {
    // Direct draw — NOT double-buffered. Two separate attempts at an
    // offscreen-GWorld + CopyBits double buffer (hardcoded 32-bit, then
    // pixelDepth=0 to match the screen) both corrupted the shared system
    // color palette SCREEN-WIDE (affecting the desktop background and other
    // apps' chrome, not just this window) after enough redraws in a live
    // session. CopyBits onto the window in this environment is unsafe
    // regardless of source depth — do not reintroduce it here without a
    // fundamentally different approach. Flicker on expensive redraws
    // (rotated text/frames) is mitigated instead by restricting which
    // callers actually need to redraw the whole window (see `clipTo`).
    //
    // SetPortWindowPort must happen BEFORE UpdateAllTextShapeBounds: that
    // call measures text via StringWidth/CharWidth, which read the CURRENT
    // port's font state — if some other window (Inspector, Layers) was
    // last active, auto-sizing text would get measured against the wrong
    // port. Clicking to select a shape refreshes those panels right before
    // this runs, so this ordering bug was live on exactly that path.
    SetPortWindowPort(win);
    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    gActivePortBounds = portRect;

    UpdateAllTextShapeBounds(gDocument);
    RunDocumentLayout(gDocument);

    // Explicitly reset the clip to the full port on every call, rather than
    // relying on SetPortWindowPort's clip-reset side effect: if that's
    // skipped when `win` is already the current port (a common SetPort
    // optimization), a tight clip left behind by an earlier resize/rotate
    // drag's dirtyRect could silently leak into a later plain/unclipped
    // call here, so only that small stale region actually gets erased and
    // redrawn while genuinely-stale content everywhere else on screen goes
    // untouched. ClipRect wants a non-const Rect* in these old headers, so
    // a mutable local copy is needed either way.
    ClipRect(&portRect);
    if (clipTo) { Rect cr = *clipTo; ClipRect(&cr); }
    // The classic Mac OS software cursor auto-saves/restores the pixels
    // under it, but only for drawing it can see going through QuickDraw.
    // FastPixelWriter writes raw bytes straight into the framebuffer,
    // bypassing QuickDraw entirely -- the cursor manager never learns
    // those pixels changed, so the next time it needs to redraw the
    // cursor (any mouse movement) it pastes its own stale saved copy of
    // what used to be there right back over the freshly-rotated glyph
    // pixels wherever the cursor happens to be sitting. HideCursor/
    // ShowCursor nest via an internal counter, so bracketing every redraw
    // here is safe even if a caller further up also does it.
    HideCursor();
    DrawCanvasInto(portRect);
    ShowCursor();
    if (clipTo) ClipRect(&portRect);
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

// Recursively search inside `f`. Returns deepest match. `ambient` is the
// rotation chain accumulated from f's ancestors (empty if f is top-level or
// no ancestor is rotated) — see RotChain / DrawFrame for the convention.
static HitResult HitTestFrame(Frame* f, Point pt, const RotChain& ambient = {}) {
    Rect r = CanvasRect(f->bounds);
    RotChain childChain;
    if (f->rotation != 0) {
        double cx = (r.left + r.right) * 0.5, cy = (r.top + r.bottom) * 0.5;
        childChain.push_back({ static_cast<double>(f->rotation), cx, cy });
    }
    childChain.insert(childChain.end(), ambient.begin(), ambient.end());

    // Children are checked even when pt falls outside the frame's own
    // rect (unless clipContent visually cuts them off there too): a
    // rotated child can legitimately extend past its parent and remain
    // fully visible, and needs to stay clickable/draggable there --
    // bailing out early on the frame's own bounds made an overflowing
    // rotated shape impossible to select once enough of it sat outside
    // its parent frame.
    bool insideFrame = PtInLocalRect(f->bounds, pt, ambient);
    if (!insideFrame && f->clipContent) return {};

    if (!f->childOrder.empty()) {
        // Iterate in reverse childOrder (topmost first for correct z-order hit-testing)
        for (auto it = f->childOrder.rbegin(); it != f->childOrder.rend(); ++it) {
            if (it->isFrame) {
                HitResult res = HitTestFrame(f->childFrames[it->idx].get(), pt, childChain);
                if (res.found) return res;
            } else {
                const auto& s = f->children[it->idx];
                bool hit = (s->rotation != 0)
                    ? HitTestRotated(s->bounds, s->rotation, pt, childChain)
                    : PtInLocalRect(s->bounds, pt, childChain);
                if (hit) return { f, s.get(), true };
            }
        }
    } else {
        // Legacy fallback: child frames then shapes (both in reverse for topmost-first)
        for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
            HitResult res = HitTestFrame(it->get(), pt, childChain);
            if (res.found) return res;
        }
        for (auto it = f->children.rbegin(); it != f->children.rend(); ++it) {
            const auto& s = *it;
            bool hit = (s->rotation != 0)
                ? HitTestRotated(s->bounds, s->rotation, pt, childChain)
                : PtInLocalRect(s->bounds, pt, childChain);
            if (hit) return { f, s.get(), true };
        }
    }
    if (!insideFrame) return {};  // no overflowing child hit, and click wasn't on the frame body itself
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

// Walks upward from `startFrame` through Frame::parent, collecting each
// rotated ancestor's (angle, own local screen center) in [nearest...root]
// order — the RotChain convention used throughout rendering/hit-testing.
static RotChain AncestorChainFor(Frame* startFrame) {
    RotChain chain;
    for (Frame* f = startFrame; f != nullptr; f = f->parent) {
        if (f->rotation != 0) {
            Rect r = CanvasRect(f->bounds);
            double cx = (r.left + r.right) * 0.5, cy = (r.top + r.bottom) * 0.5;
            chain.push_back({ static_cast<double>(f->rotation), cx, cy });
        }
    }
    return chain;
}

// Ancestor rotation chain for whichever single object is currently selected
// (empty if nothing selected, or nothing above it is rotated).
static RotChain SelectedAmbientChain() {
    if (gSelectedShape) return AncestorChainFor(LocateShapeParent(gSelectedShape));
    if (gSelectedFrame) return AncestorChainFor(gSelectedFrame->parent);
    return {};
}

static double SelectedOwnRotation() {
    if (gSelectedShape) return static_cast<double>(gSelectedShape->rotation);
    if (gSelectedFrame)  return static_cast<double>(gSelectedFrame->rotation);
    return 0.0;
}

// Returns the handle index (0-7) if pt lands on one of the 8 selection
// handles drawn by DrawSelectionHighlight, or -1 if nothing selected / miss.
// Handle order: 0=TL 1=TC 2=TR 3=MR 4=BR 5=BC 6=BL 7=ML
// Populate hx[8]/hy[8] with the 8 handle positions for the current selection.
// Order matches DrawSelectionHighlight: corners 0,2,4,6; edges 1,3,5,7.
static bool ComputeSelectionHandles(short hx[8], short hy[8]) {
    if (!gSelectedShape && !gSelectedFrame) return false;

    double ownRot = SelectedOwnRotation();
    RotChain ambient = SelectedAmbientChain();

    if (ownRot != 0.0 || !ambient.empty()) {
        Rect r = gSelectedShape ? CanvasRect(gSelectedShape->bounds)
                                : CanvasRect(gSelectedFrame->bounds);
        double cx = (r.left + r.right) * 0.5, cy = (r.top + r.bottom) * 0.5;
        double hw = (r.right - r.left) * 0.5, hh = (r.bottom - r.top) * 0.5;
        double rad = ownRot * 3.14159265358979323846 / 180.0;
        double cosA = std::cos(rad), sinA = std::sin(rad);
        double lx[4] = {-hw, hw, hw, -hw}, ly[4] = {-hh, -hh, hh, hh};
        short px[4], py[4];
        for (int i = 0; i < 4; ++i) {
            double ox = cx + lx[i]*cosA - ly[i]*sinA;
            double oy = cy + lx[i]*sinA + ly[i]*cosA;
            double fx, fy;
            ApplyRotChain(ambient, ox, oy, fx, fy);
            Point p = ToQDPoint(fx, fy);
            px[i] = p.h; py[i] = p.v;
        }
        // Interleave corners (px[0..3] = TL,TR,BR,BL) with edge midpoints so the
        // index convention matches the unrotated branch below: 0=TL,1=N,2=TR,3=E,
        // 4=BR,5=S,6=BL,7=W. (A flat 0..3=corners,4..7=mids layout here would put
        // TR/BL at indices 1/3 — outside the {0,2,4,6} corner set used elsewhere —
        // making those two corners' rotate zone unreachable once rotated.)
        hx[0]=px[0];              hy[0]=py[0];
        hx[1]=(px[0]+px[1])/2;    hy[1]=(py[0]+py[1])/2;
        hx[2]=px[1];              hy[2]=py[1];
        hx[3]=(px[1]+px[2])/2;    hy[3]=(py[1]+py[2])/2;
        hx[4]=px[2];              hy[4]=py[2];
        hx[5]=(px[2]+px[3])/2;    hy[5]=(py[2]+py[3])/2;
        hx[6]=px[3];              hy[6]=py[3];
        hx[7]=(px[3]+px[0])/2;    hy[7]=(py[3]+py[0])/2;
    } else {
        Rect r = gSelectedShape ? CanvasRect(gSelectedShape->bounds)
                                : CanvasRect(gSelectedFrame->bounds);
        short cx = static_cast<short>((r.left + r.right) / 2);
        short cy = static_cast<short>((r.top  + r.bottom) / 2);
        hx[0]=r.left;  hy[0]=r.top;
        hx[1]=cx;      hy[1]=r.top;
        hx[2]=r.right; hy[2]=r.top;
        hx[3]=r.right; hy[3]=cy;
        hx[4]=r.right; hy[4]=r.bottom;
        hx[5]=cx;      hy[5]=r.bottom;
        hx[6]=r.left;  hy[6]=r.bottom;
        hx[7]=r.left;  hy[7]=cy;
    }
    return true;
}

static int HitTestHandles(Point pt) {
    short hx[8], hy[8];
    if (!ComputeSelectionHandles(hx, hy)) return -1;
    for (int i = 0; i < 8; ++i) {
        short adx = static_cast<short>(pt.h - hx[i]); if (adx < 0) adx = -adx;
        short ady = static_cast<short>(pt.v - hy[i]); if (ady < 0) ady = -ady;
        if (adx <= kHandleHW && ady <= kHandleHW) return i;
    }
    return -1;
}

// Caps the rotate-zone reach for corner `cornerIdx` so it never swallows the
// hit-zone of either adjacent edge-mid handle. Without this, on small shapes
// (short text boxes especially) the rotate zone can extend past an edge
// handle entirely, making that handle unreachable except by zooming in.
static short EffectiveRotateZone(const short hx[8], const short hy[8], int cornerIdx) {
    int adj[2] = { (cornerIdx + 1) % 8, (cornerIdx + 7) % 8 };
    short minDist = 32767;
    for (int a : adj) {
        short adx = static_cast<short>(hx[cornerIdx] - hx[a]); if (adx < 0) adx = -adx;
        short ady = static_cast<short>(hy[cornerIdx] - hy[a]); if (ady < 0) ady = -ady;
        short dist = adx > ady ? adx : ady;
        if (dist < minDist) minDist = dist;
    }
    short cap = static_cast<short>(minDist - kHandleHW - 2);
    if (cap < 4) cap = 4;
    return (cap < kRotateZone) ? cap : kRotateZone;
}

// Returns the handle index (corners only — 0,2,4,6) if pt is in that
// handle's rotate zone (near the handle, outside its square), or -1 if not
// in any rotate zone. Edge midpoints (1,3,5,7) are resize-only by design:
// corners do both resize (click directly on the handle) and rotate (click
// just outside it), matching UpdateCanvasCursor's cursor selection above,
// which never showed a rotate cursor over an edge handle in the first
// place -- extending the hit-test to edges (a prior session's change) left
// the cursor and the actual click behavior inconsistent with each other.
static int HitTestRotateZone(Point pt) {
    short hx[8], hy[8];
    if (!ComputeSelectionHandles(hx, hy)) return -1;
    static const int kCorner[4] = {0, 2, 4, 6};
    for (int ci = 0; ci < 4; ++ci) {
        int i = kCorner[ci];
        short adx = static_cast<short>(pt.h - hx[i]); if (adx < 0) adx = -adx;
        short ady = static_cast<short>(pt.v - hy[i]); if (ady < 0) ady = -ady;
        short dist = adx > ady ? adx : ady;
        short zone = EffectiveRotateZone(hx, hy, i);
        if (dist > kHandleHW && dist <= kHandleHW + zone) return i;
    }
    return -1;
}

// Live poll of the physical Shift key, independent of whatever modifiers
// were latched at mouseDown -- used so aspect-lock during a resize drag
// engages/disengages in real time as the user holds/releases Shift mid-drag
// (matches Figma), rather than being fixed for the whole drag by whether
// Shift happened to be down at the initial click.
static bool IsShiftKeyDownNow() {
    KeyMap km;
    GetKeys(km);
    return (reinterpret_cast<UInt8*>(km)[56 >> 3] & (1 << (56 & 7))) != 0;
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
    // Own rotation matters for keeping the opposite handle visually anchored
    // (that math is entirely local, see below); the mouse, though, moves in
    // true screen space, so un-rotating its delta needs the NET rotation —
    // own rotation plus every rotated ancestor frame above it.
    double ownRotDeg = SelectedOwnRotation();
    double ambientRotDeg = 0.0;
    for (const auto& step : SelectedAmbientChain()) ambientRotDeg += step.angleDeg;
    double netRotDeg = ownRotDeg + ambientRotDeg;

    static const SInt32 kMin = 10;
    bool isCorner   = (hi == 0 || hi == 2 || hi == 4 || hi == 6);
    Bounds2 origB   = *b;   // snapshot for absolute delta calculation
    double origHalfW = origB.w * 0.5, origHalfH = origB.h * 0.5;
    double origCenterX = origB.x + origHalfW, origCenterY = origB.y + origHalfH;
    double netRad = netRotDeg * 3.14159265358979323846 / 180.0;
    double netCosT = std::cos(netRad), netSinT = std::sin(netRad);
    double ownRad = ownRotDeg * 3.14159265358979323846 / 180.0;
    double ownCosT = std::cos(ownRad), ownSinT = std::sin(ownRad);
    // Anchor sign: the side NOT being dragged (0 on an axis this handle doesn't touch).
    int signX = bL[hi] ? 1 : (bR[hi] ? -1 : 0);
    int signY = bT[hi] ? 1 : (bB[hi] ? -1 : 0);

    Point prev = startPt, curr = startPt;
    bool pushedUndo = false;

    // Same dirty-rect clipping HandleRotateDrag already uses, adapted for
    // resize: unlike rotation the object's on-screen footprint isn't a
    // fixed radius (it's changing every frame by definition), so recompute
    // it each frame from the CURRENT bounds and union with the previous
    // frame's footprint -- covers both what needs to be redrawn (new
    // position) and what needs to be erased (old position), without
    // resorting to a full, unclipped whole-window redraw on every
    // mouse-move like this was doing before.
    RotChain resizeAmbient = SelectedAmbientChain();
    auto ComputeReachRect = [&](const Bounds2& bnd) -> Rect {
        Rect rr = CanvasRect(bnd);
        double cx = (rr.left+rr.right)*0.5, cy = (rr.top+rr.bottom)*0.5;
        double hw = (rr.right-rr.left)*0.5, hh = (rr.bottom-rr.top)*0.5;
        double rad = ownRotDeg * 3.14159265358979323846 / 180.0;
        double ca = std::cos(rad), sa = std::sin(rad);
        double lx[4] = {-hw, hw, hw, -hw}, ly[4] = {-hh, -hh, hh, hh};
        short minX=32767, maxX=-32768, minY=32767, maxY=-32768;
        for (int i = 0; i < 4; ++i) {
            double ox = cx + lx[i]*ca - ly[i]*sa, oy = cy + lx[i]*sa + ly[i]*ca;
            double fx, fy; ApplyRotChain(resizeAmbient, ox, oy, fx, fy);
            Point p = ToQDPoint(fx, fy);
            minX = std::min(minX, p.h); maxX = std::max(maxX, p.h);
            minY = std::min(minY, p.v); maxY = std::max(maxY, p.v);
        }
        short pad = 60;
        return Rect{ static_cast<short>(minY-pad), static_cast<short>(minX-pad),
                     static_cast<short>(maxY+pad), static_cast<short>(maxX+pad) };
    };
    Rect prevReach = ComputeReachRect(*b);

    while (Button()) {
        GetMouse(&curr);
        if (curr.h != prev.h || curr.v != prev.v) {
            if (!pushedUndo) {
                PushUndo();
                pushedUndo = true;

                // Text sizing mode must switch right as the drag starts, not
                // after it ends: DrawWindowContent re-derives bounds from the
                // CURRENT sizing mode every frame (UpdateTextShapeBounds), so
                // while the shape is still Auto Width it fights every frame
                // of the drag back to its hugged size — the first drag looks
                // like it does nothing until the mode actually switches.
                // Matches Figma: touching height (alone or via a corner,
                // which touches both) always fixes it; touching only width
                // while Auto Width switches to Auto Height (width becomes
                // fixed, height keeps hugging wrapped content) instead.
                if (gSelectedShape && gSelectedShape->GetType() == Shape::kText) {
                    TextShape* ts = static_cast<TextShape*>(gSelectedShape);
                    bool widthChanged  = bL[hi] || bR[hi];
                    bool heightChanged = bT[hi] || bB[hi];
                    if (heightChanged) {
                        ts->textSizing = TextSizing::Fixed;
                    } else if (widthChanged && ts->textSizing == TextSizing::AutoWidth) {
                        ts->textSizing = TextSizing::AutoHeight;
                    }
                }
                // Same idea for a Frame using Auto Layout Hug/Fill sizing:
                // manually dragging a resize handle always overrides that
                // axis to Fixed, same as Figma, or the frame just snaps
                // right back to its hugged size on the very next redraw
                // (RunDocumentLayout re-derives Hug/Fill dimensions every
                // frame) and the drag looks like it does nothing.
                if (gSelectedFrame) {
                    bool widthChanged  = bL[hi] || bR[hi];
                    bool heightChanged = bT[hi] || bB[hi];
                    if (widthChanged  && gSelectedFrame->widthSizing  != SizingMode::Fixed)
                        gSelectedFrame->widthSizing  = SizingMode::Fixed;
                    if (heightChanged && gSelectedFrame->heightSizing != SizingMode::Fixed)
                        gSelectedFrame->heightSizing = SizingMode::Fixed;
                }
            }

            // Total mouse delta from drag start, in canvas units.
            double dCanvasX = static_cast<double>(curr.h - startPt.h) * 100.0 / gCanvasZoom;
            double dCanvasY = static_cast<double>(curr.v - startPt.v) * 100.0 / gCanvasZoom;

            // Un-rotate into the shape's own local axes: dragging along a rotated
            // edge/corner should change local width/height, not screen x/y directly.
            // Uses the NET (own + ancestor) rotation since the mouse moves in
            // true screen space, past every rotated frame this object sits in.
            double localDX =  dCanvasX * netCosT + dCanvasY * netSinT;
            double localDY = -dCanvasX * netSinT + dCanvasY * netCosT;

            double newW = origB.w, newH = origB.h;
            if (bL[hi]) newW = origB.w - localDX;
            if (bR[hi]) newW = origB.w + localDX;
            if (bT[hi]) newH = origB.h - localDY;
            if (bB[hi]) newH = origB.h + localDY;

            // Aspect ratio lock: inspector button OR Shift currently held (polled
            // live every mouse-move, not just at drag start, so toggling Shift
            // mid-drag engages/disengages the lock in real time, matching Figma).
            bool lockAR = isCorner && (IsAspectLocked() || IsShiftKeyDownNow());
            if (lockAR && origB.w > 0 && origB.h > 0)
                newH = newW * origB.h / origB.w;

            if (newW < kMin) newW = kMin;
            if (newH < kMin) newH = kMin;

            // Keep the handle opposite the dragged one fixed on screen: solve for the
            // new center that leaves that anchor point's rotated position unchanged.
            // Purely local math (bounds.x/y live in local/pre-ambient space, and any
            // enclosing ambient rotation is constant during this drag), so it only
            // needs the object's OWN rotation, not the net one used above.
            double halfW = newW * 0.5, halfH = newH * 0.5;
            double dLocalX = signX * (origHalfW - halfW);
            double dLocalY = signY * (origHalfH - halfH);
            double newCenterX = origCenterX + (dLocalX * ownCosT - dLocalY * ownSinT);
            double newCenterY = origCenterY + (dLocalX * ownSinT + dLocalY * ownCosT);

            double newX = newCenterX - halfW, newY = newCenterY - halfH;
            b->w = static_cast<SInt32>(newW + (newW >= 0 ? 0.5 : -0.5));
            b->h = static_cast<SInt32>(newH + (newH >= 0 ? 0.5 : -0.5));
            b->x = static_cast<SInt32>(newX + (newX >= 0 ? 0.5 : -0.5));
            b->y = static_cast<SInt32>(newY + (newY >= 0 ? 0.5 : -0.5));

            Rect newReach = ComputeReachRect(*b);
            Rect dirtyRect = {
                std::min(prevReach.top,  newReach.top),
                std::min(prevReach.left, newReach.left),
                std::max(prevReach.bottom, newReach.bottom),
                std::max(prevReach.right,  newReach.right)
            };
            DrawWindowContent(win, &dirtyRect);
            prevReach = newReach;
            prev = curr;
        }
    }

    Rect pr; GetWindowPortBounds(win, &pr); InvalWindowRect(win, &pr);
}

// --------------------------------------------------------------------------
// Name-label hit-tests (canvas coordinate space, port = main window)
// --------------------------------------------------------------------------

// The clickable rect for a name label, matching where it's actually drawn:
// DrawShapeNameLabel anchors a rotated label above whichever of the 4
// corners is currently highest on screen (labels don't spin, but they do
// track rotation's effect on the shape's own footprint). The label
// hit-test rects used to always sit above the UNROTATED bounding box, so
// for a rotated shape that phantom zone could swing up right into the
// visually rotated body itself — a click (or double-click, meant to enter
// text edit) landing anywhere in that overlap got misidentified as a
// label hit instead of a body hit.
static Rect LabelHitRect(const Bounds2& bounds, double rotationDeg, short textWidthPx) {
    Rect r = CanvasRect(bounds);
    if (rotationDeg == 0.0) {
        return Rect{ static_cast<short>(r.top-16), r.left,
                     static_cast<short>(r.top-1),   static_cast<short>(r.left+textWidthPx+4) };
    }
    double cx = (r.left+r.right)*0.5, cy = (r.top+r.bottom)*0.5;
    double hw = (r.right-r.left)*0.5, hh = (r.bottom-r.top)*0.5;
    double rad = rotationDeg * 3.14159265358979323846 / 180.0;
    double cosA = std::cos(rad), sinA = std::sin(rad);
    double lx4[4] = {-hw, hw, hw, -hw}, ly4[4] = {-hh, -hh, hh, hh};
    double topX = 0, topY = 1e18;
    for (int i = 0; i < 4; ++i) {
        double px = cx + lx4[i]*cosA - ly4[i]*sinA, py = cy + lx4[i]*sinA + ly4[i]*cosA;
        if (py < topY) { topY = py; topX = px; }
    }
    Point lp = ToQDPoint(topX, topY - 5);
    return Rect{ static_cast<short>(lp.v-10), lp.h,
                 static_cast<short>(lp.v+3),  static_cast<short>(lp.h+textWidthPx+4) };
}

// Returns the Shape whose name label contains pt, searching recursively
// through the given frame and its children.
static Shape* HitTestShapeLabel(const Frame* f, Point pt) {
    for (auto it = f->children.rbegin(); it != f->children.rend(); ++it) {
        Shape* s = it->get();
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
        Rect lr = LabelHitRect(s->bounds, s->rotation, tw);
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
    Str255 pn; pn[0] = 0;
    const char* nm = f->name.c_str();
    for (int i = 0; nm[i] && i < 63; ++i) { pn[i+1] = (unsigned char)nm[i]; pn[0]++; }

    TextSize(10);
    short tw = StringWidth(pn);
    TextSize(12);

    Rect label = LabelHitRect(f->bounds, f->rotation, tw);
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
        std::string lbl = s->name;
        if (lbl.empty()) {
            if      (s->GetType() == Shape::kEllipse) lbl = "Ellipse";
            else if (s->GetType() == Shape::kText)    lbl = "Text";
            else                                      lbl = "Rectangle";
        }
        Str255 pn; ToPStr(lbl, pn);
        TextSize(10); short tw = StringWidth(pn); TextSize(12);
        Rect lr = LabelHitRect(s->bounds, s->rotation, tw);
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

// Rotate the selected shape/frame by dragging around its center.
static void HandleRotateDrag(WindowRef win, Point startPt, int cornerIdx) {
    SInt16* pRot = gSelectedShape ? &gSelectedShape->rotation
                                  : (gSelectedFrame ? &gSelectedFrame->rotation : nullptr);
    if (!pRot) return;

    Bounds2* pB = gSelectedShape ? &gSelectedShape->bounds : &gSelectedFrame->bounds;
    Rect r = CanvasRect(*pB);
    // True screen-space pivot: the object's own local center carried through any
    // rotated ancestor frames (a rigid rotation preserves angles, so once the
    // pivot is right, mouse-angle delta maps 1:1 onto the object's own rotation
    // regardless of ambient rotation).
    RotChain ambient = SelectedAmbientChain();
    double screenCX, screenCY;
    ApplyRotChain(ambient, (r.left + r.right) * 0.5, (r.top + r.bottom) * 0.5, screenCX, screenCY);

    double startAngle = std::atan2(static_cast<double>(startPt.v) - screenCY,
                                   static_cast<double>(startPt.h) - screenCX)
                        * 180.0 / 3.14159265358979323846;
    SInt16 origRot = *pRot;
    bool pushedUndo = false;

    double ambientRotDeg = 0.0;
    for (const auto& step : ambient) ambientRotDeg += step.angleDeg;
    SetCursor(GetRotateCursor(HandleBucket(cornerIdx, origRot + ambientRotDeg)));

    // Clip every redraw during the drag to a generous region around the
    // pivot instead of the whole window: DrawWindowContent redraws the
    // entire canvas from scratch every call (erase, then repaint), and with
    // no double buffer available in this environment (see DrawWindowContent
    // — CopyBits corrupts the screen here), that erase is what's visible as
    // flicker. Restricting it to just the rotating object's own worst-case
    // footprint makes each redraw touch far fewer pixels, without changing
    // anything about how or what gets drawn. The radius a rotating object
    // can reach from its own center is rotation-invariant (a rigid rotation
    // preserves distances, ambient or own), so this region is computed once
    // and covers every angle throughout the drag. Padding only needs to
    // cover selection handles, the rotate-zone reach beyond them (well under
    // kHandleHW+kRotateZone px), and a short name label — keep it tight, a
    // large pad on anything but a tiny shape makes the "restricted" region
    // barely smaller than the whole window and defeats the point.
    double halfW = (r.right - r.left) * 0.5, halfH = (r.bottom - r.top) * 0.5;
    double reach = std::sqrt(halfW*halfW + halfH*halfH) + 60.0;
    Rect dirtyRect = {
        static_cast<short>(screenCY - reach), static_cast<short>(screenCX - reach),
        static_cast<short>(screenCY + reach), static_cast<short>(screenCX + reach)
    };

    Point prev = startPt, curr = startPt;
    while (Button()) {
        GetMouse(&curr);
        if (curr.h != prev.h || curr.v != prev.v) {
            if (!pushedUndo) { PushUndo(); pushedUndo = true; }
            double curAngle = std::atan2(static_cast<double>(curr.v) - screenCY,
                                         static_cast<double>(curr.h) - screenCX)
                              * 180.0 / 3.14159265358979323846;
            double delta = curAngle - startAngle;
            int newRotI = static_cast<int>(origRot + delta + 0.5);
            *pRot = static_cast<SInt16>(((newRotI % 360) + 360) % 360);
            SetCursor(GetRotateCursor(HandleBucket(cornerIdx, *pRot + ambientRotDeg)));
            // DrawWindowContent already runs layout itself right before drawing;
            // doing it again here was pure redundant work on every mouse-move.
            DrawWindowContent(win, &dirtyRect);
            prev = curr;
        }
    }

    Rect pr; GetWindowPortBounds(win, &pr); InvalWindowRect(win, &pr);
}

// Forward declaration — full definition lives further down, near HandleTextPlace.
static void EditTextInPlace(WindowRef win, TextShape* ts, bool pushUndoOnCommit = true);

// --------------------------------------------------------------------------
// Select tool: resize handles → name labels → body hit-test → move + reparent
// --------------------------------------------------------------------------

void HandleCanvasSelect(WindowRef win, Point startGlobal, UInt16 modifiers) {
    if (!gDocument) return;

    SetPortWindowPort(win);
    Point pt = startGlobal;
    GlobalToLocal(&pt);

    // ---- 1a. Rotate zone (near corner, outside handle — checked before resize) ----
    int rotateCorner = HitTestRotateZone(pt);
    if (rotateCorner >= 0) {
        bool selLocked = gSelectedShape ? gSelectedShape->locked
                                        : (gSelectedFrame ? gSelectedFrame->locked : false);
        if (!selLocked) HandleRotateDrag(win, pt, rotateCorner);
        return;
    }

    // ---- 1b. Resize handle (on the handle square) ----
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
            std::string lbl = s->name;
            if (lbl.empty()) {
                if      (s->GetType() == Shape::kEllipse) lbl = "Ellipse";
                else if (s->GetType() == Shape::kText)    lbl = "Text";
                else                                      lbl = "Rectangle";
            }
            Str255 pn; ToPStr(lbl, pn);
            TextSize(10); short tw = StringWidth(pn); TextSize(12);
            Rect lr = LabelHitRect(s->bounds, s->rotation, tw);
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

    // ---- Double-click: edit text content, or rename the hit object ----
    if (found && gIsDoubleClick) {
        // Determine which object's name to edit.
        // Priority: shape label > frame label > any body hit
        std::string* targetName = nullptr;
        bool isLabelHit = false;

        // Check shape labels across all frames first
        for (auto it = gDocument->frames.begin(); it != gDocument->frames.end() && !targetName; ++it) {
            Shape* sl = HitTestShapeLabel(it->get(), pt);
            if (sl) { targetName = &sl->name; isLabelHit = true; }
        }
        // Check frame labels
        if (!targetName) {
            for (auto it = gDocument->frames.begin(); it != gDocument->frames.end() && !targetName; ++it) {
                Frame* fl = HitTestFrameLabel(it->get(), pt);
                if (fl) { targetName = &fl->name; isLabelHit = true; }
            }
        }

        // A double-click on a text shape's BODY (not its name label) edits its
        // on-canvas text content instead of renaming its layer name.
        if (!isLabelHit && gSelectedShape && gSelectedShape->GetType() == Shape::kText
            && !gSelectedShape->locked) {
            EditTextInPlace(win, static_cast<TextShape*>(gSelectedShape));
            gIsDoubleClick = false;
            RefreshLayersPanel();
            RefreshInspector();
            return;
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

        // Single-shape move: same dirty-rect approach HandleResizeDrag/
        // HandleRotateDrag already use, so this loop stops doing a full,
        // unclipped whole-window redraw on every mouse-move. That matters
        // more than it used to now that a rotated text box wider than the
        // window does real per-frame capture/paint work (see the kText
        // case's needR slice) -- without this, a slow redraw falls behind
        // the physical mouse, and whatever frame last finished rendering
        // is what's left on screen, looking like newly-exposed content
        // stays hidden until you keep dragging.
        // Excludes isLayoutDrag: an auto-layout parent can reposition OTHER
        // siblings live as a side effect of this drag (reflow), and a narrow
        // dirty-rect covering only the dragged shape's own reach never erases
        // those siblings' previous positions, leaving ghost copies on screen.
        bool singleShapeMove = !isMultiDrag && gSelectedFrames.empty() && hitShape != nullptr && !isLayoutDrag;
        auto reachFor = [&](Shape* s) -> Rect {
            RotChain amb = AncestorChainFor(LocateShapeParent(s));
            Rect rr = CanvasRect(s->bounds);
            double cx = (rr.left+rr.right)*0.5, cy = (rr.top+rr.bottom)*0.5;
            double hw = (rr.right-rr.left)*0.5, hh = (rr.bottom-rr.top)*0.5;
            double rad = static_cast<double>(s->rotation) * 3.14159265358979323846 / 180.0;
            double ca = std::cos(rad), sa = std::sin(rad);
            double lx[4] = {-hw, hw, hw, -hw}, ly[4] = {-hh, -hh, hh, hh};
            short minX=32767, maxX=-32768, minY=32767, maxY=-32768;
            for (int i = 0; i < 4; ++i) {
                double ox = cx + lx[i]*ca - ly[i]*sa, oy = cy + lx[i]*sa + ly[i]*ca;
                double fx, fy; ApplyRotChain(amb, ox, oy, fx, fy);
                Point p = ToQDPoint(fx, fy);
                minX = std::min(minX, p.h); maxX = std::max(maxX, p.h);
                minY = std::min(minY, p.v); maxY = std::max(maxY, p.v);
            }
            short pad = 60;
            return Rect{ static_cast<short>(minY-pad), static_cast<short>(minX-pad),
                         static_cast<short>(maxY+pad), static_cast<short>(maxX+pad) };
        };
        Rect prevReach = singleShapeMove ? reachFor(hitShape) : Rect{0,0,0,0};

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

                if (singleShapeMove) {
                    Rect currReach = reachFor(hitShape);
                    Rect dirty = { std::min(prevReach.top, currReach.top),
                                    std::min(prevReach.left, currReach.left),
                                    std::max(prevReach.bottom, currReach.bottom),
                                    std::max(prevReach.right, currReach.right) };
                    DrawWindowContent(win, &dirty);
                    prevReach = currReach;
                } else {
                    DrawWindowContent(win);
                }
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
// Text placement + in-canvas text editing (TENew overlay directly on the
// canvas, matching Figma's V-tool-double-click / T-tool-click-to-edit)
// --------------------------------------------------------------------------

// Topmost shape body (any type) under `pt`, searching frame contents first
// (deepest/topmost within each) and root-level items, or nullptr for none.
static Shape* HitTestAnyShapeBodyAt(Point pt) {
    if (!gDocument->rootChildOrder.empty()) {
        for (auto it = gDocument->rootChildOrder.rbegin(); it != gDocument->rootChildOrder.rend(); ++it) {
            if (it->isFrame) {
                HitResult res = HitTestFrame(gDocument->frames[it->idx].get(), pt);
                if (res.found) return res.shape;  // may be nullptr (hit frame body, not a shape)
            } else {
                Shape* s = gDocument->rootShapes[it->idx].get();
                bool hit = (s->rotation != 0) ? HitTestRotated(s->bounds, s->rotation, pt)
                                               : PtInLocalRect(s->bounds, pt, {});
                if (hit) return s;
            }
        }
    } else {
        for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend(); ++it) {
            HitResult res = HitTestFrame(it->get(), pt);
            if (res.found) return res.shape;
        }
        for (auto it = gDocument->rootShapes.rbegin(); it != gDocument->rootShapes.rend(); ++it) {
            Shape* s = it->get();
            bool hit = (s->rotation != 0) ? HitTestRotated(s->bounds, s->rotation, pt)
                                           : PtInLocalRect(s->bounds, pt, {});
            if (hit) return s;
        }
    }
    return nullptr;
}

// Edits `ts`'s text content in place, as a TextEdit overlay drawn directly on
// the canvas at the shape's current (unrotated — TE can't rotate either)
// screen rect. Blocks until the user clicks elsewhere or presses Enter/Escape.
// If `pushUndoOnCommit` is false, the caller is responsible for having already
// pushed an undo snapshot (used when creating a brand-new shape, so "type text
// then click away" is one undo step, not two).
static void EditTextInPlace(WindowRef win, TextShape* ts, bool pushUndoOnCommit) {
    if (!ts || ts->locked) return;
    gEditingTextShape = ts;

    SetPortWindowPort(win);
    Rect editR = CanvasRect(ts->bounds);
    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    gActivePortBounds = portRect;

    short fontID = 0;
    if (!ts->fontFamily.empty()) {
        Str255 fname; fname[0] = 0;
        for (size_t i = 0; i < ts->fontFamily.size() && i < 63; ++i) {
            fname[i+1] = static_cast<unsigned char>(ts->fontFamily[i]); fname[0]++;
        }
        GetFNum(fname, &fontID);
    }
    short scaledSize = static_cast<short>(SInt32(ts->fontSize) * gCanvasZoom / 100);
    if (scaledSize < 4) scaledSize = 4;
    if (scaledSize > 127) scaledSize = 127;
    auto applyFont = [&]() { TextFont(fontID); TextSize(scaledSize); TextFace(ts->fontFace); };
    applyFont();

    // AutoWidth text should grow to fit what's typed, never wrap. destRect
    // controls TE's own word-wrap width, so it gets a generous fixed
    // allowance so TE itself never wraps -- but viewRect controls what TE
    // actually erases/draws/clips to, so it must stay matched to the real
    // (small, growing) box, or TEUpdate erases across destRect's whole
    // width regardless of what rect gets passed to it, leaving a stray
    // block of blank canvas out past the visible text. viewRect gets
    // resynced to editR on every remeasure below as the box grows.
    Rect destR = editR;
    if (ts->textSizing == TextSizing::AutoWidth) {
        destR.right  = static_cast<short>(destR.left + 4000);
        destR.bottom = static_cast<short>(destR.top + 1000);
    }
    Rect viewR = editR;
    TEHandle teh = TENew(&destR, &viewR);
    if (!teh) { gEditingTextShape = nullptr; return; }

    // TE uses CR (\r) for line breaks; our stored text uses LF (\n).
    std::string teText = ts->text;
    for (char& c : teText) if (c == '\n') c = '\r';
    if (!teText.empty())
        TESetText(const_cast<Ptr>(teText.c_str()), static_cast<long>(teText.size()), teh);
    TESetSelect(0, static_cast<long>((*teh)->teLength), teh);
    TEActivate(teh);

    // Rotation this shape has while editing: own rotation plus whatever
    // ambient rotation its parent frame chain contributes. TextEdit itself
    // has no concept of rotation at all -- it always draws upright into
    // `editR`. To make editing itself appear rotated (not just the static,
    // non-editing view), every redraw renders TE's upright output into
    // editR as a staging area, captures those exact pixels, restores
    // whatever was underneath, then paints the captured block into its
    // rotated position via the same technique used for the settled/static
    // text rendering (see PaintRotatedPixelBlock).
    double ownRot = static_cast<double>(ts->rotation);
    RotChain ambient = AncestorChainFor(LocateShapeParent(ts));
    RotChain full;
    short editSrcW = 0, editSrcH = 0;
    bool anyRot = (ownRot != 0.0) || !ambient.empty();
    bool canRotateEdit = false;
    short lineH = static_cast<short>(SInt32(scaledSize) * ts->lineHeight / 100);
    if (lineH < 1) lineH = scaledSize;

    // Rotation pivot is fixed RELATIVE TO editR's top-left corner (not an
    // absolute screen point) for the whole editing session, established
    // once here from the box's ORIGINAL center. Two things can move editR
    // afterward, and they need to be treated differently:
    //  - AutoWidth growth extends the right/bottom edge while left/top
    //    stay put. If the pivot tracked editR's center live (like the
    //    settled/static renderer does), the center would shift every
    //    keystroke and rotating around a moving pivot makes the box
    //    visibly swing as you type. Keeping the pivot fixed relative to
    //    the (unmoving) top-left corner stops that.
    //  - A parent using Auto Layout can legitimately reposition editR's
    //    top-left (see the ts->bounds resync in redraw() below) -- that's
    //    a real move, not growth, so the pivot SHOULD track it, or the
    //    rotated view would be left pointing at the shape's old location.
    // Storing the pivot as an offset from top-left (rebuilt into an
    // absolute point every rebuildFull() call) gets both right at once.
    double pivotOffsetX = (editR.right - editR.left) * 0.5;
    double pivotOffsetY = (editR.bottom - editR.top) * 0.5;

    // Rebuilds `full` (rotation chain, pivoted per the fixed offset above)
    // and the derived src dimensions/cap check -- called once up front and
    // again after every remeasure/resync below, since editR changes while
    // typing (size from AutoWidth growth, position from layout reflow).
    auto rebuildFull = [&]() {
        full.clear();
        double pivotX = editR.left + pivotOffsetX, pivotY = editR.top + pivotOffsetY;
        if (ownRot != 0.0) full.push_back({ownRot, pivotX, pivotY});
        full.insert(full.end(), ambient.begin(), ambient.end());
        editSrcW = static_cast<short>(editR.right - editR.left);
        editSrcH = static_cast<short>(editR.bottom - editR.top);
        // No area/window-fit cap here anymore: editR itself can be far
        // wider than the window (a long AutoWidth sentence), but only the
        // portion that actually maps into the visible, on-screen part of
        // the rotated destination ever needs to be captured -- see the
        // needR computation in redraw() below. That's what's checked for
        // size, not the full box.
        canRotateEdit = anyRot && editSrcW > 0 && editSrcH > 0;
    };
    rebuildFull();

    // AutoWidth: editR should hug whatever's actually typed, growing (never
    // wrapping) as content is added -- TE's own destRect is deliberately
    // oversized (see TENew above) specifically so this can resize editR
    // independently of it. Keeps the box's top-left anchored, matching how
    // UpdateTextShapeBounds grows AutoWidth boxes elsewhere in the app.
    // Also writes the live measured size into ts->bounds itself (not just
    // the local editR) so a parent frame using Auto Layout can grow/reflow
    // to follow the text while it's actively being typed, not just after
    // commit -- ts->bounds is otherwise frozen during editing (see
    // UpdateAllTextShapeBounds's gEditingTextShape check).
    auto remeasureAutoWidth = [&]() {
        if (ts->textSizing != TextSizing::AutoWidth) return;
        Handle h = (*teh)->hText; long len = (*teh)->teLength;
        std::string cur;
        if (h && len > 0) { HLock(h); cur.assign(reinterpret_cast<char*>(*h), static_cast<size_t>(len)); HUnlock(h); }

        applyFont();
        short maxW = 8;
        int nLines = 0;
        size_t pos = 0;
        do {
            size_t nl  = cur.find('\r', pos);
            size_t len2 = (nl == std::string::npos) ? cur.size() - pos : nl - pos;
            Str255 pl; pl[0] = 0;
            for (size_t ci = 0; ci < len2 && ci < 255; ++ci) { pl[ci+1] = static_cast<unsigned char>(cur[pos+ci]); pl[0]++; }
            short lw = StringWidth(pl);
            if (lw > maxW) maxW = lw;
            ++nLines;
            if (nl == std::string::npos) break;
            pos = nl + 1;
        } while (pos < cur.size());
        if (nLines == 0) nLines = 1;

        short newW = static_cast<short>(maxW + 8);
        short newH = static_cast<short>(lineH * nLines + 4);
        editR.right  = static_cast<short>(editR.left + newW);
        editR.bottom = static_cast<short>(editR.top + newH);
        (*teh)->viewRect = editR;
        if (gCanvasZoom > 0) {
            ts->bounds.w = static_cast<SInt32>((SInt32)newW * 100 / gCanvasZoom);
            ts->bounds.h = static_cast<SInt32>((SInt32)newH * 100 / gCanvasZoom);
        }
        rebuildFull();
    };
    remeasureAutoWidth();

    // Catches any position change to ts->bounds that happened OUTSIDE this
    // function -- specifically, an Auto Layout parent reflowing after
    // remeasureAutoWidth just fed it a fresh live size. editR (and TE's
    // own destRect/viewRect, which is where it actually draws/erases) are
    // otherwise a frozen snapshot from whenever editing started or last
    // resynced; without this, the live-edit view would keep rendering at
    // the shape's OLD location after layout moved it to a new one.
    auto resyncPositionFromModel = [&]() {
        Rect modelR = CanvasRect(ts->bounds);
        short dx = static_cast<short>(modelR.left - editR.left);
        short dy = static_cast<short>(modelR.top  - editR.top);
        if (dx == 0 && dy == 0) return;
        editR.left   = static_cast<short>(editR.left   + dx);
        editR.right  = static_cast<short>(editR.right  + dx);
        editR.top    = static_cast<short>(editR.top    + dy);
        editR.bottom = static_cast<short>(editR.bottom + dy);
        (*teh)->destRect.left   = static_cast<short>((*teh)->destRect.left   + dx);
        (*teh)->destRect.right  = static_cast<short>((*teh)->destRect.right  + dx);
        (*teh)->destRect.top    = static_cast<short>((*teh)->destRect.top    + dy);
        (*teh)->destRect.bottom = static_cast<short>((*teh)->destRect.bottom + dy);
        (*teh)->viewRect = editR;
        rebuildFull();
    };

    RGBColor white = {0xFFFF,0xFFFF,0xFFFF}, black = {0,0,0}, blue = {0x1177,0x55AA,0xFFFF};

    // Straight (unrotated) draw of the TE box -- used whenever the shape
    // isn't rotated, or the box is too large to cheaply rotate-capture.
    auto redrawStraight = [&]() {
        RGBBackColor(&white); RGBForeColor(&black);
        // Reset to the full window clip before erasing: DrawWindowContent
        // (just called by our caller / about to be called again next redraw)
        // sets and restores several narrower clips of its own (e.g. a
        // frame's own clipContent region) as it draws -- if editR happens to
        // extend past whatever clip was left active, EraseRect only wipes
        // the clipped sub-area, leaving stale background pixels (frame fill,
        // canvas gray) in the rest of editR that later get captured as if
        // they were real content and painted as a solid block.
        { Rect cr = portRect; ClipRect(&cr); }
        EraseRect(&editR);
        applyFont();
        // Clip to editR: content that wraps/overflows past the box's
        // bottom (or right, for a fixed-width box) would otherwise be
        // drawn by TE anyway, unconstrained, spilling stray text outside
        // the box instead of being clipped like a normal text box.
        { Rect cr = editR; ClipRect(&cr); }
        TEUpdate(&editR, teh);
        { Rect cr = portRect; ClipRect(&cr); }
        RGBForeColor(&blue); FrameRect(&editR);
    };

    auto redraw = [&]() {
        SetPortWindowPort(win);
        remeasureAutoWidth();     // update ts->bounds live BEFORE layout sees it
        DrawWindowContent(win);   // full canvas, minus `ts` (skipped by DrawShape while editing); runs layout, which may reposition ts->bounds.x/y
        SetPortWindowPort(win);
        resyncPositionFromModel(); // pick up any position change layout just made
        if (!canRotateEdit) { redrawStraight(); return; }

        // editR can be far wider/taller than the window itself (a long
        // AutoWidth sentence) -- matching Figma means that content simply
        // extends past the visible viewport, not that it stops rendering
        // rotated once it no longer fits. So don't try to capture the
        // whole box: compute the full rotated destination, clip it to the
        // window (only this part is ever going to be seen), then
        // inverse-map that clipped rect back into editR's own coordinate
        // space to find exactly which (much smaller) slice of the box is
        // actually needed. That slice is what gets staged/captured -- it's
        // bounded by roughly the window's own size regardless of how wide
        // editR has grown, so it always fits for staging.
        Point fc[4]; double ffx, ffy;
        ApplyRotChain(full, editR.left,  editR.top,    ffx,ffy); fc[0] = ToQDPoint(ffx,ffy);
        ApplyRotChain(full, editR.right, editR.top,    ffx,ffy); fc[1] = ToQDPoint(ffx,ffy);
        ApplyRotChain(full, editR.right, editR.bottom, ffx,ffy); fc[2] = ToQDPoint(ffx,ffy);
        ApplyRotChain(full, editR.left,  editR.bottom, ffx,ffy); fc[3] = ToQDPoint(ffx,ffy);
        short fMinX = std::min(std::min(fc[0].h,fc[1].h), std::min(fc[2].h,fc[3].h));
        short fMaxX = std::max(std::max(fc[0].h,fc[1].h), std::max(fc[2].h,fc[3].h));
        short fMinY = std::min(std::min(fc[0].v,fc[1].v), std::min(fc[2].v,fc[3].v));
        short fMaxY = std::max(std::max(fc[0].v,fc[1].v), std::max(fc[2].v,fc[3].v));
        short visMinX = std::max(fMinX, portRect.left);
        short visMaxX = std::min(fMaxX, static_cast<short>(portRect.right - 1));
        short visMinY = std::max(fMinY, portRect.top);
        short visMaxY = std::min(fMaxY, static_cast<short>(portRect.bottom - 1));
        if (visMinX > visMaxX || visMinY > visMaxY) return; // fully off-window: nothing to draw

        double isx0,isy0, isx1,isy1, isx2,isy2, isx3,isy3;
        ApplyRotChainInverse(full, visMinX, visMinY, isx0, isy0);
        ApplyRotChainInverse(full, visMaxX, visMinY, isx1, isy1);
        ApplyRotChainInverse(full, visMaxX, visMaxY, isx2, isy2);
        ApplyRotChainInverse(full, visMinX, visMaxY, isx3, isy3);
        double needMinXd = std::min(std::min(isx0,isx1), std::min(isx2,isx3));
        double needMaxXd = std::max(std::max(isx0,isx1), std::max(isx2,isx3));
        double needMinYd = std::min(std::min(isy0,isy1), std::min(isy2,isy3));
        double needMaxYd = std::max(std::max(isy0,isy1), std::max(isy2,isy3));

        // Generous margin on the geometrically-strict bound (not just
        // +-1px), in case of any small mismatch elsewhere (rounding, TE's
        // own glyph metrics vs measured width, etc.). No longer capped to
        // the window's size -- see the striping loop below for why that
        // capping approach was itself the bug, not the fix.
        short needPad = 60;
        short winW = static_cast<short>(portRect.right - portRect.left);
        short winH = static_cast<short>(portRect.bottom - portRect.top);
        Rect needR;
        needR.left   = std::max(editR.left,   static_cast<short>(std::floor(needMinXd) - needPad));
        needR.right  = std::min(editR.right,  static_cast<short>(std::ceil(needMaxXd)  + needPad));
        needR.top    = std::max(editR.top,    static_cast<short>(std::floor(needMinYd) - needPad));
        needR.bottom = std::min(editR.bottom, static_cast<short>(std::ceil(needMaxYd)  + needPad));
        if (needR.right <= needR.left || needR.bottom <= needR.top) return;
        short needSrcW = static_cast<short>(needR.right - needR.left);
        short needSrcH = static_cast<short>(needR.bottom - needR.top);

        // No hard cap on needR anymore: capping it (however computed)
        // only ever moved the cutoff point around, because the STAGING
        // draw fundamentally needs source-width screen space regardless
        // of how compact the final rotated result is (rotation is what
        // compacts the footprint, and staging happens before rotation is
        // applied -- an upright 776px line needs a 776px-wide place to
        // draw into no matter how small it looks once rotated). Instead,
        // capture needR in successive window-sized STRIPS, each shifted
        // into the window independently and stitched into the full
        // content/ink buffers at the right offset, then rotate the whole
        // thing in one paint pass. A single strip is always <= the
        // window's width, so it always fits for staging regardless of
        // how wide needR itself is.
        if ((SInt32)needSrcW * needSrcH > 2000000) return; // pathological angle/size guard

        size_t n = static_cast<size_t>(needSrcW) * needSrcH;
        std::vector<RGBColor> content(n);
        std::vector<bool> ink(n);

        FastPixelWriter fastW = GetFastPixelWriter();
        bool useFast = fastW.Ready();
        auto getPx = [&](short px, short py) -> RGBColor {
            if (useFast) return fastW.Get(px, py);
            RGBColor c; GetCPixel(px, py, &c); return c;
        };
        auto setPx = [&](short px, short py, const RGBColor& c) {
            if (useFast) fastW.Set(px, py, c);
            else         SetCPixel(px, py, const_cast<RGBColor*>(&c));
        };

        Rect savedViewRect = (*teh)->viewRect;
        Rect savedDestRect = (*teh)->destRect;
        short stripMaxW = static_cast<short>(std::max<short>(8, winW - 4));

        for (short stripLeft = 0; stripLeft < needSrcW; stripLeft = static_cast<short>(stripLeft + stripMaxW)) {
            short stripW = static_cast<short>(std::min<short>(stripMaxW, needSrcW - stripLeft));
            Rect stripR = { needR.top, static_cast<short>(needR.left + stripLeft),
                             needR.bottom, static_cast<short>(needR.left + stripLeft + stripW) };

            // Shift THIS STRIP (never the whole needR) to fit inside the
            // window -- a single strip is always <= window width, so
            // this always succeeds.
            Rect stageR = stripR;
            short stageShiftX = 0, stageShiftY = 0;
            if (stageR.right > portRect.right) stageShiftX = static_cast<short>(portRect.right - stageR.right);
            if (static_cast<short>(stageR.left + stageShiftX) < portRect.left)
                stageShiftX = static_cast<short>(portRect.left - stageR.left);
            if (stageR.bottom > portRect.bottom) stageShiftY = static_cast<short>(portRect.bottom - stageR.bottom);
            if (static_cast<short>(stageR.top + stageShiftY) < portRect.top)
                stageShiftY = static_cast<short>(portRect.top - stageR.top);
            stageR.left   = static_cast<short>(stageR.left   + stageShiftX);
            stageR.right  = static_cast<short>(stageR.right  + stageShiftX);
            stageR.top    = static_cast<short>(stageR.top    + stageShiftY);
            stageR.bottom = static_cast<short>(stageR.bottom + stageShiftY);

            // Repoint TE so this strip's slice of the real text lands at
            // the staging position: destRect shifts by the same amount as
            // everything else (preserving flow geometry, just translated),
            // viewRect becomes the staging rect itself.
            short teShiftX = static_cast<short>(stageR.left - stripR.left);
            short teShiftY = static_cast<short>(stageR.top  - stripR.top);
            (*teh)->viewRect = stageR;
            (*teh)->destRect.left   = static_cast<short>(savedDestRect.left   + teShiftX);
            (*teh)->destRect.right  = static_cast<short>(savedDestRect.right  + teShiftX);
            (*teh)->destRect.top    = static_cast<short>(savedDestRect.top    + teShiftY);
            (*teh)->destRect.bottom = static_cast<short>(savedDestRect.bottom + teShiftY);

            std::vector<RGBColor> under(static_cast<size_t>(stripW) * needSrcH);
            for (short y = 0; y < needSrcH; ++y)
                for (short x = 0; x < stripW; ++x)
                    under[static_cast<size_t>(y)*stripW + x] =
                        getPx(static_cast<short>(stageR.left+x), static_cast<short>(stageR.top+y));

            // Erase to a flat color sampled from this strip's own real
            // background instead of a hardcoded white constant, and
            // compare captured content against a RE-READ sample of that
            // same erase -- see the older version of this comment for why
            // a literal constant isn't reliable across color depths.
            RGBColor bg = (stripW > 0 && needSrcH > 0) ? under[0] : white;
            RGBBackColor(&bg); RGBForeColor(&black);
            { Rect cr = portRect; ClipRect(&cr); }
            EraseRect(&stageR);
            RGBColor bgCaptured = getPx(stageR.left, stageR.top);
            applyFont();
            { Rect cr = stageR; ClipRect(&cr); }
            TEUpdate(&stageR, teh);
            { Rect cr = portRect; ClipRect(&cr); }

            for (short y = 0; y < needSrcH; ++y) {
                for (short x = 0; x < stripW; ++x) {
                    RGBColor c = getPx(static_cast<short>(stageR.left+x), static_cast<short>(stageR.top+y));
                    size_t di = static_cast<size_t>(y)*needSrcW + static_cast<size_t>(stripLeft+x);
                    content[di] = c;
                    ink[di] = c.red != bgCaptured.red || c.green != bgCaptured.green || c.blue != bgCaptured.blue;
                }
            }

            for (short y = 0; y < needSrcH; ++y)
                for (short x = 0; x < stripW; ++x)
                    setPx(static_cast<short>(stageR.left+x), static_cast<short>(stageR.top+y),
                          under[static_cast<size_t>(y)*stripW + x]);
        }

        (*teh)->viewRect = savedViewRect;
        (*teh)->destRect = savedDestRect;

        HideCursor();  // raw pixel writes bypass QuickDraw -- see FastPixelWriter
        PaintRotatedPixelBlock(content, &ink, needSrcW, needSrcH, needR, full, fastW, useFast);
        ShowCursor();

        // Rotated border around the edit area, matching the rotated
        // destination instead of framing the (invisible, staging-only)
        // unrotated editR.
        Point rc[4]; double fx, fy;
        ApplyRotChain(full, editR.left,  editR.top,    fx,fy); rc[0] = ToQDPoint(fx,fy);
        ApplyRotChain(full, editR.right, editR.top,    fx,fy); rc[1] = ToQDPoint(fx,fy);
        ApplyRotChain(full, editR.right, editR.bottom, fx,fy); rc[2] = ToQDPoint(fx,fy);
        ApplyRotChain(full, editR.left,  editR.bottom, fx,fy); rc[3] = ToQDPoint(fx,fy);
        RGBForeColor(&blue);
        PenSize(2, 2);
        MoveTo(rc[0].h, rc[0].v);
        LineTo(rc[1].h, rc[1].v); LineTo(rc[2].h, rc[2].v);
        LineTo(rc[3].h, rc[3].v); LineTo(rc[0].h, rc[0].v);
        PenSize(1, 1);
    };
    redraw();

    while (Button()) {}  // wait out the click/double-click that triggered this

    bool done = false, confirmed = false;
    int idleTicks = 0;
    EventRecord evt;
    while (!done) {
        bool got = WaitNextEvent(everyEvent, &evt, 3, nullptr);
        if (got) {
            idleTicks = 0;
            switch (evt.what) {
                case keyDown: case autoKey: {
                    char c = static_cast<char>(evt.message & charCodeMask);
                    if (c == 0x03 || c == 0x1B) {   // Enter or Escape: commit and exit
                        confirmed = true; done = true;
                    } else {
                        SetPortWindowPort(win); applyFont();
                        TEKey(c, teh);
                        // A rotated redraw isn't cheap (full canvas redraw plus
                        // a per-pixel capture/rotate over the box's current
                        // area, which grows with the text), and gets slower
                        // the longer the text gets. If a fast typist outpaces
                        // it, the OS event queue can fill up and silently drop
                        // keystrokes before they ever reach TEKey -- text
                        // appears to stop growing partway through a sentence,
                        // worse the longer it gets, never at 0 degrees (where
                        // redraw stays cheap). Drain every already-queued key
                        // event into TE's buffer first, then redraw once for
                        // the final state, so typing speed can never outrun
                        // rendering enough to lose a character.
                        EventRecord more;
                        while (EventAvail(keyDownMask | autoKeyMask, &more)) {
                            if (!GetNextEvent(keyDownMask | autoKeyMask, &more)) break;
                            char c2 = static_cast<char>(more.message & charCodeMask);
                            if (c2 == 0x03 || c2 == 0x1B) { confirmed = true; done = true; break; }
                            TEKey(c2, teh);
                        }
                        // The captured/painted box now always exactly matches
                        // editR (confirmed via the debug readout), yet text
                        // still stopped mid-sentence with headroom left in
                        // the box -- meaning TE's own drawn state wasn't
                        // reflecting everything actually in its text buffer.
                        // Force a full internal recalculation from the
                        // current buffer before this redraw draws anything,
                        // rather than trusting whatever incremental state
                        // TEKey left behind.
                        TECalText(teh);
                        redraw();
                    }
                    break;
                }
                case mouseDown: {
                    WindowRef hitWin; short part = FindWindow(evt.where, &hitWin);
                    Point local = evt.where;
                    SetPortWindowPort(win); GlobalToLocal(&local);
                    bool inside = false;
                    Point teLocal = local;
                    if (hitWin == win && part == inContent) {
                        if (!canRotateEdit) {
                            inside = PtInRect(local, &editR);
                        } else {
                            // Click landed somewhere on the visually rotated
                            // edit box; TE only understands the unrotated
                            // staging rect, so map the click back through
                            // the same rotation before testing/forwarding it.
                            double lx, ly;
                            ApplyRotChainInverse(full, local.h, local.v, lx, ly);
                            teLocal = ToQDPoint(lx, ly);
                            inside = PtInRect(teLocal, &editR);
                        }
                    }
                    if (!inside) {
                        confirmed = true; done = true;  // click elsewhere commits
                    } else {
                        applyFont();
                        if (!canRotateEdit) {
                            // Not rotated: TEClick's own internal drag
                            // feedback already draws at editR's real
                            // (unrotated) screen position, which is
                            // correct here, so there's nothing to work
                            // around.
                            TEClick(teLocal, (evt.modifiers & shiftKey) != 0, teh);
                        } else {
                            // Rotated: TEClick blocks internally and draws
                            // its own live drag-select highlight upright,
                            // at editR's real (unrotated) screen position,
                            // with no hook to intercept or redirect that
                            // per-frame drawing -- so a drag-select gesture
                            // on rotated text used to flash an upright
                            // selection block for the gesture's duration.
                            // Replace TEClick with a manual click+drag
                            // implementation: hit-test each point to a
                            // character offset ourselves (inverse-rotating
                            // it first, same as the initial click above),
                            // update the selection, and redraw through the
                            // normal rotated capture/paint path on every
                            // move, so the live highlight is correctly
                            // rotated throughout instead of just in the
                            // final, settled result.
                            short clickOffset = TEGetOffset(teLocal, teh);
                            short anchor;
                            if (evt.modifiers & shiftKey) {
                                short selStart = (*teh)->selStart, selEnd = (*teh)->selEnd;
                                anchor = (std::abs(clickOffset - selStart) > std::abs(clickOffset - selEnd))
                                         ? selStart : selEnd;
                            } else {
                                anchor = clickOffset;
                            }
                            TESetSelect(std::min(anchor, clickOffset), std::max(anchor, clickOffset), teh);
                            redraw();
                            short lastOffset = clickOffset;
                            while (Button()) {
                                Point dragLocal;
                                SetPortWindowPort(win);
                                GetMouse(&dragLocal);
                                double lx, ly;
                                ApplyRotChainInverse(full, dragLocal.h, dragLocal.v, lx, ly);
                                Point dragTeLocal = ToQDPoint(lx, ly);
                                short dragOffset = TEGetOffset(dragTeLocal, teh);
                                if (dragOffset != lastOffset) {
                                    TESetSelect(std::min(anchor, dragOffset), std::max(anchor, dragOffset), teh);
                                    redraw();
                                    lastOffset = dragOffset;
                                }
                            }
                        }
                        redraw();
                    }
                    break;
                }
                case updateEvt: {
                    WindowRef updWin = reinterpret_cast<WindowRef>(evt.message);
                    if (updWin == win) { BeginUpdate(win); redraw(); EndUpdate(win); }
                    break;
                }
                default: break;
            }
        } else {
            SetPortWindowPort(win); applyFont();
            if (!canRotateEdit) {
                TEIdle(teh);
            } else if (++idleTicks >= 10) {
                // TEIdle blinks the cursor by drawing directly at editR's
                // real (unrotated) position -- fine when not rotated, but
                // wrong here, so a rotated edit re-runs the full capture/
                // rotate-paint on a throttled timer instead of calling
                // TEIdle directly, to keep the blink roughly visible
                // without redrawing on every ~50ms idle poll.
                idleTicks = 0;
                TEIdle(teh);
                redraw();
            }
        }
    }

    if (confirmed) {
        Handle h = (*teh)->hText; long len = (*teh)->teLength;
        std::string newText;
        if (h && len > 0) {
            HLock(h);
            newText.assign(reinterpret_cast<char*>(*h), static_cast<size_t>(len));
            HUnlock(h);
        }
        for (char& c : newText) if (c == '\r') c = '\n';
        if (newText != ts->text) {
            if (pushUndoOnCommit) PushUndo();
            ts->text = newText;
        }
    }

    TEDispose(teh);
    gEditingTextShape = nullptr;

    UpdateTextShapeBounds(*ts);

    // Reconcile the rotation pivot mismatch between the live-edit view
    // (which deliberately freezes its pivot at the box's ORIGINAL,
    // pre-typing center to avoid visibly swinging as AutoWidth grows the
    // box -- see pivotOffsetX/Y above) and the settled renderer (which
    // always rotates around the box's CURRENT true center, recomputed
    // fresh every draw with no memory of "where the pivot used to be").
    // Once the box's size has changed during editing, those two pivots
    // differ, and the text visibly jumps at the instant editing ends --
    // even though ts->bounds.x/y themselves never change, which is why
    // this only shows up on rotated text (no pivot-dependent transform to
    // mismatch when rotation is 0) and only on commit (the live view is
    // internally consistent with its own frozen pivot throughout typing).
    //
    // Fix: recompute ts->bounds.x/y so the settled renderer's own
    // true-center pivot produces the exact same on-screen result the live
    // view's last frame did, instead of leaving x/y untouched and letting
    // the effective pivot silently change out from under the shape.
    if (ownRot != 0.0 && gCanvasZoom > 0) {
        double liveTopLeftX, liveTopLeftY;
        ApplyRotChain(full, editR.left, editR.top, liveTopLeftX, liveTopLeftY);

        double preAmbientX, preAmbientY;
        ApplyRotChainInverse(ambient, liveTopLeftX, liveTopLeftY, preAmbientX, preAmbientY);

        Rect newR = CanvasRect(ts->bounds);
        double W = newR.right - newR.left, H = newR.bottom - newR.top;
        RotChain ownOnly; ownOnly.push_back({ownRot, 0.0, 0.0});
        double rx, ry;
        ApplyRotChain(ownOnly, -W / 2.0, -H / 2.0, rx, ry);

        double newLocalX = preAmbientX - W / 2.0 - rx;
        double newLocalY = preAmbientY - H / 2.0 - ry;
        ts->bounds.x = static_cast<SInt32>(std::lround((newLocalX - gCanvasOffsetX) * 100.0 / gCanvasZoom));
        ts->bounds.y = static_cast<SInt32>(std::lround((newLocalY - gCanvasOffsetY) * 100.0 / gCanvasZoom));
    }

    RunDocumentLayout(gDocument);
    // Redraw immediately rather than just InvalWindowRect-ing and waiting
    // for the next async updateEvt: for a rotated text shape, the plain
    // axis-aligned edit box (TextEdit can't render rotation) only partly
    // overlaps where the shape's rotated glyphs actually sit on screen, so
    // any gap before the real repaint fires leaves that mismatch visibly
    // on screen — looking like part of the text got erased — until some
    // unrelated later event happens to trigger a redraw.
    DrawWindowContent(win);
}

static void HandleTextPlace(WindowRef win, Point localPt, Point globalPt) {
    (void)globalPt;
    Shape* existing = HitTestAnyShapeBodyAt(localPt);
    if (existing && existing->GetType() == Shape::kText && !existing->locked) {
        gSelectedShapes.clear(); gSelectedFrames.clear();
        gSelectedShape = existing;
        gSelectedFrame = LocateShapeParent(existing);
        EditTextInPlace(win, static_cast<TextShape*>(existing));
        return;
    }

    PushUndo();

    // Same fix as HandleCanvasCreate: un-rotate through the target frame's
    // ambient chain before converting to canvas coordinates, or a text
    // placed inside a rotated frame lands with skewed local bounds.
    Frame* target  = DeepestFrameAt(localPt);
    RotChain targetAmbient = AncestorChainFor(target);
    Point localPlacePt = localPt;
    if (!targetAmbient.empty()) {
        double lx, ly;
        ApplyRotChainInverse(targetAmbient, localPt.h, localPt.v, lx, ly);
        localPlacePt = ToQDPoint(lx, ly);
    }
    Point cPt = ScreenToCanvas(localPlacePt);

    auto tOwned  = std::make_unique<TextShape>();
    TextShape* t = tOwned.get();
    t->name      = "Text " + istr(gNextTextNum++);
    t->text      = "";
    t->fontSize  = 14;
    t->fontFace  = 0;
    t->bounds    = { cPt.h, cPt.v, 100, 20 };
    t->fillColor = { 0, 0, 0 };  // black text color
    t->hasFill   = true;
    t->hasStroke = false;
    gSelectedShapes.clear(); gSelectedFrames.clear();
    gSelectedShape = t;
    gSelectedFrame = target;
    if (target) {
        target->childOrder.push_back({ false, (int)target->children.size() });
        target->children.push_back(std::move(tOwned));
    } else {
        RootOrderInsert(0, false, (int)gDocument->rootShapes.size());
        gDocument->rootShapes.push_back(std::move(tOwned));
    }

    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    InvalWindowRect(win, &portRect);

    EditTextInPlace(win, t, /*pushUndoOnCommit=*/false);

    if (t->text.empty()) {
        // Nothing typed — remove the placeholder, matching click-away-discards.
        gSelectedShape = t;
        DeleteSelected();
    }
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

        // Center in screen space for DeepestFrameAt (uses CanvasRect = screen rects)
        Point center;
        center.h = static_cast<short>((sMin(startPt.h, currPt.h) + sMax(startPt.h, currPt.h)) / 2);
        center.v = static_cast<short>((sMin(startPt.v, currPt.v) + sMax(startPt.v, currPt.v)) / 2);

        // Which frame this new shape/frame lands in has to be resolved before
        // computing its bounds: if that frame (or any of its own ancestors)
        // is rotated, the rubber-band's screen-space corners need to be
        // un-rotated through that ambient chain first, or the new object's
        // local bounds end up skewed relative to what was actually dragged
        // on screen (previously always used a plain, rotation-oblivious
        // screen->canvas conversion here).
        Frame* dropParent = DeepestFrameAt(center);
        RotChain dropAmbient = AncestorChainFor(dropParent);
        Point localStart = startPt, localEnd = currPt;
        if (!dropAmbient.empty()) {
            double lx, ly;
            ApplyRotChainInverse(dropAmbient, startPt.h, startPt.v, lx, ly);
            localStart = ToQDPoint(lx, ly);
            ApplyRotChainInverse(dropAmbient, currPt.h, currPt.v, lx, ly);
            localEnd = ToQDPoint(lx, ly);
        }

        // Convert rubber-band screen corners (rotation already undone above) to canvas coordinates
        Point cStart = ScreenToCanvas(localStart);
        Point cEnd   = ScreenToCanvas(localEnd);

        Bounds2 b;
        b.x = sMin(cStart.h, cEnd.h);
        b.y = sMin(cStart.v, cEnd.v);
        b.w = sMax(cStart.h, cEnd.h) - b.x;
        b.h = sMax(cStart.v, cEnd.v) - b.y;

        gSelectedShape = nullptr;
        gSelectedFrame = nullptr;

        if (gActiveTool == Tool::Frame) {
            auto f = std::make_unique<Frame>();
            f->name           = "Frame " + istr(gNextFrameNum++);
            f->bounds         = b;
            f->backgroundColor = { 0xFFFF, 0xFFFF, 0xFFFF };

            Frame* parent = dropParent;  // nest inside containing frame if any
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
            Frame* target = dropParent;

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
