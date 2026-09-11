#include "AYUI/UIManager.h"
#include "AYUI/Widget.h"
#include "AYUI/InteractiveWidget.h"
#include "AYUI/FocusableWidget.h"
#include "AYUI/Box.h"
#include "AYUI/Button.h"
#include "AYUI/CheckBox.h"
#include "AYUI/RadioButton.h"
#include "AYUI/Slider.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/FocusableWidget.h"
#include "AYDevice/TextInput.h"
#include "AYUI/TextInput.h"
#include "AYUI/TextArea.h"
#include "AYUI/Tooltip.h"
#include "AYUI/Separator.h"
#include "AYUI/MenuItem.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuBar.h"
#include "AYUI/ToolBar.h"
#include "AYUI/StatusBar.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/ScrollView.h"
#include "AYUI/ListView.h"
#include "AYUI/TileView.h"
#include "AYUI/ComboBox.h"
#include "AYUI/TabControl.h"
#include "AYUI/GridPanel.h"
#include "AYUI/TreeNode.h"
#include "AYUI/TreeView.h"
#include "AYUI/RichText.h"
#include "AYUI/UnicodeText.h"
#include "AYUI/Image.h"
#include "AYUI/TextLabel.h"
#include "AYUI/Window.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/Dimmer.h"
#include "AYUI/SplitterHandle.h"
#include "AYUI/DockTrace.h"
#include "AYMath/MathUtils.h"
#include "AYUI/WidgetFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <typeinfo>
#include <unordered_map>
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

// AYUI_TRACE_INPUT — opt-in stderr trace for input-related root type and
// PR-InputTrace: emit root/overlay type info at any point — used both
// after UIManager::initialize (default canvas) and after loadLayout /
// loadFromString (where Gallery replaces _root with the loaded tree).
void traceRootType(const char* tag, Widget* root, Widget* overlay)
{
    if (!ayuiTraceInputEnabled()) {
        return;
    }
    const bool rootCompound =
        (dynamic_cast<CompoundWidget*>(root) != nullptr);
    const bool overlayCompound =
        (dynamic_cast<CompoundWidget*>(overlay) != nullptr);
    std::fprintf(stderr,
        "[AYUI-InputTrace] %s: _root type=%s isCompound=%d"
        " _overlayRoot type=%s isCompound=%d"
        " rootSize=(%.1f, %.1f)\n",
        tag,
        typeid(*root).name(), rootCompound ? 1 : 0,
        typeid(*overlay).name(), overlayCompound ? 1 : 0,
        static_cast<double>(root ? root->getWidth() : 0.0f),
        static_cast<double>(root ? root->getHeight() : 0.0f));
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
    // AYUI-Perf-2026-08-26: reuse a thread-local scratch buffer instead
    // of allocating a fresh std::vector every call. The function is
    // invoked once per window on the stuck-drag recovery path; the
    // allocation is small but hot (millisecond budget on shutdown).
    static thread_local std::vector<Widget*> stack;
    stack.clear();
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
                    box->slotSize(static_cast<int>(i)),
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

// UIManager is consumed across module boundaries and its concrete size has
// historically remained stable. Keep Production UI Layer state out-of-line
// so adding retained presentation cannot shift legacy fields or invalidate
// incrementally-built hosts that allocate UIManager by value.
struct RootLayerState {
    IRenderBackend::LayerHandle layer{};
    IRenderBackend::LayerDesc desc{};
    bool enabled = false;
    bool descValid = false;
    bool painted = false;
};

std::unordered_map<const UIManager*, RootLayerState> g_rootLayerStates;

// H-2: removed process-global `g_lastDragHadDropTarget`. The flag is now
// an instance member (`_lastDragHadDropTarget`), so multi-window hosts
// running one UIManager per native window don't read another window's
// drag-end state.
} // namespace

UIManager* UIManager::tryGet() {
    return g_activeUIManager;
}

bool UIManager::lastDragHadDropTarget() const {
    return _lastDragHadDropTarget;
}

// =============================================================================
// D5 — multi-window ActiveScope. RAII guard that swaps g_activeUIManager in its
// ctor and restores the previous owner in its dtor.
//
// Single-thread invariant (K-INV-D5-1): nested scopes are safe; concurrent
// scopes on different threads are UB. The editor tick (single thread) is the
// only caller in D5 v1. Hosting frameworks that introduce threading must
// introduce their own per-thread registry.
// =============================================================================
UIManager::ActiveScope::ActiveScope(UIManager* next)
    : _prev(g_activeUIManager)
    , _next(next)
    , _tookOwnership(next != nullptr) {
    if (_tookOwnership) {
        g_activeUIManager = next;
    }
}

UIManager::ActiveScope::~ActiveScope() {
    release();
}

UIManager::ActiveScope::ActiveScope(ActiveScope&& other) noexcept
    : _prev(other._prev)
    , _next(other._next)
    , _tookOwnership(other._tookOwnership) {
    other._prev = nullptr;
    other._next = nullptr;
    other._tookOwnership = false;
}

UIManager::ActiveScope& UIManager::ActiveScope::operator=(ActiveScope&& other) noexcept {
    if (this != &other) {
        release();
        _prev = other._prev;
        _next = other._next;
        _tookOwnership = other._tookOwnership;
        other._prev = nullptr;
        other._next = nullptr;
        other._tookOwnership = false;
    }
    return *this;
}

void UIManager::ActiveScope::release() noexcept {
    // Restore only while this guard still owns the process-wide slot. A
    // shutdown or an explicit makeActive() inside the scope deliberately
    // changes that slot; overwriting it here can resurrect a stale manager.
    if (_tookOwnership && g_activeUIManager == _next) {
        g_activeUIManager = _prev;
    }
    _prev = nullptr;
    _next = nullptr;
    _tookOwnership = false;
}

UIManager::ActiveScope UIManager::pushActive(UIManager* next) {
    return ActiveScope(next);
}

void UIManager::makeActive(UIManager* manager) {
    g_activeUIManager = manager;
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
    if (!f.isRegistered("TileView")) f.registerCreator("TileView", createTileViewWidget);
    if (!f.isRegistered("ComboBox")) f.registerCreator("ComboBox", createComboBoxWidget);
    if (!f.isRegistered("TabControl")) f.registerCreator("TabControl", createTabControlWidget);
    if (!f.isRegistered("Window"))    f.registerCreator("Window",    createWindowWidget);
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
    // Phase D §5.3 — ModalDialog / MessageBox template. Modal is also
    // registered because UILayoutLoader and the serializer expose it as a
    // public base type; ModalDialog remains the usual JSON-facing choice.
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
    _physicalClientWidth = 0.0f;
    _physicalClientHeight = 0.0f;
    _shutdown = false;

    // Canvas root for hosts/tests that attach widgets without loadFromString.
    // Without this, `um.root()->addChildExternal(...)` is nullptr UB and can
    // "pass" CHECKs then SEGV later in batch (corrupt heap / static teardown).
    //
    // MUST be CompoundWidget — plain Widget::hitTest never descends, so a
    // promoted DockCard (or any addChild without loadLayout) paints via
    // renderChildren but never receives clicks. The child HWND still eats
    // OS mouse input → "blocks the main window and its own UI is dead".
    // loadLayout replaces this root with the JSON tree (also compound).
    if (_root == nullptr) {
        _root = new CompoundWidget();
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

    // PR-InputTrace: pure-additive stderr trace guarded by AYUI_TRACE_INPUT.
    // Gallery S1/S2 (2026-08-07) root-cause verification: is _root actually
    // a CompoundWidget (so pickTopmostWidget descends into VBox children)
    // or a plain Widget (so hitTest only matches self, blocking ScrollBar
    // drag/click routing from UIManager::onMouseButtonDown)? Also surface
    // overlay root type and client size for cross-check.
    traceRootType("UIManager::initialize", _root, _overlayRoot);
}

// PR-C1 — Tooltip driver hooks (passive hover-timer).
//
// registerTooltip is called by Tooltip::attachTo AFTER the overlay
// mount succeeds; unregisterTooltip by Tooltip::detach / ~Tooltip.
// Idempotent: registering twice is a no-op; unregistering an unknown
// tooltip is also a no-op (defends against double-unregister races).
//
// The list is non-owning; destroyWidgetTree on the overlay frees the
// Tooltip nodes, but the `unregister` call MUST happen before the
// destroy so the next update() doesn't deref a freed pointer — hence
// the explicit `_tooltips.clear()` at the top of tearDownOverlayChildren
// and the per-popup erase in closePopup.
void UIManager::registerTooltip(Tooltip* tip) {
    if (tip == nullptr) return;
    if (std::find(_tooltips.begin(), _tooltips.end(), tip) != _tooltips.end()) {
        return;   // already registered
    }
    _tooltips.push_back(tip);
}

void UIManager::unregisterTooltip(Tooltip* tip) {
    if (tip == nullptr) return;
    auto it = std::find(_tooltips.begin(), _tooltips.end(), tip);
    if (it == _tooltips.end()) return;
    _tooltips.erase(it);
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

    // PR-C1 — drop the Tooltip registry BEFORE destroyWidgetTree runs
    // on the overlay's children. The destroy loop frees the Tooltip
    // nodes (openPopup mounted them via addChild, not addChildExternal),
    // and the next update() would deref the dead pointers if we kept
    // them in `_tooltips`. Same R3 landmine class as the _activeDropdown
    // and _activeModal clears above.
    _tooltips.clear();

    // UI animation lane — flush the pending-close queue BEFORE the
    // destroy loop below. Soft-close entries (destroy=false) detach here,
    // leaving the popup an orphan for its owner (ComboBox dtor) to free —
    // exactly the pre-fade contract. destroy=true entries' popups are
    // still mounted, so the destroy loop below frees them as usual; the
    // queue must be cleared so no stale entry dereferences the freed
    // pointer on a later update.
    for (auto& entry : _pendingPopupCloses) {
        if (entry.popup != nullptr && !entry.destroy) {
            entry.popup->detachFromParent();
        }
    }
    _pendingPopupCloses.clear();

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
        // Soft-dismiss MenuBar-owned Menus: never destroyWidgetTree them
        // (MenuBar::_menus still holds the pointer). Hard-destroy other
        // overlay popups (ComboBox ListView, tooltips, etc.).
        if (Menu* menu = dynamic_cast<Menu*>(child)) {
            // Prefer abandon over dismissFromManager during tree teardown:
            // dismiss reparents onto MenuBar via addChild and can race
            // destroyWidgetTree ownership. Soft-clear + leave parent as-is;
            // MenuBar::~MenuBar deletes via _menus.
            menu->detachForHostDestruction();
            if (menu->getParent() == _overlayRoot) {
                menu->detachFromParent();
            }
            continue;
        }
        destroyWidgetTree(child);
    }
}

// ============================================================================
// Polish (P3) — Menu accelerator registration.
// ============================================================================
// MenuBar ctor calls registerMenuBar(this) (only when tryGet() returns
// non-null; tests construct MenuBars without a UIManager and rely on
// findAccel being inert in that case). unregisterMenuBar runs from
// MenuBar's dtor. We do NOT own these pointers — they're borrowed. If
// the host neglects to call shutdown() and just deletes MenuBars, the
// dtors will null themselves out via unregisterMenuBar.
// ============================================================================

void UIManager::registerMenuBar(MenuBar* bar) {
    if (bar == nullptr) return;
    // Skip duplicates (idempotent). Tests that construct MenuBars in a
    // scope + drop them repeatedly should not pile up stale entries.
    for (MenuBar* existing : _menuBars) {
        if (existing == bar) return;
    }
    _menuBars.push_back(bar);
}

void UIManager::unregisterMenuBar(MenuBar* bar) {
    if (bar == nullptr) return;
    for (auto it = _menuBars.begin(); it != _menuBars.end(); ++it) {
        if (*it == bar) {
            _menuBars.erase(it);
            return;
        }
    }
}

void UIManager::shutdown() {
    if (_shutdown) {
        return;
    }
    _shutdown = true;

    // Polish (P3): clear the MenuBar registry before destroying the tree.
    // MenuBar dtors will unregister themselves too, but we walk-and-clear
    // first to defend against a path where a MenuBar's dtor runs BEFORE
    // Menu widgets underneath it (a CompoundWidget destruction pattern);
    // without this clear, the dtor chain could touch stale _menuBars
    // entries mid-shutdown.
    _menuBars.clear();

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
    // G12 — cancel any in-flight drag session. We do NOT fire the
    // source's _onDragEnd here because by shutdown time the source may
    // be mid-destruction; the R3-safe clearDragStateNoDispatch is the
    // parallel to clearFocusNoDispatch above. Ghost is torn down below
    // with the overlay root.
    clearDragStateNoDispatch(nullptr);
    // Phase C (S4): also drop any in-flight composition. We don't fire
    // End on the owner because by shutdown time the owner may already
    // be part of the tree about to be destroyed, and we just cleared
    // _focusedWidget above so cancelComposition's owner-strict check
    // would no-op anyway. Force-clear the slot.
    _compositionOwner = nullptr;
    _composing = false;
    _loader.clearEventBindings();
    _loader.clearWidgetRegistry();
    releaseRootLayer();
    g_rootLayerStates.erase(this);

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
    releaseRootLayer();
    if (_focusedWidget != nullptr) {
        // Phase D (D2) — clearFocusNoDispatch instead of
        // `dynamic_cast + setFocus(false)`. By tree-mutating time (a
        // reload/loadLayout, the about-to-die widgets in _root may be
        // partially destroyed; invoking a virtual function on them is
        // undefined behavior (R3 landmine, same pattern as Phase C
        // TextInput::~TextInput). See clearFocusNoDispatch docstring.
        clearFocusNoDispatch(_focusedWidget);
    }
    clearDragStateNoDispatch(nullptr);
    // Reload / host callbacks often run from inside onMouseButtonUp.
    // Null hover + capture WITHOUT onMouseLeave — those widgets are about
    // to be freed, and updateHoverWidget after the callback would UAF
    // (Gallery Reload JSON: updateHoverWidget → onMouseLeave on 0xF...F).
    _capturedWidget = nullptr;
    _hoverWidget = nullptr;
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
    traceRootType("UIManager::loadLayout", _root, _overlayRoot);
    return _root != nullptr;
}

bool UIManager::loadFromString(const std::string& json) {
    releaseRootLayer();
    if (_focusedWidget != nullptr) {
        // Phase D (D2) — clearFocusNoDispatch instead of
        // `dynamic_cast + setFocus(false)`. By tree-mutating time (a
        // reload/loadLayout, the about-to-die widgets in _root may be
        // partially destroyed; invoking a virtual function on them is
        // undefined behavior (R3 landmine, same pattern as Phase C
        // TextInput::~TextInput). See clearFocusNoDispatch docstring.
        clearFocusNoDispatch(_focusedWidget);
    }
    clearDragStateNoDispatch(nullptr);
    _capturedWidget = nullptr;
    _hoverWidget = nullptr;
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
    traceRootType("UIManager::loadFromString", _root, _overlayRoot);
    return _root != nullptr;
}

void UIManager::disableLayoutHotReload() {
    _loader.stopHotReload();
}

void UIManager::bindEvent(const std::string& widgetId, const std::string& eventType,
                          std::function<void()> handler) {
    _loader.bindEvent(widgetId, eventType, handler);
}

void UIManager::setClientSize(float width, float height) {
    _physicalClientWidth = std::isfinite(width) ? std::max(0.0f, width) : 0.0f;
    _physicalClientHeight = std::isfinite(height) ? std::max(0.0f, height) : 0.0f;
    _clientWidth = _physicalClientWidth / _effectiveScale;
    _clientHeight = _physicalClientHeight / _effectiveScale;
    if (_root) {
        _root->setPosition(math::FVector2(0.0f, 0.0f));
        _root->setSize(math::FVector2(_clientWidth, _clientHeight));
        // Force the next layout() to actually run — we just resized the
        // root, which means children need a fresh performLayout pass.
        _lastLayoutWidth = -1.0f;
        _lastLayoutHeight = -1.0f;
    }
    // Phase A: keep overlay in lock-step with viewport so popup world
    // coords map 1:1 to screen.
    if (_overlayRoot != nullptr) {
        _overlayRoot->setPosition(math::FVector2(0.0f, 0.0f));
        _overlayRoot->setSize(math::FVector2(_clientWidth, _clientHeight));
    }
    if (auto it = g_rootLayerStates.find(this); it != g_rootLayerStates.end()) {
        it->second.descValid = false;
    }
}

void UIManager::setDpiScale(float scale) {
    const float clamped = std::isfinite(scale) ? std::clamp(scale, 0.5f, 8.0f) : 1.0f;
    if (_dpiScale == clamped) return;
    _dpiScale = clamped;
    _effectiveScale = _dpiScale * _uiScale;
    setClientSize(_physicalClientWidth, _physicalClientHeight);
}

void UIManager::setUiScale(float scale) {
    const float clamped = std::isfinite(scale) ? std::clamp(scale, 0.5f, 4.0f) : 1.0f;
    if (_uiScale == clamped) return;
    _uiScale = clamped;
    _effectiveScale = _dpiScale * _uiScale;
    setClientSize(_physicalClientWidth, _physicalClientHeight);
}

math::FVector2 UIManager::physicalToLogical(const math::FVector2& point) const {
    return math::FVector2(point.x / _effectiveScale, point.y / _effectiveScale);
}

math::FVector2 UIManager::logicalToPhysical(const math::FVector2& point) const {
    return math::FVector2(point.x * _effectiveScale, point.y * _effectiveScale);
}

void UIManager::update(float dt) {
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);
    if (Widget* reloaded = _loader.tryReload()) {
        // R-7: cancel any in-flight mouse capture BEFORE destroying the
        // tree. The captured widget (typically a Window being dragged) is
        // about to be freed; without this, _capturedWidget stays non-null
        // and the next onMouseButtonUp dereferences a dangling pointer.
        // cancelCapture() notifies the captured widget so transactional
        // gestures can roll back while ordinary drag controls still reset.
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
        releaseRootLayer();
        if (_root != nullptr) {
            destroyWidgetTree(_root);
        }
        _root = reloaded;
        _lastLayoutWidth = -1.0f;
        _lastLayoutHeight = -1.0f;
        setClientSize(_physicalClientWidth, _physicalClientHeight);
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
    // PR-C3 — tick the overlay subtree too. Menus live on the overlay
    // (via openPopup → _overlayRoot->addChild) and need tick(dt) to
    // drive their typeahead timer (Menu::tick accumulates the buffer
    // reset window). Tooltips get an explicit driver loop below; menus
    // get the cascade here. PR-anim: compoundDescendTick, NOT
    // _overlayRoot->tick() — the overlay root is a plain Widget whose
    // default tick does not walk children, so a bare tick() never reaches
    // popups: no fade advance for the menu pop-in animation, no typeahead
    // auto-clear. The compound helper advances the overlay root and invokes
    // each popup's virtual tick; popup overrides keep their base chain.
    if (_overlayRoot != nullptr) {
        compoundDescendTick(_overlayRoot, dt);
    }

    // Re-validate hover against the last known pointer. Pure-hover leave
    // for SplitterHandle depends on updateHoverWidget firing onMouseLeave;
    // if a prior move left `_hover` armed because hitTest still returned
    // the same (too-wide) handle, the next frame's re-hit with a corrected
    // band clears it. Do not synthesize onMouseMove here — that would
    // re-arm hover every tick while the cursor is idle on the band.
    if (_root != nullptr && _hasLastMouse && _capturedWidget == nullptr) {
        Widget* hit = pickTopmostWidget(math::FVector2(_lastMouseX, _lastMouseY));
        if (ayuiTraceInputEnabled() && hit != _hoverWidget) {
            dockTrace("[RevalTrace] lastMouse=(%.1f,%.1f) hover=%s -> hit=%s "
                      "type=%s\n",
                      static_cast<double>(_lastMouseX),
                      static_cast<double>(_lastMouseY),
                      widgetLabel(_hoverWidget),
                      widgetLabel(hit),
                      hit ? typeid(*hit).name() : "-");
        }
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

    // PR-C1 — drive all registered Tooltips. tick(dt, mousePos, viewport)
    // advances the hover-timer using the current mouse position; the
    // tooltip's hit test (target's worldBounds.contains(mousePos)) decides
    // whether the timer accumulates or resets. Tooltip::tick already
    // guards `_target == nullptr` and falls back to UIManager::getClientSize
    // when given (0,0), so this driver needs no extra logic.
    //
    // Skip the entire loop when `_hasLastMouse == false` — passing
    // (0,0) would falsely trigger any tooltip whose anchor happens to
    // contain the origin. The tooltip stays in whatever state it was
    // before initialize() — typically hidden — which matches "no mouse
    // state yet → no hover intent".
    //
    // We snapshot the list before iterating because a Tooltip's tick path
    // could trigger detach() (e.g. an edge case where the user wires a
    // callback that calls detach() under a hover) which would mutate
    // `_tooltips` mid-loop. The snapshot keeps the iteration stable.
    if (_hasLastMouse && !_tooltips.empty()) {
        const std::vector<Tooltip*> snapshot = _tooltips;
        const math::FVector2 mousePos(_lastMouseX, _lastMouseY);
        const math::FVector2 viewport(_clientWidth, _clientHeight);
        for (Tooltip* tip : snapshot) {
            if (tip != nullptr) {
                tip->tick(dt, mousePos, viewport);
            }
        }
    }

    // UI animation lane: finalize popups whose fade-out completed this
    // frame (detach / destroy). Runs AFTER the tick cascade so the fade
    // tween itself was advanced above.
    flushPendingPopupCloses();
}

void UIManager::invalidateLayout() {
    _lastLayoutWidth = -1.0f;
    _lastLayoutHeight = -1.0f;
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
    // AI-1 (2026-07-20): single-call wrapper preserves the legacy
    // contract for callers that want the full populate+flush in one
    // call (tests, standalone demo drivers). AYEditor's composite
    // path uses populateFrame + flushFrame directly to interleave
    // the RenderPass dispatch between populate and flush.
    populateFrame();
    flushFrame();
}

void UIManager::populateFrame() {
    if (!_backend || !_root) {
        return;
    }
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);

    _backend->beginFrame();
    _backend->setUiScale(_effectiveScale);
    math::FRectangle viewport(0.0f, 0.0f,
                              _physicalClientWidth, _physicalClientHeight);
    _backend->beginCanvas(viewport);
    bool rootPresented = false;
    auto rootLayerIt = g_rootLayerStates.find(this);
    if (rootLayerIt != g_rootLayerStates.end() && rootLayerIt->second.enabled
        && _backend->supportsRenderTargets()
        && _clientWidth > 0.0f && _clientHeight > 0.0f) {
        RootLayerState& rootLayer = rootLayerIt->second;
        IRenderBackend::LayerDesc desc;
        desc.logicalBounds = math::FRectangle(0.0f, 0.0f, _clientWidth, _clientHeight);
        desc.dpiScale = _effectiveScale;
        desc.hasAlpha = true;
        desc.overlay = false;
        desc.clearMode = IRenderBackend::LayerClearMode::Transparent;

        const bool descChanged = !rootLayer.descValid
            || rootLayer.desc.logicalBounds.minX != desc.logicalBounds.minX
            || rootLayer.desc.logicalBounds.minY != desc.logicalBounds.minY
            || rootLayer.desc.logicalBounds.maxX != desc.logicalBounds.maxX
            || rootLayer.desc.logicalBounds.maxY != desc.logicalBounds.maxY
            || rootLayer.desc.dpiScale != desc.dpiScale;
        if (!rootLayer.layer.isValid()) {
            rootLayer.layer = _backend->createLayer(desc);
            rootLayer.painted = false;
        } else if (descChanged && !_backend->updateLayer(rootLayer.layer, desc)) {
            _backend->releaseLayer(rootLayer.layer);
            rootLayer.layer = _backend->createLayer(desc);
            rootLayer.painted = false;
        }
        if (rootLayer.layer.isValid()) {
            rootLayer.desc = desc;
            rootLayer.descValid = true;
            const bool backendDirty = _backend->isLayerDirty(rootLayer.layer);
            std::vector<math::FRectangle> pendingRegions = _root->getDirtyRegions();
            const bool hasDamage = !pendingRegions.empty() || _root->hasDirtyRect();
            const bool needsPaint = !rootLayer.painted || _root->isDirtyThis()
                || hasDamage || backendDirty;
            bool layerReady = !needsPaint;
            if (needsPaint) {
                bool fullRedraw = !rootLayer.painted || _root->isDirtyThis()
                    || backendDirty;
                if (!fullRedraw && hasDamage) {
                    if (pendingRegions.empty()) {
                        pendingRegions.push_back(_root->getDirtyRect());
                    }
                    float repaintArea = 0.0f;
                    for (math::FRectangle& damage : pendingRegions) {
                        damage = math::FRectangle(
                            std::max(desc.logicalBounds.minX, damage.minX),
                            std::max(desc.logicalBounds.minY, damage.minY),
                            std::min(desc.logicalBounds.maxX, damage.maxX),
                            std::min(desc.logicalBounds.maxY, damage.maxY));
                        if (!Widget::isDirtyRectEmpty(damage)) {
                            repaintArea += (damage.maxX - damage.minX)
                                * (damage.maxY - damage.minY);
                        }
                    }
                    pendingRegions.erase(std::remove_if(
                        pendingRegions.begin(), pendingRegions.end(),
                        [](const math::FRectangle& damage) {
                            return Widget::isDirtyRectEmpty(damage);
                        }), pendingRegions.end());
                    const float layerArea = _clientWidth * _clientHeight;
                    if (pendingRegions.empty() || repaintArea >= layerArea * 0.70f) {
                        fullRedraw = true;
                    }
                }
                if (fullRedraw) pendingRegions.assign(1u, desc.logicalBounds);

                bool allRegionsPainted = true;
                for (const math::FRectangle& damage : pendingRegions) {
                    if (rootLayer.painted && !backendDirty) {
                        _backend->invalidateLayer(rootLayer.layer,
                            fullRedraw ? math::FRectangle() : damage);
                    }
                    IRenderBackend::LayerPaint paint;
                    paint.damage = damage;
                    paint.fullRedraw = fullRedraw;
                    if (!_backend->beginLayerPaint(rootLayer.layer, paint)) {
                        allRegionsPainted = false;
                        break;
                    }
                    if (!fullRedraw) _backend->pushClip(damage);
                    _root->render(*_backend);
                    if (!fullRedraw) _backend->popClip();
                    _backend->endLayerPaint(rootLayer.layer);
                    rootLayer.painted = true;
                }
                if (!allRegionsPainted) {
                    _backend->invalidateLayer(rootLayer.layer);
                    rootLayer.painted = false;
                    layerReady = false;
                } else {
                    layerReady = rootLayer.painted
                        && !_backend->isLayerDirty(rootLayer.layer);
                }
            }
            if (layerReady) {
                _backend->compositeLayer(rootLayer.layer, desc.logicalBounds, 1.0f);
                rootPresented = true;
            }
        }
    }
    if (!rootPresented) {
        _root->render(*_backend);
    }
    // Phase A: render overlay AFTER the main tree so popups paint on top.
    if (_overlayRoot != nullptr) {
        _overlayRoot->render(*_backend);
    }
    // G12 — render drag ghost AFTER overlay so the cursor-following
    // indicator paints above popups. paintGhost draws the plate +
    // border + payload text; _dragGhost->render() would be a no-op
    // (base Widget has empty onRender).
    paintGhost(*_backend);
    // AI-1: NO flushBatches here. The flushBatches call (which on the
    // bgfx UIRenderBackend flushes pending text batches via
    // flushPendingText()) moved to UIPass::execute so the RenderPass
    // pipeline owns the UI submission boundary. Rects are still
    // flushed in flushFrame() → backend->endFrame() →
    // flushColoredRects().
}

void UIManager::setRootLayerCachingEnabled(bool enabled)
{
    if (!enabled) {
        const auto it = g_rootLayerStates.find(this);
        if (it == g_rootLayerStates.end()) return;
        releaseRootLayer();
        g_rootLayerStates.erase(this);
        return;
    }

    RootLayerState& state = g_rootLayerStates[this];
    if (state.enabled) return;
    state.enabled = true;
    state.painted = false;
}

bool UIManager::isRootLayerCachingEnabled() const
{
    const auto it = g_rootLayerStates.find(this);
    return it != g_rootLayerStates.end() && it->second.enabled;
}

IRenderBackend::LayerCacheStats UIManager::getLayerCacheStats() const
{
    return _backend != nullptr ? _backend->getLayerCacheStats()
                               : IRenderBackend::LayerCacheStats{};
}

void UIManager::setLayerCacheBudgetBytes(size_t bytes)
{
    if (_backend != nullptr) _backend->setLayerCacheBudgetBytes(bytes);
}

void UIManager::resetLayerCacheStats()
{
    if (_backend != nullptr) _backend->resetLayerCacheStats();
}

void UIManager::releaseRootLayer()
{
    const auto it = g_rootLayerStates.find(this);
    if (it == g_rootLayerStates.end()) return;
    RootLayerState& state = it->second;
    if (_backend != nullptr && state.layer.isValid()) {
        _backend->releaseLayer(state.layer);
    }
    state.layer = {-1};
    state.desc = {};
    state.descValid = false;
    state.painted = false;
}

void UIManager::flushFrame() {
    if (!_backend || !_root) {
        return;
    }
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);

    // AI-1: flushBatches() removed here (moved to UIPass::execute).
    // endCanvas + endFrame remain — endFrame() inside the backend
    // flushes pendingRects via flushColoredRects(), then any
    // remaining text via flushPendingText() (no-op now since UIPass
    // already flushed text).
    _backend->endCanvas();
    _backend->endFrame();
}

Widget* UIManager::findById(const std::string& id) const {
    if (Widget* indexed = _loader.findWidgetById(id)) return indexed;
    if (id.empty() || _root == nullptr) return nullptr;

    // Loader-authored widgets stay O(1) through the registry above. Runtime
    // chrome, virtual hosts and other dynamically attached widgets are not
    // loader entries, so fall back to the live tree before reporting a miss.
    std::vector<Widget*> pending{_root};
    while (!pending.empty()) {
        Widget* widget = pending.back();
        pending.pop_back();
        if (widget == nullptr) continue;
        if (widget->getId() == id) return widget;
        const auto& children = widget->getChildren();
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return nullptr;
}

math::FVector2 UIManager::getDragLastMousePos() const {
    // endDrag clears `active` before onDrop/onDragEnd but keeps
    // `source` until those callbacks return — prefer the session's
    // last cursor so DockArea hit-tests the real drop point.
    if (_dragSession.active || _dragSession.source != nullptr) {
        return _dragSession.lastMousePos;
    }
    if (_hasLastMouse) {
        return math::FVector2(_lastMouseX, _lastMouseY);
    }
    return math::FVector2(0.0f, 0.0f);
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

    // UI animation lane: reopening a popup cancels any pending fade-out
    // close (the popup is logically open again). The in-flight fade-out
    // tween is overwritten by the reopen's own fade-in below.
    cancelPendingPopupClose(popup);

    // Close any other popup first. Single-active invariant.
    // Menus are soft-dismissed (MenuBar keeps durable Menu*); ComboBox
    // and other popups still take the hard-destroy path.
    if (_activeDropdown != nullptr && _activeDropdown != popup) {
        if (Menu* menu = dynamic_cast<Menu*>(_activeDropdown)) {
            menu->dismissFromManager();
        } else {
            closePopup(_activeDropdown);
        }
    }

    // Reparent onto the overlay. If popup already lives somewhere,
    // detach it first (it shouldn't but defensive).
    if (popup->getParent() != nullptr) {
        popup->getParent()->removeChild(popup);
    }
    // Menu is owned by MenuBar for the session (soft mount). ComboBox /
    // other popups are owned by the overlay until closePopup(destroy=true).
    // Using addChild for Menu made tearDownOverlayChildren destroyWidgetTree
    // free the Menu while MenuBar::_menus still held it → ~MenuBar UAF.
    if (dynamic_cast<Menu*>(popup) != nullptr) {
        _overlayRoot->addChildExternal(popup);
    } else {
        _overlayRoot->addChild(popup);
    }

    // Track as active.
    _activeDropdown = popup;
    _activeDropdownAnchor = anchor;
    _activeDropdownAnchorIsComboBox =
        (dynamic_cast<ComboBox*>(anchor) != nullptr);

    // A newly mounted popup invalidates its overlay branch for any future
    // retained presentation cache.
    popup->markDirty();
}

void UIManager::abandonPopup(Widget* popup) {
    if (popup == nullptr) return;
    // UI animation lane: the popup is going away through a non-close
    // path — drop any pending fade-out finalize so it can't double-free.
    cancelPendingPopupClose(popup);
    // Bookkeeping only — no isDescendantOf (may walk freed parents) and
    // no removeChild (parent may be mid-destruction with a dead vector).
    if (_activeDropdown == popup) {
        _activeDropdown = nullptr;
        _activeDropdownAnchor = nullptr;
        _activeDropdownAnchorIsComboBox = false;
    }
    if (_capturedWidget == popup) {
        _capturedWidget = nullptr;
    }
    if (_focusedWidget == popup) {
        _focusedWidget = nullptr;
    }
    if (_hoverWidget == popup) {
        _hoverWidget = nullptr;
    }
}

void UIManager::closePopup(Widget* popup, bool destroy) {
    if (popup == nullptr) return;
    // UI animation lane: a synchronous close must cancel any pending
    // fade-out finalize, or the popup would be destroyed twice (once
    // here, once by flushPendingPopupCloses).
    cancelPendingPopupClose(popup);

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

    // Same contract for focus / hover — destroying (or even soft-
    // unmounting) a focused Menu left _focusedWidget dangling and the
    // next setFocus() blew up with std::__non_rtti_object.
    if (_focusedWidget != nullptr) {
        if (_focusedWidget == popup ||
            isDescendantOf(_focusedWidget, popup)) {
            clearFocusNoDispatch(_focusedWidget);
        }
    }
    if (_hoverWidget != nullptr) {
        if (_hoverWidget == popup ||
            isDescendantOf(_hoverWidget, popup)) {
            clearHoverNoDispatch(_hoverWidget);
        }
    }

    // Snapshot the external-owned flag BEFORE removeChild clears it.
    // P3 (Tooltip attachTo change): externally-owned popups are NOT
    // freed by closePopup — the host owns the lifetime and is
    // responsible for the matching `destroyWidgetTree(tip)`. Without
    // this snapshot, the flag would be lost and we'd double-free the
    // popup's internal children (e.g. Tooltip::_label).
    const bool externallyOwned = popup->isExternallyOwned();

    if (popup->getParent() != nullptr) {
        popup->getParent()->removeChild(popup);
    }

    // PR-C1 — if the popup being closed is a Tooltip, drop it from the
    // hover-timer driver list BEFORE destroyWidgetTree (matches the
    // tearDownOverlayChildren clear pattern). Tooltip::detach already
    // unregisters, but hosts calling closePopup(tip, true) directly
    // bypass detach and would otherwise leave a dangling pointer.
    if (Tooltip* tip = dynamic_cast<Tooltip*>(popup)) {
        unregisterTooltip(tip);
    }

    if (destroy) {
        if (notifyCombo && anchor != nullptr) {
            static_cast<ComboBox*>(anchor)->onPopupDismissedByManager();
        }
        if (externallyOwned) {
            return;
        }
        destroyWidgetTree(popup);
    }
}

// =============================================================================
// UI animation lane (cut 1) — animated popup close (fade-out).
//
// closePopup stays synchronous and is used by every path that must not
// linger (openPopup's close-previous, teardown, host dtor). beginPopupFadeOut
// is the UX-driven close: bookkeeping runs NOW (the popup is logically
// closed — input/focus/capture stop immediately), but the popup stays
// mounted to render its fade-out, and a later update() finalizes the
// detach (+ optional destroy) once the fade completes. Reopening the
// popup before then cancels the pending close via openPopup's
// cancelPendingPopupClose, so a quick toggle never eats the popup.
// =============================================================================
namespace {

constexpr float kPopupFadeOutMs = 120.0f;

} // namespace

void UIManager::beginPopupFadeOut(Widget* popup, bool destroy,
                                  Widget* companion) {
    if (popup == nullptr) return;
    if (isPendingPopupClose(popup)) return;

    // Capture anchor BEFORE clearing bookkeeping — ComboBox hosts need a
    // dismiss notification so they can null their non-owning popup pointer
    // before we destroyWidgetTree it. Same snapshot contract as closePopup.
    Widget* anchor = nullptr;
    bool notifyCombo = false;
    if (_activeDropdown == popup) {
        anchor = _activeDropdownAnchor;
        notifyCombo = _activeDropdownAnchorIsComboBox;
        _activeDropdown = nullptr;
        _activeDropdownAnchor = nullptr;
        _activeDropdownAnchorIsComboBox = false;
    }

    // If the captured widget is inside this popup, null it BEFORE the
    // popup is logically closed. Otherwise the next mouse event
    // dereferences a widget that stopped being interactive.
    if (_capturedWidget != nullptr) {
        if (_capturedWidget == popup ||
            isDescendantOf(_capturedWidget, popup)) {
            _capturedWidget = nullptr;
        }
    }

    // Same contract for focus / hover — a closing popup must not keep
    // the manager's focus / hover pointers (mirror of closePopup).
    if (_focusedWidget != nullptr) {
        if (_focusedWidget == popup ||
            isDescendantOf(_focusedWidget, popup)) {
            clearFocusNoDispatch(_focusedWidget);
        }
    }
    if (_hoverWidget != nullptr) {
        if (_hoverWidget == popup ||
            isDescendantOf(_hoverWidget, popup)) {
            clearHoverNoDispatch(_hoverWidget);
        }
    }

    // Snapshot the external-owned flag — removeChild at finalize would
    // clear it (same reason closePopup snapshots it).
    const bool externallyOwned = popup->isExternallyOwned();

    // PR-C1 mirror: a fading Tooltip must leave the hover-timer driver
    // list immediately (it is logically hidden).
    if (Tooltip* tip = dynamic_cast<Tooltip*>(popup)) {
        unregisterTooltip(tip);
    }

    // destroy=true: the ComboBox anchor must forget its non-owning popup
    // pointer NOW — the tree dies at finalize and the anchor must not
    // touch it before then.
    if (destroy && notifyCombo && anchor != nullptr) {
        static_cast<ComboBox*>(anchor)->onPopupDismissedByManager();
    }

    // Start the fade-out (from the current opacity — also correct when a
    // fade-in is cut short by an early close). A popup that is already
    // fully transparent finalizes immediately: no queue entry, no visual.
    popup->animateOpacity(0.0f, kPopupFadeOutMs, AnimationCurve::EaseIn);
    if (!popup->isOpacityAnimating() && popup->getOpacity() <= 0.01f) {
        finishPopupClose(popup, destroy, externallyOwned, companion);
        return;
    }

    PendingPopupClose entry;
    entry.popup = popup;
    entry.destroy = destroy;
    entry.notifyCombo = notifyCombo;
    entry.anchor = anchor;
    entry.externallyOwned = externallyOwned;
    entry.companion = companion;
    _pendingPopupCloses.push_back(entry);
}

void UIManager::finishPopupClose(Widget* popup, bool destroy,
                                 bool externallyOwned, Widget* companion) {
    // Detach the companion (Modal dimmer) first — it renders after the
    // popup on the overlay.
    if (companion != nullptr && companion->getParent() != nullptr) {
        companion->getParent()->removeChild(companion);
    }
    if (popup->getParent() != nullptr) {
        popup->getParent()->removeChild(popup);
    }
    // Menu soft-unmount: the MenuBar owns the tree for the session, so the
    // menu reparents back under it instead of dying.
    if (Menu* menu = dynamic_cast<Menu*>(popup)) {
        menu->onPopupFadeOutCompleted();
        return;
    }
    if (destroy && !externallyOwned) {
        destroyWidgetTree(popup);
    }
}

void UIManager::cancelPendingPopupClose(Widget* popup) {
    if (popup == nullptr) return;
    auto& vec = _pendingPopupCloses;
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (it->popup == popup) {
            vec.erase(it);
            return;
        }
    }
}

bool UIManager::isPendingPopupClose(const Widget* popup) const {
    if (popup == nullptr) return false;
    for (const auto& entry : _pendingPopupCloses) {
        if (entry.popup == popup) {
            return true;
        }
    }
    return false;
}

void UIManager::flushPendingPopupCloses() {
    if (_pendingPopupCloses.empty()) {
        return;
    }
    // Move the list out first: finishPopupClose mutates the overlay
    // (removeChild / destroyWidgetTree) and can re-enter the manager;
    // erasing while iterating the live vector would invalidate us.
    std::vector<PendingPopupClose> pending = std::move(_pendingPopupCloses);
    _pendingPopupCloses.clear();
    for (const auto& entry : pending) {
        if (entry.popup == nullptr) {
            continue;
        }
        if (!entry.popup->isOpacityAnimating() &&
            entry.popup->getOpacity() <= 0.01f) {
            finishPopupClose(entry.popup, entry.destroy,
                             entry.externallyOwned, entry.companion);
        } else {
            // Fade still running — re-queue for the next update.
            _pendingPopupCloses.push_back(entry);
        }
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

    // A newly mounted modal invalidates its overlay branch.
    modal->markDirty();
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

    // UI animation lane: a modal mid-fade-out (closeModal followed by
    // beginPopupFadeOut) stays mounted to render its fade — the pending
    // finalize detaches it instead. Plain close / dtor paths detach here
    // as before.
    if (modal->getParent() != nullptr && !isPendingPopupClose(modal)) {
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
            // UI animation lane: a popup mid-fade-out is logically closed
            // — it renders but must not receive input.
            if (isPendingPopupClose(*it)) {
                continue;
            }
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

    // D5 multi-window invariant (see AYUIManager.h:39-56): input/update
    // dispatch runs with THIS manager as g_activeUIManager, so widget-side
    // tryGet() (DockCard::beginDrag, ComboBox/Menu popup mounts, ListView
    // modifier reads, Tooltip ticks, ...) resolves to the manager that
    // actually received the event. A promoted child window's initialize()
    // re-points g_activeUIManager at the child; without this guard the
    // PRIMARY's card drags would begin sessions inside the child manager
    // (ghost renders in the child window, child clicks get swallowed by
    // the phantom session via the drag-active short-circuit below).
    const ActiveScope selfGuard(this);

    const math::FVector2 logical = physicalToLogical(math::FVector2(x, y));
    x = logical.x;
    y = logical.y;
    _lastMouseX = x;
    _lastMouseY = y;
    _hasLastMouse = true;

    // G12 — drag session is its own channel: when active, route the move
    // through updateDrag (drop target detection) and return. Captured-
    // mouse widgets (SplitterHandle / Slider / ScrollBar thumb / Window
    // title-drag / TextInput drag-select) are independent; a drag
    // session is rejected if capture is non-null in beginDrag, so we
    // never see drag+active-capture concurrently.
    if (_dragSession.active) {
        updateDrag(x, y);
        return true;
    }

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
        const bool handled = hit->onMouseMove(UIMouseEvent(pos, 0));
        if (ayuiTraceInputEnabled()) {
            if (auto* iw = dynamic_cast<InteractiveWidget*>(hit)) {
                dockTrace("[InputTrace] onMouseMove pos=(%.1f,%.1f) hit=%s "
                          "type=%s parent=%s handled=%d hover=%d state=%d\n",
                          static_cast<double>(x), static_cast<double>(y),
                          widgetLabel(hit), typeid(*hit).name(),
                          (hit->getParent() != nullptr)
                              ? widgetLabel(hit->getParent()) : "-",
                          handled ? 1 : 0,
                          iw->isMouseOver() ? 1 : 0,
                          static_cast<int>(iw->getState()));
            } else {
                dockTrace("[InputTrace] onMouseMove pos=(%.1f,%.1f) hit=%s "
                          "type=%s handled=%d (non-interactive)\n",
                          static_cast<double>(x), static_cast<double>(y),
                          widgetLabel(hit), typeid(*hit).name(),
                          handled ? 1 : 0);
            }
        }
        return handled;
    }
    return false;
}

// PR-B3 — wheel routing. Hit-tests the topmost widget, then walks UP the
// parent chain calling onMouseWheel on each ancestor until one returns
// true (the widget scrolled something, so we stop). The bottom-up walk
// mirrors how focus routing works: the deepest widget that knows how to
// handle a wheel wins, so nested containers (ScrollView inside another
// ScrollView, ComboBox popup with an outer ScrollView) route correctly.
//
// Why not the reverse (outer first): Win32 / macOS / GTK all route wheel
// to the deepest scrollable under the cursor. The OS convention is
// "scroll the thing under the cursor" — bubbling UP only after the inner
// widget declines.
//
// Why not pop the overlay first explicitly: pickTopmostWidget already
// prefers the overlay over the root tree (see implementation), so a
// ComboBox popup ListView is the natural deepest hit when the cursor
// is on the popup. We then bubble through the popup's parent chain
// (which is the ComboBox itself → its parent), and the popup's
// onMouseWheel override consumes the event before the outer ScrollView
// gets a chance.
bool UIManager::onMouseWheel(float x, float y, float deltaY) {
    if (!_root) {
        return false;
    }
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);
    const math::FVector2 pos = physicalToLogical(math::FVector2(x, y));
    Widget* hit = pickTopmostWidget(pos);
    if (hit == nullptr) {
        return false;
    }
    const UIMouseWheelEvent ev(pos, deltaY);
    Widget* cur = hit;
    while (cur != nullptr) {
        if (cur->onMouseWheel(ev)) {
            return true;
        }
        cur = cur->getParent();
    }
    return false;
}

bool UIManager::onMouseButtonDown(float x, float y, int button) {
    if (!_root) {
        return false;
    }
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);
    const math::FVector2 logical = physicalToLogical(math::FVector2(x, y));
    x = logical.x;
    y = logical.y;

    // G12 — drag-active short-circuit. If a drag is in progress and the
    // user presses a button, treat it as an explicit end-of-drag (e.g.
    // drag-then-press on a popup). Without this, the click-outside-popup
    // detector + modal-block checks would fire mid-drag and confuse the
    // session. Mouse-up still owns the canonical end-of-drag path; this
    // is the fallback.
    if (_dragSession.active) {
        endDrag(true);
        return true;
    }

    math::FVector2 pos(x, y);
    Widget* hit = pickTopmostWidget(pos);
    if (ayuiTraceInputEnabled()) {
        // dockTrace (file-backed) instead of fprintf(stderr): GUI apps
        // launched from Git Bash don't inherit stderr into redirected
        // files, so stderr-only traces silently vanish.
        dockTrace("[InputTrace] onMouseButtonDown pos=(%.1f,%.1f) hit=%s type=%s parent=%s\n",
                  static_cast<double>(x), static_cast<double>(y),
                  widgetLabel(hit),
                  hit ? typeid(*hit).name() : "-",
                  (hit != nullptr && hit->getParent() != nullptr)
                      ? widgetLabel(hit->getParent()) : "-");
    }
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
            if (Menu* menu = dynamic_cast<Menu*>(_activeDropdown)) {
                menu->dismissFromManager();
            } else {
                // UI animation lane: UX-driven close — fade out instead of
                // vanishing. Pass the popup by value (beginPopupFadeOut
                // clears _activeDropdown, so a later read would be null).
                // destroy=true: the popup tree is freed at finalize.
                beginPopupFadeOut(_activeDropdown, /*destroy=*/true);
            }
        }
    }

    // Click-away: clear text-editing focus when the press lands outside
    // the focused field. Other widgets may claim focus in onMouseButtonDown
    // immediately after (e.g. another TextInput / ListView).
    if (_focusedWidget != nullptr && isTextEditing(_focusedWidget)) {
        const bool insideFocused = (hit != nullptr) && (
            hit == _focusedWidget ||
            isDescendantOf(hit, _focusedWidget));
        if (!insideFocused) {
            setFocus(nullptr);
        }
    }

    Widget* candidate = hit;
    while (candidate != nullptr) {
        if (candidate->onMouseButtonDown(UIMouseEvent(pos, button))) {
            _capturedWidget = candidate;
            return true;
        }

        Widget* current = candidate;
        Widget* next = nullptr;
        bool blocked = false;
        while (current != nullptr && current->getParent() != nullptr) {
            Widget* parent = current->getParent();
            if (parent->blocksLowerPointerInput()) {
                blocked = true;
                break;
            }
            const bool maySearch =
                parent->retriesUnhandledPointerWithinChildren()
                || current->allowsUnhandledPointerRetryBehind();
            if (maySearch) {
                const auto& siblings = parent->getChildren();
                const auto branch = std::find(siblings.begin(), siblings.end(), current);
                if (branch != siblings.end()) {
                    for (auto it = std::make_reverse_iterator(branch);
                         it != siblings.rend(); ++it) {
                        if (*it == nullptr) continue;
                        next = (*it)->hitTest(pos);
                        if (next != nullptr) break;
                    }
                }
                if (next != nullptr) break;
            }
            current = parent;
        }
        if (blocked) return true;
        candidate = next;
    }
    return false;
}

bool UIManager::onMouseButtonUp(float x, float y, int button) {
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);
    const math::FVector2 logical = physicalToLogical(math::FVector2(x, y));
    x = logical.x;
    y = logical.y;
    // Phase A (A2): the click-outside detector already fired in
    // onMouseButtonDown; we only need to deliver the up to whatever was
    // captured (or whatever's under the cursor). When _root is null we
    // can still update hover state but there's nothing to forward to.
    _lastMouseX = x;
    _lastMouseY = y;
    _hasLastMouse = true;
    if (_dragSession.active) {
        // Final hit-test at the release point — without this, currentTarget
        // is stuck on the last move and a same-slot / retarget drop can
        // miss DockArea::onDrop entirely (or use a stale target).
        updateDrag(x, y);
    }
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
    //
    // If the up-handler reloaded the tree (loadLayout), `_hoverWidget` was
    // already nulled; updateHoverWidget(nullptr, next) is then safe.
    // Guard the empty-root case so we don't walk a null tree.
    if (_root == nullptr) {
        _hoverWidget = nullptr;
    } else {
        updateHoverWidget(_hoverWidget, pickTopmostWidget(pos));
    }

    // G12 — drag session ends on mouse-up. endDrag fires target->onDrop
    // if the cursor is over an accepting widget, then resets state.
    // Returns true so the host/manager can short-circuit follow-ups
    // (e.g. click-outside-popup-close won't fire because we already
    // returned). captured-widget drags (Slider/SplitterHandle/etc.)
    // are independent — handled above by the target->onMouseButtonUp
    // path; G12 just observes the up as the "drop here" event.
    if (_dragSession.active) {
        endDrag(true);
        return true;
    }

    return handled;
}

// =============================================================================
// G12 — Drag & Drop session implementation.
// =============================================================================
//
// Session lifecycle:
//   beginDrag(source)   → creates session, fires source->_onDragStart,
//                          shows ghost at cursor
//   updateDrag(x,y)     → on every mouse-move; walks widget-under-cursor
//                          up to nearest accepting ancestor; fires
//                          onDragLeave / onDragEnter on transition
//   endDrag(accepted)   → on mouse-up; if accepted AND target present,
//                          fires target->onDrop; fires source->_onDragEnd;
//                          resets state
//   cancelDrag()        → Esc / external; endDrag(false) — no onDrop fires,
//                          onDragEnd(false) fires on source
//
// Invariants:
//   - at most ONE active session (beginDrag is no-op if active)
//   - session does NOT touch _capturedWidget (separate channel)
//   - ghost is addChildExternal to overlay root (or main root if no overlay)
//   - clearDragStateNoDispatch is the R3-safe dtor path that bypasses
//     virtual callbacks; called by ~Widget equivalents and by shutdown
// =============================================================================

void UIManager::ensureGhostCreated() {
    if (_dragGhost != nullptr) return;
    _dragGhost = new Widget();
    _dragGhost->setSize(math::FVector2(120.0f, 24.0f));
    _dragGhost->setVisible(false);
    // Ghost lives on the overlay if there is one; otherwise on the main
    // root. addChildExternal = host (UIManager) owns delete; we tear it
    // down in shutdown via destroyWidgetTree.
    if (_overlayRoot != nullptr) {
        _overlayRoot->addChildExternal(_dragGhost);
    } else if (_root != nullptr) {
        _root->addChildExternal(_dragGhost);
    }
}

void UIManager::updateGhostPosition(const math::FVector2& pos) {
    if (_dragGhost == nullptr) return;
    // Slight cursor offset so the ghost doesn't sit directly under the
    // mouse cursor (small "+12, +8" indirection — matches Qt's default
    // and most editor conventions).
    _dragGhost->setPosition(math::FVector2(pos.x + 12.0f, pos.y + 8.0f));
    // Ghost movement invalidates any future cached presentation.
    _dragGhost->markDirty();
}

void UIManager::paintGhost(IRenderBackend& renderer) {
    if (!_dragSession.active || _dragGhost == nullptr) return;
    if (!_dragGhost->isVisible()) return;
    const math::FRectangle b = _dragGhost->getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;
    // Dark semi-transparent plate — B3: rounded to match the 2px border.
    renderer.drawRoundedRect(b, math::FVector4(0.15f, 0.16f, 0.20f, 0.85f), 2.0f);
    // 1px accent border (matches the drop-target highlight palette).
    renderer.drawBorderRect(b,
        math::FVector4(0.40f, 0.48f, 0.62f, 1.0f), 1.0f, 2.0f);
    // Payload text (best-effort: mock backends return 0 from measureText,
    // so we don't try to clip the string to the ghost width — we just
    // pass it through and let the renderer truncate).
    if (!_dragSession.payload.text.empty()) {
        renderer.drawText(b, _dragSession.payload.text, 12,
            math::FVector4(0.95f, 0.95f, 0.97f, 1.0f));
    } else if (!_dragSession.payload.kind.empty()) {
        // Fallback: show the kind tag if text is empty.
        const std::wstring kindW(_dragSession.payload.kind.begin(),
                                  _dragSession.payload.kind.end());
        renderer.drawText(b, kindW, 12,
            math::FVector4(0.95f, 0.95f, 0.97f, 1.0f));
    }
}

bool UIManager::beginDrag(Widget* source) {
    if (!_root || source == nullptr) {
        return false;
    }
    if (_dragSession.active) {
        return false;   // one session at a time
    }
    if (_capturedWidget != nullptr) {
        if (_capturedWidget != source) {
            // SplitterHandle/Slider/ScrollBar thumb/Window title-drag own
            // the capture; their drags don't overlap with G12. Bail.
            return false;
        }
        // Threshold-based drag sources capture on press so they continue to
        // receive motion even if a low-frequency pointer jumps directly over
        // another widget. Promotion to the cross-widget drag channel transfers
        // ownership away from ordinary mouse capture without synthesizing an
        // early mouse-up on the source.
        _capturedWidget = nullptr;
    }
    if (source->getParent() == nullptr) {
        // Host must wire source into the tree first; otherwise the ghost
        // can't render against a sensible coordinate space and the
        // session is meaningless.
        return false;
    }
    _dragSession.active        = true;
    _dragSession.source        = source;
    _dragSession.currentTarget = nullptr;
    _dragSession.lastMousePos  = math::FVector2(0.0f, 0.0f);
    _lastDragHadDropTarget = false;

    if (source->_onDragStart) {
        source->_onDragStart();
    }
    // Re-read AFTER onDragStart so hosts can refresh kind/text/data
    // (DockCard stamps payload.data = this there). Pre-stamp before
    // beginDrag remains required for the empty-payload gate.
    _dragSession.payload = source->getDragPayload();
    ensureGhostCreated();
    if (_dragGhost != nullptr) {
        _dragGhost->setVisible(true);
        // Visibility change invalidates any future cached presentation.
        _dragGhost->markDirty();
        // PR-S5e: start the ghost at the PRESS position (last known
        // cursor), not the source's center. The old code spawned it at
        // the card's center and the first onMouseMove snapped it to the
        // cursor — dock title drags read as "the label lags the pointer
        // for one frame, then teleports". _lastMouseX/_lastMouseY are
        // the most recent cursor position (the down that triggered
        // beginDrag); source-center remains the fallback for the
        // no-mouse-yet edge.
        if (_hasLastMouse) {
            updateGhostPosition(math::FVector2(_lastMouseX, _lastMouseY));
        } else {
            const math::FRectangle sb = source->getWorldBounds();
            updateGhostPosition(math::FVector2(
                (sb.minX + sb.maxX) * 0.5f,
                (sb.minY + sb.maxY) * 0.5f));
        }
    }
    return true;
}

void UIManager::updateDrag(float x, float y) {
    if (!_dragSession.active) return;

    _dragSession.lastMousePos = math::FVector2(x, y);
    updateGhostPosition(math::FVector2(x, y));

    // Walk up from the hit widget to find the nearest accepting
    // ancestor. Skipping the source's own subtree (we don't allow
    // dragging onto ourselves — Qt behavior). Also ignore the drag
    // ghost (lives on the overlay root and must not steal the drop).
    Widget* hit = pickTopmostWidget(math::FVector2(x, y));
    if (hit == _dragGhost) {
        hit = pickWidgetAt(_root, math::FVector2(x, y));
    }
    Widget* newTarget = nullptr;
    Widget* w = hit;
    while (w != nullptr) {
        if (w == _root) break;   // don't target the root directly
        if (w == _dragGhost) {
            w = w->getParent();
            continue;
        }
        // AYUI-Audit-2026-08-26 (D batch): per-kind acceptance. A target
        // whose acceptDropKinds list excludes the current payload's kind
        // is invisible to this drag (no onDragEnter highlight, no
        // onDrop). Empty list = accept any (legacy behavior preserved).
        if (w->isAcceptDrops() &&
            w->acceptsKind(_dragSession.payload.kind)) {
            newTarget = w;
            break;
        }
        w = w->getParent();
    }

    // Reject the drag source itself as a drop target. A common editor
    // convention: dragging a tab onto itself is a no-op (vs. Unity
    // Editor which DOES allow it for "duplicate into self"; we choose
    // the conservative Qt behavior here — host can override by
    // disabling the source's acceptDrops / not marking source accept).
    if (newTarget == _dragSession.source) {
        newTarget = nullptr;
    }

    if (newTarget != _dragSession.currentTarget) {
        // Leave old target.
        if (_dragSession.currentTarget != nullptr) {
            _dragSession.currentTarget->setCurrentDropTarget(false);
            // Highlight state is part of the widget presentation cache key.
            _dragSession.currentTarget->markDirty();
            if (_dragSession.currentTarget->_onDragLeave) {
                _dragSession.currentTarget->_onDragLeave();
            }
        }
        // Enter new target.
        if (newTarget != nullptr) {
            newTarget->setCurrentDropTarget(true);
            // Highlight state is part of the widget presentation cache key.
            newTarget->markDirty();
            if (newTarget->_onDragEnter) {
                newTarget->_onDragEnter(_dragSession.payload);
            }
        }
        _dragSession.currentTarget = newTarget;
    }
}

bool UIManager::endDrag(bool accepted) {
    if (!_dragSession.active) return false;

    // Snapshot target/payload; keep `source` alive through onDrop /
    // onDragEnd so DockArea can resolve getDragSource() (header contract)
    // and DockCard can promote on void-drop. Hide the ghost and mark
    // inactive first so isDragging() is false during callbacks.
    Widget*     source = _dragSession.source;
    Widget*     target = _dragSession.currentTarget;
    DragPayload payload = _dragSession.payload;
    const bool  hadTarget = (target != nullptr);
    _lastDragHadDropTarget = hadTarget;

    if (target != nullptr) {
        target->setCurrentDropTarget(false);
        // Drop-target highlight state changed.
        target->markDirty();
    }
    _dragSession.active        = false;
    _dragSession.currentTarget = nullptr;
    if (_dragGhost != nullptr) {
        _dragGhost->setVisible(false);
        // Preserve invalidation metadata for a later retained cache.
        _dragGhost->markDirty();
    }

    if (target != nullptr) {
        if (accepted && target->_onDrop) {
            target->_onDrop(payload);
        } else {
            dockTrace("[dock] endDrag skip onDrop accepted=%d hasCb=%d\n",
                      accepted ? 1 : 0, target->_onDrop ? 1 : 0);
        }
        if (target->_onDragLeave) {
            target->_onDragLeave();
        }
    } else {
        dockTrace("[dock] endDrag NO drop target — void drop path\n");
    }

    // PR-Dock-TearOff: report plain `accepted` (Esc/cancel → false, mouse
    // up → true), NOT `accepted && hadTarget`. A drag released over no
    // accepting target is a legitimate "I dropped it in the void" signal —
    // DockCard uses it to promote the card to a host window. Previously
    // the void-drop collapsed to false and was indistinguishable from Esc.
    if (source != nullptr && source->_onDragEnd) {
        source->_onDragEnd(accepted);
    }

    _dragSession.source  = nullptr;
    _dragSession.payload = DragPayload{};

    // Dock float/hide changes slot weights without resizing the client.
    if (accepted && hadTarget) {
        invalidateLayout();
        layout();
    }
    return true;
}

void UIManager::cancelDrag() {
    if (!_dragSession.active) return;
    // Cancel = endDrag(false): no onDrop fires, but source still gets
    // _onDragEnd(false) so it can refresh any "is dragging" UI state.
    endDrag(false);
}

void UIManager::clearDragStateNoDispatch(Widget* candidate) {
    // R3-safe parallel of clearFocusNoDispatch / clearCaptureNoDispatch.
    // Called by ~Widget-equivalent paths and shutdown. Does NOT fire any
    // virtual callbacks (no onDragLeave, no onDragEnd, no onDrop) — those
    // would dispatch on a mid-destruction widget (UB landmine, same
    // pattern as Phase D R3 round 2 — commit 3844a95).
    //
    // `candidate == nullptr` is the shutdown-style "drop everything"
    // mode: cancel unconditionally without dispatch. Otherwise only drop
    // the slot if it points at `candidate` (mirrors the focus/capture/
    // hover no-dispatch helpers' contract).
    if (candidate != nullptr) {
        if (_dragSession.source == candidate) {
            _dragSession.source = nullptr;
        }
        if (_dragSession.currentTarget == candidate) {
            _dragSession.currentTarget = nullptr;
        }
        if (_dragSession.source == nullptr && _dragSession.currentTarget == nullptr) {
            _dragSession.active = false;
            if (_dragGhost != nullptr) _dragGhost->setVisible(false);
        }
        return;
    }
    // candidate == nullptr: drop everything, hide ghost. Used by shutdown.
    _dragSession = DragSession{};
    if (_dragGhost != nullptr) {
        _dragGhost->setVisible(false);
    }
}

// G12 — paint the ghost during render(). Called from UIManager::render()
// AFTER _overlayRoot->render() (so the ghost paints above popups).
// (paintGhost is the actual implementation; this comment keeps the
// call-site in render() self-explanatory.)

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
    captured->onCaptureCancelled();
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

void UIManager::clearTransientStateForSubtree(Widget* root) noexcept {
    if (root == nullptr) return;
    const auto belongsToSubtree = [root](Widget* candidate) {
        return candidate != nullptr
            && (candidate == root || isDescendantOf(candidate, root));
    };
    if (belongsToSubtree(_focusedWidget)) _focusedWidget = nullptr;
    if (belongsToSubtree(_capturedWidget)) _capturedWidget = nullptr;
    if (belongsToSubtree(_hoverWidget)) _hoverWidget = nullptr;
    if (belongsToSubtree(_compositionOwner)) {
        _compositionOwner = nullptr;
        _composing = false;
    }
    if (belongsToSubtree(_dragSession.source)
        || belongsToSubtree(_dragSession.currentTarget)) {
        clearDragStateNoDispatch(nullptr);
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
        // Focus ring state changed.
        prev->markDirty();
    }
    if (_focusedWidget != nullptr) {
        FocusableWidget* nextAsFw =
            dynamic_cast<FocusableWidget*>(_focusedWidget);
        if (nextAsFw != nullptr) {
            nextAsFw->setFocus(true);
        }
        // Focus ring state changed, symmetric to the previous owner.
        _focusedWidget->markDirty();
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
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);
    // Phase B (S3) keyboard nav — modifier tracking + Tab interception
    // happen BEFORE delegating to the focused widget. Widgets must NOT
    // see modifier keys or Tab (TextInput.swallow Tab defensively too).
    if (keyCode == UIKey_Shift || keyCode == UIKey_Control || keyCode == UIKey_Alt) {
        const uint32_t bit = 1u << (keyCode - UIKey_Shift);
        _modifiers |= bit;
        return true;
    }

    // Polish (P3): menu accelerator dispatch. Runs BEFORE the focused-
    // widget path so Ctrl+S triggers Save even when focus is on a TextInput
    // inside the same window — same behavior as every text editor IDE.
    //
    // Mods/keyCode must match a MenuItem's parsed (mods, keyCode) for
    // ANY currently registered MenuBar. Because we iterate `_menuBars`
    // rather than pick a single one, hosts can have multiple bars (main
    // + future context bar) and both react — but for v1.1 we expect one
    // bar per top-level window so the iteration cost is negligible.
    //
    // The keyCode passed in here is the non-modifier key (after Tab +
    // modifier keys have already been intercepted above). So we don't
    // need to re-check Shift/Control/Alt membership here.
    if (!_menuBars.empty()) {
        // Derive the (mods, keyCode) bitmask the same way MenuBar's
        // registry computes it: mods in 3 LSB bits, key in upper bits.
        const uint8_t mods = static_cast<uint8_t>(_modifiers & 0x07u);
        for (MenuBar* bar : _menuBars) {
            if (bar == nullptr) continue;
            MenuItem* hit = bar->findAccel(mods, keyCode);
            if (hit != nullptr) {
                // Polish (P3) invariant: item is alive because MenuBar
                // owns its Menu which owns MenuItems. Empty `_menuBars`
                // (post-dtor race in shutdown) is guarded above.
                //
                // Fire via the protected helper, NOT _onActivate(): the
                // helper is what MenuItem's click path uses and goes
                // through the same handleClick() so observers see one
                // consistent event stream.
                //
                // ORDER MATTERS (R3 landmine): handleClick must run BEFORE
                // closeOpenMenu because closeOpenMenu destroys the open
                // Menu via closePopup→destroyWidgetTree, which frees
                // `hit` (the MenuItem is owned by Menu). Calling
                // handleClick after closeOpenMenu is UAF.
                hit->handleClick();
                // Close any menu that might have been open — the
                // canonical "Save in File menu" pattern is to dismiss
                // the menu as soon as the accelerator fires. This
                // mirrors how VS Code's Ctrl+S dismisses its menu.
                bar->closeOpenMenu();
                return true;
            }
        }
    }

    if (keyCode == UIKey_Tab) {
        // Give text editors one chance to consume Tab (for indentation).
        // Ordinary TextInput returns false and keeps focus traversal; the
        // TextArea document only consumes it when code indentation is on.
        if (TextArea::focusedDocumentAcceptsTab(_focusedWidget)
            && _focusedWidget->onKeyDown(keyCode)) {
            return true;
        }
        if (_modifiers & (1u << (UIKey_Shift - UIKey_Shift))) focusPrev();
        else                                                  focusNext();
        return true;
    }

    // G12 — Esc cancels an active drag session BEFORE the modal-Esc
    // branch below. Rationale: a drag-in-progress means the user is mid-
    // gesture; an Esc press during drag should kill the drag, not the
    // modal behind it. If both happen to be active (drag started inside
    // a modal — unusual but possible), drag wins; the modal stays open.
    if (keyCode == UIKey_Escape && _dragSession.active) {
        cancelDrag();
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
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);
    if (keyCode == UIKey_Shift || keyCode == UIKey_Control || keyCode == UIKey_Alt) {
        const uint32_t bit = 1u << (keyCode - UIKey_Shift);
        _modifiers &= ~bit;
        return true;
    }
    if (_focusedWidget == nullptr) return false;
    return _focusedWidget->onKeyUp(keyCode);
}

bool UIManager::onTextInput(wchar_t ch) {
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);
    if (_focusedWidget == nullptr) return false;
    return _focusedWidget->onTextInput(ch);
}

bool UIManager::onTextInputText(const std::wstring& text) {
    const ActiveScope selfGuard(this);
    if (_focusedWidget == nullptr || text.empty()) return false;
    return _focusedWidget->onTextInputText(text);
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
    // D5 self-active — see the onMouseMove guard comment.
    const ActiveScope selfGuard(this);
    // Empty/null payload is a no-op (some platforms emit zero-length
    // chunks; we don't want to fire onTextInput with nothing).
    if (utf8 == nullptr || byteCount <= 0) return false;
    if (_focusedWidget == nullptr) return false;

    const std::wstring committed = decodeUtf8Text(
        std::string(utf8, static_cast<size_t>(byteCount)));
    if (committed.empty()) return false;
    return _focusedWidget->onTextInputText(committed);
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
    // During a drag the captured widget owns the cursor. NO geometry
    // gate here: Window resize/title drags are state-driven
    // (_isResizing/_isDragging, AYWindow.cpp) and the pointer
    // legitimately leaves the window when the size/position is clamped
    // (min-size / parent edge) — a contains() gate killed the Size*/
    // Move cursor mid-drag (PR-B1 regression, reverted). ScrollBars
    // report Default themselves, so dragging a thumb past the bar's
    // bounds already reverts to the arrow without a gate.
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
