#include "AutoLayout.h"

// Unified view of one child item.  Both Shape* and Frame* children use this.
struct LayoutItem {
    SInt32* x;
    SInt32* y;
    SInt32* w;
    SInt32* h;
    UInt8   wSizing;
    UInt8   hSizing;
    SInt32  xtraW    = 0;   // stroke width added to primary/secondary measurement
    SInt32  xtraH    = 0;   // (only non-zero when f->strokesInLayout and hasStroke)
    SInt32  baseline = 0;   // distance from bounds top to text baseline; 0 = non-text
};

static void RunFrameLayout(Frame* f) {
    for (auto& cf : f->childFrames) RunFrameLayout(cf.get());

    if (f->layoutMode == LayoutMode::None) return;

    std::vector<LayoutItem> items;
    items.reserve(f->children.size() + f->childFrames.size());

    // Compute stroke layout extras for an element with the given stroke properties.
    auto computeXtra = [&](bool hasStroke, UInt16 sw, UInt8 align,
                            SInt32& xw, SInt32& xh) {
        xw = xh = 0;
        if (!f->strokesInLayout || !hasStroke) return;
        SInt32 s = static_cast<SInt32>(sw);
        if      (align == 0) { xw = s;     xh = s;     }  // center: overflow = s/2 each side
        else if (align == 2) { xw = s * 2; xh = s * 2; }  // outside: full overflow each side
        // inside (1): stroke stays within bounds — no extra
    };

    for (auto& s : f->children) {
        LayoutItem it;
        it.x = &s->bounds.x; it.y = &s->bounds.y;
        it.w = &s->bounds.w; it.h = &s->bounds.h;
        it.wSizing = s->wSizing;
        it.hSizing = s->hSizing;
        computeXtra(s->hasStroke, s->strokeWidth, s->strokeAlign, it.xtraW, it.xtraH);
        it.baseline = 0;
        if (f->alignTextBaseline && s->GetType() == Shape::kText) {
            it.baseline = static_cast<SInt32>(
                static_cast<TextShape*>(s.get())->fontSize) * 3 / 4;
        }
        items.push_back(it);
    }
    for (auto& cf : f->childFrames) {
        LayoutItem it;
        it.x = &cf->bounds.x; it.y = &cf->bounds.y;
        it.w = &cf->bounds.w; it.h = &cf->bounds.h;
        it.wSizing = static_cast<UInt8>(cf->widthSizing);
        it.hSizing = static_cast<UInt8>(cf->heightSizing);
        computeXtra(cf->hasStroke, cf->strokeWidth, cf->strokeAlign, it.xtraW, it.xtraH);
        it.baseline = 0;
        items.push_back(it);
    }

    if (items.empty()) {
        if (f->widthSizing  == SizingMode::Hug) f->bounds.w = f->paddingLeft  + f->paddingRight;
        if (f->heightSizing == SizingMode::Hug) f->bounds.h = f->paddingTop   + f->paddingBottom;
        return;
    }

    bool isHoriz = (f->layoutMode == LayoutMode::Horizontal);
    SInt32 n     = static_cast<SInt32>(items.size());

    SInt32 padPri1 = isHoriz ? f->paddingLeft   : f->paddingTop;
    SInt32 padPri2 = isHoriz ? f->paddingRight  : f->paddingBottom;
    SInt32 padSec1 = isHoriz ? f->paddingTop    : f->paddingLeft;
    SInt32 padSec2 = isHoriz ? f->paddingBottom : f->paddingRight;
    SInt32 gap     = static_cast<SInt32>(f->layoutGap);

    SInt32 framePri = isHoriz ? f->bounds.w : f->bounds.h;
    SInt32 frameSec = isHoriz ? f->bounds.h : f->bounds.w;

    bool hugSec = isHoriz ? (f->heightSizing == SizingMode::Hug)
                           : (f->widthSizing  == SizingMode::Hug);

    SInt32 originPri = isHoriz ? f->bounds.x : f->bounds.y;
    SInt32 originSec = isHoriz ? f->bounds.y : f->bounds.x;

    // Effective primary/secondary size including stroke extras.
    auto ePri = [&](const LayoutItem& it) -> SInt32 {
        return (isHoriz ? *it.w : *it.h) + (isHoriz ? it.xtraW : it.xtraH);
    };
    auto eSec = [&](const LayoutItem& it) -> SInt32 {
        return (isHoriz ? *it.h : *it.w) + (isHoriz ? it.xtraH : it.xtraW);
    };

    // Baseline post-pass (H layout only): shift text items so baselines align.
    auto applyBaselinePass = [&](const std::vector<int>& indices, SInt32 secOff) {
        if (!f->alignTextBaseline || !isHoriz) return;
        SInt32 maxBase = 0;
        for (int ii : indices) if (items[ii].baseline > maxBase) maxBase = items[ii].baseline;
        if (maxBase <= 0) return;
        SInt32 target = originSec + secOff + maxBase;
        for (int ii : indices) {
            if (items[ii].baseline > 0)
                *items[ii].y = target - items[ii].baseline;
        }
    };

    // ---- Wrap layout ----
    if (f->layoutWrap) {
        struct WrapLine { std::vector<int> indices; SInt32 crossMax = 0; };
        SInt32 counterGap = static_cast<SInt32>(f->layoutCounterGap);

        SInt32 available = framePri - padPri1 - padPri2;
        if (available < 1) available = 1;

        std::vector<WrapLine> lines;
        WrapLine cur;
        SInt32 curUsed = 0;

        for (int i = 0; i < static_cast<int>(items.size()); i++) {
            const auto& it = items[i];
            UInt8  priSiz = isHoriz ? it.wSizing : it.hSizing;
            SInt32 priSz  = ePri(it);
            SInt32 secSz  = eSec(it);

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

        SInt32 totalCross = 0;
        for (auto& ln : lines) totalCross += ln.crossMax;
        if (lines.size() > 1)
            totalCross += counterGap * static_cast<SInt32>(lines.size() - 1);

        SInt32 lineSecOff = padSec1;
        if (!hugSec && f->crossAlign != CrossAlign::Start) {
            SInt32 rem = frameSec - padSec1 - padSec2 - totalCross;
            if (rem > 0) {
                if (f->crossAlign == CrossAlign::Center) lineSecOff = padSec1 + rem / 2;
                else                                     lineSecOff = frameSec - padSec2 - totalCross;
            }
        }

        for (auto& ln : lines) {
            int nL = static_cast<int>(ln.indices.size());

            SInt32 fixedPriSum = (nL > 1) ? gap * (nL - 1) : 0;
            int    fillCnt = 0;
            for (int ii : ln.indices) {
                UInt8 ps = isHoriz ? items[ii].wSizing : items[ii].hSizing;
                if (ps == static_cast<UInt8>(SizingMode::Fill)) ++fillCnt;
                else fixedPriSum += ePri(items[ii]);
            }
            SInt32 fillPriSz = 0;
            if (fillCnt > 0) {
                SInt32 rem = available - fixedPriSum;
                fillPriSz = (rem > 0) ? rem / fillCnt : 0;
            }

            for (int ii : ln.indices) {
                auto& it = items[ii];
                UInt8 ps = isHoriz ? it.wSizing : it.hSizing;
                UInt8 ss = isHoriz ? it.hSizing : it.wSizing;
                if (ps == static_cast<UInt8>(SizingMode::Fill)) {
                    if (isHoriz) *it.w = fillPriSz; else *it.h = fillPriSz;
                }
                if (!hugSec && ss == static_cast<UInt8>(SizingMode::Fill)) {
                    SInt32 fs = ln.crossMax - (isHoriz ? it.xtraH : it.xtraW);
                    if (fs < 1) fs = 1;
                    if (isHoriz) *it.h = fs; else *it.w = fs;
                }
            }

            SInt32 lineContent = 0;
            for (int ii : ln.indices) lineContent += ePri(items[ii]);
            SInt32 lineGap = gap;
            if (f->primaryAlign == PrimaryAlign::SpaceBetween && nL > 1) {
                SInt32 rem = available - lineContent;
                lineGap = (rem > 0) ? rem / (nL - 1) : 0;
            }
            SInt32 lineSpan = lineContent + (nL > 1 ? lineGap * (nL - 1) : 0);

            SInt32 pos = padPri1;
            switch (f->primaryAlign) {
                case PrimaryAlign::Center: pos = (framePri - lineSpan) / 2; if (pos < padPri1) pos = padPri1; break;
                case PrimaryAlign::End:    pos = framePri - padPri2 - lineSpan; if (pos < padPri1) pos = padPri1; break;
                default: break;
            }

            for (int ii : ln.indices) {
                auto& it = items[ii];
                SInt32 priSz = ePri(it);
                SInt32 secSz = eSec(it);

                if (isHoriz) *it.x = originPri + pos;
                else         *it.y = originPri + pos;

                SInt32 itemCross = lineSecOff;
                switch (f->crossAlign) {
                    case CrossAlign::Center: itemCross = lineSecOff + (ln.crossMax - secSz) / 2; break;
                    case CrossAlign::End:    itemCross = lineSecOff + ln.crossMax - secSz;        break;
                    default: break;
                }
                if (isHoriz) *it.y = originSec + itemCross;
                else         *it.x = originSec + itemCross;

                pos += priSz + lineGap;
            }

            applyBaselinePass(ln.indices, lineSecOff);
            lineSecOff += ln.crossMax + counterGap;
        }

        if (isHoriz && f->heightSizing == SizingMode::Hug) {
            SInt32 h = totalCross + padSec1 + padSec2; f->bounds.h = (h > 0) ? h : 1;
        } else if (!isHoriz && f->widthSizing == SizingMode::Hug) {
            SInt32 w = totalCross + padSec1 + padSec2; f->bounds.w = (w > 0) ? w : 1;
        }
        return;
    }

    // ---- Non-wrap layout ----

    // Pass 1: measure.
    int    fillPriCount  = 0;
    SInt32 fixedPriTotal = padPri1 + padPri2 + gap * (n - 1);
    SInt32 maxSecSize    = 0;

    for (const auto& it : items) {
        UInt8  priSiz = isHoriz ? it.wSizing : it.hSizing;
        UInt8  secSiz = isHoriz ? it.hSizing : it.wSizing;
        SInt32 priSz  = ePri(it);
        SInt32 secSz  = eSec(it);

        if (priSiz == static_cast<UInt8>(SizingMode::Fill)) { ++fillPriCount; }
        else                                                 { fixedPriTotal += priSz; }

        bool secFill = (secSiz == static_cast<UInt8>(SizingMode::Fill));
        if (!hugSec || !secFill) { if (secSz > maxSecSize) maxSecSize = secSz; }
    }

    SInt32 fillPriSize = 0;
    if (fillPriCount > 0) {
        SInt32 rem = framePri - fixedPriTotal;
        fillPriSize = (rem > 0) ? rem / fillPriCount : 0;
    }

    SInt32 totalContent = 0;
    for (const auto& it : items) {
        UInt8  priSiz = isHoriz ? it.wSizing : it.hSizing;
        SInt32 priSz  = ePri(it);
        totalContent += (priSiz == static_cast<UInt8>(SizingMode::Fill)) ? fillPriSize : priSz;
    }
    SInt32 totalWithGap = totalContent + gap * (n - 1);

    SInt32 actualGap = gap;
    if (f->primaryAlign == PrimaryAlign::SpaceBetween && n > 1) {
        SInt32 rem = framePri - padPri1 - padPri2 - totalContent;
        actualGap = (rem > 0) ? rem / (n - 1) : 0;
    }

    SInt32 pos = padPri1;
    switch (f->primaryAlign) {
        case PrimaryAlign::Center: pos = (framePri - totalWithGap) / 2; if (pos < padPri1) pos = padPri1; break;
        case PrimaryAlign::End:    pos = framePri - padPri2 - totalWithGap; if (pos < padPri1) pos = padPri1; break;
        default: break;
    }

    SInt32 fillSecSize = frameSec - padSec1 - padSec2;
    if (fillSecSize < 0) fillSecSize = 0;

    // Pass 2: assign sizes and positions.
    for (auto& it : items) {
        UInt8 priSiz = isHoriz ? it.wSizing : it.hSizing;
        UInt8 secSiz = isHoriz ? it.hSizing : it.wSizing;

        if (priSiz == static_cast<UInt8>(SizingMode::Fill)) {
            if (isHoriz) *it.w = fillPriSize; else *it.h = fillPriSize;
        }
        if (!hugSec && secSiz == static_cast<UInt8>(SizingMode::Fill)) {
            SInt32 fs = fillSecSize - (isHoriz ? it.xtraH : it.xtraW);
            if (fs < 1) fs = 1;
            if (isHoriz) *it.h = fs; else *it.w = fs;
        }

        SInt32 priSz = ePri(it);
        SInt32 secSz = eSec(it);

        if (isHoriz) *it.x = originPri + pos;
        else         *it.y = originPri + pos;

        SInt32 secPos = padSec1;
        switch (f->crossAlign) {
            case CrossAlign::Center:
                secPos = (frameSec - secSz) / 2; if (secPos < padSec1) secPos = padSec1; break;
            case CrossAlign::End:
                secPos = frameSec - padSec2 - secSz; if (secPos < padSec1) secPos = padSec1; break;
            default: break;
        }
        if (isHoriz) *it.y = originSec + secPos;
        else         *it.x = originSec + secPos;

        pos += priSz + actualGap;
    }

    // Baseline alignment post-pass (H layout only).
    {
        std::vector<int> allIdx;
        allIdx.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) allIdx.push_back(i);
        applyBaselinePass(allIdx, padSec1);
    }

    // Hug: shrink frame to content.
    if (f->widthSizing == SizingMode::Hug || f->heightSizing == SizingMode::Hug) {
        SInt32 newPri = totalWithGap + padPri1 + padPri2; if (newPri < 1) newPri = 1;
        SInt32 newSec = maxSecSize   + padSec1 + padSec2; if (newSec < 1) newSec = 1;
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
