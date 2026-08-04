#include "AYDockArea.h"
#include "AYDockCard.h"
#include "AYDockOverlay.h"
#include "AYBox.h"
#include "IAYRenderBackend.h"
#include "AYUIManager.h"

#include <algorithm>

namespace ayt::ui {

namespace {

// Returns the slot's "weight" used by the layout pass when the host
// hasn't called setSlotWeight. Stored default values are in the header;
// this helper exists only to centralise the 0.0 = use-default rule.
float effectiveWeight(const DockArea& area, DockArea::Slot slot) {
    const float w = area.getSlotWeight(slot);
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

bool slotHasVisibleCards(const DockArea& area, DockArea::Slot slot) {
    const size_t n = area.getCardCount(slot);
    for (size_t i = 0; i < n; ++i) {
        if (const DockCard* card = area.getCard(slot, i)) {
            if (card->isVisible()) {
                return true;
            }
        }
    }
    return false;
}

float layoutSlotWeight(const DockArea& area, DockArea::Slot slot) {
    if (!slotHasVisibleCards(area, slot)) {
        return 0.0f;
    }
    return effectiveWeight(area, slot);
}

void fillContainerChildren(Widget* container, float width, float height) {
    if (container == nullptr || width <= 0.0f || height <= 0.0f) {
        return;
    }
    for (Widget* child : container->getChildren()) {
        if (child == nullptr || !child->isVisible()) {
            continue;
        }
        child->setPosition(math::FVector2(0.0f, 0.0f));
        child->setSize(math::FVector2(width, height));
        child->performLayout();
    }
}

void requestRelayout() {
    if (UIManager* ui = UIManager::tryGet()) {
        ui->invalidateLayout();
        ui->layout();
    }
}

} // namespace

DockArea::~DockArea() {
    // Clear bookkeeping aliases first — do not delete through them.
    // Actual heap teardown walks getChildren() below.
    _cardIndex.clear();
    for (int i = 0; i < (int)Slot::Count; ++i) {
        _slotCards[i].clear();
        _slotContainers[i] = nullptr;
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
    // container->addChild).
    const std::string id = card->getId();
    if (!id.empty()) {
        auto it = _cardIndex.find(id);
        if (it != _cardIndex.end() && it->second != card.get()) {
            removeCard(id);
        } else if (it != _cardIndex.end() && it->second == card.get()) {
            // Same instance: scrub slot bookkeeping without freeing.
            _cardIndex.erase(it);
            for (int i = 0; i < (int)Slot::Count; ++i) {
                auto& vec = _slotCards[i];
                vec.erase(std::remove(vec.begin(), vec.end(), card.get()),
                          vec.end());
            }
        }
    }

    // Push the card into the slot's container. All 5 slots — including
    // Center — route through their container so performLayout() can
    // position the container and let the existing layout pass handle
    // the inner card. (Center is a plain Widget, not a VBox; cards are
    // direct children of that Widget.)
    DockCard* raw = card.release();
    Widget* container = _slotContainers[(int)slot];
    if (slot == Slot::Center) {
        // Center hosts a single big panel (viewport). Detach from
        // DockArea's children (if it was attached earlier by mistake)
        // and re-parent under the Center container.
        if (container) {
            container->addChild(raw);
        } else {
            addChild(raw);
        }
    } else if (container) {
        if (auto* vbox = dynamic_cast<VBox*>(container)) {
            vbox->addWidget(raw);
        } else if (auto* hbox = dynamic_cast<HBox*>(container)) {
            hbox->addWidget(raw);
        }
    }
    _slotCards[(int)slot].push_back(raw);
    if (!id.empty()) {
        _cardIndex[id] = raw;
    }
}

bool DockArea::removeCard(const std::string& cardId) {
    auto it = _cardIndex.find(cardId);
    if (it == _cardIndex.end()) {
        return false;
    }
    DockCard* card = it->second;

    // D1 invariant: removeCard always frees the card. destroyWidgetTree
    // detaches from whatever parent (DockArea / VBox / HBox) then deletes.
    destroyWidgetTree(card);

    _cardIndex.erase(cardId);
    for (int i = 0; i < (int)Slot::Count; ++i) {
        auto& vec = _slotCards[i];
        for (size_t k = 0; k < vec.size(); ++k) {
            if (vec[k] == card) {
                vec.erase(vec.begin() + k);
                break;
            }
        }
    }
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
    const auto& vec = _slotCards[(int)slot];
    if (index >= vec.size()) {
        return nullptr;
    }
    return vec[index];
}

void DockArea::onRender(IRenderBackend& renderer) {
    AYUNREFERENCED_PARAM(renderer);
    // D3 — drop-target highlight. Polls isCurrentDropTarget() per
    // frame (cheap) and draws a generic outline over the DockArea
    // bounds when active. Slot/overlay region-level highlighting
    // would require either G12's onDragOver (not yet shipped) or
    // UIManager exposing lastMousePos to drop targets; we accept the
    // generic outline as good-enough visual feedback for v1.5.
    // See K-INV-D3-8.
    //
    // The actual rect draw lives in a separate conditional so the
    // common path (not dragging) skips the renderer call entirely.
    // K-INV-D3-4 mirrors the F3 freecam isPointOnChrome contract:
    // rendering must not add visual chrome that catches the raycast.
    if (!isCurrentDropTarget()) {
        return;
    }
    const math::FRectangle b = getWorldBounds();
    renderer.drawBorderRect(b, math::FVector4(0.30f, 0.55f, 0.95f, 1.0f),
                            2.0f, 0.0f);
}

void DockArea::performLayout() {
    // First descend into children so the overlay + slot containers
    // get a layout pass.
    compoundDescendLayout(this);

    // Then split the DockArea's bounds into 5 regions according to the
    // slot weights. Order: Top strip (full width), Bottom strip (full
    // width), then Left + Center + Right columns inside the middle row.
    const math::FVector2 sz = getSize();
    const float w = sz.x;
    const float h = sz.y;

    const float wtTop    = layoutSlotWeight(*this, Slot::Top);
    const float wtBottom = layoutSlotWeight(*this, Slot::Bottom);
    const float wtLeft   = layoutSlotWeight(*this, Slot::Left);
    const float wtRight  = layoutSlotWeight(*this, Slot::Right);
    const float wtCenter = layoutSlotWeight(*this, Slot::Center);

    const float sumVert = wtTop + wtBottom + 1.0f; // middle row takes the rest
    const float topH    = h * (wtTop    / sumVert);
    const float botH    = h * (wtBottom / sumVert);
    const float midY    = topH;
    const float midH    = h - topH - botH;

    if (Widget* top = _slotContainers[(int)Slot::Top]) {
        top->setPosition(math::FVector2(0.0f, 0.0f));
        top->setSize(math::FVector2(w, topH));
    }
    if (Widget* bot = _slotContainers[(int)Slot::Bottom]) {
        bot->setPosition(math::FVector2(0.0f, midY + midH));
        bot->setSize(math::FVector2(w, botH));
    }

    const float sumHoriz = wtLeft + wtCenter + wtRight;
    const float leftW  = (sumHoriz > 0.0f) ? w * (wtLeft   / sumHoriz) : 0.0f;
    const float rightW = (sumHoriz > 0.0f) ? w * (wtRight  / sumHoriz) : 0.0f;
    const float centerW = w - leftW - rightW;

    if (Widget* left = _slotContainers[(int)Slot::Left]) {
        left->setPosition(math::FVector2(0.0f, midY));
        left->setSize(math::FVector2(leftW, midH));
        if (leftW > 0.0f) {
            left->performLayout();
        }
    }
    if (Widget* center = _slotContainers[(int)Slot::Center]) {
        center->setPosition(math::FVector2(leftW, midY));
        center->setSize(math::FVector2(centerW, midH));
        fillContainerChildren(center, centerW, midH);
    }
    if (Widget* right = _slotContainers[(int)Slot::Right]) {
        right->setPosition(math::FVector2(leftW + centerW, midY));
        right->setSize(math::FVector2(rightW, midH));
        if (rightW > 0.0f) {
            right->performLayout();
        }
    }

    // Overlay covers everything so floating cards can be positioned
    // anywhere within the DockArea bounds.
    if (_overlay) {
        _overlay->setPosition(math::FVector2(0.0f, 0.0f));
        _overlay->setSize(sz);
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
    if (!_overlay) return false;
    auto it = _cardIndex.find(cardId);
    if (it == _cardIndex.end()) {
        return false;       // not in any slot — nothing to float
    }
    DockCard* card = it->second;
    if (card == nullptr) return false;

    // Locate the slot the card currently lives in. O(N) but N is tiny
    // (5 slots max, each holding a handful of cards).
    int oldSlotIdx = -1;
    for (int i = 0; i < (int)Slot::Count; ++i) {
        for (DockCard* c : _slotCards[i]) {
            if (c == card) {
                oldSlotIdx = i;
                break;
            }
        }
        if (oldSlotIdx >= 0) break;
    }
    if (oldSlotIdx < 0) {
        // _cardIndex says it's in a slot, but linear scan didn't find
        // it — inconsistent state. Refuse to move rather than corrupt.
        return false;
    }

    // We CANNOT use DockArea::removeCard() because it calls
    // destroyWidgetTree() — that frees the card. We need the card alive
    // to hand to the overlay.
    _cardIndex.erase(it);
    _slotCards[oldSlotIdx].erase(
        std::remove(_slotCards[oldSlotIdx].begin(),
                    _slotCards[oldSlotIdx].end(), card),
        _slotCards[oldSlotIdx].end());

    // Detach from the slot's container (VBox/HBox/Center Widget) WITHOUT
    // delete. removeWidget() + re-parent to overlay via addFloatingCard
    // (which calls addChild internally) keeps ownership consistent.
    Widget* parent = card->getParent();
    if (auto* vbox = dynamic_cast<VBox*>(parent)) {
        vbox->removeWidget(card);
    } else if (auto* hbox = dynamic_cast<HBox*>(parent)) {
        hbox->removeWidget(card);
    } else if (parent != nullptr) {
        card->detachFromParent();
    }

    // Set floating position + size and hand to overlay. addFloatingCard
    // calls addChild which re-parents and assumes ownership.
    card->setPosition(pos);
    card->setVisible(true);
    _overlay->addFloatingCard(card);

    // Reset hover state — the card just moved; the highlight from
    // before the drop is stale.
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

    // Hand to addCard. addCard's `removeCard(id)` for last-write-wins
    // is a no-op here: the floating card was never inserted into
    // _cardIndex (addFloatingCard skips the index by design), so the
    // id lookup misses and removeCard returns false without touching
    // any card. K-INV-D3-7 invariant: the card transitions from
    // "not in _cardIndex" (overlay) → "in _cardIndex" (slot).
    addCard(target, std::unique_ptr<DockCard>(card));

    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    return true;
}

bool DockArea::moveInSlot(const std::string& cardId, Slot target) {
    // Code-review 2026-08-02 #8: slot->slot move without routing through
    // the overlay. Caller is responsible for K-INV-D3-1 (same-slot no-op)
    // — the onDrop callback already pre-checks `isCardInSlot(card, target)`
    // so this function unconditionally reparents the card to `target`.
    auto it = _cardIndex.find(cardId);
    if (it == _cardIndex.end()) {
        return false;   // not docked anywhere — nothing to move
    }
    if ((int)target < 0 || (int)target >= (int)Slot::Count) {
        return false;
    }
    DockCard* card = it->second;
    if (card == nullptr) return false;

    // Locate the current slot.
    int oldSlotIdx = -1;
    for (int i = 0; i < (int)Slot::Count; ++i) {
        for (DockCard* c : _slotCards[i]) {
            if (c == card) { oldSlotIdx = i; break; }
        }
        if (oldSlotIdx >= 0) break;
    }
    if (oldSlotIdx < 0) return false;
    if (oldSlotIdx == (int)target) {
        // Same-slot no-op (K-INV-D3-1 safety net — the onDrop
        // pre-check should have caught this already).
        return false;
    }

    // Detach from the old slot's container WITHOUT freeing. Reuse the
    // same dynamic_cast cascade as floatCard.
    Widget* parent = card->getParent();
    if (auto* vbox = dynamic_cast<VBox*>(parent)) {
        vbox->removeWidget(card);
    } else if (auto* hbox = dynamic_cast<HBox*>(parent)) {
        hbox->removeWidget(card);
    } else if (parent != nullptr) {
        card->detachFromParent();
    }

    // Drop the old-slot entry from _slotCards; _cardIndex stays valid
    // (the card pointer didn't change).
    _slotCards[oldSlotIdx].erase(
        std::remove(_slotCards[oldSlotIdx].begin(),
                    _slotCards[oldSlotIdx].end(), card),
        _slotCards[oldSlotIdx].end());

    // Re-attach under the new slot's container. addCard's
    // removeCard-first safety net (last-write-wins) is a no-op here
    // because the card pointer hasn't changed; the id-keyed
    // _cardIndex lookup matches this card.
    addCard(target, std::unique_ptr<DockCard>(card));

    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    return true;
}

DockArea::Slot DockArea::hitTestSlot(const math::FVector2& worldPos) const {
    // Mirror the region math in performLayout() — same weights, same
    // order. If the slot containers haven't been laid out yet (their
    // size is 0), we fall back to Slot::Count so the highlight is
    // suppressed rather than pinned to a stale zero-size region.
    const math::FVector2 sz = getSize();
    if (sz.x <= 0.0f || sz.y <= 0.0f) {
        return Slot::Count;
    }
    const float w = sz.x;
    const float h = sz.y;

    // Drop targeting uses configured weights so empty slots remain
    // valid dock targets even when performLayout collapses them.
    const float wtTop    = effectiveWeight(*this, Slot::Top);
    const float wtBottom = effectiveWeight(*this, Slot::Bottom);
    const float wtLeft   = effectiveWeight(*this, Slot::Left);
    const float wtRight  = effectiveWeight(*this, Slot::Right);
    const float wtCenter = effectiveWeight(*this, Slot::Center);

    const float sumVert = wtTop + wtBottom + 1.0f;
    const float topH    = h * (wtTop    / sumVert);
    const float botH    = h * (wtBottom / sumVert);
    const float midY    = topH;
    const float midH    = h - topH - botH;

    const float sumHoriz = wtLeft + wtCenter + wtRight;
    const float leftW  = (sumHoriz > 0.0f) ? w * (wtLeft   / sumHoriz) : 0.0f;
    const float rightW = (sumHoriz > 0.0f) ? w * (wtRight  / sumHoriz) : 0.0f;
    const float centerW = w - leftW - rightW;

    // Test Top / Bottom first (full-width strips).
    if (worldPos.y < midY) {
        return Slot::Top;
    }
    if (worldPos.y >= midY + midH) {
        return Slot::Bottom;
    }
    // Inside the middle row — split horizontally.
    if (worldPos.x < leftW) {
        return Slot::Left;
    }
    if (worldPos.x >= leftW + centerW) {
        return Slot::Right;
    }
    return Slot::Center;
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

} // namespace ayt::ui