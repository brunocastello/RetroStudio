#include <Carbon.h>
#include "ui/window.h"
#include "ui/Palette.h"
#include "ui/LayersPanel.h"

static void InitializeMacintosh() {
    InitCursor();
}

int main(int argc, char* argv[]) {
    InitializeMacintosh();
    SetupMenus();
    SetupWindow();
    SetupPalette();       // positioned relative to main window
    SetupLayersPanel();   // positioned relative to main window

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
                        case inContent: {
                            if (win == gPaletteWindow) {
                                if (win != FrontWindow()) SelectWindow(win);
                                Point localPt = event.where;
                                SetPortWindowPort(gPaletteWindow);
                                GlobalToLocal(&localPt);
                                HandlePaletteClick(localPt);
                            } else if (win == gLayersWindow) {
                                if (win != FrontWindow()) SelectWindow(win);
                                Point localPt = event.where;
                                SetPortWindowPort(gLayersWindow);
                                GlobalToLocal(&localPt);
                                HandleLayersPanelClick(localPt);
                            } else if (win == gMainWindow) {
                                if (win != FrontWindow()) { SelectWindow(win); break; }
                                switch (gActiveTool) {
                                    case Tool::Select:
                                        HandleCanvasSelect(win, event.where);
                                        RefreshLayersPanel();  // selection may have changed
                                        break;
                                    case Tool::Frame:
                                    case Tool::Rectangle:
                                    case Tool::Ellipse:
                                        HandleCanvasCreate(win, event.where);
                                        DrawPalette();         // tool auto-switched to Select
                                        RefreshLayersPanel();  // new layer added
                                        break;
                                    default:
                                        break;
                                }
                            }
                            break;
                        }
                        case inGoAway:
                            if (TrackGoAway(win, event.where)) {
                                if (win == gMainWindow)
                                    gQuitFlag = true;
                                else if (win == gPaletteWindow)
                                    HideWindow(gPaletteWindow);
                                else if (win == gLayersWindow)
                                    HideWindow(gLayersWindow);
                            }
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
                        Tool prev = gActiveTool;
                        switch (key) {
                            // Figma-style tool shortcuts — work regardless of front window
                            case 'v': case 'V': gActiveTool = Tool::Select;    break;
                            case 'f': case 'F': gActiveTool = Tool::Frame;     break;
                            case 'r': case 'R': gActiveTool = Tool::Rectangle; break;
                            case 'o': case 'O': gActiveTool = Tool::Ellipse;   break;
                            case 't': case 'T': gActiveTool = Tool::Text;      break;
                            case 'h': case 'H': gActiveTool = Tool::Hand;      break;

                            case 0x1B: {        // Escape — deselect
                                gSelectedFrame = nullptr;
                                gSelectedShape = nullptr;
                                Rect r;
                                GetWindowPortBounds(gMainWindow, &r);
                                InvalWindowRect(gMainWindow, &r);
                                RefreshLayersPanel();
                                break;
                            }

                            // Delete key — always acts on the selected object even
                            // when the Palette or Layers panel is the front window.
                            case 0x08:   // Backspace / Delete
                            case 0x7F: { // Forward Delete
                                bool changed = false;
                                if (gSelectedShape && gSelectedFrame) {
                                    auto& ch = gSelectedFrame->children;
                                    for (auto it = ch.begin(); it != ch.end(); ++it) {
                                        if (it->get() == gSelectedShape) {
                                            ch.erase(it);
                                            changed = true;
                                            break;
                                        }
                                    }
                                    gSelectedShape = nullptr;
                                } else if (gSelectedFrame) {
                                    auto& fr = gDocument->frames;
                                    for (auto it = fr.begin(); it != fr.end(); ++it) {
                                        if (it->get() == gSelectedFrame) {
                                            fr.erase(it);
                                            changed = true;
                                            break;
                                        }
                                    }
                                    gSelectedFrame = nullptr;
                                }
                                if (changed) {
                                    Rect r;
                                    GetWindowPortBounds(gMainWindow, &r);
                                    InvalWindowRect(gMainWindow, &r);
                                    RefreshLayersPanel();
                                }
                                break;
                            }
                        }
                        if (gActiveTool != prev)
                            DrawPalette();
                    }
                    break;
                }

                case updateEvt: {
                    WindowRef win = reinterpret_cast<WindowRef>(event.message);
                    BeginUpdate(win);
                    if (win == gPaletteWindow)
                        DrawPalette();
                    else if (win == gLayersWindow)
                        DrawLayersPanel();
                    else
                        DrawWindowContent(win);
                    EndUpdate(win);
                    break;
                }

                case activateEvt: {
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
