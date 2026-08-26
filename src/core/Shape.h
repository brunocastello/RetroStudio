#pragma once
#include <Carbon.h>
#include <string>
#include <vector>
#include <memory>

// Integer 2D bounds (x, y = top-left origin; w, h = size).
// Fixed-point integers keep us off the FPU on G3/G4 for simple layout math.
struct Bounds2 {
    SInt32 x = 0, y = 0, w = 100, h = 100;
};

// Convert Bounds2 → Mac Rect for QuickDraw calls
inline Rect ToMacRect(const Bounds2& b) {
    Rect r;
    r.top    = static_cast<short>(b.y);
    r.left   = static_cast<short>(b.x);
    r.bottom = static_cast<short>(b.y + b.h);
    r.right  = static_cast<short>(b.x + b.w);
    return r;
}

// How a child repositions/resizes along one axis when its parent frame is resized.
// Start = Left/Top, End = Right/Bottom, StartEnd = stretch (both edge offsets fixed),
// Center = centered offset fixed, Scale = position and size scale proportionally.
enum class ConstraintMode : UInt8 { Start = 0, End = 1, StartEnd = 2, Center = 3, Scale = 4 };

class Shape {
public:
    enum Type { kRectangle, kEllipse, kText, kLine, kImage };

    virtual ~Shape() = default;
    virtual Type GetType() const = 0;
    virtual std::unique_ptr<Shape> Clone() const = 0;

    Bounds2     bounds;
    RGBColor    fillColor   = { 0xCCCC, 0xCCCC, 0xFFFF }; // Figma default fill
    RGBColor    strokeColor = { 0, 0, 0 };
    bool        hasFill     = true;
    bool        hasStroke   = false;
    UInt16      strokeWidth = 1;
    UInt8       strokeAlign = 0;  // 0=center 1=inside 2=outside
    bool        visible     = true;
    bool        locked      = false;
    std::string name;
    // Per-child sizing within an Auto Layout frame: 0=Fixed, 2=Fill (matches SizingMode values)
    UInt8       wSizing     = 0;
    UInt8       hSizing     = 0;
    // Sizing bounds: -1 = unset (no clamp). Applied to Fill/Hug results and to
    // interactive/flip resize drags; never applied to a Fixed-size shape typed
    // in directly (the user's explicit number always wins there).
    SInt32      minWidth    = -1;
    SInt32      maxWidth    = -1;
    SInt32      minHeight   = -1;
    SInt32      maxHeight   = -1;
    UInt8       opacity     = 100;  // 0–100 percent
    SInt16      rotation    = 0;    // degrees, 0–359 clockwise
    // Position & Constraints
    bool           isAbsolutePosition = false;  // true = opt out of the parent's Auto Layout flow, position freely (parent must have layoutMode != None)
    ConstraintMode constraintH        = ConstraintMode::Start;
    ConstraintMode constraintV        = ConstraintMode::Start;
    // Runtime-only: this shape's own bounds as of the last settled (non-live-drag)
    // constraint pass — see AutoLayout.cpp's ApplyConstraints. Not serialized; a
    // stale/default value here self-heals within one redraw (any pass where the
    // parent hasn't moved refreshes it), so no special handling is needed on
    // Clone(), load, or undo.
    Bounds2 constraintBaseline;
};

class RectShape : public Shape {
public:
    Type GetType() const override { return kRectangle; }
    std::unique_ptr<Shape> Clone() const override { return std::make_unique<RectShape>(*this); }
    SInt16 cornerRadius     = 0;
    SInt16 cornerTL = 0, cornerTR = 0, cornerBR = 0, cornerBL = 0;
    bool   cornerIndividual = false;
};

class EllipseShape : public Shape {
public:
    Type GetType() const override { return kEllipse; }
    std::unique_ptr<Shape> Clone() const override { return std::make_unique<EllipseShape>(*this); }
};

enum class TextSizing : UInt8 {
    AutoWidth  = 0,  // single line, bounds.w grows to fit text
    AutoHeight = 1,  // wraps at fixed bounds.w, bounds.h grows to fit lines
    Fixed      = 2,  // user controls both w and h; text clips
};

// fillColor = text color; hasStroke → QuickDraw outline rendered on text glyphs (not bounding box)
class TextShape : public Shape {
public:
    Type GetType() const override { return kText; }
    std::unique_ptr<Shape> Clone() const override { return std::make_unique<TextShape>(*this); }

    std::string text          = "Text";
    SInt16      fontSize      = 14;
    UInt8       fontFace      = 0;    // QuickDraw style bits: bold=1, italic=2, underline=4
    std::string fontFamily    = "";   // empty = system font; "Helvetica" etc.
    UInt8       textAlign     = 0;    // 0=left, 1=center, 2=right
    UInt16      lineHeight    = 120;  // percent of font size (120 = 1.2em)
    SInt16      letterSpacing = 0;    // extra canvas-px between characters (0=normal)
    TextSizing  textSizing    = TextSizing::AutoWidth;
    bool        flippedH      = false;  // mirrored glyphs (crossed the opposite edge on a width resize)
    bool        flippedV      = false;  // mirrored glyphs (crossed the opposite edge on a height resize)
};

// PICT-backed embedded image (first pass of image support -- PICT is the
// only format with zero codec dependency on this toolchain; see project
// memory: no Image Compression Manager/GraphicsImportComponent stub exists
// here at all). pictData holds raw PICT opcode bytes -- the picture data
// itself, NOT the 512-byte PICT-file header, which is stripped on import.
// Rendered via DrawPicture straight onto the real window port -- no
// GWorld, no CopyBits (see project memory: CopyBits screen corruption --
// every offscreen-GWorld technique tried in this rendering path has
// corrupted the shared screen palette). Rotation is not yet supported for
// images: position tracks the ambient rotation chain like any other
// shape, but the picture itself always draws upright into its own
// axis-aligned box -- the same class of deliberate scope cut as text
// flip+rotation combined.
class ImageShape : public Shape {
public:
    Type GetType() const override { return kImage; }
    std::unique_ptr<Shape> Clone() const override { return std::make_unique<ImageShape>(*this); }
    std::vector<UInt8> pictData;
};
