#include "AutoLayout.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Shape being drag-sorted (single-select) — excluded from layout.
extern Shape* gLayoutDragShape;
// Child frame being drag-sorted (single-select) — excluded from layout.
extern Frame* gLayoutDragFrame;
// During multi-select drag, all shapes in gSelectedShapes are excluded.
extern bool                gIsLayoutMultiDrag;
extern std::vector<Shape*> gSelectedShapes;

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
    SInt32  effW     = 0;   // AABB width for rotated shapes (0 = use *w)
    SInt32  effH     = 0;   // AABB height for rotated shapes (0 = use *h)
    SInt32  xOff     = 0;   // x correction after layout: (aabbW - w) / 2 for rotated
    SInt32  yOff     = 0;   // y correction after layout: (aabbH - h) / 2 for rotated
};

// value*num/den, rounded to nearest instead of truncated toward zero. Plain
// integer division here would bias every application toward shrinking (it only
// ever loses the fractional remainder, never gains it back), which is silently
// destructive across the many small resize steps a live drag produces — a
// Scale-constrained item would visibly end up smaller after shrink-then-grow
// even though the net resize was zero. int64_t avoids overflow on the product
// (SInt64 isn't declared in this toolchain's headers).
static SInt32 ScaleRounded(SInt32 value, SInt32 num, SInt32 den) {
    if (den == 0) return value;
    int64_t prod = static_cast<int64_t>(value) * static_cast<int64_t>(num);
    int64_t half = den / 2;
    return static_cast<SInt32>(prod >= 0 ? (prod + half) / den : (prod - half) / den);
}

// Live resize-handle drag in progress — see window.cpp. ApplyConstraints freezes
// its reference baseline for the whole drag while this is true, instead of
// re-basing on every mouse-move tick.
extern bool gIsResizeDragging;

// Repositions/resizes f's direct children per their constraintH/constraintV when
// f's own bounds have changed since the last SETTLED layout pass.
//
// Every child's position/size is recomputed from scratch each pass as a function
// of (a) the child's own constraintBaseline — its bounds as of the last settled
// pass — and (b) f's delta since that same settled pass (f->lastLayoutX/Y/W/H).
// This is deliberately NOT "add today's tiny delta to whatever the child's bounds
// already are": while gIsResizeDragging is true, neither f's baseline nor any
// child's baseline advances, so every tick of a live drag recomputes against the
// SAME fixed reference — mathematically identical to applying one single big
// resize, however many intermediate ticks the drag actually produces. Advancing
// the baseline every tick (the previous approach) fed each tick's already-rounded
// result back in as the next tick's input: for the additive modes that's exact
// (integer addition doesn't lose precision), but Scale's rounding is *not*
// reversible step-by-step, and a real per-pixel drag is hundreds of tiny steps —
// per-tick fractional growth routinely rounds away to nothing every single tick
// (frozen child), or different children/axes cross their own rounding threshold
// at different ticks (children drifting out of sync with each other and with
// their own aspect ratio). Freezing the baseline for the whole gesture removes
// the compounding entirely.
//
// The baseline update itself doubles as self-healing: any pass where nothing is
// actively dragging refreshes every child's baseline to its current bounds, so a
// stale baseline (fresh shape, cloned shape, post-undo state — none of these
// fields are serialized or explicitly copied) corrects itself within one redraw
// with no special-case handling needed at those call sites.
//
// Runs for every frame, not just non-layout ones: a plain frame (layoutMode==None)
// applies constraints to ALL of its children, while an Auto Layout frame applies
// them only to children flagged isAbsolutePosition (flow-managed children are
// repositioned by RunFrameLayout itself below and are skipped here).
static void ApplyConstraints(Frame* f) {
    bool primed = (f->lastLayoutW >= 0);
    SInt32 baseX = f->lastLayoutX, baseY = f->lastLayoutY;
    SInt32 baseW = f->lastLayoutW, baseH = f->lastLayoutH;
    SInt32 curX  = f->bounds.x,    curY  = f->bounds.y;
    SInt32 curW  = f->bounds.w,    curH  = f->bounds.h;

    bool freshBaseline = !primed;               // first pass ever for this frame
    bool captureNow     = freshBaseline || !gIsResizeDragging;
    if (captureNow) {
        f->lastLayoutX = curX; f->lastLayoutY = curY;
        f->lastLayoutW = curW; f->lastLayoutH = curH;
    }

    auto applyOne = [&](Bounds2& b, Bounds2& base, ConstraintMode ch, ConstraintMode cv) {
        if (freshBaseline) { base = b; return; }  // nothing to move yet — just establish the reference

        SInt32 dX = curX - baseX, dY = curY - baseY;
        SInt32 dW = curW - baseW, dH = curH - baseH;
        if (dX == 0 && dY == 0 && dW == 0 && dH == 0) {
            if (captureNow) base = b;
            return;
        }

        Bounds2 nb = base;
        switch (ch) {
            case ConstraintMode::Start:    nb.x = base.x + dX; break;                  // left edge offset fixed
            case ConstraintMode::End:      nb.x = base.x + dX + dW; break;             // right edge offset fixed
            case ConstraintMode::Center:   nb.x = base.x + dX + dW / 2; break;         // center offset fixed
            case ConstraintMode::StartEnd: {                                          // both edge offsets fixed (stretch)
                nb.x = base.x + dX;
                SInt32 w2 = base.w + dW; nb.w = (w2 < 1) ? 1 : w2;
            } break;
            case ConstraintMode::Scale:
                // base.x is an ABSOLUTE canvas coordinate, not relative to the parent
                // frame — scale the offset from the frame's baseline left edge, then
                // re-anchor to the frame's CURRENT left edge.
                if (baseW > 0) {
                    SInt32 relX = base.x - baseX;
                    nb.x = curX + ScaleRounded(relX, curW, baseW);
                    SInt32 w2 = ScaleRounded(base.w, curW, baseW);
                    nb.w = (w2 < 1) ? 1 : w2;
                }
                break;
        }
        switch (cv) {
            case ConstraintMode::Start:    nb.y = base.y + dY; break;
            case ConstraintMode::End:      nb.y = base.y + dY + dH; break;
            case ConstraintMode::Center:   nb.y = base.y + dY + dH / 2; break;
            case ConstraintMode::StartEnd: {
                nb.y = base.y + dY;
                SInt32 h2 = base.h + dH; nb.h = (h2 < 1) ? 1 : h2;
            } break;
            case ConstraintMode::Scale:
                if (baseH > 0) {
                    SInt32 relY = base.y - baseY;
                    nb.y = curY + ScaleRounded(relY, curH, baseH);
                    SInt32 h2 = ScaleRounded(base.h, curH, baseH);
                    nb.h = (h2 < 1) ? 1 : h2;
                }
                break;
        }

        b = nb;
        if (captureNow) base = nb;
    };

    bool freeForm = (f->layoutMode == LayoutMode::None);
    for (auto& s : f->children) {
        if (!freeForm && !s->isAbsolutePosition) continue;
        applyOne(s->bounds, s->constraintBaseline, s->constraintH, s->constraintV);
    }
    for (auto& cf : f->childFrames) {
        if (!freeForm && !cf->isAbsolutePosition) continue;
        applyOne(cf->bounds, cf->constraintBaseline, cf->constraintH, cf->constraintV);
    }
}

static void RunFrameLayout(Frame* f) {
    for (auto& cf : f->childFrames) RunFrameLayout(cf.get());

    ApplyConstraints(f);
    if (f->layoutMode == LayoutMode::None) return;

    // While a shape/frame is being drag-sorted anywhere in the document, freeze
    // every Hug frame's own width/height (Figma does not resize a Hug parent
    // live during a child drag — only siblings reflow live; the parent's own
    // size settles once the drag ends and RunDocumentLayout runs one more time
    // with the drag-exclusion globals cleared).
    bool dragActive = (gLayoutDragShape != nullptr || gLayoutDragFrame != nullptr || gIsLayoutMultiDrag);

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

    if (!f->childOrder.empty()) {
        // Unified z-order iteration
        for (const auto& cr : f->childOrder) {
            if (cr.isFrame) {
                const auto& cf = f->childFrames[cr.idx];
                if (!cf->visible || cf.get() == gLayoutDragFrame) continue;
                if (cf->isAbsolutePosition) continue;
                LayoutItem it;
                it.x = &cf->bounds.x; it.y = &cf->bounds.y;
                it.w = &cf->bounds.w; it.h = &cf->bounds.h;
                it.wSizing = static_cast<UInt8>(cf->widthSizing);
                it.hSizing = static_cast<UInt8>(cf->heightSizing);
                computeXtra(cf->hasStroke, cf->strokeWidth, cf->strokeAlign, it.xtraW, it.xtraH);
                it.baseline = 0;
                items.push_back(it);
            } else {
                const auto& s = f->children[cr.idx];
                if (!s->visible) continue;
                if (s.get() == gLayoutDragShape) continue;
                if (s->isAbsolutePosition) continue;
                if (gIsLayoutMultiDrag &&
                    std::find(gSelectedShapes.begin(), gSelectedShapes.end(), s.get()) != gSelectedShapes.end())
                    continue;
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
                if (s->rotation != 0) {
                    double rad  = s->rotation * 3.14159265358979323846 / 180.0;
                    double cosA = std::abs(std::cos(rad));
                    double sinA = std::abs(std::sin(rad));
                    SInt32 aw   = static_cast<SInt32>(s->bounds.w * cosA + s->bounds.h * sinA + 0.5);
                    SInt32 ah   = static_cast<SInt32>(s->bounds.w * sinA + s->bounds.h * cosA + 0.5);
                    it.effW = aw; it.effH = ah;
                    it.xOff = (aw - static_cast<SInt32>(s->bounds.w)) / 2;
                    it.yOff = (ah - static_cast<SInt32>(s->bounds.h)) / 2;
                }
                items.push_back(it);
            }
        }
    } else {
        // Legacy fallback: shapes first, then frames
        for (auto& s : f->children) {
            if (!s->visible) continue;
            if (s.get() == gLayoutDragShape) continue;
            if (s->isAbsolutePosition) continue;
            if (gIsLayoutMultiDrag &&
                std::find(gSelectedShapes.begin(), gSelectedShapes.end(), s.get()) != gSelectedShapes.end())
                continue;
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
            if (s->rotation != 0) {
                double rad  = s->rotation * 3.14159265358979323846 / 180.0;
                double cosA = std::abs(std::cos(rad));
                double sinA = std::abs(std::sin(rad));
                SInt32 aw   = static_cast<SInt32>(s->bounds.w * cosA + s->bounds.h * sinA + 0.5);
                SInt32 ah   = static_cast<SInt32>(s->bounds.w * sinA + s->bounds.h * cosA + 0.5);
                it.effW = aw; it.effH = ah;
                it.xOff = (aw - static_cast<SInt32>(s->bounds.w)) / 2;
                it.yOff = (ah - static_cast<SInt32>(s->bounds.h)) / 2;
            }
            items.push_back(it);
        }
        for (auto& cf : f->childFrames) {
            if (!cf->visible) continue;
            if (cf.get() == gLayoutDragFrame) continue;
            if (cf->isAbsolutePosition) continue;
            LayoutItem it;
            it.x = &cf->bounds.x; it.y = &cf->bounds.y;
            it.w = &cf->bounds.w; it.h = &cf->bounds.h;
            it.wSizing = static_cast<UInt8>(cf->widthSizing);
            it.hSizing = static_cast<UInt8>(cf->heightSizing);
            computeXtra(cf->hasStroke, cf->strokeWidth, cf->strokeAlign, it.xtraW, it.xtraH);
            it.baseline = 0;
            items.push_back(it);
        }
    }

    if (items.empty()) {
        // Only collapse to padding-only size when the frame is genuinely
        // childless. A frame that still has children — just none of them
        // flow-managed, e.g. every child is Absolute Position — keeps
        // whatever size it already had; there's nothing here to Hug around,
        // but that's not the same as being empty.
        bool trulyEmpty = f->children.empty() && f->childFrames.empty();
        if (!dragActive && trulyEmpty) {
            if (f->widthSizing  == SizingMode::Hug) f->bounds.w = f->paddingLeft  + f->paddingRight;
            if (f->heightSizing == SizingMode::Hug) f->bounds.h = f->paddingTop   + f->paddingBottom;
        }
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
    // For rotated shapes, effW/effH hold the AABB dimensions.
    auto ePri = [&](const LayoutItem& it) -> SInt32 {
        SInt32 base = isHoriz ? (it.effW ? it.effW : *it.w) : (it.effH ? it.effH : *it.h);
        return base + (isHoriz ? it.xtraW : it.xtraH);
    };
    auto eSec = [&](const LayoutItem& it) -> SInt32 {
        SInt32 base = isHoriz ? (it.effH ? it.effH : *it.h) : (it.effW ? it.effW : *it.w);
        return base + (isHoriz ? it.xtraH : it.xtraW);
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

        bool hugPri = isHoriz ? (f->widthSizing  == SizingMode::Hug)
                               : (f->heightSizing == SizingMode::Hug);
        SInt32 available = framePri - padPri1 - padPri2;
        if (available < 1) available = 1;
        if (hugPri) available = 0x7FFFFFFF;  // primary hugs: never wrap, grow to fit

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

        SInt32 rawCross = 0;
        for (auto& ln : lines) rawCross += ln.crossMax;

        SInt32 counterGap;
        if (f->layoutCounterGapAuto && static_cast<SInt32>(lines.size()) > 1) {
            SInt32 avail = frameSec - padSec1 - padSec2 - rawCross;
            counterGap = (avail > 0) ? avail / (static_cast<SInt32>(lines.size()) - 1) : 0;
        } else {
            counterGap = static_cast<SInt32>(f->layoutCounterGap);
        }

        SInt32 totalCross = rawCross;
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

        SInt32 maxLineSpan = 0;
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
            if (lineSpan > maxLineSpan) maxLineSpan = lineSpan;

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

        for (auto& it : items) { if (it.xOff || it.yOff) { *it.x += it.xOff; *it.y += it.yOff; } }

        // Primary hug (matches Figma: wrap+Hug behaves like no-wrap+Hug on primary axis)
        if (!dragActive) {
            if (hugPri) {
                SInt32 newPri = maxLineSpan + padPri1 + padPri2; if (newPri < 1) newPri = 1;
                if (isHoriz) f->bounds.w = newPri; else f->bounds.h = newPri;
            }
            // Secondary hug
            if (isHoriz && f->heightSizing == SizingMode::Hug) {
                SInt32 h = totalCross + padSec1 + padSec2; f->bounds.h = (h > 0) ? h : 1;
            } else if (!isHoriz && f->widthSizing == SizingMode::Hug) {
                SInt32 w = totalCross + padSec1 + padSec2; f->bounds.w = (w > 0) ? w : 1;
            }
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

    for (auto& it : items) { if (it.xOff || it.yOff) { *it.x += it.xOff; *it.y += it.yOff; } }

    // Baseline alignment post-pass (H layout only).
    {
        std::vector<int> allIdx;
        allIdx.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) allIdx.push_back(i);
        applyBaselinePass(allIdx, padSec1);
    }

    // Hug: shrink frame to content.
    if (!dragActive && (f->widthSizing == SizingMode::Hug || f->heightSizing == SizingMode::Hug)) {
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

static const UInt16 kDefaultAutoLayoutSpacing = 10;

void InferAutoLayoutSpacing(Frame* f, LayoutMode newMode) {
    struct Box { SInt32 x, y, w, h; };
    std::vector<Box> items;
    for (auto& s : f->children)     if (s->visible)  items.push_back({ s->bounds.x, s->bounds.y, s->bounds.w, s->bounds.h });
    for (auto& cf : f->childFrames) if (cf->visible) items.push_back({ cf->bounds.x, cf->bounds.y, cf->bounds.w, cf->bounds.h });

    if (items.empty()) {
        f->paddingLeft = f->paddingRight = f->paddingTop = f->paddingBottom = kDefaultAutoLayoutSpacing;
        f->layoutGap   = kDefaultAutoLayoutSpacing;
        return;
    }

    SInt32 minX = items[0].x, minY = items[0].y;
    SInt32 maxX = items[0].x + items[0].w, maxY = items[0].y + items[0].h;
    for (size_t i = 1; i < items.size(); ++i) {
        minX = std::min(minX, items[i].x);           minY = std::min(minY, items[i].y);
        maxX = std::max(maxX, items[i].x + items[i].w); maxY = std::max(maxY, items[i].y + items[i].h);
    }

    auto clampPad = [](SInt32 v) -> UInt16 {
        if (v < 0) return 0;
        return static_cast<UInt16>(v > 0xFFFF ? 0xFFFF : v);
    };
    f->paddingLeft   = clampPad(minX - f->bounds.x);
    f->paddingRight  = clampPad((f->bounds.x + f->bounds.w) - maxX);
    f->paddingTop    = clampPad(minY - f->bounds.y);
    f->paddingBottom = clampPad((f->bounds.y + f->bounds.h) - maxY);

    // Gap only makes sense along the axis being enabled, and only when the
    // first two items (sorted along that axis) don't already overlap there —
    // e.g. switching a vertically-stacked pair straight to Horizontal gives no
    // sensible horizontal gap to infer, so fall back to Figma's own default.
    if (items.size() >= 2) {
        bool isHoriz = (newMode == LayoutMode::Horizontal);
        std::sort(items.begin(), items.end(), [&](const Box& a, const Box& b) {
            return isHoriz ? (a.x < b.x) : (a.y < b.y);
        });
        SInt32 gap = isHoriz ? (items[1].x - (items[0].x + items[0].w))
                              : (items[1].y - (items[0].y + items[0].h));
        f->layoutGap = (gap >= 0) ? static_cast<UInt16>(gap) : kDefaultAutoLayoutSpacing;
    } else {
        f->layoutGap = kDefaultAutoLayoutSpacing;
    }
}
