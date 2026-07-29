#include "window.h"

WindowRef gMainWindow = nullptr;
Boolean   gQuitFlag   = false;

// zoomDocProc (8) = document window with close + zoom box.
// Not exported by all CarbonLib header versions; use the numeric literal.
static const short kZoomDocProc = 8;

static const short kFileMenuID = 129;
static const short kEditMenuID = 130;

// File menu item indices
static const short kFileNew  = 1;
static const short kFileOpen = 2;
// item 3 is separator
static const short kFileQuit = 4;

void SetupMenus() {
    MenuRef fileMenu = NewMenu(kFileMenuID, "\pFile");
    AppendMenu(fileMenu, "\pNew");
    SetItemCmd(fileMenu, kFileNew, 'N');
    AppendMenu(fileMenu, "\pOpen...");
    SetItemCmd(fileMenu, kFileOpen, 'O');
    AppendMenu(fileMenu, "\p-");
    AppendMenu(fileMenu, "\pQuit");
    SetItemCmd(fileMenu, kFileQuit, 'Q');
    InsertMenu(fileMenu, 0);

    MenuRef editMenu = NewMenu(kEditMenuID, "\pEdit");
    AppendMenu(editMenu, "\pUndo");
    SetItemCmd(editMenu, 1, 'Z');
    AppendMenu(editMenu, "\p-");
    AppendMenu(editMenu, "\pCut");
    SetItemCmd(editMenu, 3, 'X');
    AppendMenu(editMenu, "\pCopy");
    SetItemCmd(editMenu, 4, 'C');
    AppendMenu(editMenu, "\pPaste");
    SetItemCmd(editMenu, 5, 'V');
    InsertMenu(editMenu, 0);

    DrawMenuBar();
}

void SetupWindow() {
    Rect bounds = { 50, 50, 550, 900 };
    gMainWindow = NewCWindow(
        nullptr,           // storage (let OS allocate)
        &bounds,
        "\pRetroStudio",
        true,              // visible
        kZoomDocProc,
        (WindowRef)-1L,    // in front of all windows
        true,              // has close box
        0                  // refCon
    );
}

void DrawWindowContent(WindowRef win) {
    SetPortWindowPort(win);

    Rect portRect;
    GetWindowPortBounds(win, &portRect);

    // Application chrome: medium gray background
    RGBColor bgGray = { 0xBBBB, 0xBBBB, 0xBBBB };
    RGBBackColor(&bgGray);
    EraseRect(&portRect);

    // White canvas area inset from window edges
    Rect canvas = portRect;
    InsetRect(&canvas, 10, 10);

    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBForeColor(&white);
    PaintRect(&canvas);

    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);
    FrameRect(&canvas);
}

void HandleMenuCommand(long menuResult) {
    // HiWord/LoWord are unlinked in retrocarbon; use bit ops directly.
    short menuID   = static_cast<short>(menuResult >> 16);
    short menuItem = static_cast<short>(menuResult & 0xFFFF);

    switch (menuID) {
        case kFileMenuID:
            switch (menuItem) {
                case kFileQuit:
                    gQuitFlag = true;
                    break;
            }
            break;
    }

    HiliteMenu(0);
}
