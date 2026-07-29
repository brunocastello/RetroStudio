#include <Carbon.h>
#include "ui/window.h"

static void InitializeMacintosh() {
    InitCursor();
}

int main(int argc, char* argv[]) {
    InitializeMacintosh();
    SetupMenus();
    SetupWindow();

    EventRecord event;

    while (!gQuitFlag) {
        if (WaitNextEvent(everyEvent, &event, 15, nullptr)) {
            switch (event.what) {

                case mouseDown: {
                    WindowRef win;
                    short part = FindWindow(event.where, &win);
                    switch (part) {
                        case inMenuBar:
                            HandleMenuCommand(MenuSelect(event.where));
                            break;
                        case inDrag:
                            DragWindow(win, event.where, nullptr);
                            break;
                        case inContent:
                            if (win != FrontWindow())
                                SelectWindow(win);
                            break;
                        case inGoAway:
                            if (TrackGoAway(win, event.where))
                                gQuitFlag = true;
                            break;
                        case inZoomIn:
                        case inZoomOut:
                            if (TrackBox(win, event.where, part))
                                ZoomWindow(win, part, true);
                            break;
                    }
                    break;
                }

                case keyDown:
                case autoKey: {
                    char key = static_cast<char>(event.message & charCodeMask);
                    if (event.modifiers & cmdKey)
                        HandleMenuCommand(MenuKey(key));
                    break;
                }

                case updateEvt: {
                    WindowRef win = reinterpret_cast<WindowRef>(event.message);
                    BeginUpdate(win);
                    DrawWindowContent(win);
                    EndUpdate(win);
                    break;
                }

                case activateEvt:
                    // Window activation state changes are handled by the OS;
                    // trigger a redraw so the title bar repaints correctly.
                    InvalWindowRect(
                        reinterpret_cast<WindowRef>(event.message),
                        nullptr
                    );
                    break;
            }
        }
    }

    return 0;
}
