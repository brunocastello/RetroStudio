#pragma once
#include <Carbon.h>

extern WindowRef gMainWindow;
extern Boolean   gQuitFlag;

void SetupMenus();
void SetupWindow();
void DrawWindowContent(WindowRef win);
void HandleMenuCommand(long menuResult);
