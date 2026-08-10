#include "window.h"
#include "LayersPanel.h"
#include "InspectorPanel.h"
#include "RenameDialog.h"
#include "../export/DocumentSerializer.h"
#include "../canvas/AutoLayout.h"
#include <algorithm>
#include <cstring>

WindowRef  gMainWindow    = nullptr;
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
                short sw = static_cast<short>(shape.strokeWidth);
                Rect sr = r;
                if (shape.strokeAlign == 2) { sr.top-=sw; sr.left-=sw; sr.bottom+=sw; sr.right+=sw; }
                else if (shape.strokeAlign == 0) { short e=sw/2; sr.top-=e; sr.left-=e; sr.bottom+=e; sr.right+=e; }
                PenSize(sw, sw); FrameRect(&sr); PenSize(1, 1);
            }
            break;
        case Shape::kEllipse:
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
    if (frame.hasStroke) {
        RGBColor c = frame.strokeColor; RGBForeColor(&c);
        short sw = static_cast<short>(frame.strokeWidth);
        Rect sr = r;
        if (frame.strokeAlign == 2) { sr.top-=sw; sr.left-=sw; sr.bottom+=sw; sr.right+=sw; }
        else if (frame.strokeAlign == 0) { short e=sw/2; sr.top-=e; sr.left-=e; sr.bottom+=e; sr.right+=e; }
        PenSize(sw, sw); FrameRect(&sr); PenSize(1, 1);
    } else {
        RGBColor border = { 0xBBBB, 0xBBBB, 0xBBBB };
        RGBForeColor(&border); FrameRect(&r);
    }

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

    // Draw secondary multi-select borders (blue outline, no handles)
    if (gSelectedShapes.size() > 1) {
        RGBForeColor(&selBlue);
        PenSize(2, 2);
        for (Shape* s : gSelectedShapes) {
            if (s == gSelectedShape) continue;
            Rect r2 = CanvasRect(s->bounds);
            FrameRect(&r2);
        }
        PenSize(1, 1);
    }
    if (gSelectedFrames.size() > 1) {
        RGBForeColor(&selBlue);
        PenSize(2, 2);
        for (Frame* f : gSelectedFrames) {
            if (f == gSelectedFrame) continue;
            Rect r2 = CanvasRect(f->bounds);
            FrameRect(&r2);
        }
        PenSize(1, 1);
    }

    if (!gSelectedFrame && !gSelectedShape) return;

    Rect r = gSelectedShape
        ? CanvasRect(gSelectedShape->bounds)
        : CanvasRect(gSelectedFrame->bounds);

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
        for (const auto& frame : gDocument->frames)
            DrawFrame(*frame);

        // Shapes floating at canvas root (outside every frame) — labels shown here only
        for (const auto& shape : gDocument->rootShapes) {
            DrawShape(*shape);
            if (shape->visible) DrawShapeNameLabel(*shape);
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
                Rect sr = CanvasRect(s->bounds);
                if (PtInRect(pt, &sr)) return { f, s.get(), true };
            }
        }
    } else {
        // Legacy fallback: child frames then shapes (both in reverse for topmost-first)
        for (auto it = f->childFrames.rbegin(); it != f->childFrames.rend(); ++it) {
            HitResult res = HitTestFrame(it->get(), pt);
            if (res.found) return res;
        }
        for (auto it = f->children.rbegin(); it != f->children.rend(); ++it) {
            Rect sr = CanvasRect((*it)->bounds);
            if (PtInRect(pt, &sr)) return { f, it->get(), true };
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

// Extract a Shape unique_ptr from its current owner (Frame or rootShapes).
// Also removes and reindexes the entry in parent->childOrder.
static std::unique_ptr<Shape> ExtractShape(Shape* s, Frame* parent) {
    auto& vec = parent ? parent->children : gDocument->rootShapes;
    for (int i = 0; i < (int)vec.size(); ++i) {
        if (vec[i].get() == s) {
            auto o = std::move(vec[i]);
            vec.erase(vec.begin() + i);
            if (parent) {
                // Remove the childOrder entry for this shape and fix up higher indices
                for (auto it = parent->childOrder.begin(); it != parent->childOrder.end(); ) {
                    if (!it->isFrame && it->idx == i) { it = parent->childOrder.erase(it); continue; }
                    if (!it->isFrame && it->idx > i) --it->idx;
                    ++it;
                }
            }
            return o;
        }
    }
    return nullptr;
}

// Extract a Frame unique_ptr from its current owner.
// Also removes and reindexes the entry in parent->childOrder.
static std::unique_ptr<Frame> ExtractFrame(Frame* f) {
    Frame* par = f->parent;
    auto& vec = par ? par->childFrames : gDocument->frames;
    for (int i = 0; i < (int)vec.size(); ++i) {
        if (vec[i].get() == f) {
            auto o = std::move(vec[i]);
            vec.erase(vec.begin() + i);
            if (par) {
                // Remove the childOrder entry for this frame and fix up higher indices
                for (auto it = par->childOrder.begin(); it != par->childOrder.end(); ) {
                    if (it->isFrame && it->idx == i) { it = par->childOrder.erase(it); continue; }
                    if (it->isFrame && it->idx > i) --it->idx;
                    ++it;
                }
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
        for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend() && !found; ++it) {
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
        for (auto it = gDocument->frames.rbegin(); it != gDocument->frames.rend() && !found; ++it) {
            Frame* lf = HitTestFrameLabel(it->get(), pt);
            if (lf) { hitFrame = lf; found = true; }
        }
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
         std::find(gSelectedFrames.begin(), gSelectedFrames.end(), hitFrame) != gSelectedFrames.end());

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
                    // Mixed mode: gSelectedFrame is the parent context (not itself being moved).
                    // Only move shapes if their parent frame is NOT in gSelectedFrames.
                    bool parentMoved = std::find(gSelectedFrames.begin(), gSelectedFrames.end(),
                                                 gSelectedFrame) != gSelectedFrames.end();
                    if (!parentMoved)
                        for (Shape* s : gSelectedShapes) { s->bounds.x += dx; s->bounds.y += dy; }
                    // Only move top-level selected frames (skip descendants already moved by parent)
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
        // Multi-select: move all shapes to the frame under the drag endpoint.
        if (isMultiDrag && hitShape) {
            Bounds2 hitB = hitShape->bounds;
            Point hitCenter;
            hitCenter.h = static_cast<short>(SInt32(hitB.x + hitB.w/2) * gCanvasZoom / 100 + gCanvasOffsetX);
            hitCenter.v = static_cast<short>(SInt32(hitB.y + hitB.h/2) * gCanvasZoom / 100 + gCanvasOffsetY);
            Frame* newParent = DeepestFrameAt(hitCenter);
            if (newParent != origParent) {
                for (Shape* target : gSelectedShapes) {
                    auto owned = ExtractShape(target, origParent);
                    if (owned) {
                        if (newParent) {
                            newParent->childOrder.push_back({ false, (int)newParent->children.size() });
                            newParent->children.push_back(std::move(owned));
                        } else {
                            gDocument->rootShapes.push_back(std::move(owned));
                        }
                    }
                }
                gSelectedFrame = newParent;
            }
            Rect portRect; GetWindowPortBounds(win, &portRect); InvalWindowRect(win, &portRect);
            RefreshLayersPanel();
            return;
        }
        if (isMultiDrag && gSelectedFrames.size() > 1) {
            // Re-parent each selected frame to wherever it was dropped,
            // excluding all selected frames from the candidate parent search.
            for (Frame* f : gSelectedFrames) {
                Bounds2 fb = f->bounds;
                Point fc;
                fc.h = static_cast<short>(SInt32(fb.x + fb.w/2) * gCanvasZoom / 100 + gCanvasOffsetX);
                fc.v = static_cast<short>(SInt32(fb.y + fb.h/2) * gCanvasZoom / 100 + gCanvasOffsetY);
                Frame* newParent = DeepestFrameAtExcl(fc, gSelectedFrames);
                if (newParent != f->parent) {
                    auto owned = ExtractFrame(f);
                    if (owned) {
                        if (newParent) {
                            owned->parent = newParent;
                            newParent->childOrder.push_back({ true, (int)newParent->childFrames.size() });
                            newParent->childFrames.push_back(std::move(owned));
                        } else {
                            owned->parent = nullptr;
                            gDocument->frames.push_back(std::move(owned));
                        }
                    }
                }
            }
            gSelectedFrame = gSelectedFrames.empty() ? nullptr : gSelectedFrames.back();
            Rect portRect; GetWindowPortBounds(win, &portRect); InvalWindowRect(win, &portRect);
            RefreshLayersPanel();
            RefreshInspector();
            return;
        }
        if (isMultiDrag) {
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

            if (isLayoutDrag && newParent == hitFrame) {
                // Dropped inside same layout frame — sort childOrder by primary axis center
                bool isHoriz = (hitFrame->layoutMode == LayoutMode::Horizontal);
                if (!hitFrame->childOrder.empty()) {
                    std::stable_sort(hitFrame->childOrder.begin(), hitFrame->childOrder.end(),
                        [hitFrame, isHoriz](const ChildRef& a, const ChildRef& b) {
                            auto center = [hitFrame, isHoriz](const ChildRef& cr) -> SInt32 {
                                if (cr.isFrame) {
                                    const Bounds2& bnd = hitFrame->childFrames[cr.idx]->bounds;
                                    return isHoriz ? bnd.x + bnd.w/2 : bnd.y + bnd.h/2;
                                }
                                const Bounds2& bnd = hitFrame->children[cr.idx]->bounds;
                                return isHoriz ? bnd.x + bnd.w/2 : bnd.y + bnd.h/2;
                            };
                            return center(a) < center(b);
                        });
                } else {
                    std::stable_sort(hitFrame->children.begin(), hitFrame->children.end(),
                        [isHoriz](const std::unique_ptr<Shape>& a, const std::unique_ptr<Shape>& b) {
                            SInt32 aM = isHoriz ? (a->bounds.x + a->bounds.w/2)
                                                : (a->bounds.y + a->bounds.h/2);
                            SInt32 bM = isHoriz ? (b->bounds.x + b->bounds.w/2)
                                                : (b->bounds.y + b->bounds.h/2);
                            return aM < bM;
                        });
                }
            } else if (newParent != origParent) {
                auto owned = ExtractShape(hitShape, origParent);
                if (owned) {
                    if (newParent) {
                        newParent->childOrder.push_back({ false, (int)newParent->children.size() });
                        newParent->children.push_back(std::move(owned));
                    } else {
                        gDocument->rootShapes.push_back(std::move(owned));
                    }
                    gSelectedFrame = newParent;
                }
            }
        } else {
            Frame* newParent = DeepestFrameAt(center, hitFrame);
            if (isLayoutFrameDrag && newParent == hitFrame->parent && hitFrame->parent) {
                // Dropped back inside same layout parent — sort childOrder by primary axis
                Frame* layoutParent = hitFrame->parent;
                bool isHoriz = (layoutParent->layoutMode == LayoutMode::Horizontal);
                if (!layoutParent->childOrder.empty()) {
                    std::stable_sort(layoutParent->childOrder.begin(), layoutParent->childOrder.end(),
                        [layoutParent, isHoriz](const ChildRef& a, const ChildRef& b) {
                            auto center = [layoutParent, isHoriz](const ChildRef& cr) -> SInt32 {
                                if (cr.isFrame) {
                                    const Bounds2& bnd = layoutParent->childFrames[cr.idx]->bounds;
                                    return isHoriz ? bnd.x + bnd.w/2 : bnd.y + bnd.h/2;
                                }
                                const Bounds2& bnd = layoutParent->children[cr.idx]->bounds;
                                return isHoriz ? bnd.x + bnd.w/2 : bnd.y + bnd.h/2;
                            };
                            return center(a) < center(b);
                        });
                } else {
                    std::stable_sort(layoutParent->childFrames.begin(), layoutParent->childFrames.end(),
                        [isHoriz](const std::unique_ptr<Frame>& a, const std::unique_ptr<Frame>& b) {
                            SInt32 aM = isHoriz ? (a->bounds.x + a->bounds.w/2)
                                                : (a->bounds.y + a->bounds.h/2);
                            SInt32 bM = isHoriz ? (b->bounds.x + b->bounds.w/2)
                                                : (b->bounds.y + b->bounds.h/2);
                            return aM < bM;
                        });
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
    gSelectedShapes.clear();
    gSelectedFrames.clear();
    gNextFrameNum   = 2;
    gNextRectNum    = 1;
    gNextEllipseNum = 1;
    gNextTextNum    = 1;
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
        if (gSelectedFrame) {
            gSelectedFrame->childOrder.push_back({ false, (int)gSelectedFrame->children.size() });
            gSelectedFrame->children.push_back(std::move(copy));
        } else {
            gDocument->rootShapes.push_back(std::move(copy));
        }
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
    if (!gSelectedShape && !gSelectedFrame && gSelectedShapes.empty() && gSelectedFrames.empty()) return;
    PushUndo();
    bool changed = false;

    if (!gSelectedFrames.empty() && !gSelectedShapes.empty()) {
        // Mixed selection: delete all selected frames and all selected shapes
        for (Frame* target : gSelectedFrames) {
            auto owned = ExtractFrame(target);
            if (owned) changed = true;
        }
        gSelectedFrames.clear();
        for (Shape* target : gSelectedShapes) {
            auto owned = ExtractShape(target, gSelectedFrame);
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
        // Delete all shapes in the multi-select set via ExtractShape (maintains childOrder)
        for (Shape* target : gSelectedShapes) {
            auto owned = ExtractShape(target, gSelectedFrame);
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
                    gSelectedShapes.clear();
                    gSelectedFrames.clear();
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
