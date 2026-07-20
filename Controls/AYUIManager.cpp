#include "AYUIManager.h"
#include "AYWidget.h"
#include "AYFocusableWidget.h"
#include "AYBox.h"
#include "AYButton.h"
#include "AYCheckBox.h"
#include "AYRadioButton.h"
#include "AYSlider.h"
#include "AYProgressBar.h"
#include "AYFocusableWidget.h"
#include "AYTextInput.h"
#include "AYTextArea.h"
#include "AYTooltip.h"
#include "AYSeparator.h"
#include "AYMenuItem.h"
#include "AYMenu.h"
#include "AYMenuBar.h"
#include "AYToolBar.h"
#include "AYStatusBar.h"
#include "AYScrollBar.h"
#include "AYScrollView.h"
#include "AYListView.h"
#include "AYComboBox.h"
#include "AYTabControl.h"
#include "AYGridPanel.h"
#include "AYTreeNode.h"
#include "AYTreeView.h"
#include "AYRichText.h"
#include "AYImage.h"
#include "AYTextLabel.h"
#include "AYWindow.h"
#include "AYModal.h"
#include "AYModalDialog.h"
#include "AYDimmer.h"
#include "AYSplitterHandle.h"
#include "aymath/MathUtils.h"
#include "AYWidgetFactory.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ayt::ui {

namespace {

bool splitterDebugEnabled()
{
    static int cached = -1;
    if (cached < 0) {
        const char* env = std::getenv("AY_UI_SPLITTER_DEBUG");
        cached = (env != nullptr && env[0] != '\0' && env[0] != '0') ? 1 : 0;
    }
    return cached != 0;
}

const char* widgetLabel(const Widget* w)
{
    if (w == nullptr) {
        return "null";
    }
    if (!w->getId().empty()) {
        return w->getId().c_str();
    }
    if (w->isSplitterHandle()) {
        return "<SplitterHandle>";
    }
    return "<Widget>";
}

Widget* pickWidgetAt(Widget* widget, const math::FVector2& worldPos)
{
    if (widget == nullptr || !widget->isVisible()) {
        return nullptr;
    }
    return widget->hitTest(worldPos);
}

void updateHoverWidget(Widget*& hoverWidget, Widget* nextHover)
{
    if (hoverWidget == nextHover) {
        return;
    }
    const bool log = splitterDebugEnabled()
        && ((hoverWidget != nullptr && hoverWidget->isSplitterHandle())
            || (nextHover != nullptr && nextHover->isSplitterHandle()));
    if (log) {
        std::fprintf(stderr,
            "[SplitterDebug] UIManager hover %s -> %s\n",
            widgetLabel(hoverWidget), widgetLabel(nextHover));
    }
    if (hoverWidget != nullptr) {
        hoverWidget->onMouseLeave();
    }
    hoverWidget = nextHover;
}

void endStuckSplitterDrags(Widget* root)
{
    if (root == nullptr) {
        return;
    }
    std::vector<Widget*> stack;
    stack.push_back(root);
    while (!stack.empty()) {
        Widget* w = stack.back();
        stack.pop_back();
        if (w == nullptr) {
            continue;
        }
        if (auto* split = dynamic_cast<SplitterHandle*>(w)) {
            if (split->isDragging()) {
                if (splitterDebugEnabled()) {
                    std::fprintf(stderr,
                        "[SplitterDebug] UIManager: capture=0 but %s still "
                        "dragging — forcing endDrag (stuck-drag recovery)\n",
                        widgetLabel(split));
                }
                split->endDrag();
            }
        }
        for (Widget* child : w->getChildren()) {
            stack.push_back(child);
        }
    }
}

void dumpSplitterLayoutOnce(Widget* root)
{
    if (!splitterDebugEnabled() || root == nullptr) {
        return;
    }
    static bool dumped = false;
    if (dumped) {
        return;
    }
    dumped = true;

    std::fprintf(stderr, "[SplitterDebug] --- layout dump ---\n");
    std::vector<Widget*> stack;
    stack.push_back(root);
    while (!stack.empty()) {
        Widget* w = stack.back();
        stack.pop_back();
        if (w == nullptr) {
            continue;
        }
        if (w->isSplitterHandle()) {
            const math::FRectangle world = w->getWorldBounds();
            std::fprintf(stderr,
                "[SplitterDebug] layout id=%s world=[%.1f,%.1f)x[%.1f,%.1f) "
                "size=(%.1f,%.1f) isSplitterHandle=1\n",
                widgetLabel(w),
                world.minX, world.maxX, world.minY, world.maxY,
                w->getWidth(), w->getHeight());
        }
        if (HBox* box = dynamic_cast<HBox*>(w)) {
            for (int i = 0; i < static_cast<int>(box->getChildren().size()); ++i) {
                // slotWidth is public; isSplitterSlot too
                (void)i;
            }
            // Walk slots via children order (same as insertion for HBox loader)
            const auto& kids = box->getChildren();
            for (size_t i = 0; i < kids.size(); ++i) {
                std::fprintf(stderr,
                    "[SplitterDebug] HBox '%s' child[%zu] id=%s "
                    "isSplitterHandle=%d slotWidth=%.1f isSplitterSlot=%d\n",
                    widgetLabel(box), i, widgetLabel(kids[i]),
                    kids[i]->isSplitterHandle() ? 1 : 0,
                    box->slotWidth(static_cast<int>(i)),
                    box->isSplitterSlot(static_cast<int>(i)) ? 1 : 0);
            }
        }
        for (Widget* child : w->getChildren()) {
            stack.push_back(child);
        }
    }
    std::fprintf(stderr, "[SplitterDebug] --- end layout dump ---\n");
}

} // namespace

namespace {
// Active instance set by initialize() / cleared by shutdown().
// Not a owning singleton — callers still construct UIManager on the stack
// or as a member; get() just routes popup helpers to that instance.
UIManager* g_activeUIManager = nullptr;
} // namespace

UIManager* UIManager::tryGet() {
    return g_activeUIManager;
}

UIManager& UIManager::get() {
    if (g_activeUIManager != nullptr) {
        return *g_activeUIManager;
    }
    // Fallback for code paths that call get() before initialize() —
    // e.g. a bare `ComboBox cb; cb.openPopup();` in a unit test without
    // its own UIManager. Lazily bootstrap an overlay-capable instance.
    //
    // CRITICAL (batch SEGV / R3): initialize() registers `this` as
    // g_activeUIManager. If we leave the process-lifetime fallback as
    // "active", Widget dtors after a real UIManager::shutdown() call
    // get() and stash stack Widget* (focus/composition) onto the
    // fallback — those pointers die with the fixture, then
    // ~s_uninitializedFallback / the next scrub SEGV. So:
    //   1) detach fallback from g_active immediately after bootstrap
    //   2) drop any leftover transient Widget* on every fallback serve
    static UIManager s_uninitializedFallback;
    static bool s_bootstrapped = false;
    if (!s_bootstrapped) {
        s_uninitializedFallback.initialize(nullptr);
        g_activeUIManager = nullptr;
        s_bootstrapped = true;
    }
    s_uninitializedFallback.dropTransientWidgetPointers();
    return s_uninitializedFallback;
}

// Register the four built-in widget factories on first use. The
// `AYTextLabel.cpp` and (formerly) `AYButton.cpp` self-registrars
// in anonymous namespaces are vulnerable to MSVC COMDAT stripping
// when the executable's link-order doesn't pull in the registrar's
// TU as a kept symbol — leading to `[UILayoutLoader] missing
// factory creator` warnings at runtime. Centralizing the call
// here (which is the universal `initialize` entry point) makes the
// factories robust to linker decisions.
static void ensureBuiltInFactoriesRegistered() {
    WidgetFactory& f = WidgetFactory::get();
    if (!f.isRegistered("Button"))    f.registerCreator("Button",    []() { return new Button(); });
    if (!f.isRegistered("Image"))     f.registerCreator("Image",     []() { return new Image(); });
    if (!f.isRegistered("TextLabel")) f.registerCreator("TextLabel", createTextLabelWidget);
    if (!f.isRegistered("CheckBox"))  f.registerCreator("CheckBox",  createCheckBoxWidget);
    if (!f.isRegistered("RadioButton")) f.registerCreator("RadioButton", createRadioButtonWidget);
    if (!f.isRegistered("Slider"))    f.registerCreator("Slider",    createSliderWidget);
    if (!f.isRegistered("ProgressBar")) f.registerCreator("ProgressBar", createProgressBarWidget);
    if (!f.isRegistered("TextInput")) f.registerCreator("TextInput", createTextInputWidget);
    if (!f.isRegistered("TextArea")) f.registerCreator("TextArea", createTextAreaWidget);
    if (!f.isRegistered("ScrollBar")) f.registerCreator("ScrollBar", createScrollBarWidget);
    if (!f.isRegistered("ScrollView")) f.registerCreator("ScrollView", createScrollViewWidget);
    if (!f.isRegistered("ListView")) f.registerCreator("ListView", createListViewWidget);
    if (!f.isRegistered("ComboBox")) f.registerCreator("ComboBox", createComboBoxWidget);
    if (!f.isRegistered("TabControl")) f.registerCreator("TabControl", createTabControlWidget);
    if (!f.isRegistered("Window"))    f.registerCreator("Window",    []() { return new Window(); });
    // VBox/HBox/SplitterHandle also live in anonymous-namespace self-
    // registrars (AYBox.cpp / AYSplitterHandle.cpp). On MSVC those are
    // vulnerable to COMDAT stripping when the executable's link-order
    // doesn't pull in their registrar TU. Registering them centrally here
    // (the universal initialize() entry point) mirrors the fix applied to
    // Button/Image/TextLabel/Window and prevents the "[UILayoutLoader]
    // missing factory creator" warning for layout widgets.
    if (!f.isRegistered("VBox"))          f.registerCreator("VBox",          []() { return new VBox(); });
    if (!f.isRegistered("HBox"))          f.registerCreator("HBox",          []() { return new HBox(); });
    if (!f.isRegistered("SplitterHandle")) f.registerCreator("SplitterHandle", []() { return new SplitterHandle(); });
    if (!f.isRegistered("GridPanel"))  f.registerCreator("GridPanel",  []() { return new GridPanel(); });
    if (!f.isRegistered("Tooltip"))    f.registerCreator("Tooltip",    createTooltipWidget);
    if (!f.isRegistered("Separator"))  f.registerCreator("Separator",  createSeparatorWidget);
    if (!f.isRegistered("MenuItem"))   f.registerCreator("MenuItem",   createMenuItemWidget);
    if (!f.isRegistered("Menu"))       f.registerCreator("Menu",       createMenuWidget);
    if (!f.isRegistered("MenuBar"))    f.registerCreator("MenuBar",    createMenuBarWidget);
    if (!f.isRegistered("ToolBar"))    f.registerCreator("ToolBar",    createToolBarWidget);
    if (!f.isRegistered("StatusBar"))  f.registerCreator("StatusBar",  createStatusBarWidget);
    if (!f.isRegistered("TreeNode"))  f.registerCreator("TreeNode",  createTreeNodeWidget);
    if (!f.isRegistered("TreeView"))  f.registerCreator("TreeView",  createTreeViewWidget);
    if (!f.isRegistered("RichText"))  f.registerCreator("RichText",  createRichTextWidget);
    // Phase D (D2) — Modal layer
    if (!f.isRegistered("Dimmer"))  f.registerCreator("Dimmer",  []() { return new Dimmer(); });
    if (!f.isRegistered("Modal"))   f.registerCreator("Modal",   []() { return new Modal(); });
    // Phase D (D4) — TabStrip
    if (!f.isRegistered("TabStrip")) f.registerCreator("TabStrip", createTabStripWidget);
    // Phase D §5.3 — ModalDialog / MessageBox template.
    // Per Phase D PR-2 decision, Modal itself is NOT registered (kept as a
    // private detail). ModalDialog IS registered so host JSON can spawn
    // it directly via the layout loader without writing a creator wrapper.
    if (!f.isRegistered("ModalDialog")) f.registerCreator("ModalDialog", createModalDialogWidget);
}

void UIManager::initialize(IRenderBackend* backend) {
    ensureBuiltInFactoriesRegistered();
    _backend = backend;
    // Reset client size to "unset" so loadFromString doesn't auto-resize the
    // root to the default 1280x720 viewport. Callers that want auto-sizing
    // must call setClientSize() explicitly between loadFromString and the
    // first render — that matches the documented flow.
    _clientWidth = 0.0f;
    _clientHeight = 0.0f;
    _shutdown = false;

    // Canvas root for hosts/tests that attach widgets without loadFromString.
    // Without this, `um.root()->addChildExternal(...)` is nullptr UB and can
    // "pass" CHECKs then SEGV later in batch (corrupt heap / static teardown).
    if (_root == nullptr) {
        _root = new Widget();
        _root->setPosition(math::FVector2(0.0f, 0.0f));
        _root->setSize(math::FVector2(0.0f, 0.0f));
    }

    // Phase A (S1): spawn the popup overlay root. Plain Widget — not
    // CompoundWidget. With CompoundWidget::hitTest we'd need to filter
    // self-matches in pickTopmostWidget(); plain Widget::hitTest returns
    // `this` when inside bounds OR null when outside, which the funnel
    // already handles (skip self-match). The actual popups are children
    // of the overlay; the funnel manually walks _overlayRoot->getChildren()
    // via pickTopmostWidget's child-iteration when needed.
    if (_overlayRoot == nullptr) {
        _overlayRoot = new Widget();
        _overlayRoot->setPosition(math::FVector2(0.0f, 0.0f));
        _overlayRoot->setSize(math::FVector2(0.0f, 0.0f));
    }

    // Route UIManager::get() (used by ComboBox/Menu/Tooltip popup mounts)
    // to this instance for the lifetime of initialize()..shutdown().
    g_activeUIManager = this;
}

void UIManager::tearDownOverlayChildren() {
    // Notify ComboBox anchors BEFORE free — otherwise a stack-local
    // ComboBox still holds `_popup` and its destructor double-frees the
    // ListView we destroy here. Do NOT dynamic_cast `_activeDropdownAnchor`:
    // Tooltip::attachTo uses a Button as anchor, and tests may destroy that
    // Button before shutdown (tooltip_owned_via_destroy_widget_tree) —
    // RTTI on a freed object AVs ("no RTTI data").
    const bool notifyCombo = _activeDropdownAnchorIsComboBox;
    Widget* anchor = _activeDropdownAnchor;
    _activeDropdown = nullptr;
    _activeDropdownAnchor = nullptr;
    _activeDropdownAnchorIsComboBox = false;
    if (notifyCombo && anchor != nullptr) {
        static_cast<ComboBox*>(anchor)->onPopupDismissedByManager();
    }

    // Phase D (D2) — Modal layer mirror (R-3 landmine avoidance, same
    // pattern as A2 landmine #2). tearDownOverlayChildren runs BEFORE
    // _root is destroyed in shutdown/loadLayout/loadFromString. If a modal
    // was active, _activeModal points at a widget about to be destroyed
    // by the loop below. Drop the pointer NOW so a subsequent Esc or
    // closeModal(0xFEAD-feed) doesn't dereference it.
    if (_activeModal != nullptr) {
        // Restore focus to whatever the modal had saved — the modal dtor
        // path normally handles this in ~Modal, but mid-tearDown we may
        // not go through that path (host program called loadLayout while
        // a modal was open). Drop focus cleanly to avoid UAF.
        if (_focusedWidget != nullptr &&
            (_focusedWidget == _activeModal
             || isDescendantOf(_focusedWidget, _activeModal))) {
            _focusedWidget = nullptr;
        }
        if (_capturedWidget != nullptr &&
            (_capturedWidget == _activeModal
             || isDescendantOf(_capturedWidget, _activeModal))) {
            _capturedWidget = nullptr;
        }
        _activeModal = nullptr;
        _activeModalRoot = nullptr;
    }

    if (_overlayRoot == nullptr) {
        return;
    }
    // MUST copy: getChildren() returns const vector&. destroyWidgetTree
    // calls detachFromParent() which erases from the live vector.
    std::vector<Widget*> kids = _overlayRoot->getChildren();
    for (Widget* child : kids) {
        if (child == nullptr) {
            continue;
        }
        // closePopup-equivalent capture guard (popup may own the capture).
        if (_capturedWidget != nullptr &&
            (_capturedWidget == child || isDescendantOf(_capturedWidget, child))) {
            _capturedWidget = nullptr;
        }
        if (_hoverWidget != nullptr &&
            (_hoverWidget == child || isDescendantOf(_hoverWidget, child))) {
            _hoverWidget = nullptr;
        }
        destroyWidgetTree(child);
    }
}

void UIManager::shutdown() {
    if (_shutdown) {
        return;
    }
    _shutdown = true;

    // Order matters: clear captures/registry BEFORE destroying the tree so
    // any std::function holding a Widget* (or capturing by reference into a
    // soon-to-be-freed widget) is released first.
    _capturedWidget = nullptr;
    _hoverWidget = nullptr;
    // Phase D (D2) — use clearFocusNoDispatch instead of
    // `dynamic_cast + setFocus(false)`. By shutdown time, widgets in
    // _root (about to be destroyed in this function) or _overlayRoot
    // (about to be destroyed in tearDownOverlayChildren) may be partially
    // destroyed already. Calling a virtual function on the focused widget
    // is undefined behavior in C++ (R3 landmine, same pattern Phase C
    // TextInput::~TextInput → cancelComposition(this, fireEndOnOwner=false)).
    clearFocusNoDispatch(_focusedWidget);
    // Phase C (S4): also drop any in-flight composition. We don't fire
    // End on the owner because by shutdown time the owner may already
    // be part of the tree about to be destroyed, and we just cleared
    // _focusedWidget above so cancelComposition's owner-strict check
    // would no-op anyway. Force-clear the slot.
    _compositionOwner = nullptr;
    _composing = false;
    _loader.clearEventBindings();
    _loader.clearWidgetRegistry();

    // Phase A (A5): tear down overlay + main root together so a popup
    // parented on the overlay can't survive its host. Order:
    //   1) destroy overlay's children (open popups)
    //   2) destroy main root
    //   3) destroy overlay itself
    tearDownOverlayChildren();
    if (_root != nullptr) {
        destroyWidgetTree(_root);
        _root = nullptr;
    }
    if (_overlayRoot != nullptr) {
        destroyWidgetTree(_overlayRoot);
        _overlayRoot = nullptr;
    }

    _backend = nullptr;
    if (g_activeUIManager == this) {
        g_activeUIManager = nullptr;
    }
}

bool UIManager::loadLayout(const std::string& path) {
    if (_focusedWidget != nullptr) {
        // Phase D (D2) — clearFocusNoDispatch instead of
        // `dynamic_cast + setFocus(false)`. By tree-mutating time (a
        // reload/loadLayout, the about-to-die widgets in _root may be
        // partially destroyed; invoking a virtual function on them is
        // undefined behavior (R3 landmine, same pattern as Phase C
        // TextInput::~TextInput). See clearFocusNoDispatch docstring.
        clearFocusNoDispatch(_focusedWidget);
    }
    // Phase A: drop overlay children — popups may reference widgets in
    // the about-to-be-destroyed root. Keep overlay itself (it survives
    // across loads; only its contents change).
    tearDownOverlayChildren();
    if (_root != nullptr) {
        destroyWidgetTree(_root);
        _root = nullptr;
    }
    _lastLayoutWidth = -1.0f;
    _lastLayoutHeight = -1.0f;

    _root = _loader.loadFromFile(path);
    if (_root) {
        // Force root to origin so children with absolute positions are
        // measured relative to the viewport. Loader-supplied positions on
        // the root itself are intentionally overridden — the root is
        // always the top-left corner of the UI canvas.
        if (_root->isLayoutPositionManaged()) {
            _root->setPosition(math::FVector2(0.0f, 0.0f));
        }
        if (_clientWidth > 0.0f && _clientHeight > 0.0f) {
            _root->setSize(math::FVector2(_clientWidth, _clientHeight));
            layout();
        }
    }
    return _root != nullptr;
}

bool UIManager::loadFromString(const std::string& json) {
    if (_focusedWidget != nullptr) {
        // Phase D (D2) — clearFocusNoDispatch instead of
        // `dynamic_cast + setFocus(false)`. By tree-mutating time (a
        // reload/loadLayout, the about-to-die widgets in _root may be
        // partially destroyed; invoking a virtual function on them is
        // undefined behavior (R3 landmine, same pattern as Phase C
        // TextInput::~TextInput). See clearFocusNoDispatch docstring.
        clearFocusNoDispatch(_focusedWidget);
    }
    // Phase A: same overlay-teardown sequence as loadLayout.
    tearDownOverlayChildren();
    if (_root != nullptr) {
        destroyWidgetTree(_root);
        _root = nullptr;
    }
    _lastLayoutWidth = -1.0f;
    _lastLayoutHeight = -1.0f;
    _root = _loader.loadFromString(json);
    if (_root) {
        if (_root->isLayoutPositionManaged()) {
            _root->setPosition(math::FVector2(0.0f, 0.0f));
        }
        if (_clientWidth > 0.0f && _clientHeight > 0.0f) {
            _root->setSize(math::FVector2(_clientWidth, _clientHeight));
            layout();
        }
    }
    return _root != nullptr;
}

void UIManager::bindEvent(const std::string& widgetId, const std::string& eventType,
                          std::function<void()> handler) {
    _loader.bindEvent(widgetId, eventType, handler);
}

void UIManager::setClientSize(float width, float height) {
    _clientWidth = width;
    _clientHeight = height;
    if (_root) {
        _root->setPosition(math::FVector2(0.0f, 0.0f));
        _root->setSize(math::FVector2(width, height));
        // Force the next layout() to actually run — we just resized the
        // root, which means children need a fresh performLayout pass.
        _lastLayoutWidth = -1.0f;
        _lastLayoutHeight = -1.0f;
    }
    // Phase A: keep overlay in lock-step with viewport so popup world
    // coords map 1:1 to screen.
    if (_overlayRoot != nullptr) {
        _overlayRoot->setPosition(math::FVector2(0.0f, 0.0f));
        _overlayRoot->setSize(math::FVector2(width, height));
    }
}

void UIManager::update(float dt) {
    if (Widget* reloaded = _loader.tryReload()) {
        // R-7: cancel any in-flight mouse capture BEFORE destroying the
        // tree. The captured widget (typically a Window being dragged) is
        // about to be freed; without this, _capturedWidget stays non-null
        // and the next onMouseButtonUp dereferences a dangling pointer.
        // cancelCapture() synthesizes a mouse-up on the captured widget so
        // its transient drag state is reset cleanly.
        if (_capturedWidget != nullptr) {
            cancelCapture();
        }
        // Also clear hover so a stale _hoverWidget pointer doesn't survive
        // the tree swap and confuse the cursor hint.
        _hoverWidget = nullptr;
        if (_focusedWidget != nullptr) {
            FocusableWidget* fw = dynamic_cast<FocusableWidget*>(_focusedWidget);
            if (fw != nullptr) fw->setFocus(false);
            _focusedWidget = nullptr;
        }
        // Phase A (A5): tear down overlay children before _root swap so
        // any open popup is freed (it could otherwise hold references to
        // widgets in the about-to-be-destroyed root).
        tearDownOverlayChildren();
        if (_root != nullptr) {
            destroyWidgetTree(_root);
        }
        _root = reloaded;
        _lastLayoutWidth = -1.0f;
        _lastLayoutHeight = -1.0f;
        setClientSize(_clientWidth, _clientHeight);
        layout();
    }

    // Per-frame tick cascade. Drives time-based widget behavior
    // (SplitterHandle's hover-reveal delay, future animations, etc.).
    // Mirrors how layout() walks performLayout: a single call on the
    // root that CompoundWidget::tick recurses through children.
    if (_root != nullptr) {
        dumpSplitterLayoutOnce(_root);
        _root->tick(dt);
    }

    // Re-validate hover against the last known pointer. Pure-hover leave
    // for SplitterHandle depends on updateHoverWidget firing onMouseLeave;
    // if a prior move left `_hover` armed because hitTest still returned
    // the same (too-wide) handle, the next frame's re-hit with a corrected
    // band clears it. Do not synthesize onMouseMove here — that would
    // re-arm hover every tick while the cursor is idle on the band.
    if (_root != nullptr && _hasLastMouse && _capturedWidget == nullptr) {
        Widget* hit = pickTopmostWidget(math::FVector2(_lastMouseX, _lastMouseY));
        if (splitterDebugEnabled() && _hoverWidget != nullptr
            && _hoverWidget->isSplitterHandle() && hit != _hoverWidget) {
            std::fprintf(stderr,
                "[SplitterDebug] update() revalidate will leave %s -> %s "
                "mouse=(%.1f,%.1f)\n",
                widgetLabel(_hoverWidget), widgetLabel(hit),
                _lastMouseX, _lastMouseY);
        }
        updateHoverWidget(_hoverWidget, hit);
    }
}

void UIManager::layout() {
    if (!_root) {
        return;
    }
    // Phase UI-PERF-1: skip performLayout when the client size hasn't
    // changed since the last pass. The tree's bounds are already up-to-date
    // because VBox/HBox performLayout mutates children on every call.
    if (_clientWidth == _lastLayoutWidth && _clientHeight == _lastLayoutHeight) {
        return;
    }
    _root->performLayout();
    _lastLayoutWidth = _clientWidth;
    _lastLayoutHeight = _clientHeight;
}

void UIManager::render() {
    if (!_backend || !_root) {
        return;
    }

    _backend->beginFrame();
    math::FRectangle viewport(0.0f, 0.0f, _clientWidth, _clientHeight);
    _backend->beginCanvas(viewport);
    _root->render(*_backend);
    // Phase A: render overlay AFTER the main tree so popups paint on top.
    if (_overlayRoot != nullptr) {
        _overlayRoot->render(*_backend);
    }
    // Flush any batched quads accumulated during the widget tree walk.
    // Default backend implementation is a no-op; backends that override
    // addColoredQuad/addTexturedQuad for batching submit here in one go.
    _backend->flushBatches();
    _backend->endCanvas();
    _backend->endFrame();
}

Widget* UIManager::findById(const std::string& id) const {
    return _loader.findWidgetById(id);
}

// =====================================================================
// Phase A — DropdownManager (S2)
// =====================================================================
// Single-active-popup invariant: opening a new popup closes the previous
// one. closePopup tears down the popup via destroyWidgetTree (overlay owns
// lifetime end-to-end).
//
// _capturedWidget guard: if a click landed inside the popup and we close
// it, the next event must NOT route to a freed widget. We null the
// captured pointer whenever it points inside the popup's subtree.
//
// Anchor tracking: callers pass `anchor` so click-outside detection can
// distinguish "click inside anchor's own area" (don't close) from "click
// anywhere else" (close). Without this, a click on the ComboBox's main
// area would be misclassified as click-outside → popup closes → opens
// again in the same frame → flicker.
void UIManager::openPopup(Widget* anchor, Widget* popup) {
    if (popup == nullptr) return;
    if (_overlayRoot == nullptr) return;

    // Close any other popup first. Single-active invariant.
    if (_activeDropdown != nullptr && _activeDropdown != popup) {
        closePopup(_activeDropdown);
    }

    // Reparent onto the overlay. If popup already lives somewhere,
    // detach it first (it shouldn't but defensive).
    if (popup->getParent() != nullptr) {
        popup->getParent()->removeChild(popup);
    }
    _overlayRoot->addChild(popup);   // ref-only per Widget::addChild; overlay doesn't delete

    // Track as active.
    _activeDropdown = popup;
    _activeDropdownAnchor = anchor;
    _activeDropdownAnchorIsComboBox =
        (dynamic_cast<ComboBox*>(anchor) != nullptr);
}

void UIManager::closePopup(Widget* popup, bool destroy) {
    if (popup == nullptr) return;

    // Capture anchor BEFORE clearing bookkeeping — ComboBox hosts need a
    // dismiss notification so they can null their non-owning popup pointer
    // before we destroyWidgetTree it.
    Widget* anchor = nullptr;
    bool notifyCombo = false;
    if (_activeDropdown == popup) {
        anchor = _activeDropdownAnchor;
        notifyCombo = _activeDropdownAnchorIsComboBox;
        _activeDropdown = nullptr;
        _activeDropdownAnchor = nullptr;
        _activeDropdownAnchorIsComboBox = false;
    }

    // If the captured widget is inside this popup, null it BEFORE we
    // destroy the popup. Otherwise the next mouse event dereferences
    // freed memory.
    if (_capturedWidget != nullptr) {
        if (_capturedWidget == popup ||
            isDescendantOf(_capturedWidget, popup)) {
            _capturedWidget = nullptr;
        }
    }

    if (popup->getParent() != nullptr) {
        popup->getParent()->removeChild(popup);
    }

    if (destroy) {
        if (notifyCombo && anchor != nullptr) {
            static_cast<ComboBox*>(anchor)->onPopupDismissedByManager();
        }
        destroyWidgetTree(popup);
    }
}

// =============================================================================
// Phase D (D2) — Modal open/close
// =============================================================================
//
// Pattern mirrors openPopup/closePopup: single-active invariant (Q14),
// capture guard (R2 — null _capturedWidget if it's inside the modal),
// a parent-detach before reparent onto the overlay. We do NOT destroy
// the modal at closeModal time — hosts own lifetime (DECISION mirror
// of ComboBox popup ownership). The Modal dtor (R3) drives its own
// deregister-on-destroy via UIManager::closeModal(this, fireOnClose=false).
// =============================================================================
void UIManager::openModal(Modal* modal) {
    if (modal == nullptr) return;
    if (_overlayRoot == nullptr) return;

    // Single-active invariant (Q14): opening B closes A. We can't call
    // closeModal(A) here because A's _onClose should NOT fire (a chain of
    // dismiss callbacks would surprise hosts). We also can't just drop
    // _activeModalRoot because A still holds _isOpen=true; we need to
    // notify A via its own onForceClosedByManager path which clears
    // A's _isOpen, frees the focusedBefore slot, and detaches its dimmer
    // from the overlay. Same pattern as Phase A ComboBox's
    // onPopupDismissedByManager (memory landmine #2 from A2).
    if (_activeModal != nullptr && _activeModal != modal) {
        Modal* prior = _activeModal;
        Widget* priorRoot = _activeModalRoot;
        _activeModal = nullptr;
        _activeModalRoot = nullptr;
        if (prior != nullptr) {
            prior->onForceClosedByManager(priorRoot);
        }
    }

    // R2 — capture guard. If a drag currently targets a widget inside the
    // about-to-mount modal, null the capture to avoid dangling-pointer
    // dispatch (drag was using old tree; modal mount changes tree).
    if (_capturedWidget != nullptr) {
        if (_capturedWidget == modal || isDescendantOf(_capturedWidget, modal)) {
            _capturedWidget = nullptr;
        }
    }

    // Reparent onto the overlay. Defensive detach if `modal` already has a
    // parent (e.g. host mounted it under a different root).
    if (modal->getParent() != nullptr) {
        modal->getParent()->removeChild(modal);
    }
    // Host owns Modal lifetime (stack or heap) — never destroyWidgetTree it.
    _overlayRoot->addChildExternal(modal);

    _activeModal = modal;
    _activeModalRoot = modal;
}

void UIManager::closeModal(Modal* modal, bool fireOnClose) {
    // fireOnClose=true: Modal::closeModal calls _onClose + restores focus
    //                    to _focusedBefore via setFocus. Manager does NOT
    //                    touch _focusedWidget here.
    // fireOnClose=false: dtor path (~Modal). No _onClose + no setFocus
    //                     (setFocus during dtor would invoke virtual
    //                     setFocus(false) on the about-to-be-destroyed
    //                     Modal — UB, R3 landmine). Use clearFocusNoDispatch
    //                     to drop _focusedWidget if it still points at
    //                     the dying modal, mirroring Phase C TextInput
    //                     dtor's cancelComposition(this, fireEndOnOwner=false).
    if (modal == nullptr) return;
    if (_activeModal != modal) return;   // not the active one — silent no-op

    // Capture guard — never dispatch a future mouse event to a destroyed
    // modal subtree (mirror Phase A closePopup R-3).
    if (_capturedWidget != nullptr &&
        (_capturedWidget == modal || isDescendantOf(_capturedWidget, modal))) {
        _capturedWidget = nullptr;
    }
    if (_hoverWidget != nullptr &&
        (_hoverWidget == modal || isDescendantOf(_hoverWidget, modal))) {
        _hoverWidget = nullptr;
    }

    if (!fireOnClose) {
        // dtor path: manager drops _focusedWidget if it pointed at us,
        // without firing any virtual dispatch.
        clearFocusNoDispatch(modal);
    }
    // fireOnClose path: Modal::closeModal restores focus via setFocus
    // _focusedBefore. We don't touch _focusedWidget here.

    if (modal->getParent() != nullptr) {
        modal->getParent()->removeChild(modal);
    }
    _activeModal = nullptr;
    _activeModalRoot = nullptr;
}

bool UIManager::isDescendantOf(Widget* widget, Widget* ancestor) {
    if (widget == nullptr || ancestor == nullptr) return false;
    Widget* p = widget->getParent();
    while (p != nullptr) {
        if (p == ancestor) return true;
        p = p->getParent();
    }
    return false;
}

// Phase A: overlay-first hit-test funnel. Popups live on _overlayRoot as
// siblings of _root, so the overlay must be picked BEFORE _root for a
// click on an open ComboBox popup or Menu to land on the popup row
// instead of falling through to whatever is underneath in _root. Empty
// overlay is O(1): pickWidgetAt walks children, finds nothing, and we
// fall back to _root. Tooltip::hitTest returns nullptr (pick-through) so
// the funnel falls through correctly.
//
// IMPORTANT: the overlay itself is just a container. We must NOT return
// it as the hit — only its descendants count. CompoundWidget::hitTest
// descends into children first; if a child matches we get that child
// directly. Only if CompoundWidget::hitTest returns the overlay itself
// (i.e. the cursor landed inside the overlay's bounds but no popup
// captured it) do we fall back to _root.
Widget* UIManager::pickTopmostWidget(const math::FVector2& worldPos) {
    // Overlay is a plain Widget (NOT CompoundWidget) so its hitTest only
    // ever returns self. We must walk its children manually here to find
    // the popup under the cursor. Reverse order so the LAST mounted popup
    // wins (matching CompoundWidget::hitTest's reverse-iteration rule).
    if (_overlayRoot != nullptr) {
        const auto& overlayKids = _overlayRoot->getChildren();
        for (auto it = overlayKids.rbegin(); it != overlayKids.rend(); ++it) {
            if (Widget* hit = pickWidgetAt(*it, worldPos)) {
                return hit;
            }
        }
    }
    return pickWidgetAt(_root, worldPos);
}

bool UIManager::onMouseMove(float x, float y) {
    if (!_root) {
        return false;
    }

    _lastMouseX = x;
    _lastMouseY = y;
    _hasLastMouse = true;

    math::FVector2 pos(x, y);
    if (_capturedWidget != nullptr) {
        if (splitterDebugEnabled()
            && _capturedWidget->isSplitterHandle()) {
            static int captureMoveLog = 0;
            if ((captureMoveLog++ % 15) == 0) {
                std::fprintf(stderr,
                    "[SplitterDebug] onMouseMove CAPTURED by %s "
                    "mouse=(%.1f,%.1f) (no hit-test; drag path)\n",
                    widgetLabel(_capturedWidget), x, y);
            }
        }
        return _capturedWidget->onMouseMove(UIMouseEvent(pos, 0));
    }

    // Capture lost (or never held) but a splitter still has `_dragging`:
    // isRevealed() stays true forever. Recover before normal hit-test.
    endStuckSplitterDrags(_root);

    Widget* hit = pickTopmostWidget(pos);
    if (splitterDebugEnabled()
        && (_hoverWidget != hit)
        && ((_hoverWidget != nullptr && _hoverWidget->isSplitterHandle())
            || (hit != nullptr && hit->isSplitterHandle()))) {
        std::fprintf(stderr,
            "[SplitterDebug] onMouseMove hit=%s (prevHover=%s) "
            "mouse=(%.1f,%.1f) capturing=%d\n",
            widgetLabel(hit), widgetLabel(_hoverWidget), x, y,
            _capturedWidget != nullptr ? 1 : 0);
    }
    updateHoverWidget(_hoverWidget, hit);

    if (hit != nullptr) {
        return hit->onMouseMove(UIMouseEvent(pos, 0));
    }
    return false;
}

bool UIManager::onMouseButtonDown(float x, float y, int button) {
    if (!_root) {
        return false;
    }

    math::FVector2 pos(x, y);
    Widget* hit = pickTopmostWidget(pos);
    updateHoverWidget(_hoverWidget, hit);

    // Phase D (D2) — Modal input block. When a modal is active, the only
    // hits we forward are inside the modal subtree. Clicks anywhere else
    // (the dimmer, the area BEHIND the dimmer — which the dimmer already
    // swallows) are eaten: we never call the widget under the dimmer, never
    // close the modal here (Modal::onDimmerClicked owns the dismiss policy
    // via Q8). The dimmer itself records _capturedWidget on button-down so
    // subsequent onMouseMove / onMouseButtonUp still route through the
    // dimmer (which is on the overlay → hit == dimmer → onMouseButtonDown
    // returns true → capture set).
    if (_activeModal != nullptr) {
        const bool insideModal = (hit != nullptr) && (
            hit == _activeModal ||
            isDescendantOf(hit, _activeModal));
        if (!insideModal) {
            // Outside the modal subtree — the dimmer (mounted as a sibling
            // of the modal under _overlayRoot) consumed the pick via its
            // own onMouseButtonDown returning true. We just don't route
            // the event to the modal; the dimmer's sink already fired
            // closeModal via Q8.
            if (hit != nullptr && hit->onMouseButtonDown(UIMouseEvent(pos, button))) {
                _capturedWidget = hit;
                return true;
            }
            return false;
        }
    }

    // Phase A (S2): click-outside detection for active dropdown. If the
    // click landed outside the active popup AND outside its anchor's
    // subtree, close the popup. The anchor check matters: a click on the
    // ComboBox's main area while its popup is open is NOT click-outside —
    // it's the toggle-click path (closePopup → openPopup in onMouseButtonUp).
    // Without the anchor check, click-outside would close the popup first
    // and the toggle-click would reopen it, producing a 1-frame flicker.
    if (_activeDropdown != nullptr) {
        const bool insidePopup = (hit != nullptr) && (
            hit == _activeDropdown ||
            isDescendantOf(hit, _activeDropdown));
        const bool insideAnchor = (hit != nullptr) && (
            _activeDropdownAnchor == nullptr ||
            _activeDropdownAnchor == hit ||
            isDescendantOf(hit, _activeDropdownAnchor));
        if (!insidePopup && !insideAnchor) {
            closePopup(_activeDropdown);
        }
    }

    if (hit != nullptr && hit->onMouseButtonDown(UIMouseEvent(pos, button))) {
        _capturedWidget = hit;
        return true;
    }
    return false;
}

bool UIManager::onMouseButtonUp(float x, float y, int button) {
    // Phase A (A2): the click-outside detector already fired in
    // onMouseButtonDown; we only need to deliver the up to whatever was
    // captured (or whatever's under the cursor). When _root is null we
    // can still update hover state but there's nothing to forward to.
    math::FVector2 pos(x, y);
    Widget* target = _capturedWidget != nullptr ? _capturedWidget : pickTopmostWidget(pos);
    _capturedWidget = nullptr;

    // Deliver mouse-up BEFORE re-evaluating hover. SplitterHandle (and
    // similar capture targets) clear drag/hover state in onMouseButtonUp;
    // if updateHoverWidget runs first, onMouseLeave can see a still-
    // dragging widget and historically skipped the hover reset.
    bool handled = false;
    if (target != nullptr) {
        handled = target->onMouseButtonUp(UIMouseEvent(pos, button));
    }

    // Capture is gone — point `_hoverWidget` at whatever is under the
    // cursor now (for cursor hints). Do NOT synthesize onMouseMove here:
    // SplitterHandle intentionally stays un-revealed after mouse-up until
    // the next real WM_MOUSEMOVE re-arms hover.
    updateHoverWidget(_hoverWidget, pickTopmostWidget(pos));
    return handled;
}

void UIManager::clearHover() {
    // Always drop the hover target. Previously this early-returned while
    // capturing, which left SplitterHandle `_hover` stuck when the editor
    // routed viewport moves through clearHover during an in-progress drag.
    updateHoverWidget(_hoverWidget, nullptr);
}

void UIManager::onMouseLeave() {
    _hasLastMouse = false;
    if (_capturedWidget != nullptr) {
        return;
    }
    updateHoverWidget(_hoverWidget, nullptr);
}

void UIManager::cancelCapture() {
    if (_capturedWidget == nullptr) {
        return;
    }

    Widget* captured = _capturedWidget;
    _capturedWidget = nullptr;
    captured->onMouseButtonUp(UIMouseEvent(math::FVector2(0.0f, 0.0f), 0));
}

// Phase D (D2) — clearFocusNoDispatch. R3 landmine avoidance. NOT a
// general-purpose API: only ~FocusableWidget and dtor scenarios may call
// this. The candidate is typically `this` of a widget about to be
// destroyed; if the manager's focus pointer happens to point at it, we
// reset to nullptr WITHOUT firing any virtual dispatch.
void UIManager::dropTransientWidgetPointers() {
    // Do NOT destroy overlay/root trees here — bare ComboBox tests may
    // still hold non-owning pointers into fallback-mounted popups for the
    // duration of a single case. Only null raw bookkeeping slots that
    // commonly outlive stack fixtures across shutdown → get() → exit.
    _focusedWidget = nullptr;
    _capturedWidget = nullptr;
    _hoverWidget = nullptr;
    _compositionOwner = nullptr;
    _composing = false;
    _activeModal = nullptr;
    _activeModalRoot = nullptr;
    // Keep _activeDropdown / anchor identity for closePopup paths that
    // still run against the fallback within the same test; nulling those
    // mid-open would strand overlay children. They are cleared when
    // closePopup / tearDownOverlayChildren runs.
}

void UIManager::clearCaptureNoDispatch(Widget* candidate) {
    if (_capturedWidget == candidate) {
        _capturedWidget = nullptr;
    }
}

void UIManager::clearHoverNoDispatch(Widget* candidate) {
    if (_hoverWidget == candidate) {
        _hoverWidget = nullptr;
    }
}

void UIManager::clearFocusNoDispatch(Widget* candidate) {
    if (_focusedWidget == candidate) {
        _focusedWidget = nullptr;
    }
}

void UIManager::setFocus(Widget* widget) {
    if (_focusedWidget == widget) {
        return;
    }
    Widget* prev = _focusedWidget;
    _focusedWidget = widget;
    if (prev != nullptr) {
        FocusableWidget* prevAsFw = dynamic_cast<FocusableWidget*>(prev);
        if (prevAsFw != nullptr) {
            prevAsFw->setFocus(false);
        }
    }
    if (_focusedWidget != nullptr) {
        FocusableWidget* nextAsFw =
            dynamic_cast<FocusableWidget*>(_focusedWidget);
        if (nextAsFw != nullptr) {
            nextAsFw->setFocus(true);
        }
    }

    // =================================================================
    // Phase C (S4): text-editing focus gate. Flip the AYDevice TextInput
    // enable/disable signal whenever the focused widget's
    // isTextEditingWidget() status changes. Hosts wire
    // onTextEditingFocusChanged to AYDevice::TextInput::setEnabled — game
    // keybinds then go quiet while the user is typing into a form.
    //
    // We compute the "is now text editing" boolean before AND after the
    // _focusedWidget reassignment so we capture the transition correctly
    // when prev==old and widget==new.
    // =================================================================
    const bool wasTextEditing = isTextEditing(prev);
    const bool isTextEditingNow = isTextEditing(_focusedWidget);
    if (wasTextEditing != isTextEditingNow && onTextEditingFocusChanged) {
        onTextEditingFocusChanged(isTextEditingNow);
    }
}

bool UIManager::isTextEditing(Widget* w) {
    // nullptr → not editing. dynamic_cast would also work, but the
    // virtual isTextEditingWidget() is faster (one vcall vs RTTI) and
    // also handles the TextArea::TextDocument case which is not a
    // TextInput-derived type but DOES want the IME gate.
    if (w == nullptr) return false;
    return w->isTextEditingWidget();
}

bool UIManager::onKeyDown(int keyCode) {
    // Phase B (S3) keyboard nav — modifier tracking + Tab interception
    // happen BEFORE delegating to the focused widget. Widgets must NOT
    // see modifier keys or Tab (TextInput.swallow Tab defensively too).
    if (keyCode == UIKey_Shift || keyCode == UIKey_Control || keyCode == UIKey_Alt) {
        const uint32_t bit = 1u << (keyCode - UIKey_Shift);
        _modifiers |= bit;
        return true;
    }

    if (keyCode == UIKey_Tab) {
        if (_modifiers & (1u << (UIKey_Shift - UIKey_Shift))) focusPrev();
        else                                                  focusNext();
        return true;
    }

    // Phase D (D2) — Esc closes the active modal (Q7). UIManager is the
    // single owner of this policy: even if focus is on a TextInput inside
    // the modal, Esc on the modal close path wins. Modal::closeModal
    // dispatches _onClose (if set) and restores focus to the prior widget.
    if (keyCode == UIKey_Escape && _activeModal != nullptr) {
        // closeModal fires the modal's _onClose via Modal::closeModal's own
        // path; the Modal owns the user-visible dismiss side-effect.
        _activeModal->closeModal();
        return true;
    }

    if (_focusedWidget == nullptr) return false;
    return _focusedWidget->onKeyDown(keyCode);
}

bool UIManager::onKeyUp(int keyCode) {
    if (keyCode == UIKey_Shift || keyCode == UIKey_Control || keyCode == UIKey_Alt) {
        const uint32_t bit = 1u << (keyCode - UIKey_Shift);
        _modifiers &= ~bit;
        return true;
    }
    if (_focusedWidget == nullptr) return false;
    return _focusedWidget->onKeyUp(keyCode);
}

bool UIManager::onTextInput(wchar_t ch) {
    if (_focusedWidget == nullptr) return false;
    return _focusedWidget->onTextInput(ch);
}

bool UIManager::onDeviceKeyDown(::ayt::device::KeyCode kc) {
    return onKeyDown(static_cast<int>(fromDeviceKey(kc)));
}

bool UIManager::onDeviceKeyUp(::ayt::device::KeyCode kc) {
    return onKeyUp(static_cast<int>(fromDeviceKey(kc)));
}

// =============================================================================
// Phase C (S4) — IME device bridge: AYDevice::TextInput → AYUI focused widget
// =============================================================================
//
// State machine:
//   - onDeviceCompositionStart(text, caret)  sets _compositionOwner =
//     _focusedWidget, _composing = true, fires widget hook.
//   - onDeviceCompositionUpdate(text, caret)  if no Start seen, promotes to
//     Start (some Linux IBuses). Else fires widget Update hook.
//   - onDeviceCompositionEnd(committed)      fires widget End hook, then if
//     `committed` non-empty also re-pumps codepoints through onDeviceChar
//     so TextInput::replaceRange treats them as text input. Clears state.
//   - cancelComposition(owner)               if owner == _compositionOwner,
//     fires End with empty `committed` (commit-nothing) and clears state.
//     Idempotent — safe to call from ~dtor even if no composition was
//     active (Q3 — R1 mitigation).
// =============================================================================

bool UIManager::onDeviceChar(const char* utf8, int byteCount) {
    // Empty/null payload is a no-op (some platforms emit zero-length
    // chunks; we don't want to fire onTextInput with nothing).
    if (utf8 == nullptr || byteCount <= 0) return false;
    if (_focusedWidget == nullptr) return false;

    // Phase C: UTF-8 → wchar_t decode. AYDevice delivers UTF-8 (Win32
    // WM_CHAR + WideCharToMultiByte; SDL_TEXTINPUT is also UTF-8). We
    // convert per-codepoint and call onTextInput for each. Surrogate
    // pairs (supplementary-plane codepoints) arrive as two calls; the
    // TextInput stores them in std::wstring which concatenates — same
    // behaviour as typing two halves manually. R4 in the Phase C plan.
    const auto* p = reinterpret_cast<const unsigned char*>(utf8);
    int i = 0;
    while (i < byteCount) {
        unsigned char c = p[i];
        uint32_t cp = 0;
        int bytes = 0;
        if      ((c & 0x80u) == 0x00u) { cp = c;                       bytes = 1; }
        else if ((c & 0xE0u) == 0xC0u) { cp = c & 0x1Fu;                bytes = 2; }
        else if ((c & 0xF0u) == 0xE0u) { cp = c & 0x0Fu;                bytes = 3; }
        else if ((c & 0xF8u) == 0xF0u) { cp = c & 0x07u;                bytes = 4; }
        else {
            // Invalid leading byte; skip one byte to avoid infinite loop.
            ++i;
            continue;
        }
        // Bounds check on continuation bytes.
        if (i + bytes > byteCount) break;
        for (int k = 1; k < bytes; ++k) {
            if ((p[i + k] & 0xC0u) != 0x80u) {
                // Invalid continuation; abandon this codepoint.
                cp = 0;
                break;
            }
            cp = (cp << 6) | (p[i + k] & 0x3Fu);
        }
        if (cp != 0) {
            // Forward as wchar_t. On Windows wchar_t is 16-bit; surrogate
            // pairs (cp > 0xFFFF) will be truncated to a single 16-bit
            // value by the cast. TextInput tests assume BMP input; the
            // multi-codepoint supplementary-plane path is exercised only
            // by host applications needing it. R4 documented in header.
            _focusedWidget->onTextInput(static_cast<wchar_t>(cp));
        }
        i += bytes;
    }
    return true;
}

void UIManager::onDeviceCompositionStart(const std::string& text, int caret) {
    if (_focusedWidget == nullptr) return;
    // Start sets _compositionOwner to the focused widget. If a previous
    // composition is still "live" on a different widget (host glitch),
    // fire End on the old owner first so we don't leak state.
    if (_composing && _compositionOwner != nullptr && _compositionOwner != _focusedWidget) {
        if (auto* fw = dynamic_cast<FocusableWidget*>(_compositionOwner)) {
            fw->onImeCompositionEnd("");
        }
    }
    _compositionOwner = _focusedWidget;
    _composing = true;
    if (auto* fw = dynamic_cast<FocusableWidget*>(_focusedWidget)) {
        fw->onImeCompositionStart(text, caret);
    }
}

void UIManager::onDeviceCompositionUpdate(const std::string& text, int caret) {
    if (_focusedWidget == nullptr) return;
    // Promote-to-Start: if no Start was seen, treat this update as the
    // opening preview. Some Linux IBuses + macOS skip Start entirely.
    if (!_composing || _compositionOwner == nullptr) {
        onDeviceCompositionStart(text, caret);
        return;
    }
    // Owner-mismatch tolerance: if Start fired on a different widget
    // (rare — would mean focus changed mid-composition), force End on
    // the old owner and adopt the new one.
    if (_compositionOwner != _focusedWidget) {
        if (auto* oldFw = dynamic_cast<FocusableWidget*>(_compositionOwner)) {
            oldFw->onImeCompositionEnd("");
        }
        _compositionOwner = _focusedWidget;
    }
    if (auto* fw = dynamic_cast<FocusableWidget*>(_compositionOwner)) {
        fw->onImeCompositionUpdate(text, caret);
    }
}

void UIManager::onDeviceCompositionEnd(const std::string& committed) {
    if (!_composing || _compositionOwner == nullptr) return;
    // Capture owner pointer locally; the End hook may trigger logic
    // that calls cancelComposition (defensive) which would null
    // _compositionOwner before we're done reading it.
    Widget* owner = _compositionOwner;
    _compositionOwner = nullptr;
    _composing = false;

    bool consumed = false;
    if (auto* fw = dynamic_cast<FocusableWidget*>(owner)) {
        consumed = fw->onImeCompositionEnd(committed);
    }
    // Re-pump committed bytes through onDeviceChar ONLY when the widget
    // did NOT consume them. TextInput::onImeCompositionEnd inserts the
    // committed text into _text directly via replaceRange; if we also
    // re-pumped via onDeviceChar the char would land twice. The widget
    // returns true when it consumed the commit, false when it didn't
    // (e.g. read-only widget that just dropped the candidate). Linux
    // IBuses typically deliver committed chars separately through
    // onDeviceChar and pass empty here — `committed.empty()` skips
    // re-pump, which is also correct for that flow.
    if (consumed == false && !committed.empty() && _focusedWidget == owner) {
        onDeviceChar(committed.data(), static_cast<int>(committed.size()));
    }
}

void UIManager::cancelComposition(Widget* owner, bool fireEndOnOwner) {
    if (!_composing || _compositionOwner == nullptr) return;
    if (owner != nullptr && _compositionOwner != owner) return;
    Widget* real = _compositionOwner;
    _compositionOwner = nullptr;
    _composing = false;
    if (fireEndOnOwner) {
        if (auto* fw = dynamic_cast<FocusableWidget*>(real)) {
            // Commit-nothing: caller is destroying the owner, so we just
            // want the widget to drop its composing state without inserting
            // any committed text. The widget's dtor is about to free the
            // text-buffer anyway.
            fw->onImeCompositionEnd("");
        }
    }
    // When fireEndOnOwner is false the caller is the owner's dtor — a
    // virtual dispatch on `real` at this point is UB because the
    // subclass part is mid-destruction. Just drop the slot; the widget
    // doesn't need any cleanup because its memory is about to be freed
    // anyway. (This mirrors the Phase A Menu::close UAF fix.)
    //
    // Focus / hover / capture slots are dropped by the calling widget's
    // dtor directly (TextInput dtor calls setFocus(nullptr) before this
    // function). We keep cancelComposition focused on composition only
    // to avoid hidden coupling.
}

void UIManager::focusNext() {
    // Phase D (D2) — Q6 focus trap. When a modal is active and focus is
    // inside it, Tab stays inside the modal subtree. The modal's content
    // tree is the DFS root — picking the modal widget itself (not the
    // overlay) keeps the focus cycle inside the modal content even if the
    // modal is wrapped in a dimmer or another overlay sibling.
    Widget* startRoot = _root;
    if (_focusedWidget != nullptr) {
        if (_activeModal != nullptr
            && (_focusedWidget == _activeModal
                || isDescendantOf(_focusedWidget, _activeModal))) {
            startRoot = _activeModal;
        } else if (isDescendantOf(_focusedWidget, _overlayRoot)) {
            startRoot = _overlayRoot;
        }
    }
    auto all = collectFocusablesDFS(startRoot);
    // Trap cycles content only — Modal chrome itself is FocusableWidget but
    // must not sit in the Tab ring (would break wrap: …→inC→Modal→…).
    if (_activeModal != nullptr && startRoot == static_cast<Widget*>(_activeModal)) {
        all.erase(std::remove(all.begin(), all.end(), _activeModal), all.end());
    }
    if (all.empty()) return;

    int idx = -1;
    if (_focusedWidget != nullptr) {
        auto it = std::find(all.begin(), all.end(), _focusedWidget);
        if (it != all.end()) idx = static_cast<int>(it - all.begin());
    }
    const int n = static_cast<int>(all.size());
    const int next = (idx < 0) ? 0 : (idx + 1) % n;
    setFocus(all[next]);
}

void UIManager::focusPrev() {
    Widget* startRoot = _root;
    if (_focusedWidget != nullptr) {
        if (_activeModal != nullptr
            && (_focusedWidget == _activeModal
                || isDescendantOf(_focusedWidget, _activeModal))) {
            startRoot = _activeModal;
        } else if (isDescendantOf(_focusedWidget, _overlayRoot)) {
            startRoot = _overlayRoot;
        }
    }
    auto all = collectFocusablesDFS(startRoot);
    if (_activeModal != nullptr && startRoot == static_cast<Widget*>(_activeModal)) {
        all.erase(std::remove(all.begin(), all.end(), _activeModal), all.end());
    }
    if (all.empty()) return;

    int idx = -1;
    if (_focusedWidget != nullptr) {
        auto it = std::find(all.begin(), all.end(), _focusedWidget);
        if (it != all.end()) idx = static_cast<int>(it - all.begin());
    }
    const int n = static_cast<int>(all.size());
    const int next = (idx < 0) ? (n - 1) : (idx - 1 + n) % n;
    setFocus(all[next]);
}

std::vector<Widget*> UIManager::collectFocusablesDFS(Widget* root) const {
    std::vector<Widget*> out;
    if (root == nullptr) return out;
    std::function<void(Widget*)> walk = [&](Widget* w) {
        if (w == nullptr) return;
        if (!w->isVisible()) return;
        if (dynamic_cast<FocusableWidget*>(w) != nullptr) out.push_back(w);
        // children-before-siblings (pre-order DFS). addChild declaration
        // order is preserved by getChildren().
        for (Widget* c : w->getChildren()) walk(c);
    };
    walk(root);
    return out;
}

bool UIManager::isHoverInteractive() const {
    return getCursorHint() == UiCursorHint::Hand;
}

UiCursorHint UIManager::getCursorHint() const {
    if (_capturedWidget != nullptr) {
        const UiCursorHint capturedHint = _capturedWidget->getCursorHint();
        if (capturedHint != UiCursorHint::Default) {
            return capturedHint;
        }
    }

    if (_hoverWidget != nullptr) {
        return _hoverWidget->getCursorHint();
    }

    return UiCursorHint::Default;
}

} // namespace ayt::ui
