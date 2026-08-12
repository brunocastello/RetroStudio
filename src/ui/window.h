#pragma once
#include <Carbon.h>
#include <vector>
#include "../canvas/Renderer.h"
#include "../core/Document.h"
#include "../core/Tool.h"

extern WindowRef         gMainWindow;
extern Boolean           gQuitFlag;
extern Tool              gActiveTool;
extern Renderer*         gRenderer;
extern Document*         gDocument;
extern Frame*            gSelectedFrame;
extern Shape*            gSelectedShape;
extern std::vector<Shape*> gSelectedShapes;  // multi-select shapes
extern std::vector<Frame*> gSelectedFrames;  // multi-select frames
extern Shape*              gLayoutDragShape;    // single-shape drag: excluded from auto layout
extern Frame*              gLayoutDragFrame;    // single child-frame drag: excluded from auto layout
extern bool                gIsLayoutMultiDrag;  // multi-select drag: all gSelectedShapes excluded
extern int        gNextFrameNum;
extern bool       gIsDoubleClick;
extern SInt32     gCanvasOffsetX;
extern SInt32     gCanvasOffsetY;
extern int        gCanvasZoom;      // percent: 100 = 1:1, 200 = 2:1, 50 = 0.5:1
extern int        gNextRectNum;
extern int        gNextEllipseNum;
extern int        gNextTextNum;

// Canvas ↔ screen coordinate transforms
Rect  CanvasRect(const Bounds2& b);  // canvas-space bounds → screen-space Mac Rect
Point ScreenToCanvas(Point screenPt); // screen local pt → canvas pt

void SetupMenus();
void SetupWindow();
bool IsDocumentCanvas(WindowRef win);
void SwitchActiveDocument(WindowRef win);
void CloseDocumentWindow(WindowRef win);
void DrawWindowContent(WindowRef win);
void HandleCanvasCreate(WindowRef win, Point startGlobal);
void HandleCanvasSelect(WindowRef win, Point startGlobal, UInt16 modifiers = 0);
void HandleCanvasPan(WindowRef win, Point startGlobal);
void HandleMenuCommand(long menuResult);
void HandleWindowGrow(WindowRef win, Point where);
void StepZoom(int direction);   // +1 = zoom in, -1 = zoom out
void ZoomToFit();
void DeleteSelected();          // Delete/Backspace — removes selected shape or frame
void CopySelected();            // Cmd+C — copies selected shape or frame to clipboard
void PasteClipboard();          // Cmd+V — pastes clipboard at +10,+10 offset
void PushUndo();                // snapshot current document onto the undo stack
void PerformUndo();             // Cmd+Z
void PerformRedo();             // Cmd+Shift+Z
