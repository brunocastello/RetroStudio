#pragma once
#include <Carbon.h>
#include "../core/Tool.h"

// Floating tool palette — narrow vertical window anchored left of the canvas.
// Each tool slot is a fixed-height button drawn by QuickDraw.
// Clicking a slot sets gActiveTool; the active slot is highlighted.

static const short kPaletteWidth  = 44;
static const short kSlotHeight    = 40;
static const short kPaletteTop    = 50;   // matches main window top
static const short kPaletteLeft   = 6;

extern WindowRef gPaletteWindow;

void SetupPalette();
void DrawPalette();
void HandlePaletteClick(Point localPt);
