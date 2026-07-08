#include "AYUIManager.h"
#include "AYBox.h"
#include "AYButton.h"
#include "AYImage.h"
#include "AYTextLabel.h"
#include "AYWindow.h"
#include "AYMathUtils.h"
#include "AYWidgetFactory.h"

#include <vector>

namespace ayt::ui {

namespace {

void clearWidgetHandlers(Widget* widget)
{
    if (widget == nullptr) {
        return;
    }
    if (Button* button = dynamic_cast<Button*>(widget)) {
        button->setOnClicked({});
    }
    for (Widget* child : widget->getChildren()) {
        clearWidgetHandlers(child);
    }
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
}

void UIManager::initialize(IRenderBackend* backend) {
    ensureBuiltInFactoriesRegistered();
    _backend = backend;
}

void UIManager::shutdown() {
    if (_shutdown) {
        return;
    }
    _shutdown = true;

    _loader.clearEventBindings();
    _loader.clearWidgetRegistry();
    _capturedWidget = nullptr;
    _hoverWidget = nullptr;

    if (_root != nullptr) {
        clearWidgetHandlers(_root);
        delete _root;
        _root = nullptr;
    }

    _backend = nullptr;
}

bool UIManager::loadLayout(const std::string& path) {
    delete _root;
    _root = nullptr;

    _root = _loader.loadFromFile(path);
    if (_root) {
        _root->setPosition(math::FVector2(0.0f, 0.0f));
        if (_clientWidth > 0.0f && _clientHeight > 0.0f) {
            _root->setSize(math::FVector2(_clientWidth, _clientHeight));
            layout();
        }
    }
    return _root != nullptr;
}

bool UIManager::loadFromString(const std::string& json) {
    delete _root;
    _root = nullptr;
    _root = _loader.loadFromString(json);
    if (_root) {
        _root->setPosition(math::FVector2(0.0f, 0.0f));
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
    }
}

void UIManager::update(float dt) {
    AYUNREFERENCED_PARAM(dt);
    if (Widget* reloaded = _loader.tryReload()) {
        delete _root;
        _root = reloaded;
        setClientSize(_clientWidth, _clientHeight);
        layout();
    }
}

void UIManager::layout() {
    if (_root) {
        _root->performLayout();
    }
}

void UIManager::render() {
    if (!_backend || !_root) {
        return;
    }

    _backend->beginFrame();
    math::FRectangle viewport(0.0f, 0.0f, _clientWidth, _clientHeight);
    _backend->beginCanvas(viewport);
    _root->render(*_backend);
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
