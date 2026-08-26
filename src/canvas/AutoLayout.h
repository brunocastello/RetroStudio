#pragma once
#include "../core/Document.h"

// Runs the auto-layout pass on all frames in the document (bottom-up, then top-down).
// Call this before every canvas and inspector draw so bounds always reflect the
// current layout configuration.
void RunDocumentLayout(Document* doc);

// Call once, at the moment a frame's layoutMode transitions from None to
// Horizontal/Vertical, to make enabling Auto Layout non-destructive (matches
// Figma): infers padding/gap from the frame's CURRENT children so the very next
// layout pass reproduces the existing arrangement instead of snapping everything
// to zero-gap/zero-padding. Sets f's paddingTop/Right/Bottom/Left and layoutGap.
//
// Empty frame: bounds are never touched by this call (that's the caller's job —
// it should also leave sizing as Fixed rather than switching to Hug, since an
// empty Hug frame collapses to just its padding on the next layout pass); padding
// and gap default to 10 each, a reasonable starting margin for content added later.
void InferAutoLayoutSpacing(Frame* f, LayoutMode newMode);

// Non-mutating: the width/height Frame f WOULD hug to right now, given its
// CURRENT children's sizes, padding, and gap — without touching f's own
// bounds or sizing mode. Used for the resize-drag CROSS-axis "stop" guide in
// window.cpp (the primary axis uses ComputeLayoutBreakpoints below instead,
// which is more granular). Returns false for a frame with no Auto Layout or
// with Wrap enabled (a Wrap frame's cross-axis natural size needs summing
// per-row/column extents, out of scope here — not guided on that axis).
bool ComputeFrameHugSize(const Frame* f, SInt32& outW, SInt32& outH);

// Non-mutating: the set of primary-axis (width if Horizontal, height if
// Vertical) sizes at which something visually changes as the frame shrinks —
// TWO breakpoints per item boundary (see the .cpp for why): the item's own
// trailing edge, and the gap after it before the next item appears. Uses
// only the LEADING padding, never the trailing one — trailing padding is
// just empty space, so shrinking through it alone is never a visible
// "catch" point; the caller adds the one true padding+content Hug size (via
// ComputeFrameHugSize) on top of this list separately for that. Same
// breakpoint values whether Wrap is on or off (only the consequence of
// crossing one differs — reflow vs. clipping); used for the resize-drag
// "stop at paddings, gaps, and item edges" guide in window.cpp, Figma-style.
// Returns false for a frame with no Auto Layout or with no children.
bool ComputeLayoutBreakpoints(const Frame* f, std::vector<SInt32>& outBreaks);

// Non-mutating: the CROSS-axis (height if Horizontal, width if Vertical)
// counterpart — one breakpoint per item, at that item's own cross-axis size
// plus the cross axis's leading padding (no cumulative sum or gap, since
// cross-axis items aren't sequential the way primary-axis ones are). Assumes
// Start cross-alignment; Center/End alignment isn't modeled here yet.
// Returns false for a frame with no Auto Layout or with no children.
bool ComputeCrossAxisBreakpoints(const Frame* f, std::vector<SInt32>& outBreaks);
