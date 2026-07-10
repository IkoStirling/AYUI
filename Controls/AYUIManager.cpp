#include "AYUIManager.h"
#include "AYBox.h"
#include "AYButton.h"
#include "AYImage.h"
#include "AYTextLabel.h"
#include "AYWindow.h"
#include "AYSplitterHandle.h"
#include "AYMathUtils.h"
#include "AYWidgetFactory.h"

#include <vector>

namespace ayt::ui {

namespace {

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
    if (hoverWidget != nullptr) {
        hoverWidget->onMouseLeave();
    }
    hoverWidget = nextHover;
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
    if (!f.isRegistered("TextLabel")) f.registerCreator("TextLabel", []() { return new TextLabel(); });
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
    AYUNREFERENCED_PARAM(dt);
    if (Widget* reloaded = _loader.tryReload()) {
        if (_root != nullptr) {
            destroyWidgetTree(_root);
        }
        _root = reloaded;
        _lastLayoutWidth = -1.0f;
        _lastLayoutHeight = -1.0f;
        setClientSize(_clientWidth, _clientHeight);
        layout();
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

    math::FVector2 pos(x, y);
    if (_capturedWidget != nullptr) {
        return _capturedWidget->onMouseMove(UIMouseEvent(pos, 0));
    }

    Widget* hit = pickWidgetAt(_root, pos);
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
    // Re-evaluate the hovered widget now that the capture is gone —
    // otherwise `_hoverWidget` still points at the widget we were
    // dragging on (e.g. a splitter handle) and its cursor hint leaks
    // past the mouse-up. Trigger the standard hover re-evaluation at
    // the release position so the cursor hint matches the widget
    // actually under the pointer.
    updateHoverWidget(_hoverWidget, pickWidgetAt(_root, pos));
    if (target != nullptr) {
        return target->onMouseButtonUp(UIMouseEvent(pos, button));
    }
    return false;
}

void UIManager::clearHover() {
    if (_capturedWidget != nullptr) {
        return;
    }
    updateHoverWidget(_hoverWidget, nullptr);
}

void UIManager::onMouseLeave() {
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
