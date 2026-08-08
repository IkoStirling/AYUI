#include "AYDockArea.h"
#include "AYDockCard.h"
#include "AYDockOverlay.h"
#include "AYBox.h"
#include "IAYRenderBackend.h"
#include "AYUIManager.h"
#include "AYDockTrace.h"

#include <algorithm>
#include <string>
#include <typeinfo>
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
    if (DockCard* fromData = static_cast<DockCard*>(ui->getDragPayload().data)) {
        if (!fromData->getId().empty() && area.findCard(fromData->getId()) != nullptr) {
            return true;
        }
    }
    if (DockCard* fromSrc = dynamic_cast<DockCard*>(ui->getDragSource())) {
        if (!fromSrc->getId().empty() && area.findCard(fromSrc->getId()) != nullptr) {
            return true;
        }
    }
    return false;
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

// PR-Container-Shared-Contract: shared 6-value arithmetic used by
// performLayout() and hitTestSlot(). Weights order: Top, Bottom, Left,
// Right, Center. Call sites still choose their own weight source
// (layoutSlotWeight vs effectiveWeight — the difference is
// intentional, see comment at hitTestSlot line 501-502 above).
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

DockArea::DockArea() {
    // The overlay is always present and a child of DockArea. Build
    // it first so the rest of the constructor can call into it.
    _overlay = new DockOverlay();
    addChild(_overlay);

    // Left/Right = VBox, Top/Bottom = HBox, Center = CompoundWidget
    // (hitTest descends; NOT VBox — VBox reflows card to content height).
    _slotContainers[(int)Slot::Left]   = new VBox();
    _slotContainers[(int)Slot::Right]  = new VBox();
    _slotContainers[(int)Slot::Top]    = new HBox();
    _slotContainers[(int)Slot::Bottom] = new HBox();
    _slotContainers[(int)Slot::Center] = new CompoundWidget();

    for (int i = 0; i < (int)Slot::Count; ++i) {
        if (_slotContainers[i]) {
            if (auto* box = dynamic_cast<BoxBase*>(_slotContainers[i])) {
                box->setPadding(0.0f, 0.0f, 0.0f, 0.0f);
                box->setSpacing(0.0f);
            }
            addChild(_slotContainers[i]);
        }
    }

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
        const Slot targetSlot = hitTestSlot(dropPos);
        const bool isDocked = (_cardIndex.find(cardId) != _cardIndex.end());
        const bool sameSlot = isDocked && isCardInSlot(card, targetSlot);

        const char* fromSlot = "?";
        for (int i = 0; i < (int)Slot::Count; ++i) {
            if (isCardInSlot(card, static_cast<Slot>(i))) {
                fromSlot = dockSlotName(i);
                break;
            }
        }

        dockTrace(
            "[dock] onDrop dock=%s card=%s from=%s target=%s "
            "pos=(%.1f,%.1f) docked=%d sameSlot=%d floatCount=%zu "
            "counts L=%zu C=%zu R=%zu\n",
            getId().c_str(), cardId.c_str(), fromSlot,
            dockSlotName((int)targetSlot), dropPos.x, dropPos.y,
            isDocked ? 1 : 0, sameSlot ? 1 : 0,
            _overlay ? _overlay->getFloatingCardCount() : 0u,
            getCardCount(Slot::Left), getCardCount(Slot::Center),
            getCardCount(Slot::Right));

        if (isDocked) {
            if (targetSlot != Slot::Count) {
                // K-INV-D3-1: same-slot → no-op. Never floatCard here.
                if (!sameSlot) {
                    dockTrace("[dock] onDrop -> moveInSlot %s -> %s\n",
                              fromSlot, dockSlotName((int)targetSlot));
                    moveInSlot(cardId, targetSlot);
                } else {
                    dockTrace("[dock] onDrop -> same-slot NO-OP\n");
                }
            } else {
                dockTrace("[dock] onDrop docked but target=Count (ignored)\n");
            }
        } else if (targetSlot != Slot::Count) {
            dockTrace("[dock] onDrop -> dockCard into %s\n",
                      dockSlotName((int)targetSlot));
            dockCard(cardId, targetSlot);
        } else if (hitTestOverlay(dropPos)) {
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

    dockTrace("[dock] DockArea ctor cpp-body center=CompoundWidget\n");
}

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

    // Push the card into the slot's container (all five are BoxBase:
    // Left/Right/Center = VBox, Top/Bottom = HBox).
    DockCard* raw = card.release();
    Widget* container = _slotContainers[(int)slot];
    if (container) {
        if (auto* vbox = dynamic_cast<VBox*>(container)) {
            vbox->addWidget(raw);
        } else if (auto* hbox = dynamic_cast<HBox*>(container)) {
            hbox->addWidget(raw);
        } else {
            container->addChild(raw);
        }
    } else {
        addChild(raw);
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
    // Slot/drop highlight is painted in render() AFTER children so
    // opaque cards cannot cover it. onRender stays a no-op for chrome.
    AYUNREFERENCED_PARAM(renderer);
}

void DockArea::paintDropGuide(IRenderBackend& renderer) {
    // Only while a DockCard is being dragged, and only for the slot under
    // the cursor — idle / foreign payloads paint nothing.
    UIManager* ui = UIManager::tryGet();
    if (!isDockCardDrag(ui)) {
        return;
    }
    const Slot hover = hitTestSlot(ui->getDragLastMousePos());
    if (hover == Slot::Count) {
        return;
    }

    const math::FRectangle r = getSlotRect(hover);
    if (r.maxX - r.minX < 2.0f || r.maxY - r.minY < 2.0f) {
        return;
    }

    // Slot tint (low alpha so cards underneath stay readable).
    math::FVector4 fill(0.25f, 0.55f, 0.95f, 0.28f);
    switch (hover) {
        case Slot::Left:   fill = math::FVector4(0.90f, 0.25f, 0.20f, 0.28f); break;
        case Slot::Right:  fill = math::FVector4(0.20f, 0.40f, 0.95f, 0.28f); break;
        case Slot::Center: fill = math::FVector4(0.20f, 0.75f, 0.35f, 0.28f); break;
        case Slot::Top:    fill = math::FVector4(0.95f, 0.75f, 0.15f, 0.28f); break;
        case Slot::Bottom: fill = math::FVector4(0.75f, 0.30f, 0.85f, 0.28f); break;
        default: break;
    }
    renderer.drawRect(r, fill);
    renderer.drawBorderRect(r, math::FVector4(fill.x, fill.y, fill.z, 0.85f),
                            2.0f, 0.0f);
    renderer.drawText(r, slotLabel(hover), 14,
                      math::FVector4(1.0f, 1.0f, 1.0f, 0.75f));
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
    // First descend into children so the overlay + slot containers
    // get a layout pass.
    compoundDescendLayout(this);

    // Then split the DockArea's bounds into 5 regions according to the
    // slot weights. Order: Top strip (full width), Bottom strip (full
    // width), then Left + Center + Right columns inside the middle row.
    const math::FVector2 sz = getSize();
    const float w = sz.x;
    const float h = sz.y;

    // Same weight source as hitTestSlot / getSlotRect (effectiveWeight).
    // Collapsing empty slots via layoutSlotWeight made visible cards fill
    // neighboring hit bands (Left card painted under Top's hit strip →
    // own-zone drop retargeted to Top). Keep configured bands so layout,
    // hit-test, and the drop guide stay locked.
    const float layoutWeights[5] = {
        effectiveWeight(*this, Slot::Top),
        effectiveWeight(*this, Slot::Bottom),
        effectiveWeight(*this, Slot::Left),
        effectiveWeight(*this, Slot::Right),
        effectiveWeight(*this, Slot::Center),
    };
    const SlotRegions r = computeSlotRegions(w, h, layoutWeights);
    const float topH    = r.topH;
    const float botH    = r.botH;
    const float midY    = r.midY;
    const float midH    = r.midH;
    const float leftW   = r.leftW;
    const float rightW  = r.rightW;
    const float centerW = r.centerW;

    if (Widget* top = _slotContainers[(int)Slot::Top]) {
        top->setPosition(math::FVector2(0.0f, 0.0f));
        top->setSize(math::FVector2(w, topH));
    }
    if (Widget* bot = _slotContainers[(int)Slot::Bottom]) {
        bot->setPosition(math::FVector2(0.0f, midY + midH));
        bot->setSize(math::FVector2(w, botH));
    }

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
        // Full-bleed the card AFTER any compoundDescendLayout pass —
        // Center is CompoundWidget (not VBox) so it won't crush this size.
        if (centerW > 0.0f && midH > 0.0f) {
            fillContainerChildren(center, centerW, midH);
        }
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

    // Occasional layout dump for Gallery mini_dock (every ~60 layouts).
    if (getId() == "mini_dock") {
        static int s_layoutLog = 0;
        if ((s_layoutLog++ % 60) == 0) {
            Widget* center = _slotContainers[(int)Slot::Center];
            const char* centerTy = "null";
            if (center) {
                if (dynamic_cast<VBox*>(center)) centerTy = "VBox";
                else if (dynamic_cast<CompoundWidget*>(center) &&
                         !dynamic_cast<VBox*>(center) &&
                         !dynamic_cast<HBox*>(center))
                    centerTy = "CompoundWidget";
                else if (dynamic_cast<Widget*>(center) &&
                         !dynamic_cast<CompoundWidget*>(center))
                    centerTy = "Widget";
                else centerTy = typeid(*center).name();
            }
            const math::FRectangle wb = getWorldBounds();
            dockTrace(
                "[dock] layout mini_dock world=(%.0f,%.0f)-(%.0f,%.0f) "
                "centerType=%s sizes L=%.0fx%.0f C=%.0fx%.0f R=%.0fx%.0f "
                "cards L=%zu C=%zu R=%zu float=%zu\n",
                wb.minX, wb.minY, wb.maxX, wb.maxY, centerTy,
                leftW, midH, centerW, midH, rightW, midH,
                getCardCount(Slot::Left), getCardCount(Slot::Center),
                getCardCount(Slot::Right),
                _overlay ? _overlay->getFloatingCardCount() : 0u);
            for (int si = 0; si < (int)Slot::Count; ++si) {
                for (size_t ci = 0; ci < _slotCards[si].size(); ++ci) {
                    DockCard* c = _slotCards[si][ci];
                    if (!c) continue;
                    const math::FRectangle cb = c->getWorldBounds();
                    dockTrace(
                        "[dock]   card '%s' in %s bounds=(%.0f,%.0f)-(%.0f,%.0f) "
                        "size=%.0fx%.0f floatable=%d\n",
                        c->getId().c_str(), dockSlotName(si),
                        cb.minX, cb.minY, cb.maxX, cb.maxY,
                        c->getSize().x, c->getSize().y,
                        c->isFloatable() ? 1 : 0);
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
    dockTrace("[dock] floatCard OK card=%s fromSlot=%s floatCount=%zu\n",
              cardId.c_str(), dockSlotName(oldSlotIdx),
              _overlay->getFloatingCardCount());
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

    // Occupied target: float prior occupants instead of stacking
    // (parity with moveInSlot's swap — floating inbound has no vacated
    // slot to swap into).
    if (!_slotCards[(int)target].empty()) {
        const math::FRectangle tr = getSlotRect(target);
        const math::FVector2 floatPos(tr.minX + 24.0f, tr.minY + 24.0f);
        std::vector<std::string> displaceIds;
        for (DockCard* c : _slotCards[(int)target]) {
            if (c && !c->getId().empty()) {
                displaceIds.push_back(c->getId());
            }
        }
        for (const std::string& id : displaceIds) {
            floatCard(id, floatPos);
        }
    }

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
        dockTrace("[dock] moveInSlot same-slot no-op card=%s\n", cardId.c_str());
        return false;
    }

    dockTrace("[dock] moveInSlot %s -> %s; targetOccupied=%zu\n",
              dockSlotName(oldSlotIdx), dockSlotName((int)target),
              _slotCards[(int)target].size());

    auto detachCardFromContainer = [](DockCard* c) {
        Widget* parent = c->getParent();
        if (auto* vbox = dynamic_cast<VBox*>(parent)) {
            vbox->removeWidget(c);
        } else if (auto* hbox = dynamic_cast<HBox*>(parent)) {
            hbox->removeWidget(c);
        } else if (parent != nullptr) {
            c->detachFromParent();
        }
    };

    // Detach from the old slot's container WITHOUT freeing. Reuse the
    // same dynamic_cast cascade as floatCard.
    detachCardFromContainer(card);

    // Drop the old-slot entry from _slotCards; _cardIndex stays valid
    // (the card pointer didn't change).
    _slotCards[oldSlotIdx].erase(
        std::remove(_slotCards[oldSlotIdx].begin(),
                    _slotCards[oldSlotIdx].end(), card),
        _slotCards[oldSlotIdx].end());

    // Occupied target → swap: move prior occupants into the vacated
    // slot. Side slots used to VBox-stack (Gallery Left+Right piled on
    // one side); Center stacked full-bleed siblings. Swap keeps one
    // card band per side and preserves draggable title bars.
    if (!_slotCards[(int)target].empty()) {
        std::vector<DockCard*> displaced = _slotCards[(int)target];
        _slotCards[(int)target].clear();
        for (DockCard* d : displaced) {
            if (d == nullptr || d == card) {
                continue;
            }
            dockTrace("[dock] moveInSlot SWAP displace '%s' -> %s\n",
                      d->getId().c_str(), dockSlotName(oldSlotIdx));
            detachCardFromContainer(d);
            addCard(static_cast<Slot>(oldSlotIdx),
                    std::unique_ptr<DockCard>(d));
        }
    }

    // Re-attach under the new slot's container. addCard's
    // removeCard-first safety net (last-write-wins) is a no-op here
    // because the card pointer didn't change; the id-keyed
    // _cardIndex lookup matches this card.
    addCard(target, std::unique_ptr<DockCard>(card));

    _hoveredSlot = Slot::Count;
    _hoveredOverlay = false;
    markBoundsDirty();
    requestRelayout();
    dockTrace(
        "[dock] moveInSlot DONE counts L=%zu C=%zu R=%zu\n",
        getCardCount(Slot::Left), getCardCount(Slot::Center),
        getCardCount(Slot::Right));
    return true;
}

DockArea::Slot DockArea::hitTestSlot(const math::FVector2& worldPos) const {
    // Same weight source as performLayout / getSlotRect (effectiveWeight).
    // Regions are DockArea-local; mouse is world (Gallery nests under a
    // padded VBox — local-only hits pinned every drop under Center).
    const math::FRectangle wb = getWorldBounds();
    if (wb.maxX - wb.minX <= 0.0f || wb.maxY - wb.minY <= 0.0f) {
        return Slot::Count;
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

} // namespace ayt::ui