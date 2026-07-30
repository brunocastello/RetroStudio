#pragma once
#include "Shape.h"

// Frame = artboard / screen in the prototype.
// Frames own their Shape children and can contain nested child Frames.
// `parent` is a raw observer pointer (non-owning); null means top-level.
class Frame {
public:
    std::string name            = "Screen";
    Bounds2     bounds          = { 20, 20, 390, 844 };
    RGBColor    backgroundColor = { 0xFFFF, 0xFFFF, 0xFFFF };
    bool        visible         = true;
    bool        clipContent     = true;

    Frame* parent = nullptr;  // null = owned by Document::frames

    std::vector<std::unique_ptr<Shape>> children;
    std::vector<std::unique_ptr<Frame>> childFrames;
};
