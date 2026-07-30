#pragma once
#include <Carbon.h>
#include "../canvas/Renderer.h"
#include "../core/Document.h"
#include "../core/Tool.h"

extern WindowRef  gMainWindow;
extern Boolean    gQuitFlag;
extern Tool       gActiveTool;
extern Renderer*  gRenderer;
extern Document*  gDocument;
extern Frame*     gSelectedFrame;
extern Shape*     gSelectedShape;
extern int        gNextFrameNum;
extern bool       gIsDoubleClick;

void SetupMenus();
void SetupWindow();
void DrawWindowContent(WindowRef win);
void HandleCanvasCreate(WindowRef win, Point startGlobal);
void HandleCanvasSelect(WindowRef win, Point startGlobal);
void HandleMenuCommand(long menuResult);
void HandleWindowGrow(WindowRef win, Point where);
