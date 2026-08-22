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
    { 0, 0, 128, 366 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    129,
    "",
    centerMainScreen
};

/* Geometry below is pixel-measured off a real Adobe Illustrator 9 / BBEdit
   alert screenshot (366x128 dialog), not estimated -- see
   src/ui/window.cpp's ShowConfirmCloseDialog comment for the measurement
   method. Icon rect matches Inside Macintosh's documented CautionAlert
   position ([10,20,42,52]) almost exactly, which cross-validates the rest. */
resource 'DITL' (129) {
    {
        /* 1: UserItem -- fills the whole dialog with Platinum gray before
           anything else draws (items draw in index order, so this must be
           first); the OS's own dBoxProc content defaults to white here,
           not the window-chrome gray real alerts use. */
        { 0, 0, 128, 366 },
        UserItem { enabled };

        /* 2: Save -- default button */
        { 93, 292, 112, 348 },
        Button { enabled, "Save" };

        /* 3: UserItem -- draws the thick default-button ring around item 2,
           same ButtonFrameProc technique as Retro68's own Dialog sample */
        { 89, 288, 116, 352 },
        UserItem { enabled };

        /* 4: Don't Save -- deliberately isolated from the Cancel/Save pair
           (60px gap) rather than evenly spaced; measured, not a mistake. */
        { 93, 78, 112, 159 },
        Button { enabled, "Don't Save" };

        /* 5: Cancel -- tight 17px gap to Save, paired as the "safe" choices */
        { 93, 219, 112, 275 },
        Button { enabled, "Cancel" };

        /* 6: real system caution icon (ID 2), same position CautionAlert
           itself draws it at ([10,20,42,52], per Inside Macintosh) */
        { 10, 20, 42, 52 },
        Icon { enabled, 2 };

        /* 7: message, real StaticText (auto word-wraps); ^0 = document name via ParamText */
        { 14, 70, 54, 348 },
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
