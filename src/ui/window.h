#pragma once
#include <Carbon.h>
#include <vector>
#include "../canvas/Renderer.h"
#include "../core/Document.h"
#include "../core/Tool.h"

extern WindowRef         gMainWindow;
extern WindowRef         gAboutWindow;
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
void DrawAboutWindow();
bool IsDocumentCanvas(WindowRef win);
void SwitchActiveDocument(WindowRef win);
// Returns false if the user cancelled (window stays open); true if closed.
bool CloseDocumentWindow(WindowRef win);
// `clipTo`, if non-null, restricts the redraw to that screen-local rect
// instead of the whole window content area (see HandleRotateDrag) -- applied
// AFTER this function's own SetPortWindowPort, since that resets the port's
// clip region as a side effect (setting a clip before calling in would just
// get silently discarded).
void DrawWindowContent(WindowRef win, const Rect* clipTo = nullptr);
void HandleCanvasCreate(WindowRef win, Point startGlobal);
void HandleCanvasSelect(WindowRef win, Point startGlobal, UInt16 modifiers = 0);
void HandleCanvasPan(WindowRef win, Point startGlobal);
void HandleMenuCommand(long menuResult);
void HandleWindowGrow(WindowRef win, Point where);
void UpdateCanvasCursor(Point globalPt);  // call on nullEvent to update cursor over canvas
void StepZoom(int direction);   // +1 = zoom in, -1 = zoom out
void ZoomToFit();
void DeleteSelected();          // Delete/Backspace — removes selected shape or frame
void CopySelected();            // Cmd+C — copies selected shape or frame to clipboard
void PasteClipboard();          // Cmd+V — pastes clipboard at +10,+10 offset
void PushUndo();                // snapshot current document onto the undo stack
void PerformUndo();             // Cmd+Z
void PerformRedo();             // Cmd+Shift+Z
Frame* FindShapeParent(Shape* s);  // walks the frame tree to find s's owning frame (nullptr = root-level)
