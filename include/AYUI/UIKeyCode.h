#pragma once

// UIKeyCode.h - AYUI-side key code (Phase B, S3 keyboard navigation).
//
// VK-aligned enum so the integer values match the legacy anonymous
// constants in AYTextInput.cpp / AYTextArea.cpp (8/9/13/27/35/36/37/39/40/46/65...).
// Code uses named constants (UIKey_Backspace) rather than raw VK ints;
// the alignment ensures the Phase B migration is purely cosmetic.
//
// AYDevice's KeyCode (ayt::device::KeyCode, sequential USB-HID-style int
// starting Unknown=0, A=1, Z=26) does NOT line up with VK. Translation
// lives in this header's companion .cpp via fromDeviceKey() so AYUI
// stays the single point that knows about both encodings. AYDevice
// remains free of any AYUI dependency.

#include "AYDevice/InputTypes.h"   // ayt::device::KeyCode forward dependency for fromDeviceKey

namespace ayt::ui {

enum UIKeyCode : int {
    UIKey_Unknown = 0,

    // Whitespace / control
    UIKey_Backspace = 8,
    UIKey_Tab       = 9,
    UIKey_Enter     = 13,

    // Modifiers (track via UIManager::_modifiers; widgets never see these)
    UIKey_Shift     = 16,
    UIKey_Control   = 17,
    UIKey_Alt       = 18,
    UIKey_CapsLock  = 20,

    UIKey_Escape    = 27,
    UIKey_Space     = 32,

    // Navigation
    UIKey_PageUp    = 33,
    UIKey_PageDown  = 34,
    UIKey_End       = 35,
    UIKey_Home      = 36,
    UIKey_Left      = 37,
    UIKey_Up        = 38,
    UIKey_Right     = 39,
    UIKey_Down      = 40,
    UIKey_Insert    = 45,
    UIKey_Delete    = 46,

    // Top-row digits.
    UIKey_Num0 = 48, UIKey_Num1, UIKey_Num2, UIKey_Num3, UIKey_Num4,
    UIKey_Num5, UIKey_Num6, UIKey_Num7, UIKey_Num8, UIKey_Num9 = 57,

    // ASCII letters (A..Z) — value matches Win32 VK_A..VK_Z
    UIKey_A = 65, UIKey_B, UIKey_C, UIKey_D, UIKey_E, UIKey_F,
    UIKey_G, UIKey_H, UIKey_I, UIKey_J, UIKey_K, UIKey_L, UIKey_M,
    UIKey_N, UIKey_O, UIKey_P, UIKey_Q, UIKey_R, UIKey_S, UIKey_T,
    UIKey_U, UIKey_V, UIKey_W, UIKey_X, UIKey_Y, UIKey_Z = 90,

    UIKey_LeftSuper = 91,
    UIKey_RightSuper = 92,

    // Keypad and function keys retain their Win32-compatible values. KpEnter
    // has no distinct VK and therefore uses a private value outside that range.
    UIKey_Kp0 = 96, UIKey_Kp1, UIKey_Kp2, UIKey_Kp3, UIKey_Kp4,
    UIKey_Kp5, UIKey_Kp6, UIKey_Kp7, UIKey_Kp8, UIKey_Kp9 = 105,
    UIKey_KpMultiply = 106,
    UIKey_KpAdd = 107,
    UIKey_KpSubtract = 109,
    UIKey_KpDecimal = 110,
    UIKey_KpDivide = 111,
    UIKey_F1 = 112, UIKey_F2, UIKey_F3, UIKey_F4, UIKey_F5, UIKey_F6,
    UIKey_F7, UIKey_F8, UIKey_F9, UIKey_F10, UIKey_F11, UIKey_F12 = 123,
    UIKey_NumLock = 144,
    UIKey_ScrollLock = 145,

    // OEM punctuation VK values used by shortcut routing. Committed text still
    // travels through the TextCommit event and never depends on these codes.
    UIKey_Semicolon = 186,
    UIKey_Equal = 187,
    UIKey_Comma = 188,
    UIKey_Minus = 189,
    UIKey_Period = 190,
    UIKey_Slash = 191,
    UIKey_GraveAccent = 192,
    UIKey_LeftBracket = 219,
    UIKey_Backslash = 220,
    UIKey_RightBracket = 221,
    UIKey_Apostrophe = 222,
    UIKey_KpEnter = 256,
};

// Type-safe alias.
using UIKey = UIKeyCode;

// Translate AYDevice::KeyCode (USB-HID sequential) to UIKeyCode (VK-aligned).
// Lives in UIKeyCode.cpp. AYDevice must not depend on AYUI; AYUI may depend
// on AYDevice. AYUI's CMakeLists links PUBLIC AYDevice for this.
//
// Host wiring (out of Phase B scope — integration point in AYDevice):
//   WM_KEYDOWN (VK_*)
//     -> AYWindowManager::translateVirtualKey -> KeyCode
//     -> UIManager::onDeviceKeyDown(KeyCode) -> fromDeviceKey -> UIKeyCode
//     -> UIManager::onKeyDown(int)
UIKeyCode fromDeviceKey(ayt::device::KeyCode kc);

} // namespace ayt::ui
