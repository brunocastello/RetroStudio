/*
    RetroStudio.r — Application resources
    Rez source compiled by Retro68's Rez tool and bundled into the .dsk image.
*/

#include "Dialogs.r"
#include "Processes.r"

/* Save-before-close confirmation is a hand-drawn NewCWindow (see
   ShowConfirmCloseDialog in window.cpp), not a DLOG/DITL resource --
   that gave more control over layout, the caution icon, and Return/Escape
   keyboard shortcuts than the classic Dialog Manager path allowed. */

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
