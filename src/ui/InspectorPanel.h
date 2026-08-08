#pragma once
#include <Carbon.h>
#include "../core/Document.h"

// Floating Inspector panel — shows fill color, X/Y/W/H, and name of the
// currently selected Frame or Shape.  Clicking the fill swatch opens the
// native Mac OS 9 Color Picker dialog.

static const short kInspectorWidth = 176;

extern WindowRef gInspectorWindow;

void SetupInspectorPanel();
void DrawInspectorPanel();
void HandleInspectorClick(Point localPt);
void RefreshInspector();

// Inline numeric field editing — call HandleInspectorKey from the main keyDown
// handler (before menu shortcuts) when no modifier keys are held.
// Returns true if the key was consumed by an active edit field.
bool HandleInspectorKey(char key, UInt16 modifiers = 0);
bool InspectorInEditMode();
void ApplyInspectorEdit();   // commit any in-progress edit (called on click-away)
void CancelInspectorEdit();  // discard any in-progress edit (e.g. on Escape)
bool IsAspectLocked();       // true when the W/H aspect ratio lock is engaged
