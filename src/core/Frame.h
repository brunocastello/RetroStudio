#pragma once
#include "Shape.h"

// Frame = artboard / screen in the prototype.
// Direct equivalent of a Figma Frame: fixed bounds, clips its children,
// acts as the unit of navigation in prototype mode.
class Frame {
public:
    std::string name             = "Screen";
    Bounds2     bounds           = { 20, 20, 390, 844 }; // iPhone-ish default
    RGBColor    backgroundColor  = { 0xFFFF, 0xFFFF, 0xFFFF };
    bool        visible          = true;
    bool        clipContent      = true;

    std::vector<std::unique_ptr<Shape>> children;
};
