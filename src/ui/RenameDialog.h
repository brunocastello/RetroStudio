#pragma once
#include <Carbon.h>
#include <string>

// Shows a small floating text-field popup anchored at globalAnchor (global coords).
// Pre-fills with `current`. Returns the new name on confirm (Return/Enter),
// or an empty string on cancel (Escape or click outside).
std::string ShowRenameDialog(const std::string& current, Point globalAnchor);
