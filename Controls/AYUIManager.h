#pragma once

#include "AYLayoutLoader.h"
#include "AYIRenderBackend.h"

#include <memory>
#include <string>

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
};

} // namespace ayt::ui
