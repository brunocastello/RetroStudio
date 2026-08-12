/*
    RetroStudio.r — Application resources
    Rez source compiled by Retro68's Rez tool and bundled into the .dsk image.
*/

#include "Dialogs.r"
#include "Processes.r"

/* ---- About dialog (DLOG 128 + DITL 128) ---- */
resource 'DLOG' (128) {
    { 155, 90, 305, 510 },
    movableDBoxProc,
    visible,
    noGoAway,
    0,
    128,
    "About RetroStudio...",
    centerMainScreen
};

resource 'DITL' (128) {
    {
        { 118, 316, 138, 406 },
        Button { enabled, "OK" };

        { 14, 14, 36, 406 },
        StaticText { disabled, "RetroStudio 1.0" };

        { 46, 14, 66, 406 },
        StaticText { disabled, "A vector design & prototyping tool for Mac OS 9." };

        { 76, 14, 96, 406 },
        StaticText { disabled, "Bruno Castello, 2026." };

        { 108, 14, 128, 250 },
        StaticText { disabled, "\251 2026 Bruno Castello." }
    }
};

/* ---- Save-before-close confirmation (DLOG 129 + DITL 129) ---- */
resource 'DLOG' (129) {
    { 175, 110, 300, 450 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    129,
    "",
    centerMainScreen
};

resource 'DITL' (129) {
    {
        { 82, 248, 102, 326 },
        Button { enabled, "Save" };

        { 82, 130, 102, 242 },
        Button { enabled, "Don't Save" };

        { 82, 10, 102, 82 },
        Button { enabled, "Cancel" };

        { 10, 10, 30, 326 },
        StaticText { disabled, "Save changes before closing?" };

        { 40, 10, 60, 326 },
        StaticText { disabled, "Unsaved changes will be lost." }
    }
};

/* ---- SIZE resource (from official Retro68 Dialog sample) ---- */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
#ifdef TARGET_API_MAC_CARBON
    isHighLevelEventAware,
#else
    notHighLevelEventAware,
#endif
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
#ifdef TARGET_API_MAC_CARBON
    2048 * 1024,
    2048 * 1024
#else
    512 * 1024,
    512 * 1024
#endif
};
