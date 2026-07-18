#include "AYUIManager.h"
#include "AYBox.h"
#include "AYButton.h"
#include "AYCheckBox.h"
#include "AYRadioButton.h"
#include "AYImage.h"
#include "AYTextLabel.h"
#include "AYWindow.h"
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

UIManager& UIManager::get() {
    static UIManager instance;
    return instance;
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
    _loader.clearEventBindings();
    _loader.clearWidgetRegistry();

    if (_root != nullptr) {
        // destroyWidgetTree recurses through children before deleting the root,
        // so factory-allocated widgets are released exactly once.
        destroyWidgetTree(_root);
        _root = nullptr;
    }

    _backend = nullptr;
}

bool UIManager::loadLayout(const std::string& path) {
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
        Widget* hit = pickWidgetAt(_root, math::FVector2(_lastMouseX, _lastMouseY));
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

    Widget* hit = pickWidgetAt(_root, pos);
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
    Widget* hit = pickWidgetAt(_root, pos);
    updateHoverWidget(_hoverWidget, hit);

    if (hit != nullptr && hit->onMouseButtonDown(UIMouseEvent(pos, button))) {
        _capturedWidget = hit;
        return true;
    }
    return false;
}

bool UIManager::onMouseButtonUp(float x, float y, int button) {
    if (!_root) {
        return false;
    }

    math::FVector2 pos(x, y);
    Widget* target = _capturedWidget != nullptr ? _capturedWidget : pickWidgetAt(_root, pos);
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
    updateHoverWidget(_hoverWidget, pickWidgetAt(_root, pos));
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
