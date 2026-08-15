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

    UIKey_Escape    = 27,

    // Navigation
    UIKey_End       = 35,
    UIKey_Home      = 36,
    UIKey_Left      = 37,
    UIKey_Up        = 38,
    UIKey_Right     = 39,
    UIKey_Down      = 40,
    UIKey_Delete    = 46,

    // ASCII letters (A..Z) — value matches Win32 VK_A..VK_Z
    UIKey_A = 65, UIKey_B, UIKey_C, UIKey_D, UIKey_E, UIKey_F,
    UIKey_G, UIKey_H, UIKey_I, UIKey_J, UIKey_K, UIKey_L, UIKey_M,
    UIKey_N, UIKey_O, UIKey_P, UIKey_Q, UIKey_R, UIKey_S, UIKey_T,
    UIKey_U, UIKey_V, UIKey_W, UIKey_X, UIKey_Y, UIKey_Z = 90,

    // Out-of-VK navigation keys (sequential; not VK-aligned, never compared
    // to raw VK ints). UIManager / ListView use these for PageUp / PageDown.
    UIKey_PageUp,
    UIKey_PageDown,

    // Future expansion (sequential): UIKey_F1, UIKey_Space, UIKey_Insert,
    // UIKey_Kp0..Kp9, etc. Not required by Phase B B1-B4.
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
