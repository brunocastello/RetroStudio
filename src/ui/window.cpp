#include "window.h"

WindowRef gMainWindow = nullptr;
Boolean   gQuitFlag   = false;

static const MenuID kFileMenuID = 129;
static const MenuID kEditMenuID = 130;

// File menu items
static const MenuItemIndex kFileNew  = 1;
static const MenuItemIndex kFileOpen = 2;
// item 3 is separator
static const MenuItemIndex kFileQuit = 4;

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
        zoomDocProc,       // document window with zoom box
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
    short menuID   = HiWord(menuResult);
    short menuItem = LoWord(menuResult);

    switch (menuID) {
        case kFileMenuID:
            switch (menuItem) {
                case kFileQuit:
                    gQuitFlag = true;
                    break;
            }
            break;
    }

    HiliteMenu(0); // always unhighlight after handling
}
