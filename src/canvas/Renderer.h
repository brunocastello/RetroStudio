#pragma once
#include <Carbon.h>
#include "../core/Document.h"

// Renders the Document into an offscreen GWorld, then blits to the window.
// All drawing goes through the offscreen buffer — the window never draws
// shapes directly — so flicker-free updates are guaranteed even on slow G3s.
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    // Resize (or create) the offscreen GWorld to match the canvas viewport.
    // Returns false if NewGWorld fails (low memory).
    bool Resize(SInt16 w, SInt16 h);

    // Re-render the full document into the offscreen buffer.
    void Render(const Document& doc);

    // Blit the offscreen buffer to the window port at destRect.
    void BlitToWindow(WindowRef win, const Rect& destRect) const;

private:
    void RenderFrame(const Frame& frame) const;
    void RenderShape(const Shape& shape) const;
    void DrawCanvasBackground() const;

    GWorldPtr mGWorld = nullptr;
    SInt16    mWidth  = 0;
    SInt16    mHeight = 0;
};
