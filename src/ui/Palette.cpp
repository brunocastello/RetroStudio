#include "Palette.h"
#include "window.h"   // gActiveTool

WindowRef gPaletteWindow = nullptr;

// Tool slot definitions — label + shortcut key shown inside each button
struct ToolSlot {
    Tool        tool;
    const char* label;   // icon text (will be replaced by real icons later)
    char        key;
};

static const ToolSlot kSlots[] = {
    { Tool::Select,    "\24",  'V' },   // arrow (cursor char in Mac font)
    { Tool::Frame,     "#",    'F' },   // hashtag = artboard
    { Tool::Rectangle, "\xB0", 'R' },   // square placeholder
    { Tool::Ellipse,   "O",    'O' },
    { Tool::Text,      "T",    'T' },
    { Tool::Hand,      "H",    'H' },
};
static const short kNumSlots = sizeof(kSlots) / sizeof(kSlots[0]);

// --------------------------------------------------------------------------
// Setup
// --------------------------------------------------------------------------

void SetupPalette() {
    // Palette sits just to the left of the main window content, same top edge
    Rect bounds;
    GetWindowPortBounds(gMainWindow, &bounds);

    // Map main window port rect to global coords
    Point topLeft = { bounds.top, bounds.left };
    SetPortWindowPort(gMainWindow);
    LocalToGlobal(&topLeft);

    Rect palRect;
    palRect.top    = topLeft.v;
    palRect.left   = topLeft.h - kPaletteWidth - 2;
    palRect.bottom = palRect.top + kNumSlots * kSlotHeight + 4;
    palRect.right  = palRect.left + kPaletteWidth;

    // Clamp: don't go off screen left edge
    if (palRect.left < 2) {
        palRect.right  -= palRect.left - 2;
        palRect.left    = 2;
    }

    // noGrowDocProc (4) = document window without grow box.
    // Gives a title bar for dragging and a close box — like Photoshop's palettes.
    gPaletteWindow = NewCWindow(
        nullptr,
        &palRect,
        "\pTools",
        true,
        noGrowDocProc,
        (WindowRef)-1L,
        true,          // close box so user can hide the palette
        0
    );
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

// Draw a single tool slot at row index `idx`
static void DrawSlot(short idx, bool active) {
    Rect slot = {
        static_cast<short>(2 + idx * kSlotHeight),
        2,
        static_cast<short>(2 + (idx + 1) * kSlotHeight),
        static_cast<short>(kPaletteWidth - 2)
    };

    // Background
    RGBColor bg = active
        ? RGBColor{ 0x3333, 0x6666, 0xCCCC }   // blue highlight (active)
        : RGBColor{ 0xEEEE, 0xEEEE, 0xEEEE };  // light gray (inactive)
    RGBForeColor(&bg);
    PaintRect(&slot);

    // Border
    RGBColor border = { 0x9999, 0x9999, 0x9999 };
    RGBForeColor(&border);
    FrameRect(&slot);

    // Icon / label text centred in slot
    RGBColor fg = active
        ? RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF }
        : RGBColor{ 0x2222, 0x2222, 0x2222 };
    RGBForeColor(&fg);

    // Key label in small text (top-right corner)
    TextSize(9);
    Str255 keyStr;
    keyStr[0] = 1;
    keyStr[1] = kSlots[idx].key;
    short keyW = StringWidth(keyStr);
    MoveTo(slot.right - keyW - 3, slot.top + 10);
    DrawString(keyStr);

    // Tool symbol centred
    TextSize(13);
    Str255 sym;
    const char* src = kSlots[idx].label;
    sym[0] = 0;
    for (int i = 0; src[i] && i < 8; ++i) {
        sym[i + 1] = static_cast<unsigned char>(src[i]);
        sym[0]++;
    }
    short symW = StringWidth(sym);
    short slotMidX = (slot.left + slot.right)  / 2;
    short slotMidY = (slot.top  + slot.bottom) / 2 + 5;
    MoveTo(slotMidX - symW / 2, slotMidY);
    DrawString(sym);

    TextSize(12); // restore default
}

void DrawPalette() {
    if (!gPaletteWindow) return;

    SetPortWindowPort(gPaletteWindow);

    // Palette chrome background
    Rect portRect;
    GetWindowPortBounds(gPaletteWindow, &portRect);
    RGBColor chrome = { 0xDDDD, 0xDDDD, 0xDDDD };
    RGBForeColor(&chrome);
    PaintRect(&portRect);

    // Draw each tool slot
    for (short i = 0; i < kNumSlots; ++i)
        DrawSlot(i, kSlots[i].tool == gActiveTool);
}

// --------------------------------------------------------------------------
// Hit testing
// --------------------------------------------------------------------------

void HandlePaletteClick(Point localPt) {
    for (short i = 0; i < kNumSlots; ++i) {
        Rect slot = {
            static_cast<short>(2 + i * kSlotHeight),
            2,
            static_cast<short>(2 + (i + 1) * kSlotHeight),
            static_cast<short>(kPaletteWidth - 2)
        };
        if (PtInRect(localPt, &slot)) {
            gActiveTool = kSlots[i].tool;
            DrawPalette(); // redraw immediately for instant feedback
            return;
        }
    }
}
