#include "AYTest.h"
#include "AYUI/DeviceInputBridge.h"
#include "AYUI/UIKeyCode.h"
#include "AYUI/Widget.h"

#include <string>

using namespace ayt::device;
using namespace ayt::ui;

TEST_SUITE(AYUI_DeviceInputBridge)

TEST_CASE(pointer_button_wheel_and_delta_are_translated) {
    int moveCount = 0;
    int buttonCount = 0;
    int wheelCount = 0;
    int deltaCount = 0;
    int leaveCount = 0;
    float lastX = 0.0f;
    float lastY = 0.0f;
    float lastWheel = 0.0f;
    bool lastPressed = false;

    DeviceInputBridge::Callbacks callbacks{};
    callbacks.onMouseMove = [&](float x, float y) {
        ++moveCount;
        lastX = x;
        lastY = y;
        return true;
    };
    callbacks.onMouseButton = [&](float x, float y, int button, bool pressed) {
        ++buttonCount;
        lastX = x;
        lastY = y;
        lastPressed = pressed;
        return button == 1;
    };
    callbacks.onMouseWheel = [&](float x, float y, float deltaY) {
        ++wheelCount;
        lastX = x;
        lastY = y;
        lastWheel = deltaY;
        return true;
    };
    callbacks.onMouseDelta = [&](float dx, float dy) {
        ++deltaCount;
        lastX = dx;
        lastY = dy;
    };
    callbacks.onMouseLeave = [&]() { ++leaveCount; };

    DeviceInputBridge::Config config{};
    config.wheelPixelsPerNotch = 32.0f;
    config.synthesizePrimaryTouchAsMouse = false;
    DeviceInputBridge bridge(std::move(callbacks), config);

    DeviceInputEvent event{};
    event.type = DeviceInputEventType::MouseMove;
    event.x = 12.0f;
    event.y = 34.0f;
    CHECK(bridge.dispatch(event));

    event.type = DeviceInputEventType::MouseButton;
    event.mouseButton = MouseButton::Right;
    event.pressed = true;
    CHECK(bridge.dispatch(event));

    event.type = DeviceInputEventType::MouseWheel;
    event.deltaY = 1.5f;
    CHECK(bridge.dispatch(event));

    event.type = DeviceInputEventType::MouseDelta;
    event.deltaX = -3.0f;
    event.deltaY = 4.0f;
    CHECK(bridge.dispatch(event));

    event.type = DeviceInputEventType::MouseLeave;
    CHECK(bridge.dispatch(event));

    CHECK_INT_EQ(moveCount, 1);
    CHECK_INT_EQ(buttonCount, 1);
    CHECK_INT_EQ(wheelCount, 1);
    CHECK_INT_EQ(deltaCount, 1);
    CHECK_INT_EQ(leaveCount, 1);
    CHECK_FLOAT_EQ(lastX, -3.0f, 0.001f);
    CHECK_FLOAT_EQ(lastY, 4.0f, 0.001f);
    CHECK_FLOAT_EQ(lastWheel, -48.0f, 0.001f);
    CHECK(lastPressed);
}

TEST_CASE(key_text_and_composition_preserve_payload) {
    KeyCode lastKey = KeyCode::Unknown;
    bool lastPressed = false;
    bool lastRepeat = false;
    std::string committed;
    std::string composing;
    DeviceInputEventType compositionType = DeviceInputEventType::MouseMove;
    int caret = -1;

    DeviceInputBridge::Callbacks callbacks{};
    callbacks.onKey = [&](KeyCode key, bool pressed, bool repeat) {
        lastKey = key;
        lastPressed = pressed;
        lastRepeat = repeat;
        return true;
    };
    callbacks.onTextCommit = [&](const std::string& text) {
        committed = text;
        return true;
    };
    callbacks.onComposition = [&](DeviceInputEventType type,
                                  const std::string& text, int cursor) {
        compositionType = type;
        composing = text;
        caret = cursor;
    };
    DeviceInputBridge bridge(std::move(callbacks));

    DeviceInputEvent event{};
    event.type = DeviceInputEventType::Key;
    event.key = KeyCode::F5;
    event.pressed = true;
    event.repeat = true;
    CHECK(bridge.dispatch(event));

    event.type = DeviceInputEventType::TextCommit;
    event.text = "AY";
    CHECK(bridge.dispatch(event));

    event.type = DeviceInputEventType::CompositionUpdate;
    event.text = "\xE4\xBD\xA0";
    event.compositionCursor = 3;
    CHECK(bridge.dispatch(event));

    CHECK_INT_EQ(lastKey, KeyCode::F5);
    CHECK(lastPressed);
    CHECK(lastRepeat);
    CHECK(committed == "AY");
    CHECK_INT_EQ(compositionType, DeviceInputEventType::CompositionUpdate);
    CHECK(composing == "\xE4\xBD\xA0");
    CHECK_INT_EQ(caret, 3);
}

TEST_CASE(every_declared_device_key_has_a_ui_mapping) {
    int unmappedCount = 0;
    for (int raw = 1; raw < static_cast<int>(KeyCode::Count); ++raw) {
        const KeyCode key = static_cast<KeyCode>(raw);
        if (fromDeviceKey(key) == UIKey_Unknown) {
            ++unmappedCount;
        }
    }
    CHECK_INT_EQ(unmappedCount, 0);
}

TEST_CASE(primary_touch_synthesizes_one_mouse_stream) {
    int moveCount = 0;
    int downCount = 0;
    int upCount = 0;

    DeviceInputBridge::Callbacks callbacks{};
    callbacks.onMouseMove = [&](float, float) {
        ++moveCount;
        return true;
    };
    callbacks.onMouseButton = [&](float, float, int button, bool pressed) {
        if (button == 0 && pressed) ++downCount;
        if (button == 0 && !pressed) ++upCount;
        return true;
    };
    DeviceInputBridge bridge(std::move(callbacks));

    DeviceInputEvent event{};
    event.type = DeviceInputEventType::Touch;
    event.pointerId = 10;
    event.touchPhase = TouchPhase::Began;
    CHECK(bridge.dispatch(event));

    event.pointerId = 11;
    CHECK_FALSE(bridge.dispatch(event));

    event.pointerId = 10;
    event.touchPhase = TouchPhase::Moved;
    CHECK(bridge.dispatch(event));
    event.touchPhase = TouchPhase::Ended;
    CHECK(bridge.dispatch(event));

    CHECK_INT_EQ(moveCount, 3);
    CHECK_INT_EQ(downCount, 1);
    CHECK_INT_EQ(upCount, 1);
}

TEST_CASE(cursor_hints_map_without_platform_calls) {
    CHECK_INT_EQ(systemCursorFromUi(UiCursorHint::Default), SystemCursorShape::Arrow);
    CHECK_INT_EQ(systemCursorFromUi(UiCursorHint::Hand), SystemCursorShape::Hand);
    CHECK_INT_EQ(systemCursorFromUi(UiCursorHint::Beam), SystemCursorShape::Text);
    CHECK_INT_EQ(systemCursorFromUi(UiCursorHint::SizeWe),
                 SystemCursorShape::SizeHorizontal);
    CHECK_INT_EQ(systemCursorFromUi(UiCursorHint::SizeNs),
                 SystemCursorShape::SizeVertical);
    CHECK_INT_EQ(systemCursorFromUi(UiCursorHint::SizeNwse),
                 SystemCursorShape::SizeNwse);
    CHECK_INT_EQ(systemCursorFromUi(UiCursorHint::SizeNesw),
                 SystemCursorShape::SizeNesw);
    CHECK_INT_EQ(systemCursorFromUi(UiCursorHint::Move), SystemCursorShape::Move);
}

} // namespace _X_AYUI_DeviceInputBridge
