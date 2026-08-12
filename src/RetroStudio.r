/*
    RetroStudio.r — Application resources
    Rez source compiled by Retro68's Rez tool and bundled into the .dsk image.
*/

#include "Dialogs.r"
#include "Processes.r"

/* ---- SIZE resource ---- */
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

/* ---- About RetroStudio alert (ALRT 128 + DITL 128) ---- */
resource 'ALRT' (128) {
    { 150, 130, 310, 510 },   /* top, left, bottom, right — 160h × 380w */
    128,                       /* references DITL 128 */
    {
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1
    }
};

resource 'DITL' (128) {
    {
        /* [1] OK button — must be item 1 in an ALRT */
        { 120, 275, 142, 365 },
        Button { enabled, "OK" };,

        /* [2] App name */
        { 10, 10, 28, 365 },
        StaticText { disabled, "RetroStudio 1.0" };,

        /* [3] Description line 1 */
        { 40, 10, 58, 365 },
        StaticText { disabled, "A vector design & prototyping tool" };,

        /* [4] Description line 2 */
        { 62, 10, 80, 365 },
        StaticText { disabled, "for Mac OS 9." };,

        /* [5] Copyright */
        { 92, 10, 110, 365 },
        StaticText { disabled, "Copyright 2026 Bruno Castello." }
    }
};

/* ---- Save-before-close confirmation dialog (DLOG 129 + DITL 129) ---- */
resource 'DLOG' (129) {
    { 175, 110, 300, 450 },   /* 125h × 340w */
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
        /* [1] Save — default button */
        { 82, 248, 102, 326 },
        Button { enabled, "Save" };,

        /* [2] Don't Save */
        { 82, 130, 102, 242 },
        Button { enabled, "Don't Save" };,

        /* [3] Cancel */
        { 82, 10, 102, 82 },
        Button { enabled, "Cancel" };,

        /* [4] Message */
        { 10, 10, 30, 326 },
        StaticText { disabled, "Save changes before closing?" };,

        /* [5] Detail */
        { 40, 10, 60, 326 },
        StaticText { disabled, "Unsaved changes will be lost." }
    }
};
