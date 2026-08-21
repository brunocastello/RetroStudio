/*
    RetroStudio.r — Application resources
    Rez source compiled by Retro68's Rez tool and bundled into the .dsk image.
*/

#include "Dialogs.r"
#include "Processes.r"

/* ---- Save-before-close confirmation (DLOG 129 + DITL 129) ----
   Real Dialog Manager Button/StaticText items, same family as Retro68's
   own Samples/Dialog: the Button CDEF is drawn by the OS itself (real
   Appearance-themed 3D buttons, no DrawThemeButton call needed from us),
   StaticText auto-wraps and takes the "^0" ParamText substitution for the
   document name, and centerMainScreen has the OS compute real screen
   centering. See ShowConfirmCloseDialog in window.cpp. */
resource 'DLOG' (129) {
    { 0, 0, 110, 360 },
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
        /* 1: Save -- default button */
        { 75, 216, 95, 294 },
        Button { enabled, "Save" };

        /* 2: UserItem -- draws the thick default-button ring around item 1,
           same ButtonFrameProc technique as Retro68's own Dialog sample */
        { 71, 212, 99, 298 },
        UserItem { enabled };

        /* 3: Don't Save */
        { 75, 20, 95, 120 },
        Button { enabled, "Don't Save" };

        /* 4: Cancel */
        { 75, 132, 95, 204 },
        Button { enabled, "Cancel" };

        /* 5: UserItem -- draws the yellow caution triangle */
        { 20, 20, 52, 52 },
        UserItem { enabled };

        /* 6: message, real StaticText (auto word-wraps); ^0 = document name via ParamText */
        { 18, 64, 58, 344 },
        StaticText { disabled, "Save changes to the RetroStudio document \"^0\" before closing?" }
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
