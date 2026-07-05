#include "AYUIManager.h"
#include "AYBox.h"
#include "AYButton.h"
#include "AYMathUtils.h"

#include <cstdio>
#include <vector>

#if defined(_DEBUG) && defined(_MSC_VER)
#  include <crtdbg.h>

namespace {

void uiHeapCheck(const char* label)
{
    if (!_CrtCheckMemory()) {
        std::fprintf(stderr, "[UIHeapCheck] FAIL at %s\n", label);
        _CrtDbgBreak();
    } else {
        std::fprintf(stderr, "[UIHeapCheck] OK at %s\n", label);
    }
}

} // namespace
#  define UI_HEAP_CHECK(label) uiHeapCheck(label)
#else
#  define UI_HEAP_CHECK(label) ((void)0)
#endif

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

void UIManager::initialize(IRenderBackend* backend) {
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
    UI_HEAP_CHECK("loadLayout_begin");
    delete _root;
    _root = nullptr;
    UI_HEAP_CHECK("loadLayout_after_delete_root");

    _root = _loader.loadFromFile(path);
    UI_HEAP_CHECK("loadLayout_after_load_from_file");
    if (_root) {
        _root->setPosition(math::FVector2(0.0f, 0.0f));
        if (_clientWidth > 0.0f && _clientHeight > 0.0f) {
            _root->setSize(math::FVector2(_clientWidth, _clientHeight));
            UI_HEAP_CHECK("loadLayout_after_set_root_size");
            layout();
            UI_HEAP_CHECK("loadLayout_after_layout");
        }
    }
    return _root != nullptr;
}

bool UIManager::loadFromString(const std::string& json) {
    UI_HEAP_CHECK("loadFromString_begin");
    delete _root;
    _root = nullptr;
    _root = _loader.loadFromString(json);
    UI_HEAP_CHECK("loadFromString_after_parse");
    if (_root) {
        _root->setPosition(math::FVector2(0.0f, 0.0f));
        if (_clientWidth > 0.0f && _clientHeight > 0.0f) {
            _root->setSize(math::FVector2(_clientWidth, _clientHeight));
            layout();
            UI_HEAP_CHECK("loadFromString_after_layout");
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
