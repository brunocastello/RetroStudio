#pragma once
#include "Shape.h"

enum class LayoutMode   : UInt8 { None = 0, Horizontal = 1, Vertical = 2 };
enum class PrimaryAlign : UInt8 { Start = 0, Center = 1, End = 2, SpaceBetween = 3 };
enum class CrossAlign   : UInt8 { Start = 0, Center = 1, End = 2 };
enum class SizingMode   : UInt8 { Fixed = 0, Hug = 1, Fill = 2 };

// Unified z-order reference: either a Shape (isFrame=false) or child Frame (isFrame=true),
// with idx pointing into children[] or childFrames[] respectively.
// childOrder[0] = bottom-most, childOrder.back() = topmost.
struct ChildRef { bool isFrame; int idx; };

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
    SInt16      cornerRadius    = 0;
    SInt16      cornerTL = 0, cornerTR = 0, cornerBR = 0, cornerBL = 0;
    bool        cornerIndividual = false;
    bool        visible         = true;
    bool        locked          = false;
    bool        clipContent     = true;
    UInt8       opacity         = 100;  // 0–100 percent
    SInt16      rotation        = 0;    // degrees, 0–359 clockwise (stored; frames not rendered rotated yet)

    // Auto Layout — how this frame arranges its children
    LayoutMode   layoutMode           = LayoutMode::None;
    bool         layoutWrap           = false;
    bool         strokesInLayout      = false;  // true = stroke included in layout size
    bool         canvasStackReverse   = false;  // true = first child on top
    bool         alignTextBaseline    = false;  // true = H-layout aligns text baselines
    UInt16       layoutGap            = 0;
    UInt16       layoutCounterGap     = 0;    // cross-axis gap between Wrap rows
    bool         layoutCounterGapAuto = false; // true = SpaceBetween distribution for rows
    UInt8        paddingTop    = 0;
    UInt8        paddingRight  = 0;
    UInt8        paddingBottom = 0;
    UInt8        paddingLeft   = 0;
    PrimaryAlign primaryAlign  = PrimaryAlign::Start;
    CrossAlign   crossAlign    = CrossAlign::Start;

    // Auto Layout — how this frame sizes itself as a child of a layout frame
    SizingMode   widthSizing   = SizingMode::Fixed;
    SizingMode   heightSizing  = SizingMode::Fixed;

    // Position & Constraints (same meaning as Shape's — see Shape.h)
    bool           isAbsolutePosition = false;
    ConstraintMode constraintH        = ConstraintMode::Start;
    ConstraintMode constraintV        = ConstraintMode::Start;

    // Runtime-only: this frame's own bounds.w/h as of the last layout pass, used by
    // AutoLayout.cpp to compute a resize delta for constraint-based repositioning of
    // children. Intentionally NOT serialized and NOT copied in CloneFrame (window.cpp) —
    // -1 means "unprimed," so every freshly loaded/cloned/undone frame re-primes on its
    // next layout pass instead of applying a spurious delta from stale state.
    SInt32 lastLayoutW = -1;
    SInt32 lastLayoutH = -1;

    Frame* parent = nullptr;  // null = owned by Document::frames

    std::vector<std::unique_ptr<Shape>> children;
    std::vector<std::unique_ptr<Frame>> childFrames;
    std::vector<ChildRef>               childOrder;  // z-order: [0]=bottom, [back()]=top
};
