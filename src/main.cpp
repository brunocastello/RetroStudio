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
                        case inGrow:
                            HandleWindowGrow(win, event.where);
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
                    if (event.modifiers & cmdKey) {
                        HandleMenuCommand(MenuKey(key));
                    } else {
                        // Tool keyboard shortcuts (Figma-style)
                        switch (key) {
                            case 'v': case 'V': gActiveTool = Tool::Select;    break;
                            case 'f': case 'F': gActiveTool = Tool::Frame;     break;
                            case 'r': case 'R': gActiveTool = Tool::Rectangle; break;
                            case 'o': case 'O': gActiveTool = Tool::Ellipse;   break;
                            case 't': case 'T': gActiveTool = Tool::Text;      break;
                            case 'h': case 'H': gActiveTool = Tool::Hand;      break;
                        }
                    }
                    break;
                }

                case updateEvt: {
                    WindowRef win = reinterpret_cast<WindowRef>(event.message);
                    BeginUpdate(win);
                    DrawWindowContent(win);
                    EndUpdate(win);
                    break;
                }

                case activateEvt: {
                    // Trigger a redraw when focus changes so title bar repaints
                    WindowRef win = reinterpret_cast<WindowRef>(event.message);
                    Rect portRect;
                    GetWindowPortBounds(win, &portRect);
                    InvalWindowRect(win, &portRect);
                    break;
                }
            }
        }
    }

    return 0;
}
