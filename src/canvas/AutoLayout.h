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
// bounds or sizing mode. Used by the resize-drag "reached my Hug size" guide
// in window.cpp. Returns false (no meaningful hug size) for a frame with no
// Auto Layout or with Wrap enabled — Wrap's multi-line hug math isn't
// duplicated here, that guide is simply skipped for a Wrap frame.
bool ComputeFrameHugSize(const Frame* f, SInt32& outW, SInt32& outH);
