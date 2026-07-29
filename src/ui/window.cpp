#include "window.h"

WindowRef  gMainWindow = nullptr;
Boolean    gQuitFlag   = false;
Tool       gActiveTool = Tool::Select;
Renderer*  gRenderer   = nullptr;
Document*  gDocument   = nullptr;

static const short kZoomDocProc = 8; // not exported by all CarbonLib header versions

static const short kFileMenuID = 129;
static const short kEditMenuID = 130;

static const short kFileNew  = 1;
static const short kFileOpen = 2;
// item 3 = separator
static const short kFileQuit = 4;

// --------------------------------------------------------------------------
// Menus
// --------------------------------------------------------------------------

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

// --------------------------------------------------------------------------
// Window + document setup
// --------------------------------------------------------------------------

void SetupWindow() {
    Rect bounds = { 50, 50, 580, 960 };
    gMainWindow = NewCWindow(
        nullptr,
        &bounds,
        "\pRetroStudio",
        true,
        kZoomDocProc,
        (WindowRef)-1L,
        true,
        0
    );

    // Bootstrap document: one default artboard so the canvas isn't empty
    gDocument = new Document();
    gDocument->name = "Untitled";

    auto frame      = std::make_unique<Frame>();
    frame->name     = "Screen 1";
    frame->bounds   = { 40, 40, 390, 480 };
    RGBColor white  = { 0xFFFF, 0xFFFF, 0xFFFF };
    frame->backgroundColor = white;
    gDocument->frames.push_back(std::move(frame));

    gRenderer = new Renderer();
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

void DrawWindowContent(WindowRef win) {
    Rect portRect;
    GetWindowPortBounds(win, &portRect);

    SInt16 w = static_cast<SInt16>(portRect.right  - portRect.left);
    SInt16 h = static_cast<SInt16>(portRect.bottom - portRect.top);

    if (!gRenderer || !gDocument) return;

    if (!gRenderer->Resize(w, h)) {
        // Low-memory fallback: fill with gray so the window isn't blank
        RGBColor gray = { 0xAAAA, 0xAAAA, 0xAAAA };
        RGBBackColor(&gray);
        SetPortWindowPort(win);
        EraseRect(&portRect);
        return;
    }

    gRenderer->Render(*gDocument);
    gRenderer->BlitToWindow(win, portRect);
}

// --------------------------------------------------------------------------
// Window resize
// --------------------------------------------------------------------------

void HandleWindowGrow(WindowRef win, Point where) {
    Rect sizeConstraints = { 300, 400, 2000, 4000 }; // min H/W, max H/W
    long newSize = GrowWindow(win, where, &sizeConstraints);
    if (newSize == 0) return;

    SInt16 newW = static_cast<SInt16>(newSize & 0xFFFF);
    SInt16 newH = static_cast<SInt16>((newSize >> 16) & 0xFFFF);
    SizeWindow(win, newW, newH, true);

    // Invalidate entire content area so updateEvt redraws everything
    Rect portRect;
    GetWindowPortBounds(win, &portRect);
    InvalWindowRect(win, &portRect);
}

// --------------------------------------------------------------------------
// Menu commands
// --------------------------------------------------------------------------

void HandleMenuCommand(long menuResult) {
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
