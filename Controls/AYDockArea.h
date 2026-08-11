#pragma once

#include "AYCompoundFocusableWidget.h"
#include "AYBox.h"
#include "AYDockOverlay.h"
#include "AYUIManager.h"
#include "AYDockCard.h"
#include "AYDockTrace.h"
#include "AYDockTabGroup.h"
// Provides the `ayt::ui::json` alias used by the Phase-4 persistence
// helpers (serializeNode / buildNodeFromJson signatures).
#include "AYWidgetSerializer.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::ui {

// DockArea - the editor-shell root that hosts the dock tree plus a
// DockOverlay for floating cards.
//
// The dock tree IS the AYUI widget tree: children are
// [root split node, DockOverlay]. The root is a VBox{HBox} split node
// with SplitterHandles between panels; panels are DockTabGroup leaves
// (the 5 legacy slots are pinned leaves whose leafId = slot name and
// which are never pruned). Nested splits created by edge drops are
// future phases. Drop hit-testing / guides follow live leaf geometry
// after the template is laid out so splitter-resized panels stay
// adaptive placement targets.
//
// The 5-slot legacy API (addCard(Slot)/getCardCount(Slot)/adoptCard/
// moveInSlot/...) is preserved verbatim and maps onto the pinned
// leaves, so hosts and tests keep working unchanged.
class DockArea : public CompoundFocusableWidget {
public:
    enum class Slot {
        Left,
        Right,
        Top,
        Bottom,
        Center,
        Count
    };

    // Phase-3 drop placement within a tree leaf: Join = merge as a tab;
    // the four edge zones create a nested split at that side.
    enum class TreeDropZone {
        Join,
        West,
        East,
        North,
        South,
    };

    // Ctor lives in AYDockArea.cpp — MUST NOT be inline in this header.
    // Gallery constructs DockArea via WidgetFactory (AYWidgetFactory.cpp);
    // an inline ctor meant drop/Center fixes only applied when that TU
    // was rebuilt, which is why logs still showed centerType=Widget and
    // same-slot floatCard with no onDrop trace.
    DockArea();

public:

    // Frees overlay + slot containers + docked cards (heap children).
    // Stack DockArea must NOT be passed to destroyWidgetTree — that would
    // `delete` the stack object. Rely on this dtor (or heap+destroyWidgetTree).
    ~DockArea() override;

    // D3 internal — used by the ctor's onDrop lambda to enforce
    // K-INV-D3-1 (same-slot no-op). Returns true if `card` lives in
    // `slot`'s pinned leaf. Inline because the lambda body needs it.
    bool isCardInSlot(const DockCard* card, Slot slot) const {
        if (card == nullptr) return false;
        const int s = (int)slot;
        if (s < 0 || s >= (int)Slot::Count) return false;
        const DockTabGroup* leaf = _rootLeaves[s];
        return leaf != nullptr && leaf->containsCard(card);
    }

    // Slot weight (0..1) used by the layout pass for relative sizing.
    // Weights of 0 fall back to a hard-coded sensible default
    // (Left=0.20 / Right=0.25 / Top=0.15 / Bottom=0.20 / Center=0.55).
    void setSlotWeight(Slot slot, float weight) {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return;
        _slotWeight[(int)slot] = weight;
    }
    float getSlotWeight(Slot slot) const {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return 0.0f;
        return _slotWeight[(int)slot];
    }

    // Slot min size along the template main axis (logical px).
    // 0 = use BoxBase::kMinPanelSize when limits are applied to the tree.
    void setSlotMinSize(Slot slot, float minSize) {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return;
        _slotMinSize[(int)slot] = minSize;
        applyPinnedSlotMinLimits();
    }
    float getSlotMinSize(Slot slot) const {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return 0.0f;
        return _slotMinSize[(int)slot];
    }
    // Resolved floor used by template geometry + Box slot limits.
    float effectiveSlotMinSize(Slot slot) const;

    // Add a card to a named slot. DockArea takes ownership of `card`.
    // Adding a card with the same id as an existing card replaces the old
    // card (mirroring the Loader's "last write wins" rule).
    // NOTE: does NOT displace prior occupants — callers that need
    // single-occupant bands (redock / dockCard) must use adoptCard.
    void addCard(Slot slot, std::unique_ptr<DockCard> card);

    // Insert an external / floating card into `slot`, taking ownership.
    // Joins as a new tab when the leaf already has cards (Phase-3
    // DockTabGroup). Used by Gallery/Editor child-window redock and
    // dockCard. Prefer `redockAt` when a world position is known so the
    // drop can target split leaves / edge zones.
    bool adoptCard(Slot slot, DockCard* card);

    // Child-window / external redock at a world point. Prefers Phase-3
    // tree join/split under the cursor; falls back to pinned-slot
    // adoptCard so collapsed side bands can still revive.
    bool redockAt(DockCard* card, const math::FVector2& worldPos);

    // Remove a card by id. Returns true if a card was removed.
    bool removeCard(const std::string& cardId);

    // Close a card whether docked or floating on the overlay (destroys
    // the widget tree). Used by DockCard's header close affordance.
    bool closeCard(const std::string& cardId);

    // Find a card anywhere (slots or floating overlay) by id. Returns
    // nullptr if not found.
    DockCard* findCard(const std::string& cardId) const;

    // Cards currently docked in a slot = tabs of that slot's pinned
    // leaf, in insertion order. 0 until the tree exists.
    size_t getCardCount(Slot slot) const {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return 0;
        const DockTabGroup* leaf = _rootLeaves[(int)slot];
        return leaf != nullptr ? leaf->getTabCount() : 0;
    }
    DockCard* getCard(Slot slot, size_t index) const;

    // Convenience: turn a string id into a Slot enum. Returns false if
    // the string is not a valid slot name.
    static bool parseSlot(const std::string& name, Slot& outSlot);

    // The overlay that owns floating cards. Always non-null after ctor.
    DockOverlay* getOverlay() const { return _overlay; }

    void onRender(IRenderBackend& renderer) override;
    // Paint drop highlight AFTER children so opaque DockCards cannot
    // cover the pre-slot fill (Gallery "preslot matched but blocked").
    void render(IRenderBackend& renderer) override;
    // Translucent drop-zone while a DockCard drag is active (no-op when
    // idle). Painted from DockArea::render AFTER children — do not also
    // call from the host frame loop (double-paints the Phase-3 preview).
    // External child-window redock uses setExternalDropPos + legacy slot
    // tint; dock-owned drags with a live tree paint join/split only.
    void paintDropGuide(IRenderBackend& renderer);

    // D5-redock: host-side bridge for a drag originating in a promoted
    // child window. The child's G12 drag session lives in the child
    // UIManager (never touches this tree), so isDockCardDrag(primary)
    // is false and the slot highlight must come from the host feeding
    // the cursor position (primary-client / world space) here every
    // frame. paintDropGuide then paints the slot under it. Clear with
    // clearExternalDropPos() when the drag leaves (or the child dies).
    void setExternalDropPos(const math::FVector2& pos) {
        _externalDropActive = true;
        _externalDropPos = pos;
    }
    void clearExternalDropPos() { _externalDropActive = false; }
    void performLayout() override;

    // =================================================================
    // D3 — Tear-off UX. These are the public surface that the
    // DockArea's G12 drop callbacks call. Hosts don't normally call
    // them directly; the drag/drop system invokes floatCard /
    // dockCard as a side-effect of the drop event.
    // =================================================================
    // Tear a docked card off into a floating card on the overlay.
    // Returns false if no card with `cardId` lives in any slot.
    // K-INV-D3-7: `_cardIndex` is erased here because the card leaves
    // the slot namespace.
    bool floatCard(const std::string& cardId, const math::FVector2& pos);

    // Re-dock a floating card into a named slot. Returns false if no
    // floating card with `cardId` exists. K-INV-D3-1: same-slot
    // transition is a no-op (handled at the drop callback level).
    bool dockCard(const std::string& cardId, Slot target);

    // Code-review 2026-08-02 #8: same-tree move (slot→slot) without
    // routing through the overlay. Avoids the visible flicker the
    // dockCard/floatCard pair caused on docked→docked drops (the card
    // momentarily flashed at the cursor position before snapping into
    // the new slot). Returns false if the card is not in any slot.
    bool moveInSlot(const std::string& cardId, Slot target);

    // Returns the slot whose region contains `worldPos`, or Slot::Count
    // if none. Mirrors the region math in performLayout() so the
    // drop-zone highlight stays in lockstep with the layout pass.
    Slot hitTestSlot(const math::FVector2& worldPos) const;

    // PR-Dock-SlotHighlight: the world-space rect of a slot's drop
    // region, using the SAME weights as hitTestSlot (configured
    // weights, not layout weights) so the pre-slot highlight paints
    // exactly where a drop would land. Slot::Count → empty rect.
    math::FRectangle getSlotRect(Slot slot) const;

    // True if `worldPos` is inside the overlay's bounds but not on any
    // floating card. K-INV-D3-4 (DockOverlay pass-through) requires
    // this distinction: drop on the overlay EMPTY area = float, drop
    // on a floating card = no-op (handled by DockOverlay::onDrop).
    bool hitTestOverlay(const math::FVector2& worldPos) const;

    // ---- Phase 3: dock-tree drop placement ----

    // Deepest leaf under `worldPos` (recursive tree walk). Splitter
    // handles are skipped; when the cursor sits inside a split node
    // but over no child, snaps to the nearest leaf. Returns nullptr
    // when outside the tree (or the tree is not built yet).
    DockTabGroup* hitTestTree(const math::FVector2& worldPos) const;

    // Edge-vs-join classification for a leaf: normalized edge-distance
    // argmin over the four sides with a 25% threshold; ties resolve
    // West > East > North > South (argmin keeps the first winner).
    // Empty leaves are always Join (their whole rect is a join zone).
    TreeDropZone resolveTreeDropZone(const DockTabGroup* leaf,
                                     const math::FVector2& worldPos) const;

    // Unified drop resolver used by paintDropGuide (preview) and
    // redockAt / onDrop helpers (commit): collapsed-side revive via
    // hitTestSlot first, else tree leaf+zone, else pinned slot.
    enum class DropKind { None, Tree, Slot };
    struct DropTarget {
        DropKind       kind = DropKind::None;
        DockTabGroup*  leaf = nullptr;
        TreeDropZone   zone = TreeDropZone::Join;
        Slot           slot = Slot::Count;
    };
    DropTarget resolveDropTarget(const math::FVector2& worldPos) const;

    // ---- Phase 4: dock-tree persistence ----
    //
    // Serialize the whole dock tree (structure + per-leaf tab ids +
    // active tab + floating cards with rects) to JSON:
    //   {"version":1,"dockTree":{"orientation":"V","weights":[...],
    //    "children":[leaf|split...]},"floating":[{"id":..,"x":..,
    //    "y":..,"w":..,"h":..}]}
    // Weights are each panel's slot size along the box main axis
    // (0 = fill); splitters are implicit between adjacent panels.
    // Card CONTENT is not serialized — the caller must recreate the
    // cards (e.g. Gallery loadAndWire) before applyDockTree.
    std::string serializeDockTree() const;

    // Rebuild the tree from a serializeDockTree() string. Pools every
    // existing card (docked + floating), detaches without deleting
    // (UI-OWN-1), destroys the old root subtree, rebuilds per JSON
    // (missing ids are skipped; unreferenced cards float back to the
    // overlay), restores active tabs, prunes empty split leaves, then
    // relayouts. Returns false on malformed JSON. Safe on an empty
    // DockArea (builds nothing when "dockTree" is absent).
    bool applyDockTree(const std::string& jsonStr);

protected:
    // Mirrors CompoundFocusableWidget hooks. Default empty.
    void onChildAdded(Widget* child);
    void onChildRemoved(Widget* child);

private:
    // ---- dock tree ----
    // children = [rootNode, overlay]. The root node is a VBox/HBox
    // split tree; leaves are DockTabGroups. Pinned leaves hold the 5
    // legacy slots (leafId = slot name, never pruned); split-created
    // leaves use g_N ids and prune when empty (future phases).
    //
    // Raw pointers (not unique_ptr) on purpose: every node is also
    // attached as a child of DockArea via addChild, so destroyWidgetTree
    // frees them when DockArea dies. Holding them via unique_ptr would
    // double-delete (once by destroyWidgetTree, once by ~unique_ptr).
    Widget* _rootNode = nullptr;
    DockTabGroup* _rootLeaves[(int)Slot::Count] = {};
    bool _templateBuilt = false;
    // First sync skips the pristine check (the tree may have been
    // built at size 0 — the first real-size performLayout must always
    // push the weight-derived geometry).
    bool _templateSynced = false;

    float _slotWeight[(int)Slot::Count] = {0.20f, 0.25f, 0.15f, 0.20f, 0.55f};
    float _slotMinSize[(int)Slot::Count] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // id -> card (across all slots + overlay). Ownership stays in the
    // dock tree / overlay.
    std::unordered_map<std::string, DockCard*> _cardIndex;

    // Floating overlay (always present, child of DockArea). Raw pointer
    // because lifetime is owned by the children tree; we don't want a
    // unique_ptr here (it would need DockOverlay's complete type at the
    // point DockArea's dtor is implicit-synthesised).
    DockOverlay* _overlay = nullptr;

    // D3 — render-time highlight state. Set in onDragEnter, cleared in
    // onDragLeave / onDrop. Stored as enum + bool (not Widget*) so we
    // can't hold a stale pointer to a torn-off card.
    Slot _hoveredSlot = Slot::Count;   // Slot::Count sentinel = no hover
    bool _hoveredOverlay = false;

    // D5-redock — external (child-window) drag bridge state, see
    // setExternalDropPos / clearExternalDropPos above.
    bool                 _externalDropActive = false;
    math::FVector2       _externalDropPos = math::FVector2(0.0f, 0.0f);

    // D3 — last-known cursor world position from the drag session.
    // UIManager doesn't expose lastMousePos publicly; we cache it in
    // onDragEnter for use by onDrop (overlay drop uses this position).
    math::FVector2 _dragEnterPos = math::FVector2(-1.0f, -1.0f);

    // ---- tree structure ops ----
    // Lazily build the template tree (root VBox{HBox} + pinned leaves
    // per enabled slot). Never runs twice; slot sizes start at 0 (fill)
    // and are pushed by the first syncTemplateIfPristine pass.
    void ensureRootTree();
    DockTabGroup* makePinnedLeaf(Slot slot);
    DockTabGroup* leafForSlot(Slot slot);
    DockTabGroup* findLeafOfCard(DockCard* card);
    void addTabToLeaf(DockTabGroup* leaf, DockCard* card);
    void removeTabFromLeaf(DockTabGroup* leaf, DockCard* card);
    // Side pinned leaves (Left/Right/Top/Bottom) collapse when empty so
    // the fill column expands. Center is never collapsed.
    void setSidePinnedCollapsed(DockTabGroup* leaf, bool collapsed);
    void collapseEmptySidePinnedLeaves();

    // ---- Phase 3: split / prune ----
    // Edge drop: insert a new g_N leaf beside `leaf` (25% of the
    // leaf's main-axis extent) and move `card` into it. Reuses an
    // existing adjacent splitter when one is already there. Bumps
    // _structureEpoch so the template geometry turns sticky.
    void splitLeaf(DockTabGroup* leaf, TreeDropZone zone, DockCard* card);
    // Remove empty non-pinned leaves (and their adjacent splitters)
    // until the tree is stable, unwrap single-panel nests, ensure every
    // box has a visible fill panel, and hide an empty Center. This is
    // the hole-eradication choke point after structure edits.
    void pruneEmptySplitNodes();
    DockTabGroup* hitTestTreeRec(Widget* node, const math::FVector2& p) const;

    // ---- Phase 4: tree (de)serialization helpers ----
    // Recursive writers/readers; shared builder for pinned-vs-g_N
    // leaves so applyDockTree and ensureRootTree stay in lockstep.
    void serializeNode(const Widget* node, ayt::ui::json& out) const;
    Widget* buildNodeFromJson(const ayt::ui::json& j,
                              std::unordered_map<std::string, DockCard*>& pool);
    // Pool every card the dock owns (docked tabs + floating) into
    // `pool` (id → card), detaching each WITHOUT deleting (UI-OWN-1).
    // Floating cards keep their rect; callers re-apply it on redock.
    void poolAllCards(std::unordered_map<std::string, DockCard*>& pool);

    // True when `cardId` belongs to this dock: in a slot leaf (via
    // _cardIndex) or floating in the overlay. Used by the onDrop
    // dispatch to separate tree-owned drops from EXTERNAL ones. Unlike
    // dragBelongsToDock this does NOT consult the UIManager drag state —
    // by the time onDrop fires the drag session is already tearing down,
    // so isDragging() is unreliable there.
    bool cardBelongsToThisDock(const std::string& cardId) const;

    int _splitLeafCounter = 0;   // g_N leaf-id naming
    int _structureEpoch = 0;     // >0 once any split/prune changed the
                                 // tree — template geometry turns sticky
    bool _pruning = false;       // re-entrancy guard for pruneEmptySplitNodes

    // Push weight-derived slot sizes into the root template, but only
    // while the template is still "pristine": structure untouched AND
    // every panel slot size still equals the weight-derived value.
    // Splitter drags flip that and pixels become sticky — the fill
    // slot (mid / Center) absorbs any resize.
    void syncTemplateIfPristine(const math::FVector2& size);
    void applyTemplateGeometry(const math::FVector2& size);
    bool templateSizesMatchDerived(const math::FVector2& size) const;
    // Push effectiveSlotMinSize into BoxSlotLimits on each pinned leaf
    // so SplitterHandle drags honor DockArea mins (not only the global
    // BoxBase::kMinPanelSize default).
    void applyPinnedSlotMinLimits();

    // Adaptive in-region drop: merge as a tab into the target leaf
    // without swapping / floating the prior occupant. Used when the
    // cursor is in the leaf's center zone.
    bool tabIntoSlot(const std::string& cardId, Slot target);
    bool dockCardAsTab(const std::string& cardId, Slot target);
};

// Factory entry point — lives in AYDockArea.cpp so `new DockArea()` uses
// that TU's sizeof(DockArea). REGISTER_WIDGET in AYWidgetFactory.cpp would
// bake sizeof from whatever header revision that TU last saw; a stale
// .obj after DockArea grows members silently heap-corrupts on create.
Widget* createDockAreaWidget();

} // namespace ayt::ui