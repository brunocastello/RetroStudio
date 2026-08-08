#include "AutoLayout.h"

// Unified view of one child item for the layout engine.
// Both Shape* children and Frame* childFrames are represented this way.
struct LayoutItem {
    SInt32* x;         // absolute canvas x
    SInt32* y;         // absolute canvas y
    SInt32* w;
    SInt32* h;
    UInt8   wSizing;   // SizingMode cast to byte
    UInt8   hSizing;
};

static void RunFrameLayout(Frame* f) {
    // Always recurse first so child frames have correct Hug sizes before we use them.
    for (auto& cf : f->childFrames) RunFrameLayout(cf.get());

    if (f->layoutMode == LayoutMode::None) return;

    // Build unified item list: shapes (always Fixed) then child frames.
    std::vector<LayoutItem> items;
    items.reserve(f->children.size() + f->childFrames.size());

    for (auto& s : f->children) {
        LayoutItem it;
        it.x = &s->bounds.x; it.y = &s->bounds.y;
        it.w = &s->bounds.w; it.h = &s->bounds.h;
        it.wSizing = static_cast<UInt8>(SizingMode::Fixed);
        it.hSizing = static_cast<UInt8>(SizingMode::Fixed);
        items.push_back(it);
    }
    for (auto& cf : f->childFrames) {
        LayoutItem it;
        it.x = &cf->bounds.x; it.y = &cf->bounds.y;
        it.w = &cf->bounds.w; it.h = &cf->bounds.h;
        it.wSizing = static_cast<UInt8>(cf->widthSizing);
        it.hSizing = static_cast<UInt8>(cf->heightSizing);
        items.push_back(it);
    }

    // Hug on empty frame: collapse to padding only.
    if (items.empty()) {
        if (f->widthSizing  == SizingMode::Hug) f->bounds.w = f->paddingLeft + f->paddingRight;
        if (f->heightSizing == SizingMode::Hug) f->bounds.h = f->paddingTop  + f->paddingBottom;
        return;
    }

    bool isHoriz = (f->layoutMode == LayoutMode::Horizontal);
    SInt32 n     = static_cast<SInt32>(items.size());

    // Primary axis = horizontal axis when isHoriz, else vertical.
    SInt32 padPri1 = isHoriz ? f->paddingLeft  : f->paddingTop;
    SInt32 padPri2 = isHoriz ? f->paddingRight : f->paddingBottom;
    SInt32 padSec1 = isHoriz ? f->paddingTop   : f->paddingLeft;
    SInt32 padSec2 = isHoriz ? f->paddingBottom : f->paddingRight;
    SInt32 gap     = static_cast<SInt32>(f->layoutGap);

    SInt32 framePri = isHoriz ? f->bounds.w : f->bounds.h;
    SInt32 frameSec = isHoriz ? f->bounds.h : f->bounds.w;

    // If this frame is Hug on the secondary axis, Fill-secondary items can't
    // expand (the parent has no fixed cross size to fill yet).
    bool hugSec = isHoriz ? (f->heightSizing == SizingMode::Hug)
                           : (f->widthSizing  == SizingMode::Hug);

    // Pass 1 — sum fixed/hug primary sizes, count fills, find max secondary.
    int    fillPriCount  = 0;
    SInt32 fixedPriTotal = padPri1 + padPri2 + gap * (n - 1);
    SInt32 maxSecSize    = 0;

    for (const auto& it : items) {
        UInt8  priSiz = isHoriz ? it.wSizing : it.hSizing;
        UInt8  secSiz = isHoriz ? it.hSizing : it.wSizing;
        SInt32 priSz  = isHoriz ? *it.w : *it.h;
        SInt32 secSz  = isHoriz ? *it.h : *it.w;

        if (priSiz == static_cast<UInt8>(SizingMode::Fill)) {
            ++fillPriCount;
        } else {
            fixedPriTotal += priSz;
        }
        // Track max secondary for Hug-secondary calculation.
        bool secFill = (secSiz == static_cast<UInt8>(SizingMode::Fill));
        if (!hugSec || !secFill) {
            if (secSz > maxSecSize) maxSecSize = secSz;
        }
    }

    // Distribute remaining primary space evenly among Fill items.
    SInt32 fillPriSize = 0;
    if (fillPriCount > 0) {
        SInt32 rem = framePri - fixedPriTotal;
        fillPriSize = (rem > 0) ? rem / fillPriCount : 0;
    }

    // Total content width (excluding outer padding) — needed for alignment offset.
    SInt32 totalContent = 0;
    for (const auto& it : items) {
        UInt8  priSiz = isHoriz ? it.wSizing : it.hSizing;
        SInt32 priSz  = isHoriz ? *it.w : *it.h;
        totalContent += (priSiz == static_cast<UInt8>(SizingMode::Fill)) ? fillPriSize : priSz;
    }
    SInt32 totalWithGap = totalContent + gap * (n - 1);

    // SpaceBetween overrides the stored gap.
    SInt32 actualGap = gap;
    if (f->primaryAlign == PrimaryAlign::SpaceBetween && n > 1) {
        SInt32 rem = framePri - padPri1 - padPri2 - totalContent;
        actualGap = (rem > 0) ? rem / (n - 1) : 0;
    }

    // Starting offset (local, from frame's primary-axis origin).
    SInt32 pos = padPri1;
    switch (f->primaryAlign) {
        case PrimaryAlign::Center: {
            pos = (framePri - totalWithGap) / 2;
            if (pos < padPri1) pos = padPri1;
            break;
        }
        case PrimaryAlign::End: {
            pos = framePri - padPri2 - totalWithGap;
            if (pos < padPri1) pos = padPri1;
            break;
        }
        default:
            pos = padPri1;
            break;
    }

    // Fill size on secondary axis (for items that stretch across the frame).
    SInt32 fillSecSize = frameSec - padSec1 - padSec2;
    if (fillSecSize < 0) fillSecSize = 0;

    // Absolute canvas origin of this frame.
    SInt32 originPri = isHoriz ? f->bounds.x : f->bounds.y;
    SInt32 originSec = isHoriz ? f->bounds.y : f->bounds.x;

    // Pass 2 — assign sizes and positions.
    for (auto& it : items) {
        UInt8 priSiz = isHoriz ? it.wSizing : it.hSizing;
        UInt8 secSiz = isHoriz ? it.hSizing : it.wSizing;

        // Expand Fill items.
        if (priSiz == static_cast<UInt8>(SizingMode::Fill)) {
            if (isHoriz) *it.w = fillPriSize; else *it.h = fillPriSize;
        }
        if (!hugSec && secSiz == static_cast<UInt8>(SizingMode::Fill)) {
            if (isHoriz) *it.h = fillSecSize; else *it.w = fillSecSize;
        }

        SInt32 priSz = isHoriz ? *it.w : *it.h;
        SInt32 secSz = isHoriz ? *it.h : *it.w;

        // Primary axis position (absolute canvas coords).
        if (isHoriz) *it.x = originPri + pos;
        else         *it.y = originPri + pos;

        // Cross axis position.
        SInt32 secPos = padSec1;
        switch (f->crossAlign) {
            case CrossAlign::Center:
                secPos = (frameSec - secSz) / 2;
                if (secPos < padSec1) secPos = padSec1;
                break;
            case CrossAlign::End:
                secPos = frameSec - padSec2 - secSz;
                if (secPos < padSec1) secPos = padSec1;
                break;
            default:
                secPos = padSec1;
                break;
        }
        if (isHoriz) *it.y = originSec + secPos;
        else         *it.x = originSec + secPos;

        pos += priSz + actualGap;
    }

    // If the frame itself is Hug, shrink to wrap its now-positioned children.
    if (f->widthSizing == SizingMode::Hug || f->heightSizing == SizingMode::Hug) {
        SInt32 newPri = totalWithGap + padPri1 + padPri2;
        SInt32 newSec = maxSecSize   + padSec1 + padSec2;
        if (newPri < 1) newPri = 1;
        if (newSec < 1) newSec = 1;
        if (isHoriz) {
            if (f->widthSizing  == SizingMode::Hug) f->bounds.w = newPri;
            if (f->heightSizing == SizingMode::Hug) f->bounds.h = newSec;
        } else {
            if (f->heightSizing == SizingMode::Hug) f->bounds.h = newPri;
            if (f->widthSizing  == SizingMode::Hug) f->bounds.w = newSec;
        }
    }
}

void RunDocumentLayout(Document* doc) {
    if (!doc) return;
    for (auto& f : doc->frames) RunFrameLayout(f.get());
}
