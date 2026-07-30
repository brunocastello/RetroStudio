#pragma once
#include <Carbon.h>
#include "../core/Document.h"

// Floating layers panel — hierarchical list of all frames and their children.
// Clicking a row selects the object on the canvas (syncs gSelectedFrame /
// gSelectedShape).  Mirrors Figma's Layers panel paradigm.

static const short kLayersPanelWidth = 176;
static const short kLayerRowH        = 20;

extern WindowRef gLayersWindow;

void SetupLayersPanel();
void DrawLayersPanel();
void HandleLayersPanelClick(Point localPt);
void RefreshLayersPanel();   // call after any document structure or selection change
