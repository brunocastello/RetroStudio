#pragma once
#include <Carbon.h>

extern WindowRef gAutoLayoutSettingsWindow;

void SetupAutoLayoutSettingsPanel();
// Open/position the panel anchored near a global screen point (e.g. the settings button).
void OpenAutoLayoutSettingsPanel(Point globalAnchor);
void DrawAutoLayoutSettingsPanel();
void HandleAutoLayoutSettingsClick(Point localPt);
void RefreshAutoLayoutSettingsPanel();   // invalidate if visible
