/*
    RetroStudio.r — Application resources
    Rez source compiled by Retro68's Rez tool and bundled into the .dsk image.
*/

#include "Dialogs.r"

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
        Button { enabled, "Save" };,

        { 82, 130, 102, 242 },
        Button { enabled, "Don't Save" };,

        { 82, 10, 102, 82 },
        Button { enabled, "Cancel" };,

        { 10, 10, 30, 326 },
        StaticText { disabled, "Save changes before closing?" };,

        { 40, 10, 60, 326 },
        StaticText { disabled, "Unsaved changes will be lost." }
    }
};

/* ---- Save As (DLOG 130 + DITL 130) ---- */
resource 'DLOG' (130) {
    { 185, 80, 315, 460 },
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
        { 90, 278, 110, 366 },
        Button { enabled, "Save" };,

        { 90, 168, 110, 266 },
        Button { enabled, "Cancel" };,

        { 12, 12, 30, 366 },
        StaticText { disabled, "Save document as:" };,

        { 38, 12, 62, 366 },
        EditText { enabled, "" }
    }
};

/* ---- Open (DLOG 131 + DITL 131) ---- */
resource 'DLOG' (131) {
    { 185, 80, 315, 460 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    131,
    "",
    centerMainScreen
};

resource 'DITL' (131) {
    {
        { 90, 278, 110, 366 },
        Button { enabled, "Open" };,

        { 90, 168, 110, 266 },
        Button { enabled, "Cancel" };,

        { 12, 12, 30, 366 },
        StaticText { disabled, "File name on Desktop:" };,

        { 38, 12, 62, 366 },
        EditText { enabled, "" }
    }
};
