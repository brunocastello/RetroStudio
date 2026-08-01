#pragma once
#include <Carbon.h>
#include "../core/Document.h"

static const short kTypographyWidth  = 200;
static const short kTypographyHeight = 215;

extern WindowRef gTypographyWindow;

void SetupTypographyPanel();
void DrawTypographyPanel();
void HandleTypographyPanelClick(Point localPt);
bool HandleTypographyPanelKey(char key);
void RefreshTypographyPanel();
void ToggleTypographyPanel();
void CancelTypographyEdit();
bool TypographyInEditMode();
