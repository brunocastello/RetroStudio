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
};

class RectShape : public Shape {
public:
    Type GetType() const override { return kRectangle; }
    std::unique_ptr<Shape> Clone() const override { return std::make_unique<RectShape>(*this); }
    SInt16 cornerRadius = 0;
};

class EllipseShape : public Shape {
public:
    Type GetType() const override { return kEllipse; }
    std::unique_ptr<Shape> Clone() const override { return std::make_unique<EllipseShape>(*this); }
};
