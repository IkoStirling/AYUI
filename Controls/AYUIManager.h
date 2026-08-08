#pragma once

#include "AYLayoutLoader.h"
#include "IAYRenderBackend.h"
#include "UIKeyCode.h"
#include "AYDragDrop.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ayt::ui {

class MenuBar;

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

    // Non-owning: the manager currently registered by initialize(), or
    // nullptr after shutdown() / before any initialize(). Widget destructors
    // MUST prefer tryGet() over get() — get()'s static fallback must not
    // absorb stack Widget* after the real manager has shut down (batch SEGV).
    static UIManager* tryGet();

    // =================================================================
    // D5 — multi-window support: RAII singleton-swap guard.
    // =================================================================
    // When the editor spawns child top-level windows (one UIManager per
    // HWND), only ONE manager can be "active" (target of get()/tryGet()
    // and ComboBox/Menu/Tooltip popup mounts) at a time. `pushActive`
    // returns an RAII guard that swaps g_activeUIManager in its ctor and
    // restores the previous owner in its dtor. Use range-for style:
    //
    //   for (auto& entry : manager.entries()) {
    //       UIManager::ActiveScope g(entry.ui.get());
    //       entry.ui->update(dt);
    //       entry.ui->render();        // safe with _backend == nullptr
    //   }
    //
    // K-INV-D5-1: NOT thread-safe. Single-tick-thread invariant — all
    // child windows are ticked serially inside one thread, with nested
    // pushActive scopes allowed.
    class ActiveScope {
    public:
        explicit ActiveScope(UIManager* next);
        ~ActiveScope();
        ActiveScope(const ActiveScope&) = delete;
        ActiveScope& operator=(const ActiveScope&) = delete;
        ActiveScope(ActiveScope&&) = default;
        ActiveScope& operator=(ActiveScope&&) = default;
    private:
        UIManager* _prev;
        bool       _tookOwnership;
    };

    // Returns an RAII guard that swaps g_activeUIManager to `next` and
    // restores on scope exit. Manual `popActive()` is forbidden — use
    // the guard. Pair with the existing setFocus/onKeyDown/etc. APIs
    // that all read through `tryGet()`.
    static ActiveScope pushActive(UIManager* next);

    void initialize(IRenderBackend* backend);
    void shutdown();

    // True after shutdown() starts (and stays true). Widget dtors that
    // still reach tryGet() during tree teardown MUST skip setFocus /
    // virtual focus dispatch — focused / saved widgets may already be
    // partially destroyed (MenuBar::~MenuBar → Menu::close UAF).
    bool isShuttingDown() const { return _shutdown; }

    // =====================================================================
    // Polish (P3) — accelerator registry access.
    // =====================================================================
    // MenuBars register themselves via this hook in their ctor and unregister
    // in their dtor. UIManager::onKeyDown consults the registered set at the
    // top of dispatch: if any MenuBar has a MenuItem whose (mods, key) match,
    // the item's onActivate callback fires and any open menu closes BEFORE
    // the focused widget sees the key. We keep a vector rather than a single
    // pointer so hosts can host multiple MenuBars (main + context bar) and
    // both react to accelerators.
    // =====================================================================
    void registerMenuBar(class MenuBar* bar);
    void unregisterMenuBar(class MenuBar* bar);

    bool loadLayout(const std::string& path);
    bool loadFromString(const std::string& json);
    void bindEvent(const std::string& widgetId, const std::string& eventType,
                   std::function<void()> handler);

    void setClientSize(float width, float height);
    void update(float dt);
    void layout();
    // Invalidate the layout cache so the next layout() re-runs performLayout
    // even when the client size is unchanged (dock float/hide, visibility).
    void invalidateLayout();

    // AI-1 (2026-07-20): render() split into populateFrame() + flushFrame()
    // so AYRenderer's RenderPass dispatch can own the per-frame flush
    // boundary. populateFrame walks the widget tree and accumulates
    // batches on the backend (beginFrame/beginCanvas + render root +
    // overlay + drag ghost); flushFrame closes the IRenderBackend
    // lifecycle (endCanvas + endFrame). render() remains as the
    // back-compat single-call wrapper.
    //
    // The split lets AYEditor's renderCompositeFrame pipeline do:
    //   uiPass(populateFrame)  -- populate (no flush, no endFrame)
    //   Renderer::render(scene) -- dispatches [ForwardOpaque,
    //                                  Transparent, UIPass] where
    //                                  UIPass::execute calls
    //                                  backend->flushBatches() to
    //                                  submit any pending text batches
    //                                  accumulated during populate.
    //   uiPass(flushFrame)     -- close lifecycle (endCanvas +
    //                                  endFrame; endFrame flushes
    //                                  pendingRects in the backend).
    void render();
    void populateFrame();
    void flushFrame();

    Widget* root() const { return _root; }
    Widget* findById(const std::string& id) const;

    // G12 — last cursor position from the active drag session (drop target
    // resolution). Falls back to the most recent onMouseMove when idle.
    math::FVector2 getDragLastMousePos() const;
    bool isDragActive() const { return _dragSession.active; }

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
    // Soft-clear dropdown bookkeeping for `popup` without walking the
    // widget tree or calling removeChild on a possibly-dying parent.
    // Used by Menu::detachForHostDestruction during loadLayout/shutdown.
    void abandonPopup(Widget* popup);
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

    // PR-B3 — wheel routing. Hit-tests the topmost widget at (x, y),
    // then walks UP the parent chain calling onMouseWheel on each
    // ancestor until one returns true (consumed). The first TRUE wins
    // so nested scroll containers don't double-scroll — a wheel over a
    // ComboBox popup routes to the popup's ListView, NOT to an outer
    // ScrollView. Returns true iff any widget consumed the event.
    bool onMouseWheel(float x, float y, float deltaY);

    // =====================================================================
    // PR-C1 — Tooltip driver (passive hover-timer).
    // =====================================================================
    // Each call to update(dt) walks the registered tooltip list and ticks
    // every Tooltip with the current mousePos + clientSize, so a tooltip
    // attached to a target widget shows/hides based on real hover time
    // without the host wiring a per-frame driver. Hosts that want the
    // legacy behaviour can still call Tooltip::tick directly; the driver
    // is purely additive.
    //
    // registerTooltip / unregisterTooltip are wired into Tooltip::attachTo
    // and Tooltip::detach respectively (and ~Tooltip). The list is
    // non-owning — Tooltip lifetime is owned by the overlay root via
    // openPopup / closePopup (or destroyWidgetTree on the overlay).
    void registerTooltip(class Tooltip* tip);
    void unregisterTooltip(class Tooltip* tip);

    // Last mouse position from onMouseMove. Returns (0,0) if no move has
    // arrived yet (initialize() → before first cursor event). Tooltip
    // ticks use this to test `_target->getWorldBounds().contains(mousePos)`.
    math::FVector2 getMousePos() const {
        return _hasLastMouse ? math::FVector2(_lastMouseX, _lastMouseY)
                             : math::FVector2(0.0f, 0.0f);
    }
    bool hasMousePos() const { return _hasLastMouse; }

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
    // Phase D (D2) — Modal stack (S2 — opposite dismiss semantics).
    // =====================================================================
    //
    // Reuses the DropdownManager's single-active invariant (Q14): opening a
    // new modal closes the previous one. Unlike popups, modals:
    //   - Do NOT dismiss on click-outside (default — controlled by
    //     Modal::_dismissOnDimmerClick, Q8).
    //   - Trap focus inside their subtree — UIManager.onKeyDown's Tab branch
    //     routes DFS through the modal's subtree when focus is inside it.
    //   - Intercept Esc at the UIManager level (Q7) — Modal's _onClose fires
    //     from inside closeModal so UIManager doesn't need the callback.
    //
    // Host ownership mirrors ComboBox popup: the host heap-allocates the
    // Modal, setContent() / setDimmer() / setOnClose() wire it, openModal()
    // reparents onto _overlayRoot. The overlay holds the Modal as a child
    // while open; closeModal detaches. UIManager never destroys Modal —
    // destroyWidgetTree is the host's responsibility, called typically via
    // the App's "pop the dialog" code path.
    void openModal(class Modal* modal);
    // fireOnClose=true: Modal fires its _onClose callback via closeModal()'s
    // own path before returning; UIManager doesn't need to fire it again.
    // false: used by ~Modal dtor when the modal is being torn down — caller
    // doesn't want a user-visible dismiss action mid-teardown.
    void closeModal(class Modal* modal, bool fireOnClose = true);

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

    // Phase D (D2) — focus-clear hook for use during widget destruction.
    // ~FocusableWidget calls this from its body so a dangling pointer
    // never lingers in _focusedWidget. setFocus(nullptr) is NOT safe to
    // call during destruction: it does dynamic_cast<FocusableWidget*>(prev)
    // on the about-to-be-destroyed widget and invokes virtual setFocus(false)
    // — undefined behavior in C++ (R3 landmine, same pattern as Phase C
    // TextInput::~TextInput → cancelComposition(this, fireEndOnOwner=false)).
    //
    // If `candidate == _focusedWidget`, we reset to nullptr WITHOUT firing
    // any virtual dispatch. The host that owned the widget is responsible
    // for restoring focus to its previously-saved widget BEFORE the dtor
    // runs (mirrors Phase B Menu::close save-restore pattern).
    void clearFocusNoDispatch(Widget* candidate);

    // Same R3 contract as clearFocusNoDispatch: drop capture/hover if they
    // point at `candidate` without synthesizing mouse-up or other virtuals.
    void clearCaptureNoDispatch(Widget* candidate);
    void clearHoverNoDispatch(Widget* candidate);

    // =================================================================
    // G12 — Drag & Drop session lifecycle.
    // =================================================================
    // beginDrag is called BY a widget (typically from its own
    // onMouseButtonDown handler) AFTER deciding the user intent warrants
    // a cross-widget drag. beginDrag early-returns if _capturedWidget is
    // non-null (SplitterHandle / Slider / ScrollBar thumb / Window
    // title-drag / TextInput drag-select own capture — they're separate
    // channels and don't overlap with G12).
    //
    // Once active, onMouseMove routes through updateDrag → drop target
    // detection; onMouseButtonUp fires endDrag(true) which calls
    // target->onDrop; Esc fires cancelDrag() (no drop fires).
    //
    // beginDrag takes the source's stored DragPayload via getDragPayload().
    // The source widget must be in the tree (have a parent) — beginDrag
    // rejects orphan sources so the ghost can render against an attached
    // widget's coordinate space.
    bool beginDrag(Widget* source);
    void updateDrag(float x, float y);
    bool endDrag(bool accepted = true);
    void cancelDrag();

    bool        isDragging() const             { return _dragSession.active; }
    const DragPayload& getDragPayload() const  { return _dragSession.payload; }
    Widget*     getDragSource() const          { return _dragSession.source; }
    Widget*     getCurrentDropTarget() const   { return _dragSession.currentTarget; }
    // Valid during onDragEnd (and until the next beginDrag): true if the
    // just-ended drag had an accepting drop target. DockCard uses this so
    // void-drop promote does not re-float a card that DockArea::onDrop
    // already handled (including same-slot no-op). Stored out-of-line so
    // adding the flag does not shift UIManager member layout (stale
    // Gallery/AYUI mix would AV in registerTooltip's _tooltips).
    bool        lastDragHadDropTarget() const;

    // G12 R3-safe parallel of clearFocus/Capture/HoverNoDispatch. If
    // `candidate` is the drag source or current target, drop the session
    // state without firing virtual callbacks (which would dispatch on a
    // mid-destruction widget — UB landmine).
    void clearDragStateNoDispatch(Widget* candidate);

    // G12 internal — ghost helpers. ensureGhostCreated lazily spawns a
    // plain Widget attached to the overlay root (or main root). Called
    // by beginDrag; tear-down via shutdown's destroyWidgetTree on the
    // root that owns the ghost. updateGhostPosition repositions the
    // ghost on every cursor move; paintGhost draws the plate + payload
    // text (called from UIManager::render after overlay root render).
    void ensureGhostCreated();
    void updateGhostPosition(const math::FVector2& pos);
    void paintGhost(IRenderBackend& renderer);

    // Polish (P3): MenuBar accelerator hook. Called by MenuBar's ctor
    // (when a UIManager instance is reachable) and dtor (via tryGet). The
    // vector holds non-owning pointers — destruction order is the host's
    // job. We cap how often this list can grow (it's a std::vector, push
    // back is amortized constant).
    std::vector<MenuBar*> _menuBars;

private:
    // Used by get()'s static fallback: null bookkeeping Widget* so process
    // exit / cross-test get() cannot dereference fixtures that already died.
    void dropTransientWidgetPointers();

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

    // G12 — drag session state. Independent of _capturedWidget because
    // G12 drag-drop is a separate channel from widget-internal drag
    // (SplitterHandle/Slider/ScrollBar thumb/Window title-drag).
    struct DragSession {
        bool        active        = false;
        Widget*     source        = nullptr;
        DragPayload payload;
        Widget*     currentTarget = nullptr;
        math::FVector2 lastMousePos{0.0f, 0.0f};
    };
    DragSession _dragSession;
    Widget*     _dragGhost = nullptr;   // attached to overlay, drawn after _overlayRoot

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

    // Phase D (D2) — Modal stack. Same single-active invariant as the
    // DropdownManager above (Q14) — opening a new modal closes the
    // previous one. We carry `_activeModalRoot` separately so focus
    // traversal in focusNext/focusPrev can pick the right DFS root
    // without dynamic_cast in onKeyDown's hot path (R-3 landmine
    // avoidance — mirrors the ComboBox flag pattern).
    class Modal* _activeModal = nullptr;
    Widget*       _activeModalRoot = nullptr;

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

    // PR-C1 — list of attached Tooltips driven by update(dt). Non-owning;
    // the overlay root owns lifetime. Cleared on tearDownOverlayChildren /
    // shutdown BEFORE destroyWidgetTree runs so the next update doesn't
    // deref a freed Tooltip*.
    std::vector<class Tooltip*> _tooltips;

    // Phase C helper: true if `w` is a text-editing widget per the
    // isTextEditingWidget() virtual. Used by setFocus to drive the
    // onTextEditingFocusChanged callback gate. nullptr returns false.
    static bool isTextEditing(Widget* w);
};

} // namespace ayt::ui
