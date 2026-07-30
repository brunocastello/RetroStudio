#include "RenameDialog.h"

static const short kDBoxProc = 1;
static const short kW        = 210;
static const short kH        = 22;
static const short kPad      =  4;

// Helper to draw the TE record (TEDraw is not in Retro68 Carbon; use TEUpdate)
static void DrawTE(TEHandle teh, const Rect& portR) {
    TEUpdate(&portR, teh);
}

std::string ShowRenameDialog(const std::string& current, Point globalAnchor) {
    // Position popup just below the anchor point, clamped to a 1024×768 screen
    short wx = globalAnchor.h;
    short wy = static_cast<short>(globalAnchor.v + 2);
    if (wx + kW > 1024) wx = static_cast<short>(1024 - kW);
    if (wy + kH > 768)  wy = static_cast<short>(globalAnchor.v - kH - 2);

    Rect wr = { wy, wx, static_cast<short>(wy + kH), static_cast<short>(wx + kW) };

    // Use a mutable Str255 for the window title (avoids const char* → Ptr error)
    Str255 emptyTitle = { 0 };
    WindowRef popup = NewCWindow(nullptr, &wr, emptyTitle, true,
                                 kDBoxProc, (WindowRef)-1L, false, 0);
    if (!popup) return "";

    SetPortWindowPort(popup);
    TextFont(0); TextSize(12);

    Rect portR; GetWindowPortBounds(popup, &portR);

    // TE view/dest rects inset by padding
    Rect viewR = {
        static_cast<short>(portR.top    + kPad),
        static_cast<short>(portR.left   + kPad),
        static_cast<short>(portR.bottom - kPad),
        static_cast<short>(portR.right  - kPad)
    };
    Rect destR = viewR;

    TEHandle teh = TENew(&viewR, &destR);
    if (!teh) { DisposeWindow(popup); return ""; }

    // Pre-fill text and select all so the user can type immediately
    // TESetText takes Ptr (char*) in Retro68 headers, not const char*
    if (!current.empty())
        TESetText(const_cast<Ptr>(current.c_str()), static_cast<long>(current.size()), teh);
    TESetSelect(0, static_cast<long>((*teh)->teLength), teh);
    TEActivate(teh);

    // Initial draw
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    RGBColor black = { 0, 0, 0 };
    RGBColor blue  = { 0x1177, 0x55AA, 0xFFFF };
    RGBBackColor(&white); RGBForeColor(&black);
    EraseRect(&portR);
    RGBForeColor(&blue); FrameRect(&portR);  // blue border = edit mode
    RGBForeColor(&black);
    DrawTE(teh, portR);

    // Wait for the triggering mouse-up before entering the loop
    while (Button()) {}

    std::string result;
    bool done      = false;
    bool confirmed = false;
    EventRecord evt;

    while (!done) {
        bool got = WaitNextEvent(everyEvent, &evt, 3, nullptr);

        if (got) {
            switch (evt.what) {
                case keyDown:
                case autoKey: {
                    char c = static_cast<char>(evt.message & charCodeMask);
                    if (c == 0x0D || c == 0x03) {   // Return or Enter
                        confirmed = true; done = true;
                    } else if (c == 0x1B) {          // Escape
                        done = true;
                    } else {
                        TEKey(c, teh);
                        SetPortWindowPort(popup);
                        RGBBackColor(&white); RGBForeColor(&black);
                        EraseRect(&portR);
                        RGBForeColor(&blue); FrameRect(&portR);
                        RGBForeColor(&black);
                        DrawTE(teh, portR);
                    }
                    break;
                }
                case mouseDown: {
                    WindowRef hitWin;
                    FindWindow(evt.where, &hitWin);
                    if (hitWin != popup) done = true;  // click outside = cancel
                    break;
                }
                case updateEvt: {
                    WindowRef updWin = reinterpret_cast<WindowRef>(evt.message);
                    if (updWin == popup) {
                        BeginUpdate(popup);
                        SetPortWindowPort(popup);
                        RGBBackColor(&white); RGBForeColor(&black);
                        EraseRect(&portR);
                        RGBForeColor(&blue); FrameRect(&portR);
                        RGBForeColor(&black);
                        DrawTE(teh, portR);
                        EndUpdate(popup);
                    }
                    break;
                }
            }
        } else {
            // Blink the cursor on null events
            SetPortWindowPort(popup);
            TEIdle(teh);
        }
    }

    if (confirmed) {
        Handle h   = (*teh)->hText;
        long   len = (*teh)->teLength;
        if (h && len > 0) {
            HLock(h);
            result.assign(reinterpret_cast<char*>(*h), static_cast<size_t>(len));
            HUnlock(h);
        }
    }

    TEDispose(teh);
    DisposeWindow(popup);
    return result;
}
