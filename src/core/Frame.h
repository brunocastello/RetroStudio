#pragma once
#include "Shape.h"

enum class LayoutMode   : UInt8 { None = 0, Horizontal = 1, Vertical = 2 };
enum class PrimaryAlign : UInt8 { Start = 0, Center = 1, End = 2, SpaceBetween = 3 };
enum class CrossAlign   : UInt8 { Start = 0, Center = 1, End = 2 };
enum class SizingMode   : UInt8 { Fixed = 0, Hug = 1, Fill = 2 };

// Frame = artboard / screen in the prototype.
// Frames own their Shape children and can contain nested child Frames.
// `parent` is a raw observer pointer (non-owning); null means top-level.
class Frame {
public:
    std::string name            = "Screen";
    Bounds2     bounds          = { 20, 20, 390, 844 };
    RGBColor    backgroundColor = { 0xFFFF, 0xFFFF, 0xFFFF };
    bool        hasStroke       = false;
    RGBColor    strokeColor     = { 0, 0, 0 };
    UInt16      strokeWidth     = 1;
    UInt8       strokeAlign     = 0;  // 0=center 1=inside 2=outside
    bool        visible         = true;
    bool        locked          = false;
    bool        clipContent     = true;

    // Auto Layout — how this frame arranges its children
    LayoutMode   layoutMode           = LayoutMode::None;
    bool         layoutWrap           = false;
    bool         strokesInLayout      = false;  // true = stroke included in layout size
    bool         canvasStackReverse   = false;  // true = first child on top
    bool         alignTextBaseline    = false;  // true = H-layout aligns text baselines
    UInt16       layoutGap            = 0;
    UInt8        paddingTop    = 0;
    UInt8        paddingRight  = 0;
    UInt8        paddingBottom = 0;
    UInt8        paddingLeft   = 0;
    PrimaryAlign primaryAlign  = PrimaryAlign::Start;
    CrossAlign   crossAlign    = CrossAlign::Start;

    // Auto Layout — how this frame sizes itself as a child of a layout frame
    SizingMode   widthSizing   = SizingMode::Fixed;
    SizingMode   heightSizing  = SizingMode::Fixed;

    Frame* parent = nullptr;  // null = owned by Document::frames

    std::vector<std::unique_ptr<Shape>> children;
    std::vector<std::unique_ptr<Frame>> childFrames;
};
