#pragma once

#include "AYLayoutLoader.h"
#include "AYIRenderBackend.h"
#include "UIKeyCode.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ayt::ui {

class UIManager {
public:
    UIManager() = default;
    // RAII: destructor delegates to shutdown() so factory-allocated widget
    // trees are released via destroyWidgetTree even if the caller forgets
    // to invoke shutdown() explicitly. Idempotent — shutdown() guards
    // itself with _shutdown.
    ~UIManager() { shutdown(); }

    // Returns the UIManager most recently activated by initialize().
    // ComboBox / Menu / Tooltip call this to mount popups on the same
    // instance the host constructed — NOT a hidden process-wide singleton
    // (that caused overlay mounts to miss stack-local test/editor instances
    // and left dangling popups on a never-shut-down static UIManager).
    static UIManager& get();

    void initialize(IRenderBackend* backend);
    void shutdown();

    bool loadLayout(const std::string& path);
    bool loadFromString(const std::string& json);
    void bindEvent(const std::string& widgetId, const std::string& eventType,
                   std::function<void()> handler);

    void setClientSize(float width, float height);
    void update(float dt);
    void layout();
    void render();

    Widget* root() const { return _root; }
    Widget* findById(const std::string& id) const;

    // =====================================================================
    // Phase A — PopupLayer (S1) + viewport (S5)
    // =====================================================================
    // _overlayRoot is a sibling of _root, hosts floating widgets (ComboBox
    // popup / Menu / Tooltip / future ContextMenu) so they are NOT clipped
    // by the host's enclosing layout. Rendered + hit-tested AFTER _root
    // so popups overlay any host widget.
    Widget* getOverlayRoot() const { return _overlayRoot; }
    math::FVector2 getClientSize() const {
        return math::FVector2(_clientWidth, _clientHeight);
    }

    // =====================================================================
    // Phase A — DropdownManager (S2): PopupLayer API.
    // =====================================================================
    // openPopup reparents `popup` onto the overlay root and tracks it as
    // the active dropdown. If a different dropdown is already active,
    // it is closed first (single-open-popup invariant).
    //
    // closePopup removes the popup from the overlay, frees it via
    // destroyWidgetTree, and clears _activeDropdown. If `_capturedWidget`
    // points inside the popup's subtree it is reset to nullptr so the
    // next event doesn't dereference freed memory (R-3 analogy).
    //
    // Ownership: caller `new`s the popup and never deletes it directly
    // once openPopup has been called. UIManager owns lifetime end-to-end.
    // Caller DOES NOT need to track the popup — getOverlayRoot() lets
    // tests / hosts verify placement if they care.
    void openPopup(Widget* anchor, Widget* popup);
    // closePopup removes the popup from the overlay. If `destroy` is true
    // (default), the popup tree is freed via destroyWidgetTree and any
    // ComboBox anchor is notified via onPopupDismissedByManager. Pass
    // destroy=false to unmount only — required when ComboBox dismisses
    // from inside a ListView selection callback (destroy would UAF).
    void closePopup(Widget* popup, bool destroy = true);

    // True if `widget` is `ancestor` or any descendant of `ancestor`.
    // Walks the parent chain. Used by closePopup to null _capturedWidget
    // if it points into the popup being closed.
    static bool isDescendantOf(Widget* widget, Widget* ancestor);

    // Phase A: overlay-first hit-test funnel. Picks _overlayRoot first
    // (popups), falls back to _root. Empty overlay is O(1). Internal —
    // not part of the public API.
    Widget* pickTopmostWidget(const math::FVector2& worldPos);

    bool onMouseMove(float x, float y);
    bool onMouseButtonDown(float x, float y, int button);
    bool onMouseButtonUp(float x, float y, int button);
    void onMouseLeave();
    void clearHover();

    // C-3 focus + keyboard routing. setFocus replaces the currently
    // focused widget (if any) with the new one; pass nullptr to drop
    // focus. onKeyDown / onKeyUp route to the focused widget if it
    // exists and is visible / not destroyed. onTextInput routes typed
    // characters to the focused widget. Returns true if the event was
    // consumed.
    void  setFocus(Widget* widget);
    Widget* getFocusedWidget() const { return _focusedWidget; }
    bool  onKeyDown(int keyCode);
    bool  onKeyUp(int keyCode);
    bool  onTextInput(wchar_t ch);

    // =====================================================================
    // Phase B — S3 keyboard navigation
    // =====================================================================
    // focusNext/focusPrev walk the focusable widgets under the current
    // focus root (Phase A overlay-aware — see implementation). They are
    // also called by onKeyDown when Tab/Shift+Tab arrives. Manual
    // callers (e.g. tests, host app) can invoke them directly.
    //
    // Modifier state is tracked internally so Shift+Tab is distinguished
    // from Tab. Widgets never see modifier keyCodes — Shift/Ctrl/Alt are
    // intercepted in onKeyDown/onKeyUp and update the bitmask instead.
    void focusNext();
    void focusPrev();
    uint32_t getModifiers() const { return _modifiers; }

    // AYDevice bridge — host wiring point. Translates
    // AYDevice::KeyCode -> UIKeyCode via fromDeviceKey() then forwards
    // to onKeyDown/onKeyUp. Phase B adds this; integration with
    // AYWindowManager (calling these from WM_KEYDOWN) is a follow-up.
    bool onDeviceKeyDown(::ayt::device::KeyCode kc);
    bool onDeviceKeyUp(::ayt::device::KeyCode kc);

    // =====================================================================
    // Phase C (S4) — AYDevice character + IME composition bridge.
    // =====================================================================
    //
    // AYDevice's TextInput accumulator (AYDevice/include/AYTextInput.h:36-55)
    // delivers two streams of UTF-8 chunks:
    //
    //   - committed text (WM_CHAR / IME result): arrives via onCommit
    //     callbacks. We translate into per-codepoint onTextInput(wchar_t)
    //     calls on the focused widget. BMP codepoints are single calls;
    //     supplementary-plane codepoints (U+10000..U+10FFFF, e.g. some
    //     CJK extensions) arrive as TWO onTextInput calls — first the
    //     high surrogate half, then the low half. TextInput's replaceRange
    //     concatenates them (R4 in the Phase C plan).
    //
    //   - in-progress IME composition (WM_IME_COMPOSITION): arrives via
    //     onCompositionUpdate. We map the AYDevice "empty sentinel" onto
    //     an explicit 3-state contract on the AYUI side:
    //
    //       onDeviceCompositionStart(text, caret)
    //           — first non-empty preview. Sets _compositionOwner, routes
    //             to widget's onImeCompositionStart.
    //       onDeviceCompositionUpdate(text, caret)
    //           — replaces preview; routes to onImeCompositionUpdate.
    //           — If no Start has been seen yet, promote to Start (some
    //             Linux IME hosts skip the Start event entirely).
    //       onDeviceCompositionEnd(committed)
    //           — committed text arrived. Routes to onImeCompositionEnd.
    //           — If `committed` is empty, no onTextInput re-pump (Linux
    //             IBuses already deliver the committed chars separately
    //             through onDeviceChar).
    //           — Clears _compositionOwner.
    //
    // cancelComposition(owner) is called by ~TextInput / ~TextArea::
    // TextDocument before destruction to ensure UIManager doesn't hold a
    // dangling _compositionOwner pointer (Q3 — R1). Idempotent.
    bool onDeviceChar(const char* utf8, int byteCount);
    void onDeviceCompositionStart(const std::string& text, int caret);
    void onDeviceCompositionUpdate(const std::string& text, int caret);
    void onDeviceCompositionEnd(const std::string& committed);
    void cancelComposition(Widget* owner, bool fireEndOnOwner = true);

    // =====================================================================
    // Phase C (S4) — text-editing focus gate.
    // =====================================================================
    // setFocus emits this callback whenever the focused widget's
    // isTextEditingWidget() status flips. Hosts wire this to
    // AYDevice::TextInput::setEnabled so game keybinds don't fire while
    // the user is typing into a form. Default-constructed (no-op) so
    // existing tests that don't care about IME don't need to wire it.
    std::function<void(bool)> onTextEditingFocusChanged;

    bool isHoverInteractive() const;
    UiCursorHint getCursorHint() const;
    bool isCapturing() const { return _capturedWidget != nullptr; }
    void cancelCapture();

    UILayoutLoader& loader() { return _loader; }
    IRenderBackend* backend() const { return _backend; }

private:
    IRenderBackend* _backend = nullptr;
    Widget* _root = nullptr;
    // Phase A: popup overlay layer (sibling of _root). Spawned in
    // initialize(), sized by setClientSize, torn down in shutdown().
    // Popups mounted via the PopupLayer API will be reparented here.
    Widget* _overlayRoot = nullptr;
    UILayoutLoader _loader;
    float _clientWidth = 1280.0f;
    float _clientHeight = 720.0f;
    Widget* _capturedWidget = nullptr;
    Widget* _hoverWidget = nullptr;
    Widget* _focusedWidget = nullptr;
    bool _shutdown = false;

    // DropdownManager: at most one "active dropdown" at a time. Opening
    // a new popup closes the previous one. `_activeDropdownAnchor` lets
    // the click-outside detector distinguish "click in the anchor's own
    // area" (don't close) from "click anywhere else" (close). Without
    // anchor tracking, a click on the ComboBox's own main area would
    // be misclassified as click-outside and the popup would flicker
    // close-then-reopen in the same frame.
    Widget* _activeDropdown = nullptr;
    Widget* _activeDropdownAnchor = nullptr;
    // True when `_activeDropdownAnchor` was a ComboBox at openPopup time.
    // tearDownOverlayChildren must NOT dynamic_cast the anchor — the
    // Tooltip path can free the Button anchor before shutdown, and RTTI
    // on a freed object AVs ("no RTTI data"). The flag lets us notify
    // ComboBox safely with static_cast when the ComboBox is still alive
    // (shutdown tears down overlay before the main root).
    bool _activeDropdownAnchorIsComboBox = false;
    // Phase UI-PERF-1: track the last client size we laid out against. If
    // layout() is invoked again with the same values and no explicit tree
    // mutation has occurred, skip performLayout entirely. Set to a sentinel
    // (-1, -1) after tree mutations so the next layout() always re-runs.
    float _lastLayoutWidth = -1.0f;
    float _lastLayoutHeight = -1.0f;

    // Last pointer position from onMouseMove / onMouseLeave. update()
    // re-hit-tests against this so a missed leave (hit stayed on a fat
    // splitter band, etc.) is corrected every frame before render.
    float _lastMouseX = 0.0f;
    float _lastMouseY = 0.0f;
    bool _hasLastMouse = false;

    // Destroy every popup currently mounted on `_overlayRoot`.
    // Copies the child list first — `getChildren()` returns a reference,
    // and `destroyWidgetTree` detaches (mutates `_children`) so iterating
    // the live vector would invalidate the range-for and crash.
    void tearDownOverlayChildren();

    // Phase B — S3 keyboard nav. Pre-order DFS (children-before-siblings)
    // of all focusable (dynamic_cast<FocusableWidget*> != nullptr) +
    // visible + enabled widgets under `root`. Used by focusNext/focusPrev.
    std::vector<Widget*> collectFocusablesDFS(Widget* root) const;

    // Modifier bitmask: bit 0 = Shift, bit 1 = Ctrl, bit 2 = Alt. Index
    // aligns with `UIKey_Shift`/`Control`/`Alt` minus `UIKey_Shift`.
    uint32_t _modifiers = 0;

    // =================================================================
    // Phase C (S4) — composition state.
    // =================================================================
    // `_compositionOwner` is the widget that received onImeCompositionStart
    // and has not yet seen a corresponding End. _compositionOwner may
    // become dangling if the widget is destroyed without calling
    // cancelComposition — the dtor path (PR-2) handles that case. As an
    // extra safety net we null the slot before destruction in cancelComposition.
    Widget* _compositionOwner = nullptr;
    bool    _composing = false;

    // Phase C helper: true if `w` is a text-editing widget per the
    // isTextEditingWidget() virtual. Used by setFocus to drive the
    // onTextEditingFocusChanged callback gate. nullptr returns false.
    static bool isTextEditing(Widget* w);
};

} // namespace ayt::ui
