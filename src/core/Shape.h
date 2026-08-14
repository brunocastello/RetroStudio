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

class Shape {
public:
    enum Type { kRectangle, kEllipse, kText, kLine };

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
    UInt8       opacity     = 100;  // 0–100 percent
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
};
