#pragma once

#include "AYDevice/DeviceInputEvent.h"
#include "AYDevice/WindowTypes.h"

#include <functional>
#include <string>

namespace ayt::device { class DeviceManager; }

namespace ayt::ui {

class UIManager;
enum class UiCursorHint;

ayt::device::SystemCursorShape systemCursorFromUi(UiCursorHint hint);

// Product input seam between AYDevice's ordered platform-neutral events and
// AYUI/editor routing. Gallery and application hosts should connect this
// bridge instead of decoding WM_*/SDL events themselves.
class DeviceInputBridge {
public:
    struct Config {
        float wheelPixelsPerNotch = 40.0f;
        bool synthesizePrimaryTouchAsMouse = true;
    };

    struct Callbacks {
        std::function<bool(float, float)> onMouseMove;
        std::function<void(float, float)> onMouseDelta;
        std::function<void()> onMouseLeave;
        std::function<bool(float, float, int, bool)> onMouseButton;
        // deltaY is AYUI logical pixels (positive reveals lower content).
        std::function<bool(float, float, float)> onMouseWheel;
        std::function<bool(ayt::device::KeyCode, bool, bool)> onKey;
        std::function<bool(const std::string&)> onTextCommit;
        std::function<void(ayt::device::DeviceInputEventType,
                           const std::string&, int)> onComposition;
        std::function<void(int64_t, float, float,
                           ayt::device::TouchPhase)> onTouch;
    };

    explicit DeviceInputBridge(Callbacks callbacks, Config config = {});
    explicit DeviceInputBridge(UIManager& ui, Config config = {});
    ~DeviceInputBridge();

    DeviceInputBridge(const DeviceInputBridge&) = delete;
    DeviceInputBridge& operator=(const DeviceInputBridge&) = delete;

    void connect(ayt::device::DeviceManager& devices);
    void disconnect();
    bool isConnected() const noexcept { return _devices != nullptr; }

    // Wire AYUI's text-edit focus gate to AYDevice TextInput. The bridge
    // clears the hook on disconnect/destruction, so its lifetime must remain
    // nested inside UIManager's lifetime.
    void bindTextInputFocus(UIManager& ui);

    // Public for deterministic tests and replay tools; live hosts normally use
    // connect(), which invokes this during DeviceManager::pollEvents().
    bool dispatch(const ayt::device::DeviceInputEvent& event);

private:
    bool dispatchTouchAsMouse(const ayt::device::DeviceInputEvent& event);

    Callbacks _callbacks;
    Config _config;
    ayt::device::DeviceManager* _devices = nullptr;
    ayt::device::DeviceInputListenerId _listenerId = 0;
    UIManager* _textFocusUi = nullptr;
    int64_t _primaryTouchId = -1;
};

} // namespace ayt::ui
