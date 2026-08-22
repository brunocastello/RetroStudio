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
    { 0, 0, 123, 360 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    129,
    "",
    centerMainScreen
};

/* Geometry below is pixel-measured off a real Adobe Illustrator 9 / BBEdit
   alert screenshot (target render: 366x128 dialog, buttons at
   {93,78,112,159}/{93,219,112,275}/{93,292,112,348}) -- see
   src/ui/window.cpp's ShowConfirmCloseDialog comment for the measurement
   method. Icon rect matches Inside Macintosh's documented CautionAlert
   position ([10,20,42,52]) almost exactly, which cross-validates it.

   The DLOG size and every Button rect below are DECLARED smaller than
   that target and DO NOT match the numbers above directly: a second
   pixel-diff round (declared vs. actually rendered, both on this
   toolchain) found the whole dialog rendering ~6px wider / ~5px taller
   than declared, and each Button rendering ~4px narrower (eaten from the
   left edge only) and ~3px lower than declared. These numbers back that
   out so the RENDERED result lands on the target above -- confirmed
   working already for the Icon/StaticText items below using the same
   method (their declared rects are pixel-perfect against a live
   screenshot). Don't "fix" these to match the target numbers directly. */
resource 'DITL' (129) {
    {
        /* 1: Save -- default button */
        { 90, 287, 110, 347 },
        Button { enabled, "Save" };

        /* 2: UserItem -- draws the thick default-button ring around item 1,
           same ButtonFrameProc technique as Retro68's own Dialog sample. */
        { 86, 283, 114, 351 },
        UserItem { disabled };

        /* 3: Don't Save -- deliberately isolated from the Cancel/Save pair
           (60px gap) rather than evenly spaced; measured, not a mistake. */
        { 90, 73, 110, 158 },
        Button { enabled, "Don't Save" };

        /* 4: Cancel -- tight 17px gap to Save, paired as the "safe" choices */
        { 90, 214, 110, 274 },
        Button { enabled, "Cancel" };

        /* 5: real system caution icon (ID 2). Inside Macintosh's documented
           CautionAlert position is [10,20,42,52], but a direct pixel-diff
           against a live screenshot (icon+text render ~3-4px up/left of
           the Illustrator reference here) showed this toolchain's Dialog
           Manager needs a few px of compensation to land in the same spot. */
        { 13, 24, 45, 56 },
        Icon { disabled, 2 };

        /* 6: message, real StaticText (auto word-wraps); ^0 = document name via ParamText.
           Same +3/+4 compensation as the icon above, for the same reason. */
        { 13, 74, 53, 352 },
        StaticText { disabled, "Save changes to RetroStudio document \"^0\" before closing?" }
    }
};

/* No whole-dialog background-fill UserItem here anymore: adding one (to
   turn the default white dBoxProc content gray) is what caused the alert
   to hang completely on real hardware/emulation across two separate
   attempts (once enabled, once disabled) -- root cause not confirmed, not
   worth a third live-testing round-trip to find out. Content is plain
   white until that's revisited with something verified safer. */

/* ---- Revert confirmation (DLOG 130 + DITL 130) ----
   Same construction as 129 above, reusing its exact icon/text/button
   geometry (already pixel-tuned against a live screenshot), just with two
   buttons (Revert, Cancel) instead of three. See ShowConfirmRevertDialog
   in window.cpp. */
resource 'DLOG' (130) {
    { 0, 0, 123, 360 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    130,
    "",
    centerMainScreen
};

resource 'DITL' (130) {
    {
        /* 1: Revert -- default button, same rect Save used in DITL 129 */
        { 90, 287, 110, 347 },
        Button { enabled, "Revert" };

        /* 2: UserItem -- default-button ring around item 1 */
        { 86, 283, 114, 351 },
        UserItem { disabled };

        /* 3: Cancel -- same rect Cancel used in DITL 129 */
        { 90, 214, 110, 274 },
        Button { enabled, "Cancel" };

        /* 4: real system caution icon (ID 2) */
        { 13, 24, 45, 56 },
        Icon { disabled, 2 };

        /* 5: message; ^0 = document name via ParamText */
        { 13, 74, 53, 352 },
        StaticText { disabled, "Revert to the last saved version of \"^0\"? All changes will be lost." }
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
