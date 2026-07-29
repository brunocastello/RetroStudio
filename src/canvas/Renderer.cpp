#include "Renderer.h"

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static void SetFore(const RGBColor& c) { RGBForeColor(&c); }
static void SetBack(const RGBColor& c) { RGBBackColor(&c); }

// --------------------------------------------------------------------------
// Lifecycle
// --------------------------------------------------------------------------

Renderer::~Renderer() {
    if (mGWorld) {
        DisposeGWorld(mGWorld);
        mGWorld = nullptr;
    }
}

bool Renderer::Resize(SInt16 w, SInt16 h) {
    if (mGWorld && mWidth == w && mHeight == h)
        return true;

    if (mGWorld) {
        DisposeGWorld(mGWorld);
        mGWorld = nullptr;
    }

    Rect bounds = { 0, 0, h, w };
    OSErr err = NewGWorld(&mGWorld, 32, &bounds, nullptr, nullptr, 0);
    if (err != noErr) return false;

    mWidth  = w;
    mHeight = h;
    return true;
}

// --------------------------------------------------------------------------
// Rendering
// --------------------------------------------------------------------------

void Renderer::Render(const Document& doc) {
    if (!mGWorld) return;

    GWorldPtr savedGW;
    GDHandle  savedGD;
    GetGWorld(&savedGW, &savedGD);
    SetGWorld(mGWorld, nullptr);

    PixMapHandle pix = GetGWorldPixMap(mGWorld);
    LockPixels(pix);

    DrawCanvasBackground();

    for (const auto& frame : doc.frames)
        RenderFrame(*frame);

    UnlockPixels(pix);
    SetGWorld(savedGW, savedGD);
}

void Renderer::DrawCanvasBackground() const {
    // Figma-style: mid-gray canvas behind artboards
    RGBColor bg = { 0xDDDD, 0xDDDD, 0xDDDD };
    SetBack(bg);
    Rect full;
    GetPortBounds(mGWorld, &full);
    EraseRect(&full);
}

void Renderer::RenderFrame(const Frame& frame) const {
    if (!frame.visible) return;

    Rect r = ToMacRect(frame.bounds);

    // Drop shadow (1-pixel offset, dark gray)
    RGBColor shadow = { 0x6666, 0x6666, 0x6666 };
    SetFore(shadow);
    Rect shadowR = r;
    OffsetRect(&shadowR, 3, 3);
    PaintRect(&shadowR);

    // Frame fill (white by default)
    SetFore(frame.backgroundColor);
    PaintRect(&r);

    // Render children clipped to frame
    // (true clipping via regions deferred; children out-of-bounds simply overdraw)
    for (const auto& shape : frame.children)
        RenderShape(*shape);

    // Frame label above top-left corner (Figma-style name tag)
    // Drawn after children so it's always on top
    RGBColor labelColor = { 0x4444, 0x4444, 0x4444 };
    SetFore(labelColor);
    MoveTo(r.left, r.top - 4);
    // DrawString expects a Pascal string; limit name to 63 chars
    Str255 pname;
    pname[0] = 0;
    const char* src = frame.name.c_str();
    for (int i = 0; src[i] && i < 63; ++i) {
        pname[i + 1] = static_cast<unsigned char>(src[i]);
        pname[0]++;
    }
    DrawString(pname);
}

void Renderer::RenderShape(const Shape& shape) const {
    if (!shape.visible) return;

    Rect r = ToMacRect(shape.bounds);

    switch (shape.GetType()) {
        case Shape::kRectangle:
        case Shape::kLine: {
            if (shape.hasFill) {
                SetFore(shape.fillColor);
                PaintRect(&r);
            }
            if (shape.hasStroke) {
                SetFore(shape.strokeColor);
                PenSize(shape.strokeWidth, shape.strokeWidth);
                FrameRect(&r);
                PenSize(1, 1);
            }
            break;
        }
        case Shape::kEllipse: {
            if (shape.hasFill) {
                SetFore(shape.fillColor);
                PaintOval(&r);
            }
            if (shape.hasStroke) {
                SetFore(shape.strokeColor);
                PenSize(shape.strokeWidth, shape.strokeWidth);
                FrameOval(&r);
                PenSize(1, 1);
            }
            break;
        }
        default:
            break;
    }
}

// --------------------------------------------------------------------------
// Blit
// --------------------------------------------------------------------------

void Renderer::BlitToWindow(WindowRef win, const Rect& destRect) const {
    if (!mGWorld) return;

    PixMapHandle srcPix = GetGWorldPixMap(mGWorld);
    LockPixels(srcPix);

    Rect srcBounds;
    GetPortBounds(mGWorld, &srcBounds);

    SetPortWindowPort(win);
    CopyBits(
        GetPortBitMapForCopyBits(mGWorld),
        GetPortBitMapForCopyBits(GetWindowPort(win)),
        &srcBounds,
        &destRect,
        srcCopy,
        nullptr
    );

    UnlockPixels(srcPix);
}
