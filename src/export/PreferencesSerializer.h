#pragma once

#include <Carbon.h>
#include <vector>

// Loads the persisted recent-files list from the app's preferences file
// (Preferences folder in the System Folder). Entries whose file no longer
// resolves (moved/deleted, or on an unmounted volume) are silently dropped
// -- returns whatever subset still exists, most-recent-first.
void LoadRecentFilesPrefs(std::vector<FSSpec>& outFiles);

// Overwrites the preferences file with the given recent-files list.
void SaveRecentFilesPrefs(const std::vector<FSSpec>& files);
