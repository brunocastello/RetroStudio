/*
    RetroStudio.r — Application resources
    Rez source compiled by Retro68's Rez tool and bundled into the .dsk image.
*/

#include "Dialogs.r"
#include "Processes.r"

/* ---- Save-before-close confirmation (DLOG 129 + DITL 129) ----
   Real Dialog Manager Button/Icon/StaticText items, same family as
   Retro68's own Samples/Dialog: the Button CDEF is drawn by the OS itself
   (real Appearance-themed 3D buttons, no DrawThemeButton call needed from
   us), the Icon item resolves to the System file's own caution icon
   (ID 2 -- ICON 0/1/2 = stop/note/caution are standard System-file
   resources; we don't bundle our own ID-2 ICON/cicn, so the Resource
   Manager's search chain falls through to the System's real, colorized
   one), StaticText auto-wraps and takes the "^0" ParamText substitution
   for the document name, and centerMainScreen has the OS compute real
   screen centering. See ShowConfirmCloseDialog in window.cpp. */
resource 'DLOG' (129) {
    { 0, 0, 110, 330 },
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
        /* 1: UserItem -- fills the whole dialog with Platinum gray before
           anything else draws (items draw in index order, so this must be
           first); the OS's own dBoxProc content defaults to white here,
           not the window-chrome gray real alerts use. */
        { 0, 0, 110, 330 },
        UserItem { enabled };

        /* 2: Save -- default button */
        { 75, 248, 95, 316 },
        Button { enabled, "Save" };

        /* 3: UserItem -- draws the thick default-button ring around item 2,
           same ButtonFrameProc technique as Retro68's own Dialog sample */
        { 71, 244, 99, 320 },
        UserItem { enabled };

        /* 4: Don't Save */
        { 75, 82, 95, 172 },
        Button { enabled, "Don't Save" };

        /* 5: Cancel */
        { 75, 180, 95, 240 },
        Button { enabled, "Cancel" };

        /* 6: real system caution icon (ID 2), same position CautionAlert
           itself draws it at ([10,20,42,52], per Inside Macintosh) */
        { 10, 20, 42, 52 },
        Icon { enabled, 2 };

        /* 7: message, real StaticText (auto word-wraps); ^0 = document name via ParamText */
        { 16, 64, 56, 316 },
        StaticText { disabled, "Save changes to RetroStudio document \"^0\" before closing?" }
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
