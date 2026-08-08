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

    // Absolute canvas origin — used by both wrap and non-wrap paths.
    SInt32 originPri = isHoriz ? f->bounds.x : f->bounds.y;
    SInt32 originSec = isHoriz ? f->bounds.y : f->bounds.x;

    // ---- Wrap layout ----
    // Items flow in the primary direction; when the next item would overflow the
    // container's primary extent (minus padding) a new line is started.  Lines
    // are stacked in the cross direction with `gap` between them.
    if (f->layoutWrap) {
        struct WrapLine {
            std::vector<int> indices;
            SInt32 crossMax = 0;
        };

        SInt32 available = framePri - padPri1 - padPri2;
        if (available < 1) available = 1;

        std::vector<WrapLine> lines;
        WrapLine cur;
        SInt32 curUsed = 0;

        for (int i = 0; i < static_cast<int>(items.size()); i++) {
            const auto& it = items[i];
            UInt8  priSiz = isHoriz ? it.wSizing : it.hSizing;
            SInt32 priSz  = isHoriz ? *it.w : *it.h;
            SInt32 secSz  = isHoriz ? *it.h : *it.w;

            // Fill items are never forced to break — they're sized per-line later.
            bool isFill = (priSiz == static_cast<UInt8>(SizingMode::Fill));
            SInt32 needed = cur.indices.empty() ? priSz : curUsed + gap + priSz;
            if (!isFill && !cur.indices.empty() && needed > available) {
                lines.push_back(cur);
                cur = {};
                curUsed = priSz;
            } else {
                curUsed = cur.indices.empty() ? priSz : curUsed + gap + priSz;
            }
            if (secSz > cur.crossMax) cur.crossMax = secSz;
            cur.indices.push_back(i);
        }
        if (!cur.indices.empty()) lines.push_back(cur);

        // Total cross dimension across all lines (including gaps between lines).
        SInt32 totalCross = 0;
        for (auto& ln : lines) totalCross += ln.crossMax;
        if (lines.size() > 1)
            totalCross += gap * static_cast<SInt32>(lines.size() - 1);

        // Starting cross offset for the first line.
        SInt32 lineSecOff = padSec1;
        if (!hugSec && f->crossAlign != CrossAlign::Start) {
            SInt32 rem = frameSec - padSec1 - padSec2 - totalCross;
            if (rem > 0) {
                if (f->crossAlign == CrossAlign::Center)
                    lineSecOff = padSec1 + rem / 2;
                else  // End
                    lineSecOff = frameSec - padSec2 - totalCross;
            }
        }

        // Position each line.
        for (auto& ln : lines) {
            int nL = static_cast<int>(ln.indices.size());

            // Distribute Fill items in the primary direction within this line.
            SInt32 fixedPriSum = (nL > 1) ? gap * (nL - 1) : 0;
            int    fillCnt = 0;
            for (int ii : ln.indices) {
                UInt8 ps = isHoriz ? items[ii].wSizing : items[ii].hSizing;
                if (ps == static_cast<UInt8>(SizingMode::Fill)) ++fillCnt;
                else fixedPriSum += (isHoriz ? *items[ii].w : *items[ii].h);
            }
            SInt32 fillPriSz = 0;
            if (fillCnt > 0) {
                SInt32 rem = available - fixedPriSum;
                fillPriSz = (rem > 0) ? rem / fillCnt : 0;
            }

            // Apply fill sizes.
            for (int ii : ln.indices) {
                auto& it = items[ii];
                UInt8 ps = isHoriz ? it.wSizing : it.hSizing;
                UInt8 ss = isHoriz ? it.hSizing : it.wSizing;
                if (ps == static_cast<UInt8>(SizingMode::Fill)) {
                    if (isHoriz) *it.w = fillPriSz; else *it.h = fillPriSz;
                }
                // Cross Fill = expand to the line's cross extent.
                if (!hugSec && ss == static_cast<UInt8>(SizingMode::Fill)) {
                    if (isHoriz) *it.h = ln.crossMax; else *it.w = ln.crossMax;
                }
            }

            // Total line content for primary-axis alignment.
            SInt32 lineContent = 0;
            for (int ii : ln.indices) lineContent += isHoriz ? *items[ii].w : *items[ii].h;
            SInt32 lineGap = gap;
            if (f->primaryAlign == PrimaryAlign::SpaceBetween && nL > 1) {
                SInt32 rem = available - lineContent;
                lineGap = (rem > 0) ? rem / (nL - 1) : 0;
            }
            SInt32 lineSpan = lineContent + (nL > 1 ? lineGap * (nL - 1) : 0);

            SInt32 pos = padPri1;
            switch (f->primaryAlign) {
                case PrimaryAlign::Center:
                    pos = (framePri - lineSpan) / 2;
                    if (pos < padPri1) pos = padPri1;
                    break;
                case PrimaryAlign::End:
                    pos = framePri - padPri2 - lineSpan;
                    if (pos < padPri1) pos = padPri1;
                    break;
                default: pos = padPri1; break;
            }

            for (int ii : ln.indices) {
                auto& it = items[ii];
                SInt32 priSz = isHoriz ? *it.w : *it.h;
                SInt32 secSz = isHoriz ? *it.h : *it.w;

                if (isHoriz) *it.x = originPri + pos;
                else         *it.y = originPri + pos;

                SInt32 itemCross = lineSecOff;
                switch (f->crossAlign) {
                    case CrossAlign::Center:
                        itemCross = lineSecOff + (ln.crossMax - secSz) / 2;
                        break;
                    case CrossAlign::End:
                        itemCross = lineSecOff + ln.crossMax - secSz;
                        break;
                    default: break;
                }
                if (isHoriz) *it.y = originSec + itemCross;
                else         *it.x = originSec + itemCross;

                pos += priSz + lineGap;
            }

            lineSecOff += ln.crossMax + gap;
        }

        // Hug on the cross axis (wrapping makes primary-hug meaningless).
        if (isHoriz && f->heightSizing == SizingMode::Hug) {
            SInt32 h = totalCross + padSec1 + padSec2;
            f->bounds.h = (h > 0) ? h : 1;
        } else if (!isHoriz && f->widthSizing == SizingMode::Hug) {
            SInt32 w = totalCross + padSec1 + padSec2;
            f->bounds.w = (w > 0) ? w : 1;
        }
        return;
    }

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
