#pragma once

#include "AYCompoundFocusableWidget.h"
#include "AYBox.h"
#include "AYDockOverlay.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::ui {

class DockCard;

// D1: DockArea - the editor-shell root that hosts named slots
// (Left/Right/Top/Bottom/Center) plus a DockOverlay for floating cards.
//
// Each slot owns a VBox (Left/Right stack vertically; Top/Bottom stack
// horizontally; Center holds the main view). Cards added to a slot are
// pushed into that slot's VBox in insertion order; the existing layout
// engine then sizes them with fill-style stretching.
//
// D1 ships the slot tree, addCard / removeCard / findCard, and overlay
// ownership. Tear-off UX is D3; persistence is D4.
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

    DockArea() {
        // The overlay is always present and a child of DockArea. Build
        // it first so the rest of the constructor can call into it.
        _overlay = new DockOverlay();
        addChild(_overlay);

        // Create one VBox / HBox per slot. Left + Right are vertical
        // stacks (VBox), Top + Bottom are horizontal strips (HBox),
        // Center is a plain Widget that hosts a single large child.
        // The containers are heap-allocated AND attached as children
        // so destroyWidgetTree(Widget*) tears them down — we never
        // own them via unique_ptr.
        _slotContainers[(int)Slot::Left]  = new VBox();
        _slotContainers[(int)Slot::Right] = new VBox();
        _slotContainers[(int)Slot::Top]   = new HBox();
        _slotContainers[(int)Slot::Bottom]= new HBox();
        _slotContainers[(int)Slot::Center]= new Widget();

        for (int i = 0; i < (int)Slot::Count; ++i) {
            if (_slotContainers[i]) {
                addChild(_slotContainers[i]);
            }
        }
    }

    // Frees overlay + slot containers + docked cards (heap children).
    // Stack DockArea must NOT be passed to destroyWidgetTree — that would
    // `delete` the stack object. Rely on this dtor (or heap+destroyWidgetTree).
    ~DockArea() override;

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

    // Slot min size (logical pixels). Default 0 for all.
    void setSlotMinSize(Slot slot, float minSize) {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return;
        _slotMinSize[(int)slot] = minSize;
    }
    float getSlotMinSize(Slot slot) const { return _slotMinSize[(int)slot]; }

    // Add a card to a named slot. DockArea takes ownership of `card`.
    // Adding a card with the same id as an existing card replaces the old
    // card (mirroring the Loader's "last write wins" rule).
    void addCard(Slot slot, std::unique_ptr<DockCard> card);

    // Remove a card by id. Returns true if a card was removed.
    bool removeCard(const std::string& cardId);

    // Find a card anywhere (slots or floating overlay) by id. Returns
    // nullptr if not found.
    DockCard* findCard(const std::string& cardId) const;

    // Cards currently docked in a slot, in insertion order.
    size_t getCardCount(Slot slot) const { return _slotCards[(int)slot].size(); }
    DockCard* getCard(Slot slot, size_t index) const;

    // Convenience: turn a string id into a Slot enum. Returns false if
    // the string is not a valid slot name.
    static bool parseSlot(const std::string& name, Slot& outSlot);

    // The overlay that owns floating cards. Always non-null after ctor.
    DockOverlay* getOverlay() const { return _overlay; }

    void onRender(IRenderBackend& renderer) override;
    void performLayout() override;

protected:
    // Mirrors CompoundFocusableWidget hooks. Default empty.
    void onChildAdded(Widget* child);
    void onChildRemoved(Widget* child);

private:
    // The VBox / HBox for each slot. Center is a child Widget (no VBox
    // wrapper) because Center typically hosts a single large panel
    // (viewport) rather than a stack.
    //
    // Raw pointers (not unique_ptr) on purpose: every entry is also
    // attached as a child of DockArea via addChild, so destroyWidgetTree
    // frees them when DockArea dies. Holding them via unique_ptr would
    // double-delete (once by destroyWidgetTree, once by ~unique_ptr).
    Widget* _slotContainers[(int)Slot::Count] = {};   // {} zero-initialises the whole array (NOT just [0])
    float _slotWeight[(int)Slot::Count] = {0.20f, 0.25f, 0.15f, 0.20f, 0.55f};
    float _slotMinSize[(int)Slot::Count] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<DockCard*> _slotCards[(int)Slot::Count] = {};  // {} forces element-wise default-ctor

    // id -> card (across all slots + overlay). Ownership stays in
    // _slotCards / overlay.
    std::unordered_map<std::string, DockCard*> _cardIndex;

    // Floating overlay (always present, child of DockArea). Raw pointer
    // because lifetime is owned by the children tree; we don't want a
    // unique_ptr here (it would need DockOverlay's complete type at the
    // point DockArea's dtor is implicit-synthesised).
    DockOverlay* _overlay = nullptr;

    void tearDownSlots();
};

} // namespace ayt::ui