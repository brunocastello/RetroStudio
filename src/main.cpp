#include <Carbon.h>
#include "ui/window.h"
#include "ui/Palette.h"
#include "ui/LayersPanel.h"
#include "ui/InspectorPanel.h"

static void InitializeMacintosh() {
    InitCursor();
}

int main(int argc, char* argv[]) {
    InitializeMacintosh();
    SetupMenus();
    SetupWindow();
    SetupPalette();          // positioned relative to main window
    SetupLayersPanel();      // positioned relative to main window
    SetupInspectorPanel();   // positioned below layers panel

    EventRecord event;

    // Double-click detection state
    static UInt32 sLastClickWhen  = 0;
    static Point  sLastClickWhere = { 0, 0 };

    while (!gQuitFlag) {
        if (WaitNextEvent(everyEvent, &event, 15, nullptr)) {
            switch (event.what) {

                case mouseDown: {
                    // Detect double-click: same spot, within GetDblTime() ticks
                    {
                        short dx = static_cast<short>(event.where.h - sLastClickWhere.h);
                        short dy = static_cast<short>(event.where.v - sLastClickWhere.v);
                        gIsDoubleClick = ((event.when - sLastClickWhen) <= static_cast<UInt32>(GetDblTime()))
                                         && (dx*dx + dy*dy <= 25);  // within ~5px radius
                        sLastClickWhen  = event.when;
                        sLastClickWhere = event.where;
                    }

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
                                RefreshInspector();
                            } else if (win == gInspectorWindow) {
                                if (win != FrontWindow()) SelectWindow(win);
                                Point localPt = event.where;
                                SetPortWindowPort(gInspectorWindow);
                                GlobalToLocal(&localPt);
                                HandleInspectorClick(localPt);
                            } else if (win == gMainWindow) {
                                if (win != FrontWindow()) { SelectWindow(win); break; }
                                CancelInspectorEdit();
                                switch (gActiveTool) {
                                    case Tool::Select:
                                        HandleCanvasSelect(win, event.where);
                                        RefreshLayersPanel();
                                        RefreshInspector();
                                        break;
                                    case Tool::Frame:
                                    case Tool::Rectangle:
                                    case Tool::Ellipse:
                                    case Tool::Text:
                                        HandleCanvasCreate(win, event.where);
                                        DrawPalette();
                                        RefreshLayersPanel();
                                        RefreshInspector();
                                        break;
                                    case Tool::Hand:
                                        HandleCanvasPan(win, event.where);
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
                                else if (win == gInspectorWindow)
                                    HideWindow(gInspectorWindow);
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
                    // Inspector numeric-field edit mode captures non-cmd keys first
                    if (!(event.modifiers & cmdKey) && HandleInspectorKey(key)) break;
                    if (event.modifiers & cmdKey) {
                        // Cmd+Shift+Z = Redo (secondary shortcut; primary is Cmd+Y via menu)
                        if ((event.modifiers & shiftKey) && (key == 'Z' || key == 'z')) {
                            PerformRedo();
                            HiliteMenu(0);
                        } else {
                            HandleMenuCommand(MenuKey(key));
                        }
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
                                RefreshInspector();
                                break;
                            }

                            // Delete key — Figma shortcut (Backspace on Mac)
                            case 0x08:   // Backspace / Delete
                            case 0x7F:   // Forward Delete
                                DeleteSelected();
                                break;
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
                    else if (win == gInspectorWindow)
                        DrawInspectorPanel();
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
