#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"
#include "AYUI/DockTabGroup.h"
#include "AYUI/Box.h"
#include "AYUI/SplitterHandle.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/UIManager.h"
#include "AYUI/DockTrace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace ayt::ui {

namespace {

// Returns the slot's "weight" used by the layout pass when the host
// hasn't called setSlotWeight. Stored default values are in the header;
// this helper exists only to centralise the 0.0 = use-default rule.
//
// Near-zero weights (Gallery uses 1e-6 for Top/Bottom) mean "slot
// disabled" — do NOT fall back to the defaults, or the invisible strip
// still steals drops and the card collapses into a hairline band.
constexpr float kSlotWeightDisabled = 1.0e-4f;
float effectiveWeight(const DockArea& area, DockArea::Slot slot) {
    const float w = area.getSlotWeight(slot);
    if (w > 0.0f && w < kSlotWeightDisabled) {
        return 0.0f;
    }
    if (w > 0.0f) {
        return w;
    }
    // Fall back to the header-defined defaults. Keep these in sync with
    // the DockArea header initialiser list.
    switch (slot) {
        case DockArea::Slot::Left:   return 0.20f;
        case DockArea::Slot::Right:  return 0.25f;
        case DockArea::Slot::Top:    return 0.15f;
        case DockArea::Slot::Bottom: return 0.20f;
        case DockArea::Slot::Center: return 0.55f;
        default: return 0.0f;
    }
}

const wchar_t* slotLabel(DockArea::Slot slot) {
    switch (slot) {
        case DockArea::Slot::Left:   return L"Left";
        case DockArea::Slot::Right:  return L"Right";
        case DockArea::Slot::Top:    return L"Top";
        case DockArea::Slot::Bottom: return L"Bottom";
        case DockArea::Slot::Center: return L"Center";
        default: return L"?";
    }
}

bool isDockCardDrag(UIManager* ui) {
    return ui != nullptr
        && ui->isDragging()
        && ui->getDragPayload().kind == "DockCard";
}

bool dragBelongsToDock(const DockArea& area, UIManager* ui) {
    if (!isDockCardDrag(ui)) {
        return false;
    }
    auto cardBelongs = [&area](const DockCard* card) -> bool {
        if (card == nullptr || card->getId().empty()) {
            return false;
        }
        if (area.findCard(card->getId()) != nullptr) {
            return true;
        }
        // Floating cards are erased from _cardIndex (K-INV-D3-7) but
        // still belong to this dock — they live in its overlay. Without
        // this check a floating-card redrop would be misclassified as
        // EXTERNAL and take the legacy adoptCard path instead of the
        // tree join/split dispatch.
        const DockOverlay* ov = area.getOverlay();
        if (ov != nullptr) {
            for (size_t i = 0; i < ov->getFloatingCardCount(); ++i) {
                if (ov->getFloatingCard(i) == card) {
                    return true;
                }
            }
        }
        return false;
    };
    if (DockCard* fromData = static_cast<DockCard*>(ui->getDragPayload().data)) {
        if (cardBelongs(fromData)) {
            return true;
        }
    }
    if (DockCard* fromSrc = dynamic_cast<DockCard*>(ui->getDragSource())) {
        if (cardBelongs(fromSrc)) {
            return true;
        }
    }
    return false;
}

// PR-Container-Shared-Contract: shared 6-value arithmetic used by
// performLayout() and hitTestSlot(). Weights order: Top, Bottom, Left,
// Right, Center. Call sites still choose their own weight source
// (applyTemplateGeometry vs effectiveWeight — both use effectiveWeight
// today; the template never collapses empty slots).
struct SlotRegions {
    float topH, botH;
    float midY, midH;
    float leftW, rightW, centerW;
};
SlotRegions computeSlotRegions(float w, float h, const float weights[5]) {
    SlotRegions r;
    const float wtTop = weights[0];
    const float wtBot = weights[1];
    const float wtLeft = weights[2];
    const float wtRight = weights[3];
    const float wtCenter = weights[4];
    const float sumVert = wtTop + wtBot + 1.0f;  // middle row takes the rest
    r.topH = h * (wtTop / sumVert);
    r.botH = h * (wtBot / sumVert);
    r.midY = r.topH;
    r.midH = h - r.topH - r.botH;
    const float sumHoriz = wtLeft + wtCenter + wtRight;
    r.leftW   = (sumHoriz > 0.0f) ? w * (wtLeft   / sumHoriz) : 0.0f;
    r.rightW  = (sumHoriz > 0.0f) ? w * (wtRight  / sumHoriz) : 0.0f;
    r.centerW = w - r.leftW - r.rightW;
    return r;
}

// PR-Dock-SlotHighlight: the 5-region split with the SAME weight source
// as hitTestSlot (configured weights via effectiveWeight — empty slots
// stay valid drop targets even when performLayout collapses them). Used
// by both hitTestSlot and getSlotRect so the highlight rect and the
// hit-test stay in lockstep by construction.
SlotRegions hitTestRegions(const DockArea& area) {
    // Use live world-bounds size (not getSize alone). A stale/zero
    // _size with a still-valid cached world rect produced "outer frame
    // yes, every slot rect empty" in Gallery.
    const math::FRectangle wb = area.getWorldBounds();
    const float w = std::max(0.0f, wb.maxX - wb.minX);
    const float h = std::max(0.0f, wb.maxY - wb.minY);
    const float hitTestWeights[5] = {
        effectiveWeight(area, DockArea::Slot::Top),
        effectiveWeight(area, DockArea::Slot::Bottom),
        effectiveWeight(area, DockArea::Slot::Left),
        effectiveWeight(area, DockArea::Slot::Right),
        effectiveWeight(area, DockArea::Slot::Center),
    };
    return computeSlotRegions(w, h, hitTestWeights);
}

void requestRelayout() {
    if (UIManager* ui = UIManager::tryGet()) {
        ui->invalidateLayout();
        ui->layout();
    }
}

SplitterHandle* makeTreeSplitter(SplitterHandle::Orientation orientation,
                                 const std::string& id) {
    auto* handle = new SplitterHandle();
    handle->setOrientation(orientation);
    handle->setId(id);
    return handle;
}

// ---- Phase 3 helpers ------------------------------------------------------

// Recursive card lookup across the whole tree (pinned leaves AND
// split-created g_N leaves). nullptr = card not in any leaf.
DockTabGroup* findLeafInNode(Widget* node, DockCard* card) {
    if (node == nullptr || card == nullptr) {
        return nullptr;
    }
    if (auto* leaf = dynamic_cast<DockTabGroup*>(node)) {
        return leaf->containsCard(card) ? leaf : nullptr;
    }
    if (auto* box = dynamic_cast<BoxBase*>(node)) {
        for (Widget* c : box->getChildren()) {
            if (DockTabGroup* f = findLeafInNode(c, card)) {
                return f;
            }
        }
    }
    return nullptr;
}

// Collect every card across all leaves (pinned + g_N) — used by
// poolAllCards for applyDockTree's detach-all pass.
void collectLeafCards(Widget* node, std::vector<DockCard*>& out) {
    if (node == nullptr) {
        return;
    }
    if (auto* leaf = dynamic_cast<DockTabGroup*>(node)) {
        const size_t n = leaf->getTabCount();
        for (size_t i = 0; i < n; ++i) {
            if (DockCard* c = leaf->getTab(i)) {
                out.push_back(c);
            }
        }
        return;
    }
    if (auto* box = dynamic_cast<BoxBase*>(node)) {
        for (Widget* c : box->getChildren()) {
            collectLeafCards(c, out);
        }
    }
}

// Remove `leaf` (a heap object owned by the dock tree) from `box`
// together with any directly adjacent splitter handles, then free them.
// Adjacency MUST use BoxBase::_slots order (slotIndexOf) — getChildren()
// diverges after insertWidget (addChild appends). Only called for EMPTY
// non-pinned leaves — no cards to leak.
void removeLeafAndNeighborSplitters(BoxBase* box, Widget* leaf) {
    std::vector<Widget*> toRemove;
    const int li = box->slotIndexOf(leaf);
    if (li >= 0) {
        if (Widget* before = box->slotAt(li - 1)) {
            if (before->isSplitterHandle()) {
                toRemove.push_back(before);
            }
        }
        if (Widget* after = box->slotAt(li + 1)) {
            if (after->isSplitterHandle()) {
                toRemove.push_back(after);
            }
        }
    }
    toRemove.push_back(leaf);
    for (Widget* w : toRemove) {
        box->removeWidget(w);
        destroyWidgetTree(w);
    }
}

// Detach `leaf` from `box` and destroy only adjacent splitters. The leaf
// stays alive for re-parenting (empty pinned sides extracted from nests).
void detachLeafKeepAlive(BoxBase* box, Widget* leaf) {
    if (box == nullptr || leaf == nullptr) {
        return;
    }
    std::vector<Widget*> splitters;
    const int li = box->slotIndexOf(leaf);
    if (li >= 0) {
        if (Widget* before = box->slotAt(li - 1)) {
            if (before->isSplitterHandle()) {
                splitters.push_back(before);
            }
        }
        if (Widget* after = box->slotAt(li + 1)) {
            if (after->isSplitterHandle()) {
                splitters.push_back(after);
            }
        }
    }
    for (Widget* s : splitters) {
        box->removeWidget(s);
        destroyWidgetTree(s);
    }
    box->removeWidget(leaf);
}

// Template hosts: root VBox, or mid HBox (direct child of root). Nest
// wraps created by orthogonal splits are NOT template hosts — collapsing
// a pinned leaf inside them leaves a dead fill band (Gallery progressive
// corruption after Right-North wrap → join-away).
bool isTemplateHost(BoxBase* box, Widget* root) {
    return box != nullptr && root != nullptr
        && (box == root || box->getParent() == root);
}

HBox* findMidHBox(Widget* root) {
    auto* rootBox = dynamic_cast<BoxBase*>(root);
    if (rootBox == nullptr) {
        return nullptr;
    }
    for (int i = 0; ; ++i) {
        Widget* c = rootBox->slotAt(i);
        if (c == nullptr) {
            break;
        }
        if (auto* mid = dynamic_cast<HBox*>(c)) {
            return mid;
        }
    }
    return nullptr;
}

// After removing a fill/collapsed sibling from a nest, remaining panels
// may all be fixed-size (e.g. g_2=105 + g_3=26). Give the last visible
// panel fill (size 0) so it absorbs the nest's leftover extent.
void ensureNestHasVisibleFill(BoxBase* nest) {
    if (nest == nullptr) {
        return;
    }
    int lastVisible = -1;
    bool hasFill = false;
    for (int i = 0; ; ++i) {
        Widget* w = nest->slotAt(i);
        if (w == nullptr) {
            break;
        }
        if (!w->isVisible() || w->isSplitterHandle()) {
            continue;
        }
        lastVisible = i;
        if (nest->slotSize(i) <= 0.0f) {
            hasFill = true;
        }
    }
    if (!hasFill && lastVisible >= 0) {
        nest->setSlotSize(lastVisible, 0.0f);
    }
}

// Walk every BoxBase under `node` and ensure exactly one visible
// non-splitter panel is fill (size 0). Without a fill, fixed-only
// children leave unpainted leftover (Gallery black gap). Multiple
// fills split leftover and can read as a mid band after multi-splits.
bool ensureAllBoxesHaveVisibleFill(Widget* node) {
    if (node == nullptr) {
        return false;
    }
    bool any = false;
    auto* box = dynamic_cast<BoxBase*>(node);
    if (box != nullptr) {
        for (Widget* c : box->getChildren()) {
            any |= ensureAllBoxesHaveVisibleFill(c);
        }

        int lastVisible = -1;
        int centerVisible = -1;
        std::vector<int> fillIndices;
        for (int i = 0; ; ++i) {
            Widget* w = box->slotAt(i);
            if (w == nullptr) {
                break;
            }
            if (!w->isVisible() || w->isSplitterHandle()) {
                continue;
            }
            lastVisible = i;
            if (box->slotSize(i) <= 0.0f) {
                fillIndices.push_back(i);
            }
            if (auto* leaf = dynamic_cast<DockTabGroup*>(w)) {
                if (leaf->getLeafId() == "Center") {
                    centerVisible = i;
                }
            }
        }
        if (lastVisible < 0) {
            return any;
        }

        const int preferredFill =
            (centerVisible >= 0) ? centerVisible : lastVisible;

        if (fillIndices.empty()) {
            box->setSlotSize(preferredFill, 0.0f);
            any = true;
        } else {
            const bool preferCenter =
                centerVisible >= 0
                && (fillIndices.size() > 1
                    || fillIndices[0] != preferredFill);
            if (fillIndices.size() > 1 || preferCenter) {
                for (int fi : fillIndices) {
                    if (fi == preferredFill) {
                        continue;
                    }
                    Widget* w = box->slotAt(fi);
                    const float keep = (w != nullptr)
                        ? std::max(BoxBase::kMinPanelSize, w->getWidth())
                        : BoxBase::kMinPanelSize;
                    box->setSlotSize(fi, keep);
                    any = true;
                }
                if (box->slotSize(preferredFill) > 0.0f) {
                    box->setSlotSize(preferredFill, 0.0f);
                    any = true;
                }
            }
        }
    }
    return any;
}

void setLeafChromeVisible(DockTabGroup* leaf, bool visible) {
    if (leaf == nullptr) {
        return;
    }
    leaf->setVisible(visible);
    if (auto* box = dynamic_cast<BoxBase*>(leaf->getParent())) {
        const int si = box->slotIndexOf(leaf);
        for (Widget* c : box->getChildren()) {
            if (c == nullptr || !c->isSplitterHandle()) {
                continue;
            }
            const int ci = box->slotIndexOf(c);
            if (ci == si - 1 || ci == si + 1) {
                c->setVisible(visible);
            }
        }
    }
}

// A splitter is interactive only when BOTH neighboring panels are
// visible. Collapsing an empty Right hides the Center|Right handle;
// East-split then reuses that same handle for Center|g_N but used to
// leave it invisible — Gallery "can't drag Center/Right boundary".
void syncSplitterVisibility(BoxBase* box) {
    if (box == nullptr) {
        return;
    }
    for (int i = 0; ; ++i) {
        Widget* w = box->slotAt(i);
        if (w == nullptr) {
            break;
        }
        if (!box->isSplitterSlot(i)) {
            continue;
        }
        int before = -1;
        int after = -1;
        for (int j = i - 1; j >= 0; --j) {
            if (!box->isSplitterSlot(j)) {
                before = j;
                break;
            }
        }
        for (int j = i + 1; ; ++j) {
            Widget* n = box->slotAt(j);
            if (n == nullptr) {
                break;
            }
            if (!box->isSplitterSlot(j)) {
                after = j;
                break;
            }
        }
        Widget* b = (before >= 0) ? box->slotAt(before) : nullptr;
        Widget* a = (after >= 0) ? box->slotAt(after) : nullptr;
        const bool show = b != nullptr && a != nullptr
            && b->isVisible() && a->isVisible();
        if (w->isVisible() != show) {
            w->setVisible(show);
            dockTrace("[dock] syncSplitter '%s' visible=%d (neighbors %s|%s)\n",
                      w->getId().c_str(), show ? 1 : 0,
                      b ? b->getId().c_str() : "?",
                      a ? a->getId().c_str() : "?");
        }
    }
}

void syncSplitterVisibilityInTree(Widget* node) {
    if (node == nullptr) {
        return;
    }
    if (auto* box = dynamic_cast<BoxBase*>(node)) {
        for (int i = 0; ; ++i) {
            Widget* c = box->slotAt(i);
            if (c == nullptr) {
                break;
            }
            syncSplitterVisibilityInTree(c);
        }
        syncSplitterVisibility(box);
    }
}

// Re-home a hidden Center leaf into the template mid as fill. Used when
// dissolving a vacant nest that still holds an empty/hidden Center
// (unwrap refuses to promote when Center remains — that left a mid gap).
void reinsertCenterIntoMid(Widget* root, DockTabGroup* center) {
    if (root == nullptr || center == nullptr) {
        return;
    }
    HBox* mid = findMidHBox(root);
    if (mid == nullptr || mid->slotIndexOf(center) >= 0) {
        return;
    }
    const std::string idPrefix = root->getId().empty() ? "dock" : root->getId();

    int afterLeft = 0;
    int beforeRight = -1;
    for (int i = 0; ; ++i) {
        Widget* w = mid->slotAt(i);
        if (w == nullptr) {
            break;
        }
        auto* lf = dynamic_cast<DockTabGroup*>(w);
        if (lf == nullptr) {
            continue;
        }
        if (lf->getLeafId() == "Left") {
            afterLeft = i + 1;
            if (Widget* next = mid->slotAt(afterLeft)) {
                if (next->isSplitterHandle()) {
                    ++afterLeft;
                }
            }
        } else if (lf->getLeafId() == "Right") {
            beforeRight = i;
            if (beforeRight > 0) {
                Widget* prev = mid->slotAt(beforeRight - 1);
                if (prev != nullptr && prev->isSplitterHandle()) {
                    --beforeRight;
                }
            }
        }
    }
    int insertAt = afterLeft;
    if (beforeRight >= 0 && insertAt > beforeRight) {
        insertAt = beforeRight;
    }
    if (insertAt > 0) {
        Widget* prev = mid->slotAt(insertAt - 1);
        if (prev != nullptr && !prev->isSplitterHandle()) {
            mid->insertWidget(
                insertAt++,
                makeTreeSplitter(SplitterHandle::Orientation::Horizontal,
                                 idPrefix + "::split_center_rehome_l"),
                SplitterHandle::kDefaultWidth);
        }
    }
    mid->insertWidget(insertAt, center, 0.0f);
    if (Widget* next = mid->slotAt(insertAt + 1)) {
        if (!next->isSplitterHandle()) {
            mid->insertWidget(
                insertAt + 1,
                makeTreeSplitter(SplitterHandle::Orientation::Horizontal,
                                 idPrefix + "::split_center_rehome_r"),
                SplitterHandle::kDefaultWidth);
        }
    }
    mid->rebindSplitters();
    dockTrace("[dock] reinsert Center into mid at %d\n", insertAt);
}

// Re-home an empty side pinned leaf into the template mid/root so
// collapse can hide it without punching a hole in a nest wrap.
void reinsertSidePinnedIntoTemplate(Widget* root, DockTabGroup* leaf) {
    if (root == nullptr || leaf == nullptr) {
        return;
    }
    const std::string& id = leaf->getLeafId();
    const std::string idPrefix = root->getId().empty() ? "dock" : root->getId();

    if (id == "Left" || id == "Right") {
        HBox* mid = findMidHBox(root);
        if (mid == nullptr || mid->slotIndexOf(leaf) >= 0) {
            return;
        }
        if (id == "Left") {
            mid->insertWidget(0, leaf, 0.0f);
            if (Widget* next = mid->slotAt(1)) {
                if (!next->isSplitterHandle()) {
                    mid->insertWidget(
                        1,
                        makeTreeSplitter(SplitterHandle::Orientation::Horizontal,
                                         idPrefix + "::split_left_rehome"),
                        SplitterHandle::kDefaultWidth);
                }
            }
        } else {
            int n = 0;
            while (mid->slotAt(n) != nullptr) {
                ++n;
            }
            if (n > 0) {
                Widget* last = mid->slotAt(n - 1);
                if (last != nullptr && !last->isSplitterHandle()) {
                    mid->addWidget(
                        makeTreeSplitter(SplitterHandle::Orientation::Horizontal,
                                         idPrefix + "::split_right_rehome"),
                        SplitterHandle::kDefaultWidth);
                }
            }
            mid->addWidget(leaf, 0.0f);
        }
        mid->rebindSplitters();
        return;
    }

    if (id == "Top" || id == "Bottom") {
        auto* rootBox = dynamic_cast<BoxBase*>(root);
        if (rootBox == nullptr || rootBox->slotIndexOf(leaf) >= 0) {
            return;
        }
        if (id == "Top") {
            rootBox->insertWidget(0, leaf, 0.0f);
            if (Widget* next = rootBox->slotAt(1)) {
                if (!next->isSplitterHandle()) {
                    rootBox->insertWidget(
                        1,
                        makeTreeSplitter(SplitterHandle::Orientation::Vertical,
                                         idPrefix + "::split_top_rehome"),
                        SplitterHandle::kDefaultWidth);
                }
            }
        } else {
            int n = 0;
            while (rootBox->slotAt(n) != nullptr) {
                ++n;
            }
            if (n > 0) {
                Widget* last = rootBox->slotAt(n - 1);
                if (last != nullptr && !last->isSplitterHandle()) {
                    rootBox->addWidget(
                        makeTreeSplitter(SplitterHandle::Orientation::Vertical,
                                         idPrefix + "::split_bot_rehome"),
                        SplitterHandle::kDefaultWidth);
                }
            }
            rootBox->addWidget(leaf, 0.0f);
        }
        rootBox->rebindSplitters();
    }
}

// After pruning g_N leaves, a nest VBox/HBox may hold a single *visible*
// panel (collapsed pinned siblings must not block unwrap — that left a
// fill hole under g_N in Gallery). Promote the visible panel; re-home
// any remaining invisible panels into the parent.
//
// `dockRoot` is the dock's root VBox. Direct children of dockRoot (the
// template mid HBox) must NEVER unwrap: collapsed Left/Right leave mid
// with one visible panel (Center), and unwrapping mid destroyed the
// template (broke join-after-south / close-split-leaf tests).
bool unwrapSinglePanelBoxes(BoxBase* box, BoxBase* parent, Widget* dockRoot) {
    if (box == nullptr) {
        return false;
    }
    bool changed = false;
    std::vector<Widget*> childBoxes;
    for (Widget* c : box->getChildren()) {
        if (dynamic_cast<BoxBase*>(c) != nullptr) {
            childBoxes.push_back(c);
        }
    }
    for (Widget* c : childBoxes) {
        changed |= unwrapSinglePanelBoxes(static_cast<BoxBase*>(c), box, dockRoot);
    }

    if (parent == nullptr) {
        return changed;   // never unwrap the dock root
    }
    if (parent == dockRoot) {
        return changed;   // never unwrap template mid / root children
    }

    Widget* sole = nullptr;
    int visiblePanels = 0;
    std::vector<Widget*> hiddenPanels;
    std::vector<Widget*> straySplitters;
    for (int i = 0; ; ++i) {
        Widget* c = box->slotAt(i);
        if (c == nullptr) {
            break;
        }
        if (c->isSplitterHandle()) {
            straySplitters.push_back(c);
            continue;
        }
        if (c->isVisible()) {
            ++visiblePanels;
            sole = c;
        } else {
            hiddenPanels.push_back(c);
        }
    }
    if (visiblePanels != 1 || sole == nullptr) {
        return changed;
    }
    // Hidden pinned Center must stay inside the nest as the fill host.
    // Unwrapping would promote g_N into mid and reinsert Center as a
    // second fill sibling → dual-fill progressive corruption on revive.
    for (Widget* h : hiddenPanels) {
        auto* lf = dynamic_cast<DockTabGroup*>(h);
        if (lf != nullptr && lf->isPinned() && lf->getLeafId() == "Center") {
            return changed;
        }
    }
    // Orphan splitters left after a bad adjacency prune — drop them
    // before promoting the sole panel.
    for (Widget* s : straySplitters) {
        box->removeWidget(s);
        destroyWidgetTree(s);
        changed = true;
    }

    const int nestSlot = parent->slotIndexOf(box);
    if (nestSlot < 0) {
        return changed;
    }
    const float nestSize = parent->slotSize(nestSlot);
    parent->removeWidget(box);
    box->removeWidget(sole);
    for (Widget* h : hiddenPanels) {
        box->removeWidget(h);
    }
    parent->insertWidget(nestSlot, sole, nestSize);
    int insertAt = nestSlot + 1;
    for (Widget* h : hiddenPanels) {
        parent->insertWidget(insertAt++, h, 0.0f);
    }
    parent->rebindSplitters();
    destroyWidgetTree(box);
    return true;
}

// Dissolve nest boxes with ZERO visible panels. unwrapSinglePanelBoxes
// requires exactly one visible panel and refuses when a hidden pinned
// Center remains — that left a visible empty nest reserving mid width
// (Gallery gap between Left and Right+Center after multi-step ops).
bool pruneVacantBoxes(BoxBase* box, BoxBase* parent, Widget* dockRoot) {
    if (box == nullptr) {
        return false;
    }
    bool changed = false;
    std::vector<Widget*> childBoxes;
    for (Widget* c : box->getChildren()) {
        if (dynamic_cast<BoxBase*>(c) != nullptr) {
            childBoxes.push_back(c);
        }
    }
    for (Widget* c : childBoxes) {
        changed |= pruneVacantBoxes(static_cast<BoxBase*>(c), box, dockRoot);
    }

    if (parent == nullptr || parent == dockRoot) {
        return changed;   // never dissolve dock root or template mid
    }

    int visiblePanels = 0;
    std::vector<Widget*> hiddenPanels;
    std::vector<Widget*> straySplitters;
    for (int i = 0; ; ++i) {
        Widget* c = box->slotAt(i);
        if (c == nullptr) {
            break;
        }
        if (c->isSplitterHandle()) {
            straySplitters.push_back(c);
            continue;
        }
        if (c->isVisible()) {
            ++visiblePanels;
        } else {
            hiddenPanels.push_back(c);
        }
    }
    if (visiblePanels != 0) {
        return changed;
    }

    dockTrace("[dock] pruneVacant nest='%s' hidden=%zu\n",
              box->getId().c_str(), hiddenPanels.size());

    for (Widget* h : hiddenPanels) {
        auto* lf = dynamic_cast<DockTabGroup*>(h);
        box->removeWidget(h);
        if (lf != nullptr && lf->isPinned()) {
            const std::string& id = lf->getLeafId();
            if (id == "Left" || id == "Right" || id == "Top" || id == "Bottom") {
                reinsertSidePinnedIntoTemplate(dockRoot, lf);
            } else if (id == "Center") {
                reinsertCenterIntoMid(dockRoot, lf);
            } else {
                destroyWidgetTree(h);
            }
        } else {
            destroyWidgetTree(h);
        }
        changed = true;
    }
    for (Widget* s : straySplitters) {
        box->removeWidget(s);
        destroyWidgetTree(s);
        changed = true;
    }
    // Nest shell is empty — drop it and neighbor splitters from parent.
    removeLeafAndNeighborSplitters(parent, box);
    return true;
}

// One bottom-up pass over the tree: drop empty non-pinned leaves (with
// their adjacent splitters). Recursion order is safe: a child that gets
// removed is gone from the next snapshot before the parent touches it.
// Pinned leaves are never removed. Nested single-panel boxes are
// unwrapped by unwrapSinglePanelBoxes after this pass.
bool prunePass(Widget* node) {
    auto* box = dynamic_cast<BoxBase*>(node);
    if (box == nullptr) {
        return false;
    }
    bool changed = false;
    for (Widget* c : box->getChildren()) {
        if (dynamic_cast<BoxBase*>(c) != nullptr) {
            changed |= prunePass(c);
        }
    }
    for (;;) {
        Widget* victim = nullptr;
        for (Widget* c : box->getChildren()) {
            auto* leaf = dynamic_cast<DockTabGroup*>(c);
            if (leaf != nullptr && !leaf->isPinned()
                && leaf->getTabCount() == 0) {
                victim = c;
                break;
            }
        }
        if (victim == nullptr) {
            break;
        }
        removeLeafAndNeighborSplitters(box, victim);
        changed = true;
    }
    return changed;
}

// The 25%-edge band of a leaf used by split previews.
math::FRectangle treeSplitBand(const math::FRectangle& r,
                               DockArea::TreeDropZone z) {
    const float w = r.maxX - r.minX;
    const float h = r.maxY - r.minY;
    switch (z) {
        case DockArea::TreeDropZone::West:
            return math::FRectangle(r.minX, r.minY, r.minX + w * 0.25f, r.maxY);
        case DockArea::TreeDropZone::East:
            return math::FRectangle(r.maxX - w * 0.25f, r.minY, r.maxX, r.maxY);
        case DockArea::TreeDropZone::North:
            return math::FRectangle(r.minX, r.minY, r.maxX, r.minY + h * 0.25f);
        case DockArea::TreeDropZone::South:
            return math::FRectangle(r.minX, r.maxY - h * 0.25f, r.maxX, r.maxY);
        case DockArea::TreeDropZone::Join:
        default:
            return r;
    }
}

const wchar_t* treeDropZoneLabel(DockArea::TreeDropZone z) {
    switch (z) {
        case DockArea::TreeDropZone::Join:  return L"Join Tabs";
        case DockArea::TreeDropZone::West:  return L"Split Left";
        case DockArea::TreeDropZone::East:  return L"Split Right";
        case DockArea::TreeDropZone::North: return L"Split Above";
        case DockArea::TreeDropZone::South: return L"Split Below";
        default:                            return L"?";
    }
}

// Phase-3 drop preview: join → translucent leaf fill + border; split →
// 25% edge band + border + three direction ticks (ImGui style).
// `zone` is precomputed by the caller (DockArea::resolveTreeDropZone).
void paintTreeDropZone(IRenderBackend& renderer, const DockTabGroup* leaf,
                       DockArea::TreeDropZone zone) {
    const math::FRectangle r = leaf->getWorldBounds();
    if (r.maxX - r.minX < 2.0f || r.maxY - r.minY < 2.0f) {
        return;
    }
    const math::FVector4 joinFill(0.20f, 0.60f, 0.95f, 0.20f);
    const math::FVector4 splitFill(0.95f, 0.80f, 0.25f, 0.30f);
    const math::FVector4 tickColor(1.0f, 1.0f, 1.0f, 0.90f);

    if (zone == DockArea::TreeDropZone::Join) {
        renderer.drawRect(r, joinFill);
        renderer.drawBorderRect(r, math::FVector4(0.20f, 0.60f, 0.95f, 0.75f),
                                2.0f, 0.0f);
        renderer.drawText(r, L"Join Tabs", 13,
                          math::FVector4(1.0f, 1.0f, 1.0f, 0.85f));
        return;
    }

    const math::FRectangle band = treeSplitBand(r, zone);
    renderer.drawRect(band, splitFill);
    renderer.drawBorderRect(
        band, math::FVector4(0.95f, 0.80f, 0.25f, 0.90f), 2.0f, 0.0f);

    // Three direction ticks along the band's center axis.
    const float cx = (band.minX + band.maxX) * 0.5f;
    const float cy = (band.minY + band.maxY) * 0.5f;
    constexpr float kTickLen = 14.0f;
    constexpr float kTickTh = 3.0f;
    constexpr float kTickGap = 9.0f;
    if (zone == DockArea::TreeDropZone::West
        || zone == DockArea::TreeDropZone::East) {
        // Horizontal ticks (split is vertical → ticks run left/right).
        for (int i = -1; i <= 1; ++i) {
            const float y = cy + kTickGap * static_cast<float>(i);
            renderer.drawRect(math::FRectangle(cx - kTickLen * 0.5f, y - kTickTh * 0.5f,
                                               cx + kTickLen * 0.5f, y + kTickTh * 0.5f),
                              tickColor);
        }
    } else {
        // Vertical ticks (split is horizontal → ticks run up/down).
        for (int i = -1; i <= 1; ++i) {
            const float x = cx + kTickGap * static_cast<float>(i);
            renderer.drawRect(math::FRectangle(x - kTickTh * 0.5f, cy - kTickLen * 0.5f,
                                               x + kTickTh * 0.5f, cy + kTickLen * 0.5f),
                              tickColor);
        }
    }
    renderer.drawText(band, treeDropZoneLabel(zone), 12,
                      math::FVector4(1.0f, 1.0f, 1.0f, 0.85f));
}

} // namespace

DockArea::DockArea() {
    // The overlay is always present and a child of DockArea. Build it
    // first. NOTE: the dock tree (root split node + pinned leaves) is
    // built LAZILY in ensureRootTree() — the ctor cannot build it
    // because the slot weights may not be configured yet (loader /
    // hosts set weights before adding cards) and the first real size
    // is only known at layout time. ensureRootTree() adds the root
    // node as a child and brings the overlay back to the front so
    // children order stays [tree, overlay] (floating cards paint on
    // top of the tree).
    _overlay = new DockOverlay();
    addChild(_overlay);

    setAcceptDrops(true);
    setOnDragLeave([this]() {
        _hoveredSlot = Slot::Count;
        _hoveredOverlay = false;
        markBoundsDirty();
    });
    setOnDrop([this](const DragPayload& payload) {
        if (payload.kind != "DockCard") {
            dockTrace("[dock] onDrop reject kind='%s' dock=%s\n",
                      payload.kind.c_str(), getId().c_str());
            return;
        }
        UIManager* ui = UIManager::tryGet();
        if (ui == nullptr) {
            dockTrace("[dock] onDrop no UIManager dock=%s\n", getId().c_str());
            return;
        }
        DockCard* card = static_cast<DockCard*>(payload.data);
        if (card == nullptr) {
            card = dynamic_cast<DockCard*>(ui->getDragSource());
        }
        if (card == nullptr) {
            dockTrace("[dock] onDrop null card dock=%s\n", getId().c_str());
            return;
        }
        const std::string cardId = card->getId();
        if (cardId.empty()) {
            dockTrace("[dock] onDrop empty cardId dock=%s\n", getId().c_str());
            return;
        }

        const math::FVector2 dropPos = ui->getDragLastMousePos();
        const bool isDocked = (_cardIndex.find(cardId) != _cardIndex.end());
        // dragBelongsToDock needs a live drag session (isDragging), but
        // by the time this callback runs the session is already tearing
        // down — classify by ownership instead.
        const bool isExternal = !isDocked
            && !dragBelongsToDock(*this, ui)
            && !cardBelongsToThisDock(cardId);

        // External card (promoted child-window redock / foreign source):
        // legacy hitTestSlot + adoptCard path (tryRedock contract).
        if (isExternal) {
            const Slot targetSlot = hitTestSlot(dropPos);
            dockTrace(
                "[dock] onDrop EXTERNAL dock=%s card=%s target=%s "
                "pos=(%.1f,%.1f)\n",
                getId().c_str(), cardId.c_str(),
                dockSlotName((int)targetSlot), dropPos.x, dropPos.y);
            if (targetSlot != Slot::Count) {
                adoptCard(targetSlot, card);
            }
            markBoundsDirty();
            return;
        }

        // Phase-3 tree dispatch: deepest leaf under the cursor.
        DockTabGroup* srcLeaf = isDocked ? findLeafOfCard(card) : nullptr;
        DockTabGroup* dstLeaf = hitTestTree(dropPos);

        dockTrace(
            "[dock] onDrop dock=%s card=%s src=%s dst=%s pos=(%.1f,%.1f) "
            "docked=%d float=%zu\n",
            getId().c_str(), cardId.c_str(),
            srcLeaf ? srcLeaf->getLeafId().c_str() : "-",
            dstLeaf ? dstLeaf->getLeafId().c_str() : "-",
            dropPos.x, dropPos.y, isDocked ? 1 : 0,
            _overlay ? _overlay->getFloatingCardCount() : 0u);

        if (dstLeaf != nullptr) {
            const TreeDropZone zone = resolveTreeDropZone(dstLeaf, dropPos);
            if (srcLeaf == dstLeaf) {
                // Join on own leaf → NO-OP. Edge with ≥2 tabs tears the
                // active tab into a new split (Gallery multi-tab Center).
                // Solo-card edge stays NO-OP (cannot split yourself away).
                if (zone == TreeDropZone::Join || dstLeaf->getTabCount() < 2) {
                    dockTrace("[dock] onDrop -> same-leaf NO-OP\n");
                } else {
                    dockTrace("[dock] onDrop -> same-leaf edge split %s\n",
                              dstLeaf->getLeafId().c_str());
                    splitLeaf(dstLeaf, zone, card);
                }
            } else if (zone == TreeDropZone::Join) {
                // Merge as a tab into the target leaf.
                // Capture src id BEFORE prune — prune deletes empty
                // non-pinned srcLeaf (g_N); tracing getLeafId() after
                // is a UAF (Gallery: Left→Center-South→Join crash).
                const std::string srcLeafId =
                    srcLeaf ? srcLeaf->getLeafId() : "float";
                if (srcLeaf != nullptr) {
                    removeTabFromLeaf(srcLeaf, card);
                } else if (_overlay) {
                    _overlay->removeFloatingCard(card);
                }
                addTabToLeaf(dstLeaf, card);
                pruneEmptySplitNodes();
                dockTrace("[dock] onDrop -> join %s <- %s tabs=%zu\n",
                          dstLeaf->getLeafId().c_str(),
                          srcLeafId.c_str(),
                          dstLeaf->getTabCount());
            } else {
                // Edge → nested split (new g_N leaf, 25% share).
                dockTrace("[dock] onDrop -> splitLeaf %s\n",
                          dstLeaf->getLeafId().c_str());
                splitLeaf(dstLeaf, zone, card);
            }
        } else if (isDocked && hitTestOverlay(dropPos)) {
            dockTrace("[dock] onDrop -> floatCard (overlay empty)\n");
            floatCard(cardId, dropPos);
        } else {
            dockTrace("[dock] onDrop -> no action\n");
        }

        dockTrace(
            "[dock] onDrop AFTER counts L=%zu C=%zu R=%zu float=%zu\n",
            getCardCount(Slot::Left), getCardCount(Slot::Center),
            getCardCount(Slot::Right),
            _overlay ? _overlay->getFloatingCardCount() : 0u);
        markBoundsDirty();
    });

    dockTrace("[dock] DockArea ctor cpp-body dock-tree lazy\n");
}

DockArea::~DockArea() {
    // Clear bookkeeping aliases first — do not delete through them.
    // Actual heap teardown walks getChildren() below.
    _onCardCloseRequested = {};
    _hiddenCardSlots.clear();
    _cardIndex.clear();
    _rootNode = nullptr;
    for (int i = 0; i < (int)Slot::Count; ++i) {
        _rootLeaves[i] = nullptr;
    }
    _overlay = nullptr;

    // Widget::~Widget does NOT free children (UI-OWN-1). DockArea owns its
    // overlay / slot containers / cards via addChild, so we must free them
    // here. Snapshot before destroy — each destroyWidgetTree detaches.
    const std::vector<Widget*> kids = getChildren();
    for (Widget* child : kids) {
        if (child == nullptr) {
            continue;
        }
        if (child->isExternallyOwned()) {
            child->detachFromParent();
            continue;
        }
        destroyWidgetTree(child);
    }
}

Widget* createDockAreaWidget() {
    return new DockArea();
}

bool DockArea::parseSlot(const std::string& name, Slot& outSlot) {
    if (name == "Left")   { outSlot = Slot::Left;   return true; }
    if (name == "Right")  { outSlot = Slot::Right;  return true; }
    if (name == "Top")    { outSlot = Slot::Top;    return true; }
    if (name == "Bottom") { outSlot = Slot::Bottom; return true; }
    if (name == "Center") { outSlot = Slot::Center; return true; }
    return false;
}

void DockArea::addCard(Slot slot, std::unique_ptr<DockCard> card) {
    if (!card) {
        return;
    }
    if ((int)slot < 0 || (int)slot >= (int)Slot::Count) {
        return;
    }

    // If a *different* card with the same id already exists, remove it
    // first so the "last write wins" rule is consistent with the Loader.
    // Same-pointer re-add (moveInSlot) must NOT call removeCard — that
    // destroyWidgetTree's the card we're about to reparent (UAF in
    // leaf->addTab).
    const std::string id = card->getId();
    if (!id.empty()) {
        auto it = _cardIndex.find(id);
        if (it != _cardIndex.end() && it->second != card.get()) {
            removeCard(id);
        } else if (it != _cardIndex.end() && it->second == card.get()) {
            // Same instance: scrub slot bookkeeping without freeing.
            _cardIndex.erase(it);
            if (DockTabGroup* leaf = findLeafOfCard(card.get())) {
                removeTabFromLeaf(leaf, card.get());
            }
        }
    }

    ensureRootTree();
    DockCard* raw = card.release();
    DockTabGroup* leaf = leafForSlot(slot);
    if (leaf != nullptr) {
        addTabToLeaf(leaf, raw);
    } else {
        // Disabled slot (weight ≤ 1e-4 → no leaf). Never happens in
        // the real flows (Loader / Gallery keep weights + cards in
        // sync); keep ownership safe by attaching to the DockArea.
        dockTrace("[dock] addCard WARN slot=%s has no leaf — "
                  "attaching directly\n", dockSlotName((int)slot));
        addChild(raw);
        if (!id.empty()) {
            _cardIndex[id] = raw;
        }
    }
}

bool DockArea::removeCard(const std::string& cardId) {
    auto it = _cardIndex.find(cardId);
    if (it == _cardIndex.end()) {
        return false;
    }
    DockCard* card = it->second;
    _cardIndex.erase(cardId);
    _hiddenCardSlots.erase(cardId);

    // Detach from its leaf (if docked), then free.
    if (DockTabGroup* leaf = findLeafOfCard(card)) {
        removeTabFromLeaf(leaf, card);
    }
    // D1 invariant: removeCard always frees the card. destroyWidgetTree
    // detaches from whatever parent (leaf / DockArea) then deletes.
    destroyWidgetTree(card);
    // A split-created leaf may now be empty — prune it (pinned leaves
    // stay forever).
    pruneEmptySplitNodes();
    return true;
}

bool DockArea::closeCard(const std::string& cardId) {
    if (cardId.empty()) {
        return false;
    }
    if (_cardIndex.find(cardId) != _cardIndex.end()) {
        return removeCard(cardId);
    }
    if (_overlay == nullptr) {
        return false;
    }
    const size_t n = _overlay->getFloatingCardCount();
    for (size_t i = 0; i < n; ++i) {
        DockCard* c = _overlay->getFloatingCard(i);
        if (c == nullptr || c->getId() != cardId) {
            continue;
        }
        _overlay->removeFloatingCard(c);
        destroyWidgetTree(c);
        markBoundsDirty();
        requestRelayout();
        return true;
    }
    return false;
}

bool DockArea::requestCloseCard(DockCard* card) {
    if (card == nullptr) {
        return false;
    }
    if (_onCardCloseRequested && _onCardCloseRequested(card)) {
        return true;
    }
    return closeCard(card->getId());
}

bool DockArea::setCardVisible(const std::string& cardId,
                              bool visible,
                              Slot restoreSlot) {
    if (cardId.empty()
        || (int)restoreSlot < 0
        || (int)restoreSlot >= (int)Slot::Count) {
        return false;
    }

    auto hidden = _hiddenCardSlots.find(cardId);
    if (visible && hidden != _hiddenCardSlots.end()) {
        auto indexed = _cardIndex.find(cardId);
        if (indexed == _cardIndex.end() || indexed->second == nullptr) {
            _hiddenCardSlots.erase(hidden);
            return false;
        }
        ensureRootTree();
        DockTabGroup* leaf = leafForSlot(hidden->second);
        if (leaf == nullptr) {
            leaf = leafForSlot(restoreSlot);
        }
        if (leaf == nullptr) {
            return false;
        }
        DockCard* card = indexed->second;
        _hiddenCardSlots.erase(hidden);
        addTabToLeaf(leaf, card);
        card->setVisible(true);
        markBoundsDirty();
        requestRelayout();
        return true;
    }
    if (!visible && hidden != _hiddenCardSlots.end()) {
        return true;
    }

    DockCard* card = findCard(cardId);
    if (card == nullptr) {
        return false;
    }
    if (visible) {
        if (DockTabGroup* leaf = findLeafOfCard(card)) {
            leaf->activateTabById(cardId);
        }
        card->setVisible(true);
        markBoundsDirty();
        requestRelayout();
        return true;
    }

    if (DockTabGroup* leaf = findLeafOfCard(card)) {
        removeTabFromLeaf(leaf, card);
        pruneEmptySplitNodes();
    } else if (_overlay != nullptr) {
        _overlay->removeFloatingCard(card);
    }

    // Keep the widget tree and UIManager id registry alive. addChild also
    // takes ownership after the detach-only tab/overlay removal above.
    addChild(card);
    card->setVisible(false);
    _cardIndex[cardId] = card;
    _hiddenCardSlots[cardId] = restoreSlot;
    if (_overlay != nullptr) {
        _overlay->bringToFront();
    }
    markBoundsDirty();
    requestRelayout();
    return true;
}

DockCard* DockArea::findCard(const std::string& cardId) const {
    auto it = _cardIndex.find(cardId);
    if (it != _cardIndex.end()) {
        return it->second;
    }
    // Fallback: overlay cards (not indexed because the id-keyed map
    // already includes them via addCard path - we add overlay cards
    // through getOverlay()->addFloatingCard, which doesn't push into
    // _cardIndex today).
    if (_overlay) {
        const size_t n = _overlay->getFloatingCardCount();
        for (size_t i = 0; i < n; ++i) {
            DockCard* c = _overlay->getFloatingCard(i);
            if (c && c->getId() == cardId) {
                return c;
            }
        }
    }
    return nullptr;
}

DockCard* DockArea::getCard(Slot slot, size_t index) const {
    if ((int)slot < 0 || (int)slot >= (int)Slot::Count) {
        return nullptr;
    }
    const DockTabGroup* leaf = _rootLeaves[(int)slot];
    return leaf != nullptr ? leaf->getTab(index) : nullptr;
}

void DockArea::onRender(IRenderBackend& renderer) {
    // Slot/drop highlight is painted in render() AFTER children so
    // opaque cards cannot cover it. onRender stays a no-op for chrome.
    AYUNREFERENCED_PARAM(renderer);
}

void DockArea::paintDropGuide(IRenderBackend& renderer) {
    // Only while a DockCard is being dragged — idle / foreign payloads
    // paint nothing. Called from DockArea::render AFTER children so the
    // guide sits above opaque cards (hosts must NOT call this again).
    //
    // D5-redock: a drag that started in a promoted child window has its
    // G12 session in the CHILD UIManager, so isDockCardDrag(tryGet())
    // is false here. The host bridge feeds the external cursor position
    // (primary-client space) via setExternalDropPos; preview MUST use
    // the same resolveDropTarget as redockAt (commit).
    math::FVector2 cursor(0.0f, 0.0f);
    const bool external = _externalDropActive;
    UIManager* ui = nullptr;
    if (external) {
        cursor = _externalDropPos;
    } else {
        ui = UIManager::tryGet();
        if (!isDockCardDrag(ui)) {
            return;
        }
        cursor = ui->getDragLastMousePos();
    }

    // The guide is immediate-mode: its draw commands must be emitted on
    // every rendered frame. Skipping the whole paint when the cursor is
    // stationary produces an alternating present/absent overlay as the
    // backend clears its command list each frame. Keep the last cursor as
    // ABI-stable bookkeeping only; a future optimization may cache the
    // resolved DropTarget, but must still replay the guide draw commands.
    _lastDropGuideCursor = cursor;
    _lastDropGuidePainted = true;

    // Owned in-dock drag OR external child redock: one resolver.
    if (_rootNode != nullptr
        && (external || (ui != nullptr && dragBelongsToDock(*this, ui)))) {
        const DropTarget t = resolveDropTarget(cursor);
        if (t.kind == DropKind::Tree && t.leaf != nullptr) {
            paintTreeDropZone(renderer, t.leaf, t.zone);
            return;
        }
        if (t.kind == DropKind::Slot && t.slot != Slot::Count) {
            const math::FRectangle r = getSlotRect(t.slot);
            if (r.maxX - r.minX < 2.0f || r.maxY - r.minY < 2.0f) {
                return;
            }
            math::FVector4 fill(0.25f, 0.55f, 0.95f, 0.18f);
            switch (t.slot) {
                case Slot::Left:   fill = math::FVector4(0.90f, 0.25f, 0.20f, 0.18f); break;
                case Slot::Right:  fill = math::FVector4(0.20f, 0.40f, 0.95f, 0.18f); break;
                case Slot::Center: fill = math::FVector4(0.20f, 0.75f, 0.35f, 0.18f); break;
                case Slot::Top:    fill = math::FVector4(0.95f, 0.75f, 0.15f, 0.18f); break;
                case Slot::Bottom: fill = math::FVector4(0.75f, 0.30f, 0.85f, 0.18f); break;
                default: break;
            }
            renderer.drawRect(r, fill);
            renderer.drawBorderRect(r, math::FVector4(fill.x, fill.y, fill.z, 0.70f),
                                    2.0f, 0.0f);
            renderer.drawText(r, slotLabel(t.slot), 14,
                              math::FVector4(1.0f, 1.0f, 1.0f, 0.75f));
        }
        return;
    }

    // Tree not built yet: translucent slot fill via hitTestSlot.
    const Slot hover = hitTestSlot(cursor);
    if (hover == Slot::Count) {
        return;
    }
    const math::FRectangle r = getSlotRect(hover);
    if (r.maxX - r.minX < 2.0f || r.maxY - r.minY < 2.0f) {
        return;
    }
    math::FVector4 fill(0.25f, 0.55f, 0.95f, 0.18f);
    switch (hover) {
        case Slot::Left:   fill = math::FVector4(0.90f, 0.25f, 0.20f, 0.18f); break;
        case Slot::Right:  fill = math::FVector4(0.20f, 0.40f, 0.95f, 0.18f); break;
        case Slot::Center: fill = math::FVector4(0.20f, 0.75f, 0.35f, 0.18f); break;
        case Slot::Top:    fill = math::FVector4(0.95f, 0.75f, 0.15f, 0.18f); break;
        case Slot::Bottom: fill = math::FVector4(0.75f, 0.30f, 0.85f, 0.18f); break;
        default: break;
    }
    renderer.drawRect(r, fill);
    renderer.drawBorderRect(r, math::FVector4(fill.x, fill.y, fill.z, 0.70f),
                            2.0f, 0.0f);
    renderer.drawText(r, slotLabel(hover), 14,
                      math::FVector4(1.0f, 1.0f, 1.0f, 0.75f));
}

void DockArea::render(IRenderBackend& renderer) {
    if (recordNestedRenderIfNeeded(renderer)) {
        return;
    }
    if (!isVisible()) {
        return;
    }
    onRender(renderer);
    renderChildren(renderer);
    paintDropGuide(renderer);
}

void DockArea::performLayout() {
    // First descend into children so the overlay + root node get a
    // layout pass (with their previous sizes). The authoritative pass
    // happens below after the root is sized to the DockArea.
    compoundDescendLayout(this);

    // Build the tree lazily, then (re)apply the weight-derived template
    // geometry — only while the template is still pristine (no splitter
    // drags). The first real-size pass always pushes geometry.
    ensureRootTree();
    const math::FVector2 sz = getSize();
    syncTemplateIfPristine(sz);

    // Self-heal: every dock box must have a visible fill panel so
    // fixed-only leftover can never paint as a black hole. Cheap walk;
    // only mutates when a box is actually fill-less.
    if (_rootNode != nullptr) {
        ensureAllBoxesHaveVisibleFill(_rootNode);
    }

    if (_rootNode) {
        _rootNode->setPosition(math::FVector2(0.0f, 0.0f));
        _rootNode->setSize(sz);
        // VBox::performLayout → layoutChildren → recursive leaf layout.
        // Leaves (DockTabGroups) full-bleed their single card / place
        // the tab strip + active card body themselves.
        _rootNode->performLayout();
    }

    // Overlay covers everything so floating cards can be positioned
    // anywhere within the DockArea bounds.
    if (_overlay) {
        _overlay->setPosition(math::FVector2(0.0f, 0.0f));
        _overlay->setSize(sz);
    }

    // Occasional layout dump for Gallery mini_dock (every ~60 layouts).
    if (getId() == "mini_dock") {
        static int s_layoutLog = 0;
        if ((s_layoutLog++ % 60) == 0) {
            const math::FRectangle wb = getWorldBounds();
            const char* rootTy = "null";
            if (_rootNode) {
                rootTy = dynamic_cast<VBox*>(_rootNode) ? "VBox" : "HBox";
            }
            dockTrace(
                "[dock] layout mini_dock world=(%.0f,%.0f)-(%.0f,%.0f) "
                "root=%s cards L=%zu C=%zu R=%zu float=%zu\n",
                wb.minX, wb.minY, wb.maxX, wb.maxY, rootTy,
                getCardCount(Slot::Left), getCardCount(Slot::Center),
                getCardCount(Slot::Right),
                _overlay ? _overlay->getFloatingCardCount() : 0u);
            for (int si = 0; si < (int)Slot::Count; ++si) {
                DockTabGroup* leaf = _rootLeaves[si];
                if (leaf == nullptr) {
                    continue;
                }
                const size_t n = leaf->getTabCount();
                for (size_t ci = 0; ci < n; ++ci) {
                    DockCard* c = leaf->getTab(ci);
                    if (!c) continue;
                    const math::FRectangle cb = c->getWorldBounds();
                    dockTrace(
                        "[dock]   card '%s' in %s bounds=(%.0f,%.0f)-(%.0f,%.0f) "
                        "size=%.0fx%.0f floatable=%d leafActive=%d\n",
                        c->getId().c_str(), dockSlotName(si),
                        cb.minX, cb.minY, cb.maxX, cb.maxY,
                        c->getSize().x, c->getSize().y,
                        c->isFloatable() ? 1 : 0,
                        (leaf->getActiveTab() == c) ? 1 : 0);
                }
            }
        }
    }
}

void DockArea::onChildAdded(Widget* child) {
    AYUNREFERENCED_PARAM(child);
}

void DockArea::onChildRemoved(Widget* child) {
    AYUNREFERENCED_PARAM(child);
}

// =============================================================================
// D3 — Tear-off / re-dock helpers.
// =============================================================================

bool DockArea::floatCard(const std::string& cardId, const math::FVector2& pos) {
    dockTrace("[dock] floatCard ENTER dock=%s card=%s pos=(%.1f,%.1f)\n",
              getId().c_str(), cardId.c_str(), pos.x, pos.y);
    if (!_overlay) return false;
    auto it = _cardIndex.find(cardId);
    if (it == _cardIndex.end()) {
        dockTrace("[dock] floatCard FAIL not-in-index card=%s\n", cardId.c_str());
        return false;       // not in any slot — nothing to float
    }
    DockCard* card = it->second;
    if (card == nullptr) return false;

    // Locate the leaf the card lives in (pinned leaves today; the
    // Phase-3 tree walk extends this to split-created leaves).
    DockTabGroup* leaf = findLeafOfCard(card);
    if (leaf == nullptr) {
        // _cardIndex says it's docked, but no leaf contains it —
        // inconsistent state. Refuse to move rather than corrupt.
        dockTrace("[dock] floatCard FAIL no-leaf card=%s\n", cardId.c_str());
        return false;
    }

    // We CANNOT use DockArea::removeCard() because it calls
    // destroyWidgetTree() — that frees the card. We need the card alive
    // to hand to the overlay (K-INV-D3-7: index erased here).
    _cardIndex.erase(it);
    removeTabFromLeaf(leaf, card);

    // Set floating position + size and hand to overlay. addFloatingCard
    // calls addChild which re-parents and assumes ownership.
    //
    // pos is a ROOT-space (world) point — hitTestSlot / the drag session
    // both work in world coords. The card's local position is relative
    // to its parent, and the floating card's parent chain goes back
    // through THIS DockArea (world origin = getWorldPosition()). Store
    // pos minus the dock origin so the card renders exactly under the
    // cursor when the dock is nested (editor shell header / gallery
    // padding offset the dock's world origin; without the subtraction
    // the floating card drifts by that offset). dockCard's displaced-
    // occupant floatPos (from getSlotRect, also world coords) hits the
    // same correction here.
    card->setPosition(pos - getWorldPosition());
    card->setVisible(true);
    _overlay->addFloatingCard(card);

    // Reset hover state — the card just moved; the highlight from
    // before the drop is stale.
    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    dockTrace("[dock] floatCard OK card=%s fromLeaf=%s floatCount=%zu\n",
              cardId.c_str(), leaf->getLeafId().c_str(),
              _overlay->getFloatingCardCount());
    // The vacated leaf may be a split-created g_N — prune it (pinned
    // leaves stay forever). Runs after the trace: prune frees `leaf`.
    pruneEmptySplitNodes();
    return true;
}

bool DockArea::adoptCard(Slot target, DockCard* card) {
    if (card == nullptr || !_overlay) {
        return false;
    }
    if ((int)target < 0 || (int)target >= (int)Slot::Count) {
        return false;
    }
    ensureRootTree();
    DockTabGroup* leaf = leafForSlot(target);
    if (leaf == nullptr) {
        return false;
    }

    // Clear overlay bookkeeping if the card was floating (addChild alone
    // reparents but left a stale _floatingCards entry).
    _overlay->removeFloatingCard(card);

    // Phase-3: join as a tab. Displacing occupants on redock kicked the
    // Center card out whenever Gallery tryRedock hit slot=Center
    // (log: adoptCard displace 'card_center' → float).
    addTabToLeaf(leaf, card);
    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    pruneEmptySplitNodes();
    dockTrace("[dock] adoptCard OK card=%s -> %s tabs=%zu\n",
              card->getId().c_str(), dockSlotName((int)target),
              leaf->getTabCount());
    return true;
}

bool DockArea::redockAt(DockCard* card, const math::FVector2& worldPos) {
    if (card == nullptr) {
        return false;
    }
    ensureRootTree();

    // Resolve BEFORE detaching so a miss does not orphan the card, and
    // same-leaf Join can NO-OP without emptying the source mid-flight.
    const DropTarget t = resolveDropTarget(worldPos);
    if (t.kind == DropKind::None) {
        return false;
    }

    DockTabGroup* src = findLeafOfCard(card);

    if (t.kind == DropKind::Tree && t.leaf != nullptr) {
        if (src == t.leaf) {
            if (t.zone == TreeDropZone::Join || t.leaf->getTabCount() < 2) {
                dockTrace("[dock] redockAt same-leaf NO-OP leaf=%s\n",
                          t.leaf->getLeafId().c_str());
                return true;
            }
            splitLeaf(t.leaf, t.zone, card);
            pruneEmptySplitNodes();
            return true;
        }
        if (src != nullptr) {
            removeTabFromLeaf(src, card);
        } else if (_overlay != nullptr) {
            _overlay->removeFloatingCard(card);
        }
        if (t.zone == TreeDropZone::Join) {
            addTabToLeaf(t.leaf, card);
            dockTrace("[dock] redockAt join leaf=%s card=%s tabs=%zu\n",
                      t.leaf->getLeafId().c_str(), card->getId().c_str(),
                      t.leaf->getTabCount());
        } else {
            splitLeaf(t.leaf, t.zone, card);
            dockTrace("[dock] redockAt split leaf=%s zone=%d card=%s\n",
                      t.leaf->getLeafId().c_str(), static_cast<int>(t.zone),
                      card->getId().c_str());
        }
        pruneEmptySplitNodes();
        markBoundsDirty();
        requestRelayout();
        return true;
    }

    if (t.kind == DropKind::Slot && t.slot != Slot::Count) {
        if (src != nullptr) {
            // Already in that pinned leaf → NO-OP.
            if (src == leafForSlot(t.slot)) {
                return true;
            }
            removeTabFromLeaf(src, card);
        }
        return adoptCard(t.slot, card);
    }
    return false;
}

bool DockArea::dockCard(const std::string& cardId, Slot target) {
    if (!_overlay) return false;
    if ((int)target < 0 || (int)target >= (int)Slot::Count) {
        return false;
    }

    // Locate the floating card on the overlay. Linear scan; the
    // overlay's _floatingCards vector is non-indexed.
    DockCard* card = nullptr;
    const size_t n = _overlay->getFloatingCardCount();
    for (size_t i = 0; i < n; ++i) {
        DockCard* c = _overlay->getFloatingCard(i);
        if (c && c->getId() == cardId) {
            card = c;
            break;
        }
    }
    if (card == nullptr) {
        return false;       // not on the overlay — nothing to dock
    }

    // Detach from overlay (does NOT delete — see
    // DockOverlay::removeFloatingCard).
    _overlay->removeFloatingCard(card);

    // K-INV-D3-7: overlay → slot via adoptCard (joins as a tab).
    return adoptCard(target, card);
}

bool DockArea::moveInSlot(const std::string& cardId, Slot target) {
    // Code-review 2026-08-02 #8: slot->slot move without routing through
    // the overlay. Caller is responsible for K-INV-D3-1 (same-slot no-op)
    // — the onDrop callback already pre-checks `isCardInSlot(card, target)`
    // so this function unconditionally reparents the card to `target`.
    dockTrace("[dock] moveInSlot ENTER card=%s target=%s\n",
              cardId.c_str(), dockSlotName((int)target));
    auto it = _cardIndex.find(cardId);
    if (it == _cardIndex.end()) {
        dockTrace("[dock] moveInSlot FAIL not-docked card=%s\n", cardId.c_str());
        return false;   // not docked anywhere — nothing to move
    }
    if ((int)target < 0 || (int)target >= (int)Slot::Count) {
        return false;
    }
    DockCard* card = it->second;
    if (card == nullptr) return false;

    ensureRootTree();
    DockTabGroup* srcLeaf = findLeafOfCard(card);
    DockTabGroup* dstLeaf = leafForSlot(target);
    if (srcLeaf == nullptr || dstLeaf == nullptr) {
        return false;
    }
    if (srcLeaf == dstLeaf) {
        // Same-slot no-op (K-INV-D3-1 safety net — the onDrop
        // pre-check should have caught this already).
        dockTrace("[dock] moveInSlot same-slot no-op card=%s\n", cardId.c_str());
        return false;
    }

    dockTrace("[dock] moveInSlot %s -> %s; targetOccupied=%zu\n",
              srcLeaf->getLeafId().c_str(), dstLeaf->getLeafId().c_str(),
              dstLeaf->getTabCount());

    // Occupied target → swap: move prior occupants into the vacated
    // slot (test-pinned swap semantics, Test_DockFloat.cpp). Keep one
    // card band per side and preserve draggable title bars.
    if (dstLeaf->getTabCount() > 0) {
        std::vector<DockCard*> displaced;
        const size_t n = dstLeaf->getTabCount();
        for (size_t i = 0; i < n; ++i) {
            DockCard* d = dstLeaf->getTab(i);
            if (d != nullptr && d != card) {
                displaced.push_back(d);
            }
        }
        for (DockCard* d : displaced) {
            dockTrace("[dock] moveInSlot SWAP displace '%s' -> %s\n",
                      d->getId().c_str(), srcLeaf->getLeafId().c_str());
            removeTabFromLeaf(dstLeaf, d);
            addTabToLeaf(srcLeaf, d);
        }
    }

    // Move the card itself. _cardIndex stays valid (the card pointer
    // didn't change); addTabToLeaf refreshes the entry.
    removeTabFromLeaf(srcLeaf, card);
    addTabToLeaf(dstLeaf, card);

    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    dockTrace(
        "[dock] moveInSlot DONE counts L=%zu C=%zu R=%zu\n",
        getCardCount(Slot::Left), getCardCount(Slot::Center),
        getCardCount(Slot::Right));
    // srcLeaf may be a split-created leaf left empty by the swap.
    pruneEmptySplitNodes();
    return true;
}

DockArea::Slot DockArea::hitTestSlot(const math::FVector2& worldPos) const {
    // Prefer live leaf geometry once the dock tree is laid out so
    // splitter-resized panels stay the adaptive drop targets. Fall
    // back to weight-derived regions when the tree isn't built yet
    // (unit tests hit-test before addCard / layout).
    const math::FRectangle wb = getWorldBounds();
    if (wb.maxX - wb.minX <= 0.0f || wb.maxY - wb.minY <= 0.0f) {
        return Slot::Count;
    }
    // Outside the dock → Count. Without this, local.y < midY maps ANY
    // point above the dock to Top (including y<0), so child-window
    // redock snapped torn-off cards into the disabled Top band.
    if (!wb.contains(worldPos)) {
        return Slot::Count;
    }

    const math::FVector2 local(worldPos.x - wb.minX, worldPos.y - wb.minY);
    const SlotRegions r = hitTestRegions(*this);
    const float midY    = r.midY;
    const float midH    = r.midH;
    const float leftW   = r.leftW;
    const float centerW = r.centerW;

    Slot weightSlot = Slot::Center;
    if (local.y < midY) {
        weightSlot = Slot::Top;
    } else if (local.y >= midY + midH) {
        weightSlot = Slot::Bottom;
    } else if (local.x < leftW) {
        weightSlot = Slot::Left;
    } else if (local.x >= leftW + centerW) {
        weightSlot = Slot::Right;
    }

    // Collapsed empty side slots are invisible and their band is eaten
    // by the fill column. Prefer the weight-region slot so external
    // redock / adoptCard can revive Left/Right/Top/Bottom.
    if (weightSlot != Slot::Center && weightSlot != Slot::Count) {
        const DockTabGroup* side = _rootLeaves[(int)weightSlot];
        if (side != nullptr && !side->isVisible()) {
            return weightSlot;
        }
    }

    if (_rootNode != nullptr) {
        Slot best = Slot::Count;
        float bestArea = std::numeric_limits<float>::max();
        for (int i = 0; i < (int)Slot::Count; ++i) {
            const DockTabGroup* leaf = _rootLeaves[i];
            if (leaf == nullptr || !leaf->isVisible()) {
                continue;
            }
            const math::FRectangle lb = leaf->getWorldBounds();
            const float lw = lb.maxX - lb.minX;
            const float lh = lb.maxY - lb.minY;
            if (lw < 2.0f || lh < 2.0f) {
                continue;
            }
            if (!lb.contains(worldPos)) {
                continue;
            }
            const float area = lw * lh;
            if (area < bestArea) {
                bestArea = area;
                best = static_cast<Slot>(i);
            }
        }
        if (best != Slot::Count) {
            return best;
        }
    }

    return weightSlot;
}

math::FRectangle DockArea::getSlotRect(Slot slot) const {
    if ((int)slot < 0 || (int)slot >= (int)Slot::Count) {
        return math::FRectangle();
    }
    // Live leaf bounds track sticky splitter geometry; weight regions
    // remain the fallback for pre-tree / disabled-slot / collapsed probes.
    // Invisible (collapsed) leaves keep stale world rects — never use them.
    if (const DockTabGroup* leaf = _rootLeaves[(int)slot]) {
        if (leaf->isVisible()) {
            const math::FRectangle lb = leaf->getWorldBounds();
            if (lb.maxX - lb.minX >= 2.0f && lb.maxY - lb.minY >= 2.0f) {
                return lb;
            }
        }
    }
    const math::FRectangle wb = getWorldBounds();
    const float szX = std::max(0.0f, wb.maxX - wb.minX);
    const float szY = std::max(0.0f, wb.maxY - wb.minY);
    const SlotRegions r = hitTestRegions(*this);
    math::FRectangle local;
    switch (slot) {
        case Slot::Top:    local = math::FRectangle(0.0f, 0.0f, szX, r.topH); break;
        case Slot::Bottom: local = math::FRectangle(0.0f, r.midY + r.midH, szX, szY); break;
        case Slot::Left:   local = math::FRectangle(0.0f, r.midY, r.leftW, r.midY + r.midH); break;
        case Slot::Center: local = math::FRectangle(r.leftW, r.midY, r.leftW + r.centerW, r.midY + r.midH); break;
        case Slot::Right:  local = math::FRectangle(r.leftW + r.centerW, r.midY, szX, r.midY + r.midH); break;
        default:           return math::FRectangle();
    }
    return math::FRectangle(wb.minX + local.minX, wb.minY + local.minY,
                            wb.minX + local.maxX, wb.minY + local.maxY);
}

bool DockArea::hitTestOverlay(const math::FVector2& worldPos) const {
    if (_overlay == nullptr) return false;
    const math::FRectangle overlayBounds = _overlay->getWorldBounds();
    if (!overlayBounds.contains(worldPos)) {
        return false;
    }
    // Inside overlay bounds — but not on any floating card. Walk the
    // overlay's children in reverse insertion order (top-most first),
    // matching the Widget hit-test descent order.
    const std::vector<Widget*>& kids = _overlay->getChildren();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        Widget* w = *it;
        if (w && w->isVisible() && w->hitTest(worldPos)) {
            return false;       // hit a floating card — overlay
                                // region not "empty"
        }
    }
    return true;
}

// =============================================================================
// Dock tree — structure + template geometry
// =============================================================================

namespace {
constexpr float kTemplateEpsilon = 0.01f;
} // namespace

void DockArea::ensureRootTree() {
    if (_rootNode != nullptr) {
        return;
    }

    // Template tree (with splitters between panels):
    //   root VBox { [Top leaf, V-split]*, mid HBox, [V-split, Bottom leaf]* }
    //   mid HBox  { [Left leaf, H-split]*, Center leaf, [H-split, Right leaf]* }
    // Slot sizes start at 0 (= fill) and are pushed by the first
    // applyTemplateGeometry pass — panel sizes match computeSlotRegions;
    // Center / mid stay fill and absorb the splitter thickness.
    //
    // Disabled slots (weight ≤ 1e-4 → effectiveWeight 0) get NO leaf —
    // Gallery disables Top/Bottom that way; a disabled slot with a card
    // is a host bug (addCard falls back to a direct DockArea child).
    VBox* root = new VBox();
    root->setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    root->setSpacing(0.0f);
    root->setId(getId() + "::root");

    HBox* mid = new HBox();
    mid->setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    mid->setSpacing(0.0f);
    mid->setId(getId() + "::mid");

    const float wts[5] = {
        effectiveWeight(*this, Slot::Top),
        effectiveWeight(*this, Slot::Bottom),
        effectiveWeight(*this, Slot::Left),
        effectiveWeight(*this, Slot::Right),
        effectiveWeight(*this, Slot::Center),
    };
    const std::string idPrefix = getId().empty() ? "dock" : getId();

    if (wts[0] > 0.0f) {
        root->addWidget(makePinnedLeaf(Slot::Top), 0.0f);
        root->addWidget(
            makeTreeSplitter(SplitterHandle::Orientation::Vertical,
                             idPrefix + "::split_top"),
            SplitterHandle::kDefaultWidth);
    }
    root->addWidget(mid, 0.0f);
    if (wts[1] > 0.0f) {
        root->addWidget(
            makeTreeSplitter(SplitterHandle::Orientation::Vertical,
                             idPrefix + "::split_bot"),
            SplitterHandle::kDefaultWidth);
        root->addWidget(makePinnedLeaf(Slot::Bottom), 0.0f);
    }

    if (wts[2] > 0.0f) {
        mid->addWidget(makePinnedLeaf(Slot::Left), 0.0f);
        mid->addWidget(
            makeTreeSplitter(SplitterHandle::Orientation::Horizontal,
                             idPrefix + "::split_left"),
            SplitterHandle::kDefaultWidth);
    }
    // Center is the fill column — always present (it absorbs the
    // remainder of the middle row even when its weight is tiny).
    mid->addWidget(makePinnedLeaf(Slot::Center), 0.0f);
    if (wts[3] > 0.0f) {
        mid->addWidget(
            makeTreeSplitter(SplitterHandle::Orientation::Horizontal,
                             idPrefix + "::split_right"),
            SplitterHandle::kDefaultWidth);
        mid->addWidget(makePinnedLeaf(Slot::Right), 0.0f);
    }

    root->rebindSplitters();
    mid->rebindSplitters();
    applyPinnedSlotMinLimits();

    addChild(root);
    _rootNode = root;
    _templateBuilt = true;
    _templateSynced = false;
    // Floating cards paint on top of the tree — overlay must be the
    // LAST child.
    _overlay->bringToFront();

    // Side slots with no cards yet should not steal fill width (Gallery
    // / tests that only populate Center).
    collapseEmptySidePinnedLeaves();

    dockTrace("[dock] ensureRootTree dock=%s wts=[%.3f,%.3f,%.3f,%.3f,%.3f] "
              "splitters=on\n",
              getId().c_str(), wts[0], wts[1], wts[2], wts[3], wts[4]);
}

DockTabGroup* DockArea::makePinnedLeaf(Slot slot) {
    auto* leaf = new DockTabGroup();
    leaf->setLeafId(dockSlotName((int)slot));
    leaf->setPinned(true);
    leaf->setOnCloseTab([this](DockCard* card) {
        if (card != nullptr) {
            requestCloseCard(card);
        }
    });
    _rootLeaves[(int)slot] = leaf;
    return leaf;
}

DockTabGroup* DockArea::leafForSlot(Slot slot) {
    if ((int)slot < 0 || (int)slot >= (int)Slot::Count) {
        return nullptr;
    }
    return _rootLeaves[(int)slot];
}

DockTabGroup* DockArea::findLeafOfCard(DockCard* card) {
    if (card == nullptr) {
        return nullptr;
    }
    return findLeafInNode(_rootNode, card);
}

bool DockArea::cardBelongsToThisDock(const std::string& cardId) const {
    if (cardId.empty()) {
        return false;
    }
    if (_cardIndex.find(cardId) != _cardIndex.end()) {
        return true;
    }
    const DockOverlay* ov = _overlay;
    if (ov != nullptr) {
        for (size_t i = 0; i < ov->getFloatingCardCount(); ++i) {
            const DockCard* c = ov->getFloatingCard(i);
            if (c != nullptr && c->getId() == cardId) {
                return true;
            }
        }
    }
    return false;
}

// ---- Phase 3: tree drop placement -----------------------------------------

DockTabGroup* DockArea::hitTestTree(const math::FVector2& worldPos) const {
    if (_rootNode == nullptr) {
        return nullptr;
    }
    return hitTestTreeRec(_rootNode, worldPos);
}

DockTabGroup* DockArea::hitTestTreeRec(Widget* node,
                                       const math::FVector2& p) const {
    if (node == nullptr || !node->isVisible()) {
        return nullptr;
    }
    if (auto* leaf = dynamic_cast<DockTabGroup*>(node)) {
        return leaf->getWorldBounds().contains(p) ? leaf : nullptr;
    }
    auto* box = dynamic_cast<BoxBase*>(node);
    if (box == nullptr) {
        return nullptr;
    }
    // A point outside the node's own bounds can never resolve into it —
    // the snap fallback below must not drag a cursor that is outside the
    // tree onto the nearest leaf.
    if (!box->getWorldBounds().contains(p)) {
        return nullptr;
    }
    // Containing child wins; otherwise snap to the nearest leaf (splitter
    // handles are never containers and never targets — a 4px band cannot
    // swallow a drop).
    DockTabGroup* nearest = nullptr;
    float nearestD = std::numeric_limits<float>::max();
    const std::vector<Widget*>& kids = box->getChildren();
    for (Widget* c : kids) {
        if (c == nullptr || !c->isVisible()) {
            continue;
        }
        const math::FRectangle cb = c->getWorldBounds();
        if (cb.maxX - cb.minX < 2.0f || cb.maxY - cb.minY < 2.0f) {
            continue;
        }
        if (cb.contains(p)) {
            if (DockTabGroup* f = hitTestTreeRec(c, p)) {
                return f;
            }
            continue;
        }
        if (auto* lf = dynamic_cast<DockTabGroup*>(c)) {
            const math::FVector2 ctr((cb.minX + cb.maxX) * 0.5f,
                                     (cb.minY + cb.maxY) * 0.5f);
            const float dx = ctr.x - p.x;
            const float dy = ctr.y - p.y;
            const float d = dx * dx + dy * dy;
            if (d < nearestD) {
                nearestD = d;
                nearest = lf;
            }
        }
    }
    return nearest;
}

DockArea::TreeDropZone DockArea::resolveTreeDropZone(
    const DockTabGroup* leaf, const math::FVector2& worldPos) const {
    if (leaf == nullptr) {
        return TreeDropZone::Join;
    }
    const math::FRectangle r = leaf->getWorldBounds();
    const float w = r.maxX - r.minX;
    const float h = r.maxY - r.minY;
    if (w < 2.0f || h < 2.0f) {
        return TreeDropZone::Join;
    }
    // Empty leaves are a pure join zone — their entire rect merges.
    if (leaf->getTabCount() == 0) {
        return TreeDropZone::Join;
    }
    // Normalized edge distance, argmin over the four sides with a 25%
    // threshold. Array order West/East/North/South + strict `<` makes
    // ties resolve West > East > North > South.
    const float cand[4][2] = {
        {(worldPos.x - r.minX) / w, (float)TreeDropZone::West},
        {(r.maxX - worldPos.x) / w, (float)TreeDropZone::East},
        {(worldPos.y - r.minY) / h, (float)TreeDropZone::North},
        {(r.maxY - worldPos.y) / h, (float)TreeDropZone::South},
    };
    int best = 0;
    for (int i = 1; i < 4; ++i) {
        if (cand[i][0] < cand[best][0]) {
            best = i;
        }
    }
    if (cand[best][0] < 0.25f) {
        return static_cast<TreeDropZone>(static_cast<int>(cand[best][1]));
    }
    return TreeDropZone::Join;
}

DockArea::DropTarget DockArea::resolveDropTarget(
    const math::FVector2& worldPos) const {
    DropTarget t;
    const Slot slotProbe = hitTestSlot(worldPos);
    const bool collapsedSide =
        (slotProbe != Slot::Center && slotProbe != Slot::Count
         && _rootLeaves[(int)slotProbe] != nullptr
         && !_rootLeaves[(int)slotProbe]->isVisible());

    // Collapsed-side revive uses a thin outer strip so it does not steal
    // edge-split drops on a fill leaf that expanded into the side band
    // (Center east split vs collapsed Right).
    constexpr float kReviveStrip = 10.0f;
    bool inReviveStrip = false;
    if (collapsedSide) {
        const math::FRectangle wb = getWorldBounds();
        switch (slotProbe) {
            case Slot::Left:
                inReviveStrip = (worldPos.x <= wb.minX + kReviveStrip);
                break;
            case Slot::Right:
                inReviveStrip = (worldPos.x >= wb.maxX - kReviveStrip);
                break;
            case Slot::Top:
                inReviveStrip = (worldPos.y <= wb.minY + kReviveStrip);
                break;
            case Slot::Bottom:
                inReviveStrip = (worldPos.y >= wb.maxY - kReviveStrip);
                break;
            default:
                break;
        }
    }
    if (collapsedSide && inReviveStrip) {
        t.kind = DropKind::Slot;
        t.slot = slotProbe;
        return t;
    }

    if (DockTabGroup* leaf = hitTestTree(worldPos)) {
        t.kind = DropKind::Tree;
        t.leaf = leaf;
        t.zone = resolveTreeDropZone(leaf, worldPos);
        return t;
    }

    if (slotProbe != Slot::Count) {
        t.kind = DropKind::Slot;
        t.slot = slotProbe;
    }
    return t;
}

void DockArea::splitLeaf(DockTabGroup* leaf, TreeDropZone zone,
                         DockCard* card) {
    if (leaf == nullptr || card == nullptr) {
        return;
    }
    auto* box = dynamic_cast<BoxBase*>(leaf->getParent());
    if (box == nullptr) {
        return;   // leaf not inside a split node
    }
    const bool vertical = (zone == TreeDropZone::North
                           || zone == TreeDropZone::South);
    const bool before = (zone == TreeDropZone::North
                         || zone == TreeDropZone::West);

    // IMPORTANT: slot index space = BoxBase::_slots order, NOT
    // getChildren() order. addChild appends to the children vector while
    // insertWidget inserts into _slots, so the two orders diverge after
    // the first insert — deriving leafSlot from children and passing it
    // to slotSize/setSlotSize/insertWidget would resize the wrong panel
    // (observed: second split shrank Right instead of the g_0 target).
    const int leafSlot = box->slotIndexOf(leaf);
    if (leafSlot < 0) {
        return;
    }

    // New leaf takes 25% of the target leaf's main-axis extent, floored
    // at BoxBase::kMinPanelSize when the host is large enough for two
    // min-sized panes + splitter. Undersized hosts split 50/50.
    const float leafExtent = vertical ? leaf->getSize().y : leaf->getSize().x;
    const float splitterW = SplitterHandle::kDefaultWidth;
    const float kMin = BoxBase::kMinPanelSize;
    float newSize = leafExtent * 0.25f;
    if (leafExtent >= 2.0f * kMin + splitterW) {
        newSize = std::clamp(newSize, kMin, leafExtent - kMin - splitterW);
    } else {
        newSize = std::max(0.0f, (leafExtent - splitterW) * 0.5f);
    }

    auto* newLeaf = new DockTabGroup();
    const int n = _splitLeafCounter++;
    newLeaf->setLeafId("g_" + std::to_string(n));
    newLeaf->setPinned(false);
    newLeaf->setOnCloseTab([this](DockCard* c) {
        if (c != nullptr) {
            requestCloseCard(c);
        }
    });

    const SplitterHandle::Orientation orient = vertical
        ? SplitterHandle::Orientation::Vertical
        : SplitterHandle::Orientation::Horizontal;
    SplitterHandle* splitter = makeTreeSplitter(
        orient, getId() + "::split_g" + std::to_string(n));

    // Orthogonal split (e.g. South on a leaf inside an HBox): wrap the
    // leaf in a nested VBox/HBox. Inserting a vertical splitter into an
    // HBox (or vice versa) mis-assigns main-axis sizes — Center looked
    // "short" after dropping Right below it because 25% of height was
    // applied as an HBox width and no nested fill column existed.
    const bool parentIsV = (dynamic_cast<VBox*>(box) != nullptr);
    const bool needWrap = (vertical && !parentIsV) || (!vertical && parentIsV);

    if (needWrap) {
        const float oldSize = box->slotSize(leafSlot);
        box->removeWidget(leaf);   // detach only — leaf stays alive

        BoxBase* nest = vertical
            ? static_cast<BoxBase*>(new VBox())
            : static_cast<BoxBase*>(new HBox());
        nest->setId(getId() + "::nest_g" + std::to_string(n));
        // Match template mid/root: default BoxBase padding (4) would inset
        // the whole column vs Left and shrink Center|g_N below mid height.
        nest->setPadding(0.0f, 0.0f, 0.0f, 0.0f);
        nest->setSpacing(0.0f);

        if (before) {
            nest->addWidget(newLeaf, newSize);
            nest->addWidget(splitter, SplitterHandle::kDefaultWidth);
            nest->addWidget(leaf, 0.0f);   // fill remainder
        } else {
            nest->addWidget(leaf, 0.0f);   // fill remainder
            nest->addWidget(splitter, SplitterHandle::kDefaultWidth);
            nest->addWidget(newLeaf, newSize);
        }
        nest->rebindSplitters();

        // Wrapper inherits the leaf's prior slot size (0 = fill column).
        box->insertWidget(leafSlot, nest, oldSize);
        box->rebindSplitters();
        syncSplitterVisibility(nest);
        syncSplitterVisibility(box);
    } else {
        // Same-axis insert into the existing parent. Reuse an adjacent
        // splitter when one already borders the leaf — otherwise two
        // handles would stack into a fat dead band.
        const bool hasSplitterBefore = leafSlot > 0
            && box->isSplitterSlot(leafSlot - 1);
        const bool hasSplitterAfter = box->isSplitterSlot(leafSlot + 1);

        // Target was fixed-size: shrink it so the new pane fits. Fill
        // (size 0) stays fill and absorbs the remainder at layout.
        // Never leave a hairline fixed remainder — below min becomes fill.
        if (box->slotSize(leafSlot) > 0.0f) {
            const float remain = box->slotSize(leafSlot) - newSize
                - SplitterHandle::kDefaultWidth;
            box->setSlotSize(leafSlot,
                (remain >= BoxBase::kMinPanelSize) ? remain : 0.0f);
        }

        if (before) {
            if (hasSplitterBefore) {
                // [.., splitL, leaf] → [.., g_N, splitL, leaf]
                box->insertWidget(leafSlot - 1, newLeaf, newSize);
                destroyWidgetTree(splitter);   // unused — reused neighbor
                splitter = nullptr;
                // Reused handle may still be hidden from a prior
                // collapsed neighbor (see syncSplitterVisibility).
                if (Widget* reused = box->slotAt(leafSlot)) {
                    reused->setVisible(true);
                }
            } else {
                box->insertWidget(leafSlot, splitter,
                                  SplitterHandle::kDefaultWidth);
                box->insertWidget(leafSlot, newLeaf, newSize);
            }
        } else {
            if (hasSplitterAfter) {
                // [leaf, splitR, ..] → [leaf, splitR, g_N, ..]
                box->insertWidget(leafSlot + 2, newLeaf, newSize);
                destroyWidgetTree(splitter);
                splitter = nullptr;
                if (Widget* reused = box->slotAt(leafSlot + 1)) {
                    reused->setVisible(true);
                }
            } else {
                box->insertWidget(leafSlot + 1, splitter,
                                  SplitterHandle::kDefaultWidth);
                box->insertWidget(leafSlot + 2, newLeaf, newSize);
            }
        }
        box->rebindSplitters();
        syncSplitterVisibility(box);
    }

    // Move the card into the new leaf (detach from its current leaf or
    // the overlay; never free it — UI-OWN-1).
    if (DockTabGroup* src = findLeafOfCard(card)) {
        removeTabFromLeaf(src, card);
    } else if (_overlay) {
        _overlay->removeFloatingCard(card);
    }
    addTabToLeaf(newLeaf, card);

    // Structure changed — the weight-derived template geometry turns
    // sticky (fill slots absorb; splitter drags survive relayouts).
    _structureEpoch++;
    applyPinnedSlotMinLimits();
    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    dockTrace("[dock] splitLeaf target=%s zone=%s new=%s card=%s "
              "extent=%.0f newSize=%.0f wrap=%d\n",
              leaf->getLeafId().c_str(),
              zone == TreeDropZone::West ? "West" :
              zone == TreeDropZone::East ? "East" :
              zone == TreeDropZone::North ? "North" : "South",
              newLeaf->getLeafId().c_str(), card->getId().c_str(),
              static_cast<double>(leafExtent),
              static_cast<double>(newSize),
              needWrap ? 1 : 0);
    // Moving the card out of a prior g_N (same-leaf multi-step or
    // redockAt chain) can leave that leaf empty. Without prune it stays
    // as a transparent band taking fixed width — Gallery black gap
    // between Center and Right after East then West splits.
    pruneEmptySplitNodes();
}

void DockArea::pruneEmptySplitNodes() {
    if (_rootNode == nullptr || _pruning) {
        return;
    }
    _pruning = true;
    bool any = false;
    bool changed = true;
    while (changed) {
        changed = prunePass(_rootNode);
        any |= changed;
    }
    // Fold nest boxes left with a single panel after g_N removal; also
    // dissolve nests that have zero visible panels (hidden Center alone).
    auto* rootBox = dynamic_cast<BoxBase*>(_rootNode);
    if (rootBox != nullptr) {
        bool structural = true;
        while (structural) {
            structural = false;
            while (unwrapSinglePanelBoxes(rootBox, nullptr, _rootNode)) {
                structural = true;
                any = true;
            }
            while (pruneVacantBoxes(rootBox, nullptr, _rootNode)) {
                structural = true;
                any = true;
            }
        }
    }
    // After prune/unwrap (or even when nothing was removed), every box
    // must still have a visible fill panel — fixed-only children leave
    // unpainted leftover bands.
    const bool fillRepaired = ensureAllBoxesHaveVisibleFill(_rootNode);
    any |= fillRepaired;

    // Empty Center that is somehow still visible (e.g. after applyDockTree)
    // would paint nothing / steal a band — hide and re-fill siblings.
    if (DockTabGroup* center = _rootLeaves[(int)Slot::Center]) {
        if (center->getTabCount() == 0 && center->isVisible()) {
            if (auto* box = dynamic_cast<BoxBase*>(center->getParent())) {
                setLeafChromeVisible(center, false);
                ensureNestHasVisibleFill(box);
                any = true;
            }
        }
    }
    collapseEmptySidePinnedLeaves();
    applyPinnedSlotMinLimits();
    // Always: collapse hides neighbor splitters; a later split may reuse
    // them for two visible panels — re-derive visibility from neighbors.
    syncSplitterVisibilityInTree(_rootNode);

    if (any) {
        // Only bump when structure/fill actually changed — unconditional
        // bumps made the weight template sticky after every float/join.
        _structureEpoch++;
        markBoundsDirty();
        requestRelayout();
    }
    _pruning = false;
}

void DockArea::addTabToLeaf(DockTabGroup* leaf, DockCard* card) {
    if (leaf == nullptr || card == nullptr) {
        return;
    }
    // Re-expand a collapsed side slot before attaching the card.
    setSidePinnedCollapsed(leaf, false);
    // Center may have been nest-hidden when emptied; revive chrome.
    if (!leaf->isVisible()) {
        setLeafChromeVisible(leaf, true);
    }
    if (!leaf->containsCard(card)) {
        leaf->addTab(card);
    }
    const std::string id = card->getId();
    if (!id.empty()) {
        _cardIndex[id] = card;
    }
}

void DockArea::removeTabFromLeaf(DockTabGroup* leaf, DockCard* card) {
    if (leaf == nullptr || card == nullptr) {
        return;
    }
    leaf->removeTab(card);   // detach-only — caller owns the card
    if (leaf->getTabCount() == 0) {
        setSidePinnedCollapsed(leaf, true);
        // Empty Center (template mid OR nest wrap): hide chrome so
        // siblings absorb the fill. Promote/float of the last Center
        // card used to leave a dead middle band. Revive on addTabToLeaf.
        // Unwrap refuses to fold nests that still hold a hidden Center
        // (avoids dual-fill mid after g_N promote).
        if (leaf->getLeafId() == "Center") {
            if (auto* box = dynamic_cast<BoxBase*>(leaf->getParent())) {
                dockTrace("[dock] hide empty Center parent=%s\n",
                          box->getId().c_str());
                setLeafChromeVisible(leaf, false);
                ensureNestHasVisibleFill(box);
                // Prevent pristine template from rewriting sibling sizes
                // back to weight bands (would recreate the empty middle).
                _structureEpoch++;
            }
        }
    }
}

void DockArea::setSidePinnedCollapsed(DockTabGroup* leaf, bool collapsed) {
    if (leaf == nullptr || !leaf->isPinned()) {
        return;
    }
    // Center is the fill host — never collapse it (nested ops empty it
    // temporarily; hiding it broke depth-2 splits).
    const std::string& id = leaf->getLeafId();
    if (id != "Left" && id != "Right" && id != "Top" && id != "Bottom") {
        return;
    }
    if (collapsed) {
        if (auto* box = dynamic_cast<BoxBase*>(leaf->getParent())) {
            if (!isTemplateHost(box, _rootNode)) {
                // Empty side leaf sits inside an orthogonal wrap nest.
                // Hiding it there leaves a dead fill band under g_N
                // (log: Right-North wrap → join-away). Re-home
                // into mid/root first, then collapse in the template.
                dockTrace("[dock] extract pinned '%s' from nest → template\n",
                          id.c_str());
                detachLeafKeepAlive(box, leaf);
                ensureNestHasVisibleFill(box);
                reinsertSidePinnedIntoTemplate(_rootNode, leaf);
            }
        }
    }
    const bool wantVisible = !collapsed;
    setLeafChromeVisible(leaf, wantVisible);
    markBoundsDirty();
    if (collapsed) {
        // Drop residual fixed width so siblings/fill can absorb. Do NOT
        // prune here — splitLeaf calls removeTabFromLeaf mid-mutation
        // (new leaf not yet attached); pruning then crashed. Callers
        // (removeCard / splitLeaf / extract path) prune when safe.
        if (auto* box = dynamic_cast<BoxBase*>(leaf->getParent())) {
            const int si = box->slotIndexOf(leaf);
            if (si >= 0) {
                box->setSlotSize(si, 0.0f);
            }
            ensureNestHasVisibleFill(box);
        }
    }
    // Never prune recursively here. removeTabFromLeaf is called while
    // splitLeaf still has a structurally attached but empty destination
    // leaf. Pruning at this point can delete that destination and the nest
    // around it, after which splitLeaf would add the card through a freed
    // pointer. Every top-level remove/move/split path prunes after the card
    // has reached its final parent.
}

void DockArea::collapseEmptySidePinnedLeaves() {
    for (int i = 0; i < (int)Slot::Count; ++i) {
        if (i == (int)Slot::Center) {
            continue;
        }
        DockTabGroup* leaf = _rootLeaves[i];
        if (leaf != nullptr && leaf->getTabCount() == 0) {
            setSidePinnedCollapsed(leaf, true);
        }
    }
}

void DockArea::syncTemplateIfPristine(const math::FVector2& size) {
    if (!_templateBuilt) {
        return;
    }
    if (_templateSynced
        && (_structureEpoch > 0 || !templateSizesMatchDerived(size))) {
        // A splitter drag (or any manual sizing) made the slot sizes
        // sticky — never clobber them. The fill slot absorbs any
        // resize (VS Code behaviour). Phase-3 structure edits
        // (splitLeaf / prune) bump _structureEpoch, which also turns
        // the template sticky: inserted g_N leaves would otherwise
        // shift the fixed-slot index mapping in applyTemplateGeometry.
        return;
    }
    applyTemplateGeometry(size);
    _templateSynced = true;
}

float DockArea::effectiveSlotMinSize(Slot slot) const {
    if ((int)slot < 0 || (int)slot >= (int)Slot::Count) {
        return BoxBase::kMinPanelSize;
    }
    const float configured = _slotMinSize[(int)slot];
    return (configured > 0.0f) ? configured : BoxBase::kMinPanelSize;
}

void DockArea::applyPinnedSlotMinLimits() {
    for (int i = 0; i < (int)Slot::Count; ++i) {
        DockTabGroup* leaf = _rootLeaves[i];
        if (leaf == nullptr) {
            continue;
        }
        auto* box = dynamic_cast<BoxBase*>(leaf->getParent());
        if (box == nullptr) {
            continue;
        }
        const int si = box->slotIndexOf(leaf);
        if (si < 0) {
            continue;
        }
        BoxSlotLimits limits;
        limits.minWidth = effectiveSlotMinSize(static_cast<Slot>(i));
        box->setSlotLimits(si, limits);
    }
}

void DockArea::applyTemplateGeometry(const math::FVector2& size) {
    VBox* root = dynamic_cast<VBox*>(_rootNode);
    if (root == nullptr) {
        return;
    }
    HBox* mid = nullptr;
    for (Widget* c : root->getChildren()) {
        if (auto* h = dynamic_cast<HBox*>(c)) {
            mid = h;
            break;
        }
    }
    if (mid == nullptr) {
        return;
    }

    const float w = std::max(0.0f, size.x);
    const float h = std::max(0.0f, size.y);
    const float wts[5] = {
        effectiveWeight(*this, Slot::Top),
        effectiveWeight(*this, Slot::Bottom),
        effectiveWeight(*this, Slot::Left),
        effectiveWeight(*this, Slot::Right),
        effectiveWeight(*this, Slot::Center),
    };
    SlotRegions r = computeSlotRegions(w, h, wts);

    // Floor pinned bands so the first layout (and pristine resizes)
    // never paints a hairline Left/Right that splitter drag then has
    // to "discover" a min for. Prefer Center as the shrink target.
    const float minL = (_rootLeaves[(int)Slot::Left] != nullptr)
        ? effectiveSlotMinSize(Slot::Left) : 0.0f;
    const float minR = (_rootLeaves[(int)Slot::Right] != nullptr)
        ? effectiveSlotMinSize(Slot::Right) : 0.0f;
    const float minC = effectiveSlotMinSize(Slot::Center);
    const float minT = (_rootLeaves[(int)Slot::Top] != nullptr)
        ? effectiveSlotMinSize(Slot::Top) : 0.0f;
    const float minB = (_rootLeaves[(int)Slot::Bottom] != nullptr)
        ? effectiveSlotMinSize(Slot::Bottom) : 0.0f;

    if (minL > 0.0f) {
        r.leftW = std::max(r.leftW, minL);
    }
    if (minR > 0.0f) {
        r.rightW = std::max(r.rightW, minR);
    }
    if (r.leftW + r.rightW + minC > w && (r.leftW + r.rightW) > 0.0f) {
        const float room = std::max(0.0f, w - minC);
        const float scale = room / (r.leftW + r.rightW);
        r.leftW *= scale;
        r.rightW *= scale;
        if (minL > 0.0f) {
            r.leftW = std::max(r.leftW, std::min(minL, room));
        }
        if (minR > 0.0f) {
            r.rightW = std::max(r.rightW, std::min(minR, room - r.leftW));
        }
    }
    r.centerW = std::max(0.0f, w - r.leftW - r.rightW);

    if (minT > 0.0f) {
        r.topH = std::max(r.topH, minT);
    }
    if (minB > 0.0f) {
        r.botH = std::max(r.botH, minB);
    }
    if (r.topH + r.botH > h) {
        const float scale = (h > 0.0f) ? (h / (r.topH + r.botH)) : 0.0f;
        r.topH *= scale;
        r.botH *= scale;
    }
    r.midH = std::max(0.0f, h - r.topH - r.botH);
    r.midY = r.topH;

    // Walk panel + splitter slots: [Top, V-split]*, mid, [V-split, Bottom]*
    // and [Left, H-split]*, Center, [H-split, Right]*. Skip splitters;
    // leave mid / Center at size 0 (fill).
    int idx = 0;
    if (_rootLeaves[(int)Slot::Top] != nullptr) {
        root->setSlotSize(idx++, r.topH);
        ++idx;   // vertical splitter under Top
    }
    ++idx;   // mid fill slot stays 0
    if (_rootLeaves[(int)Slot::Bottom] != nullptr) {
        ++idx;   // vertical splitter above Bottom
        root->setSlotSize(idx++, r.botH);
    }

    idx = 0;
    if (_rootLeaves[(int)Slot::Left] != nullptr) {
        mid->setSlotSize(idx++, r.leftW);
        ++idx;   // horizontal splitter after Left
    }
    ++idx;   // Center fill slot stays 0
    if (_rootLeaves[(int)Slot::Right] != nullptr) {
        ++idx;   // horizontal splitter before Right
        mid->setSlotSize(idx++, r.rightW);
    }

    applyPinnedSlotMinLimits();

    dockTrace(
        "[dock] applyTemplateGeometry dock=%s size=%.0fx%.0f "
        "topH=%.1f botH=%.1f leftW=%.1f rightW=%.1f\n",
        getId().c_str(), w, h, r.topH, r.botH, r.leftW, r.rightW);
}

bool DockArea::templateSizesMatchDerived(const math::FVector2& size) const {
    const VBox* root = dynamic_cast<const VBox*>(_rootNode);
    if (root == nullptr) {
        return false;
    }
    const HBox* mid = nullptr;
    for (Widget* c : root->getChildren()) {
        if (auto* h = dynamic_cast<HBox*>(c)) {
            mid = h;
            break;
        }
    }
    if (mid == nullptr) {
        return false;
    }

    const float w = std::max(0.0f, size.x);
    const float h = std::max(0.0f, size.y);
    const float wts[5] = {
        effectiveWeight(*this, Slot::Top),
        effectiveWeight(*this, Slot::Bottom),
        effectiveWeight(*this, Slot::Left),
        effectiveWeight(*this, Slot::Right),
        effectiveWeight(*this, Slot::Center),
    };
    const SlotRegions r = computeSlotRegions(w, h, wts);

    // Walk the slots in the same order applyTemplateGeometry writes
    // them (skipping splitter slots). Any panel mismatch (splitter
    // drag) makes the geometry sticky.
    int idx = 0;
    if (_rootLeaves[(int)Slot::Top] != nullptr) {
        if (std::fabs(root->slotSize(idx) - r.topH) > kTemplateEpsilon) {
            return false;
        }
        ++idx;
        ++idx;   // vertical splitter under Top
    }
    if (std::fabs(root->slotSize(idx)) > kTemplateEpsilon) {
        return false;   // mid must stay fill (0)
    }
    ++idx;
    if (_rootLeaves[(int)Slot::Bottom] != nullptr) {
        ++idx;   // vertical splitter above Bottom
        if (std::fabs(root->slotSize(idx) - r.botH) > kTemplateEpsilon) {
            return false;
        }
    }

    idx = 0;
    if (_rootLeaves[(int)Slot::Left] != nullptr) {
        if (std::fabs(mid->slotSize(idx) - r.leftW) > kTemplateEpsilon) {
            return false;
        }
        ++idx;
        ++idx;   // horizontal splitter after Left
    }
    if (std::fabs(mid->slotSize(idx)) > kTemplateEpsilon) {
        return false;   // Center must stay fill (0)
    }
    ++idx;
    if (_rootLeaves[(int)Slot::Right] != nullptr) {
        ++idx;   // horizontal splitter before Right
        if (std::fabs(mid->slotSize(idx) - r.rightW) > kTemplateEpsilon) {
            return false;
        }
    }
    return true;
}

bool DockArea::tabIntoSlot(const std::string& cardId, Slot target) {
    auto it = _cardIndex.find(cardId);
    if (it == _cardIndex.end()) {
        return false;
    }
    if ((int)target < 0 || (int)target >= (int)Slot::Count) {
        return false;
    }
    DockCard* card = it->second;
    if (card == nullptr) {
        return false;
    }
    ensureRootTree();
    DockTabGroup* srcLeaf = findLeafOfCard(card);
    DockTabGroup* dstLeaf = leafForSlot(target);
    if (srcLeaf == nullptr || dstLeaf == nullptr || srcLeaf == dstLeaf) {
        return false;
    }
    removeTabFromLeaf(srcLeaf, card);
    addTabToLeaf(dstLeaf, card);
    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    dockTrace("[dock] tabIntoSlot OK card=%s -> %s tabs=%zu\n",
              cardId.c_str(), dockSlotName((int)target),
              dstLeaf->getTabCount());
    // srcLeaf may be a split-created leaf left empty by the move.
    pruneEmptySplitNodes();
    return true;
}

bool DockArea::dockCardAsTab(const std::string& cardId, Slot target) {
    if (!_overlay) {
        return false;
    }
    if ((int)target < 0 || (int)target >= (int)Slot::Count) {
        return false;
    }
    DockCard* card = nullptr;
    const size_t n = _overlay->getFloatingCardCount();
    for (size_t i = 0; i < n; ++i) {
        DockCard* c = _overlay->getFloatingCard(i);
        if (c && c->getId() == cardId) {
            card = c;
            break;
        }
    }
    if (card == nullptr) {
        return false;
    }
    ensureRootTree();
    DockTabGroup* leaf = leafForSlot(target);
    if (leaf == nullptr) {
        return false;
    }
    _overlay->removeFloatingCard(card);
    addTabToLeaf(leaf, card);
    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    dockTrace("[dock] dockCardAsTab OK card=%s -> %s tabs=%zu\n",
              cardId.c_str(), dockSlotName((int)target),
              leaf->getTabCount());
    return true;
}

// ---- Phase 4: dock-tree persistence ----------------------------------------

std::string DockArea::serializeDockTree() const {
    ayt::ui::json root;
    root["version"] = 1;
    if (_rootNode != nullptr) {
        ayt::ui::json tree;
        serializeNode(_rootNode, tree);
        root["dockTree"] = tree;
    }
    ayt::ui::json floating = ayt::ui::json::array();
    if (_overlay != nullptr) {
        const size_t n = _overlay->getFloatingCardCount();
        for (size_t i = 0; i < n; ++i) {
            const DockCard* c = _overlay->getFloatingCard(i);
            if (c == nullptr) {
                continue;
            }
            const math::FVector2 pos = c->getPosition();
            const math::FVector2 sz = c->getSize();
            floating.push_back({
                {"id", c->getId()}, {"x", pos.x}, {"y", pos.y},
                {"w", sz.x}, {"h", sz.y}});
        }
    }
    root["floating"] = floating;
    ayt::ui::json hidden = ayt::ui::json::array();
    for (const auto& entry : _hiddenCardSlots) {
        hidden.push_back({
            {"id", entry.first},
            {"slot", dockSlotName((int)entry.second)}});
    }
    root["hidden"] = hidden;
    return root.dump();
}

void DockArea::serializeNode(const Widget* node, ayt::ui::json& out) const {
    if (node == nullptr) {
        return;
    }
    if (const auto* leaf = dynamic_cast<const DockTabGroup*>(node)) {
        out["leaf"] = leaf->getLeafId();
        ayt::ui::json tabs = ayt::ui::json::array();
        const size_t n = leaf->getTabCount();
        for (size_t i = 0; i < n; ++i) {
            const DockCard* c = leaf->getTab(i);
            if (c != nullptr && !c->getId().empty()) {
                tabs.push_back(c->getId());
            }
        }
        out["tabs"] = tabs;
        const std::string act = leaf->getActiveTabId();
        out["active"] = act.empty() ? ayt::ui::json(nullptr) : ayt::ui::json(act);
        return;
    }
    if (const auto* box = dynamic_cast<const BoxBase*>(node)) {
        out["orientation"] =
            (dynamic_cast<const VBox*>(box) != nullptr) ? "V" : "H";
        ayt::ui::json children = ayt::ui::json::array();
        ayt::ui::json weights = ayt::ui::json::array();
        // children order diverges from _slots order after insertWidget
        // (insertWidget inserts into _slots while addChild appends to
        // the children vector — see BoxBase::slotIndexOf). Layout walks
        // _slots, so the JSON must too; otherwise the weights array
        // misaligns on apply and panels land in the wrong order.
        std::vector<Widget*> ordered;
        for (Widget* c : box->getChildren()) {
            if (c != nullptr && !c->isSplitterHandle()) {
                ordered.push_back(c);
            }
        }
        std::stable_sort(ordered.begin(), ordered.end(),
            [box](Widget* a, Widget* b) {
                return box->slotIndexOf(a) < box->slotIndexOf(b);
            });
        for (Widget* c : ordered) {
            // slotIndexOf gives the _slots order; slotSize must read
            // that index.
            const int si = box->slotIndexOf(c);
            ayt::ui::json childJson;
            serializeNode(c, childJson);
            children.push_back(childJson);
            weights.push_back(si >= 0 ? box->slotSize(si) : 0.0f);
        }
        out["children"] = children;
        out["weights"] = weights;
    }
}

bool DockArea::applyDockTree(const std::string& jsonStr) {
    ayt::ui::json j;
    try {
        j = ayt::ui::json::parse(jsonStr);
    } catch (...) {
        return false;
    }

    // Pool every card this dock owns (docked tabs + floating), detaching
    // each WITHOUT deleting (UI-OWN-1 — the widgets stay alive and are
    // re-attached below or floated back).
    std::unordered_map<std::string, DockCard*> pool;
    poolAllCards(pool);

    // Destroy the old root subtree (leaves + splitters). All cards were
    // detached above, so nothing alive is lost.
    if (_rootNode != nullptr) {
        destroyWidgetTree(_rootNode);
        _rootNode = nullptr;
        for (int i = 0; i < (int)Slot::Count; ++i) {
            _rootLeaves[i] = nullptr;
        }
        _templateBuilt = false;
        _templateSynced = false;
    }

    if (j.contains("dockTree") && j["dockTree"].is_object()) {
        _rootNode = buildNodeFromJson(j["dockTree"], pool);
        if (_rootNode != nullptr) {
            addChild(_rootNode);
            // Overlay must stay the topmost child (K-INV-D3-4).
            if (_overlay != nullptr) {
                _overlay->bringToFront();
            }
            _templateBuilt = true;
            _templateSynced = false;
        }
    }

    // Restore floating cards from JSON rects; unreferenced pool
    // leftovers (cards the JSON never mentions) float back at
    // staggered offsets so they never stack exactly.
    if (_overlay != nullptr) {
        if (j.contains("floating") && j["floating"].is_array()) {
            for (const auto& fj : j["floating"]) {
                const std::string fid = fj.value("id", "");
                if (fid.empty()) {
                    continue;
                }
                auto it = pool.find(fid);
                if (it == pool.end()) {
                    continue;
                }
                DockCard* c = it->second;
                pool.erase(it);
                c->setPosition(math::FVector2(
                    fj.value("x", 0.0f), fj.value("y", 0.0f)));
                c->setSize(math::FVector2(
                    fj.value("w", 320.0f), fj.value("h", 220.0f)));
                _overlay->addFloatingCard(c);
            }
        }
        if (j.contains("hidden") && j["hidden"].is_array()) {
            for (const auto& hj : j["hidden"]) {
                const std::string id = hj.value("id", "");
                Slot slot = Slot::Center;
                if (id.empty()
                    || !parseSlot(hj.value("slot", "Center"), slot)) {
                    continue;
                }
                auto it = pool.find(id);
                if (it == pool.end() || it->second == nullptr) {
                    continue;
                }
                DockCard* card = it->second;
                pool.erase(it);
                addChild(card);
                card->setVisible(false);
                _cardIndex[id] = card;
                _hiddenCardSlots[id] = slot;
            }
            _overlay->bringToFront();
        }
        int k = 0;
        for (auto& kv : pool) {
            DockCard* c = kv.second;
            if (c == nullptr) {
                continue;
            }
            const float off = 24.0f + 16.0f * static_cast<float>(k % 8);
            c->setPosition(math::FVector2(off, off));
            _overlay->addFloatingCard(c);
            ++k;
        }
    }

    if (_rootNode != nullptr) {
        pruneEmptySplitNodes();
    }
    collapseEmptySidePinnedLeaves();
    // An applied layout is user structure — the weight-derived template
    // turns sticky and never clobbers it.
    _structureEpoch++;
    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    dockTrace("[dock] applyDockTree OK cards=%zu float=%zu\n",
              pool.size(), _overlay ? _overlay->getFloatingCardCount() : 0u);
    return true;
}

void DockArea::poolAllCards(
    std::unordered_map<std::string, DockCard*>& pool) {
    std::vector<DockCard*> cards;
    collectLeafCards(_rootNode, cards);
    for (DockCard* c : cards) {
        if (c == nullptr) {
            continue;
        }
        if (DockTabGroup* leaf = findLeafOfCard(c)) {
            removeTabFromLeaf(leaf, c);
        }
        if (!c->getId().empty()) {
            pool[c->getId()] = c;
        }
    }
    if (_overlay != nullptr) {
        while (_overlay->getFloatingCardCount() > 0) {
            DockCard* c = _overlay->getFloatingCard(0);
            if (c == nullptr) {
                break;
            }
            _overlay->removeFloatingCard(c);
            if (!c->getId().empty()) {
                pool[c->getId()] = c;
            }
        }
    }
    for (const auto& entry : _hiddenCardSlots) {
        auto it = _cardIndex.find(entry.first);
        if (it == _cardIndex.end() || it->second == nullptr) {
            continue;
        }
        DockCard* card = it->second;
        if (card->getParent() == this) {
            removeChild(card);
        }
        pool[entry.first] = card;
    }
    _hiddenCardSlots.clear();
    _cardIndex.clear();
}

Widget* DockArea::buildNodeFromJson(
    const ayt::ui::json& j,
    std::unordered_map<std::string, DockCard*>& pool) {
    if (j.contains("leaf")) {
        const std::string id = j.value("leaf", "");
        DockTabGroup* leaf = nullptr;
        Slot slot;
        if (parseSlot(id, slot)) {
            leaf = makePinnedLeaf(slot);
        } else {
            leaf = new DockTabGroup();
            leaf->setLeafId(id.empty()
                ? "g_" + std::to_string(_splitLeafCounter++)
                : id);
            leaf->setPinned(false);
            leaf->setOnCloseTab([this](DockCard* c) {
                if (c != nullptr) {
                    requestCloseCard(c);
                }
            });
        }
        if (j.contains("tabs") && j["tabs"].is_array()) {
            for (const auto& t : j["tabs"]) {
                const std::string tid = t.get<std::string>();
                auto it = pool.find(tid);
                if (it != pool.end()) {
                    addTabToLeaf(leaf, it->second);
                    pool.erase(it);
                }
            }
        }
        if (j.contains("active") && j["active"].is_string()) {
            const std::string act = j.value("active", "");
            if (!act.empty()) {
                leaf->activateTabById(act);
            }
        }
        return leaf;
    }

    // Split node.
    const std::string orient = j.value("orientation", "H");
    BoxBase* box = (orient == "V")
        ? static_cast<BoxBase*>(new VBox())
        : static_cast<BoxBase*>(new HBox());
    box->setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    box->setSpacing(0.0f);
    const SplitterHandle::Orientation so = (orient == "V")
        ? SplitterHandle::Orientation::Vertical
        : SplitterHandle::Orientation::Horizontal;
    const ayt::ui::json kids =
        j.contains("children") && j["children"].is_array()
            ? j["children"] : ayt::ui::json::array();
    const ayt::ui::json wts =
        j.contains("weights") && j["weights"].is_array()
            ? j["weights"] : ayt::ui::json::array();
    bool first = true;
    int wi = 0;
    for (const auto& kj : kids) {
        if (!first) {
            box->addWidget(makeTreeSplitter(
                so, getId() + "::s" + std::to_string(++_splitLeafCounter)),
                SplitterHandle::kDefaultWidth);
        }
        first = false;
        float w = 0.0f;
        if (wi < static_cast<int>(wts.size()) && wts[wi].is_number()) {
            w = wts[wi].get<float>();
        }
        ++wi;
        Widget* child = buildNodeFromJson(kj, pool);
        if (child != nullptr) {
            box->addWidget(child, w);
        }
    }
    box->rebindSplitters();
    return box;
}

} // namespace ayt::ui
