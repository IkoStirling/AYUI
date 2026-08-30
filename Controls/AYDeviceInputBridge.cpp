#include "AYUI/DeviceInputBridge.h"

#include "AYDevice/DeviceManager.h"
#include "AYUI/UIManager.h"

#include <utility>

namespace ayt::ui {

ayt::device::SystemCursorShape systemCursorFromUi(UiCursorHint hint)
{
    using ayt::device::SystemCursorShape;
    switch (hint) {
    case UiCursorHint::Hand:           return SystemCursorShape::Hand;
    case UiCursorHint::Beam:           return SystemCursorShape::Text;
    case UiCursorHint::SizeHorizontal:
    case UiCursorHint::SizeWe:         return SystemCursorShape::SizeHorizontal;
    case UiCursorHint::SizeVertical:
    case UiCursorHint::SizeNs:         return SystemCursorShape::SizeVertical;
    case UiCursorHint::SizeNwse:       return SystemCursorShape::SizeNwse;
    case UiCursorHint::SizeNesw:       return SystemCursorShape::SizeNesw;
    case UiCursorHint::Move:           return SystemCursorShape::Move;
    case UiCursorHint::Default:        return SystemCursorShape::Arrow;
    }
    return SystemCursorShape::Arrow;
}

DeviceInputBridge::DeviceInputBridge(Callbacks callbacks, Config config)
    : _callbacks(std::move(callbacks)), _config(config)
{
}

DeviceInputBridge::DeviceInputBridge(UIManager& ui, Config config)
    : _config(config)
{
    _callbacks.onMouseMove = [&ui](float x, float y) {
        return ui.onMouseMove(x, y);
    };
    _callbacks.onMouseLeave = [&ui]() { ui.onMouseLeave(); };
    _callbacks.onMouseButton = [&ui](float x, float y, int button, bool pressed) {
        return pressed ? ui.onMouseButtonDown(x, y, button)
                       : ui.onMouseButtonUp(x, y, button);
    };
    _callbacks.onMouseWheel = [&ui](float x, float y, float deltaY) {
        return ui.onMouseWheel(x, y, deltaY);
    };
    _callbacks.onKey = [&ui](ayt::device::KeyCode key, bool pressed, bool) {
        return pressed ? ui.onDeviceKeyDown(key) : ui.onDeviceKeyUp(key);
    };
    _callbacks.onTextCommit = [&ui](const std::string& text) {
        return !text.empty()
            && ui.onDeviceChar(text.data(), static_cast<int>(text.size()));
    };
    _callbacks.onComposition = [&ui](ayt::device::DeviceInputEventType type,
                                      const std::string& text, int cursor) {
        switch (type) {
        case ayt::device::DeviceInputEventType::CompositionStart:
            ui.onDeviceCompositionStart(text, cursor);
            break;
        case ayt::device::DeviceInputEventType::CompositionUpdate:
            ui.onDeviceCompositionUpdate(text, cursor);
            break;
        case ayt::device::DeviceInputEventType::CompositionEnd:
            ui.onDeviceCompositionEnd("");
            break;
        default:
            break;
        }
    };
}

DeviceInputBridge::~DeviceInputBridge()
{
    disconnect();
}

void DeviceInputBridge::connect(ayt::device::DeviceManager& devices)
{
    if (_devices == &devices && _listenerId != 0) {
        return;
    }
    disconnect();
    _devices = &devices;
    _listenerId = devices.addInputListener(
        [this](const ayt::device::DeviceInputEvent& event) { dispatch(event); });
}

void DeviceInputBridge::disconnect()
{
    if (_textFocusUi != nullptr) {
        _textFocusUi->onTextEditingFocusChanged = nullptr;
        _textFocusUi = nullptr;
    }
    if (_devices != nullptr && _listenerId != 0) {
        _devices->removeInputListener(_listenerId);
    }
    _listenerId = 0;
    _devices = nullptr;
    _primaryTouchId = -1;
}

void DeviceInputBridge::bindTextInputFocus(UIManager& ui)
{
    _textFocusUi = &ui;
    ui.onTextEditingFocusChanged = [this](bool editing) {
        if (_devices != nullptr) {
            _devices->textInput().setEnabled(editing);
        }
    };
}

bool DeviceInputBridge::dispatchTouchAsMouse(
    const ayt::device::DeviceInputEvent& event)
{
    if (!_config.synthesizePrimaryTouchAsMouse) {
        return false;
    }

    using ayt::device::TouchPhase;
    if (event.touchPhase == TouchPhase::Began) {
        if (_primaryTouchId != -1) {
            return false;
        }
        _primaryTouchId = event.pointerId;
        bool handled = false;
        if (_callbacks.onMouseMove) {
            handled = _callbacks.onMouseMove(event.x, event.y);
        }
        if (_callbacks.onMouseButton) {
            handled = _callbacks.onMouseButton(event.x, event.y, 0, true) || handled;
        }
        return handled;
    }
    if (event.pointerId != _primaryTouchId) {
        return false;
    }
    if (event.touchPhase == TouchPhase::Moved
        || event.touchPhase == TouchPhase::Stationary) {
        return _callbacks.onMouseMove
            ? _callbacks.onMouseMove(event.x, event.y) : false;
    }
    if (event.touchPhase == TouchPhase::Ended
        || event.touchPhase == TouchPhase::Cancelled) {
        bool handled = false;
        if (_callbacks.onMouseMove) {
            handled = _callbacks.onMouseMove(event.x, event.y);
        }
        if (_callbacks.onMouseButton) {
            handled = _callbacks.onMouseButton(event.x, event.y, 0, false) || handled;
        }
        _primaryTouchId = -1;
        return handled;
    }
    return false;
}

bool DeviceInputBridge::dispatch(const ayt::device::DeviceInputEvent& event)
{
    using ayt::device::DeviceInputEventType;
    switch (event.type) {
    case DeviceInputEventType::MouseMove:
        return _callbacks.onMouseMove
            ? _callbacks.onMouseMove(event.x, event.y) : false;
    case DeviceInputEventType::MouseDelta:
        if (_callbacks.onMouseDelta) {
            _callbacks.onMouseDelta(event.deltaX, event.deltaY);
            return true;
        }
        return false;
    case DeviceInputEventType::MouseButton:
        return _callbacks.onMouseButton
            ? _callbacks.onMouseButton(event.x, event.y,
                  static_cast<int>(event.mouseButton), event.pressed)
            : false;
    case DeviceInputEventType::MouseWheel:
        return _callbacks.onMouseWheel
            ? _callbacks.onMouseWheel(event.x, event.y,
                  -event.deltaY * _config.wheelPixelsPerNotch)
            : false;
    case DeviceInputEventType::MouseLeave:
        if (_callbacks.onMouseLeave) {
            _callbacks.onMouseLeave();
            return true;
        }
        return false;
    case DeviceInputEventType::Key:
        return _callbacks.onKey
            ? _callbacks.onKey(event.key, event.pressed, event.repeat) : false;
    case DeviceInputEventType::TextCommit:
        return _callbacks.onTextCommit
            ? _callbacks.onTextCommit(event.text) : false;
    case DeviceInputEventType::CompositionStart:
    case DeviceInputEventType::CompositionUpdate:
    case DeviceInputEventType::CompositionEnd:
        if (_callbacks.onComposition) {
            _callbacks.onComposition(event.type, event.text,
                                     event.compositionCursor);
            return true;
        }
        return false;
    case DeviceInputEventType::Touch:
        if (_callbacks.onTouch) {
            _callbacks.onTouch(event.pointerId, event.x, event.y,
                               event.touchPhase);
            return true;
        }
        return dispatchTouchAsMouse(event);
    }
    return false;
}

} // namespace ayt::ui
