#include "AYDockArea.h"
#include "AYDockCard.h"
#include "AYDockOverlay.h"
#include "AYDockTabGroup.h"
#include "AYBox.h"
#include "AYSplitterHandle.h"
#include "IAYRenderBackend.h"
#include "AYUIManager.h"
#include "AYDockTrace.h"

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

// In-region adaptive drop zones (IDE-style): edges retarget / swap the
// leaf; the center band merges as a tab into the leaf under the cursor.
enum class LeafDropZone { Center, West, East, North, South };

LeafDropZone resolveLeafDropZone(const math::FRectangle& r,
                                 const math::FVector2& worldPos) {
    const float w = r.maxX - r.minX;
    const float h = r.maxY - r.minY;
    if (w < 2.0f || h < 2.0f) {
        return LeafDropZone::Center;
    }
    const float ex = std::max(24.0f, w * 0.22f);
    const float ey = std::max(24.0f, h * 0.22f);
    if (worldPos.x < r.minX + ex) {
        return LeafDropZone::West;
    }
    if (worldPos.x >= r.maxX - ex) {
        return LeafDropZone::East;
    }
    if (worldPos.y < r.minY + ey) {
        return LeafDropZone::North;
    }
    if (worldPos.y >= r.maxY - ey) {
        return LeafDropZone::South;
    }
    return LeafDropZone::Center;
}

math::FRectangle leafDropZoneRect(const math::FRectangle& r, LeafDropZone z) {
    const float w = r.maxX - r.minX;
    const float h = r.maxY - r.minY;
    const float ex = std::max(24.0f, w * 0.22f);
    const float ey = std::max(24.0f, h * 0.22f);
    switch (z) {
        case LeafDropZone::West:
            return math::FRectangle(r.minX, r.minY, r.minX + ex, r.maxY);
        case LeafDropZone::East:
            return math::FRectangle(r.maxX - ex, r.minY, r.maxX, r.maxY);
        case LeafDropZone::North:
            return math::FRectangle(r.minX, r.minY, r.maxX, r.minY + ey);
        case LeafDropZone::South:
            return math::FRectangle(r.minX, r.maxY - ey, r.maxX, r.maxY);
        case LeafDropZone::Center:
        default:
            return math::FRectangle(r.minX + ex, r.minY + ey,
                                    r.maxX - ex, r.maxY - ey);
    }
}

const wchar_t* leafDropZoneLabel(LeafDropZone z) {
    switch (z) {
        case LeafDropZone::West:   return L"Dock Left";
        case LeafDropZone::East:   return L"Dock Right";
        case LeafDropZone::North:  return L"Dock Top";
        case LeafDropZone::South:  return L"Dock Bottom";
        case LeafDropZone::Center: return L"Tab Here";
        default: return L"?";
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

// Remove `leaf` (a heap object owned by the dock tree) from `box`
// together with any directly adjacent splitter handles, then free them.
// removeWidget detaches + rebinds; delete frees the heap node. Only
// called for EMPTY non-pinned leaves — no cards to leak.
void removeLeafAndNeighborSplitters(BoxBase* box, Widget* leaf) {
    std::vector<Widget*> toRemove;
    const std::vector<Widget*> kids = box->getChildren();
    int li = -1;
    for (int i = 0; i < static_cast<int>(kids.size()); ++i) {
        if (kids[i] == leaf) {
            li = i;
            break;
        }
    }
    if (li >= 0) {
        if (li > 0 && kids[li - 1]->isSplitterHandle()) {
            toRemove.push_back(kids[li - 1]);
        }
        if (li + 1 < static_cast<int>(kids.size())
            && kids[li + 1]->isSplitterHandle()) {
            toRemove.push_back(kids[li + 1]);
        }
    }
    toRemove.push_back(leaf);
    for (Widget* w : toRemove) {
        box->removeWidget(w);
        delete w;
    }
}

// One bottom-up pass over the tree: drop empty non-pinned leaves (with
// their adjacent splitters). Recursion order is safe: a child that gets
// removed is gone from the next snapshot before the parent touches it.
// Pinned leaves are never removed. Folding single-panel split nodes is
// intentionally NOT implemented — the current tree shape (root
// VBox{Top?, mid HBox, Bottom?}) always keeps ≥1 pinned leaf per box, so
// a single-panel box is unreachable; revisit if Top/Bottom gain
// pinned-less nesting.
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
            if (srcLeaf == dstLeaf) {
                // K-INV-D3-1: same leaf → no-op, center AND edge zones
                // (a 20px release near the edge must stay docked).
                dockTrace("[dock] onDrop -> same-leaf NO-OP\n");
            } else {
                const TreeDropZone zone =
                    resolveTreeDropZone(dstLeaf, dropPos);
                if (zone == TreeDropZone::Join) {
                    // Merge as a tab into the target leaf.
                    if (srcLeaf != nullptr) {
                        removeTabFromLeaf(srcLeaf, card);
                    } else if (_overlay) {
                        _overlay->removeFloatingCard(card);
                    }
                    addTabToLeaf(dstLeaf, card);
                    pruneEmptySplitNodes();
                    dockTrace("[dock] onDrop -> join %s <- %s tabs=%zu\n",
                              dstLeaf->getLeafId().c_str(),
                              srcLeaf ? srcLeaf->getLeafId().c_str() : "float",
                              dstLeaf->getTabCount());
                } else {
                    // Edge → nested split (new g_N leaf, 25% share).
                    dockTrace("[dock] onDrop -> splitLeaf %s\n",
                              dstLeaf->getLeafId().c_str());
                    splitLeaf(dstLeaf, zone, card);
                }
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
    // Only while a DockCard is being dragged, and only for the slot under
    // the cursor — idle / foreign payloads paint nothing.
    //
    // D5-redock: a drag that started in a promoted child window has its
    // G12 session in the CHILD UIManager, so isDockCardDrag(tryGet())
    // is false here. The host bridge feeds the external cursor position
    // (primary-client space) via setExternalDropPos; that path bypasses
    // the session check and hit-tests the fed position instead.
    Slot hover = Slot::Count;
    math::FVector2 cursor(0.0f, 0.0f);
    if (_externalDropActive) {
        cursor = _externalDropPos;
        hover = hitTestSlot(cursor);
    } else {
        UIManager* ui = UIManager::tryGet();
        if (!isDockCardDrag(ui)) {
            return;
        }
        cursor = ui->getDragLastMousePos();
        hover = hitTestSlot(cursor);
    }
    if (hover == Slot::Count) {
        return;
    }

    // Live leaf rect when the dock tree is up — follows splitter-resized
    // panels so the guide stays locked to adaptive placement targets.
    const math::FRectangle r = getSlotRect(hover);
    if (r.maxX - r.minX < 2.0f || r.maxY - r.minY < 2.0f) {
        return;
    }

    // Slot tint (low alpha so cards underneath stay readable).
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

    // Adaptive sub-zone under the cursor (center = tab, edges = dock).
    const LeafDropZone zone = resolveLeafDropZone(r, cursor);
    const math::FRectangle zr = leafDropZoneRect(r, zone);
    math::FVector4 zoneFill(fill.x, fill.y, fill.z, 0.42f);
    if (zone == LeafDropZone::Center) {
        zoneFill = math::FVector4(0.15f, 0.70f, 0.95f, 0.45f);
    }
    if (zr.maxX - zr.minX >= 2.0f && zr.maxY - zr.minY >= 2.0f) {
        renderer.drawRect(zr, zoneFill);
        renderer.drawBorderRect(zr,
            math::FVector4(zoneFill.x, zoneFill.y, zoneFill.z, 0.90f),
            2.0f, 0.0f);
        renderer.drawText(zr, leafDropZoneLabel(zone), 13,
                          math::FVector4(1.0f, 1.0f, 1.0f, 0.90f));
    } else {
        renderer.drawText(r, slotLabel(hover), 14,
                          math::FVector4(1.0f, 1.0f, 1.0f, 0.75f));
    }

    // Phase-3 tree layer: precise join/split preview from LIVE leaf
    // geometry (follows splitter-resized panels). Painted only for a
    // drag that belongs to this dock — external (child-window) redock
    // drags keep the legacy slot fill above as their hint.
    if (_rootNode != nullptr) {
        UIManager* treeUi = UIManager::tryGet();
        if (isDockCardDrag(treeUi) && dragBelongsToDock(*this, treeUi)) {
            if (DockTabGroup* leaf = hitTestTree(cursor)) {
                paintTreeDropZone(renderer, leaf,
                                  resolveTreeDropZone(leaf, cursor));
            }
        }
    }
}

void DockArea::render(IRenderBackend& renderer) {
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

    // Occupied target: float prior occupants instead of stacking
    // (Gallery redock used bare addCard and piled cards full-bleed).
    // Inbound has no vacated slot to swap into — float is the dockCard
    // / IDE parity when dropping from outside the dock tree. Test-pinned
    // displace-to-float (Test_DockFloat.cpp).
    if (leaf->getTabCount() > 0) {
        const math::FRectangle tr = getSlotRect(target);
        const math::FVector2 floatPos(tr.minX + 24.0f, tr.minY + 24.0f);
        std::vector<std::string> displaceIds;
        const size_t n = leaf->getTabCount();
        for (size_t i = 0; i < n; ++i) {
            DockCard* c = leaf->getTab(i);
            if (c == nullptr || c == card || c->getId().empty()) {
                continue;
            }
            displaceIds.push_back(c->getId());
        }
        for (const std::string& id : displaceIds) {
            dockTrace("[dock] adoptCard displace '%s' -> float\n", id.c_str());
            floatCard(id, floatPos);
        }
    }

    addTabToLeaf(leaf, card);
    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    return true;
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

    // K-INV-D3-7: overlay → slot via adoptCard (displaces occupants).
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

    const math::FVector2 local(worldPos.x - wb.minX, worldPos.y - wb.minY);
    const SlotRegions r = hitTestRegions(*this);
    const float midY    = r.midY;
    const float midH    = r.midH;
    const float leftW   = r.leftW;
    const float centerW = r.centerW;

    if (local.y < midY) {
        return Slot::Top;
    }
    if (local.y >= midY + midH) {
        return Slot::Bottom;
    }
    if (local.x < leftW) {
        return Slot::Left;
    }
    if (local.x >= leftW + centerW) {
        return Slot::Right;
    }
    return Slot::Center;
}

math::FRectangle DockArea::getSlotRect(Slot slot) const {
    if ((int)slot < 0 || (int)slot >= (int)Slot::Count) {
        return math::FRectangle();
    }
    // Live leaf bounds track sticky splitter geometry; weight regions
    // remain the fallback for pre-tree / disabled-slot probes.
    if (const DockTabGroup* leaf = _rootLeaves[(int)slot]) {
        const math::FRectangle lb = leaf->getWorldBounds();
        if (lb.maxX - lb.minX >= 2.0f && lb.maxY - lb.minY >= 2.0f) {
            return lb;
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

    addChild(root);
    _rootNode = root;
    _templateBuilt = true;
    _templateSynced = false;
    // Floating cards paint on top of the tree — overlay must be the
    // LAST child.
    _overlay->bringToFront();

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
            closeCard(card->getId());
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

    // New leaf takes 25% of the target leaf's main-axis extent. If the
    // target leaf has a fixed size, shrink it; if it is fill (size 0)
    // it stays fill and absorbs the remainder at the next layout.
    const float leafExtent = vertical ? leaf->getSize().y : leaf->getSize().x;
    const float newSize = std::max(0.0f, leafExtent * 0.25f);
    if (box->slotSize(leafSlot) > 0.0f) {
        box->setSlotSize(leafSlot, std::max(
            0.0f, box->slotSize(leafSlot) - newSize
                  - SplitterHandle::kDefaultWidth));
    }

    auto* newLeaf = new DockTabGroup();
    const int n = _splitLeafCounter++;
    newLeaf->setLeafId("g_" + std::to_string(n));
    newLeaf->setPinned(false);
    newLeaf->setOnCloseTab([this](DockCard* c) {
        if (c != nullptr) {
            closeCard(c->getId());
        }
    });

    // Reuse an adjacent splitter when one already borders the leaf —
    // otherwise two handles would stack into a fat dead band.
    const bool hasSplitterBefore = leafSlot > 0
        && box->isSplitterSlot(leafSlot - 1);
    // isSplitterSlot bounds-checks itself (out-of-range → false).
    const bool hasSplitterAfter = box->isSplitterSlot(leafSlot + 1);

    if (before) {
        if (hasSplitterBefore) {
            // [.., splitL, leaf] → [.., g_N, splitL, leaf]: the
            // existing splitter moves between g_N and leaf.
            box->insertWidget(leafSlot - 1, newLeaf, newSize);
        } else {
            const SplitterHandle::Orientation orient = vertical
                ? SplitterHandle::Orientation::Vertical
                : SplitterHandle::Orientation::Horizontal;
            SplitterHandle* splitter = makeTreeSplitter(
                orient, getId() + "::split_g" + std::to_string(n));
            box->insertWidget(leafSlot, splitter,
                              SplitterHandle::kDefaultWidth);
            box->insertWidget(leafSlot, newLeaf, newSize);
        }
    } else {
        if (hasSplitterAfter) {
            // [leaf, splitR, ..] → [leaf, splitR, g_N, ..]: the
            // existing splitter stays between leaf and g_N.
            box->insertWidget(leafSlot + 2, newLeaf, newSize);
        } else {
            const SplitterHandle::Orientation orient = vertical
                ? SplitterHandle::Orientation::Vertical
                : SplitterHandle::Orientation::Horizontal;
            SplitterHandle* splitter = makeTreeSplitter(
                orient, getId() + "::split_g" + std::to_string(n));
            box->insertWidget(leafSlot + 1, splitter,
                              SplitterHandle::kDefaultWidth);
            box->insertWidget(leafSlot + 2, newLeaf, newSize);
        }
    }
    box->rebindSplitters();

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
    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    dockTrace("[dock] splitLeaf target=%s zone=%s new=%s card=%s "
              "extent=%.0f newSize=%.0f\n",
              leaf->getLeafId().c_str(),
              zone == TreeDropZone::West ? "West" :
              zone == TreeDropZone::East ? "East" :
              zone == TreeDropZone::North ? "North" : "South",
              newLeaf->getLeafId().c_str(), card->getId().c_str(),
              static_cast<double>(leafExtent),
              static_cast<double>(newSize));
    {
        const std::vector<Widget*>& ck = box->getChildren();
        for (Widget* c : ck) {
            const math::FRectangle wb = c->getWorldBounds();
            dockTrace("    [child] %s %.0f..%.0f x %.0f..%.0f\n",
                      c->getId().c_str(), wb.minX, wb.maxX, wb.minY, wb.maxY);
        }
    }
}

void DockArea::pruneEmptySplitNodes() {
    if (_rootNode == nullptr) {
        return;
    }
    _structureEpoch++;
    bool any = false;
    bool changed = true;
    while (changed) {
        changed = prunePass(_rootNode);
        any |= changed;
    }
    if (any) {
        markBoundsDirty();
        requestRelayout();
    }
}

void DockArea::addTabToLeaf(DockTabGroup* leaf, DockCard* card) {
    if (leaf == nullptr || card == nullptr) {
        return;
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
    const SlotRegions r = computeSlotRegions(w, h, wts);

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

} // namespace ayt::ui