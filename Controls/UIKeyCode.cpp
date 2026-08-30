#include "AYUI/UIKeyCode.h"

// UIKeyCode.h already pulls in AYDevice/InputTypes.h.

namespace ayt::ui {

// Translate AYDevice's KeyCode (Unknown=0, A=1, ..., Z=26, Num0=27, ...,
// Escape=..., Tab=..., Backspace=..., Enter=..., Delete=..., Home=..., End=...,
// PageUp=..., PageDown=..., Left=..., Up=..., Right=..., Down=...) to
// UIKeyCode (VK-aligned: Backspace=8, Tab=9, Enter=13, Escape=27, End=35,
// Home=36, Left=37, Up=38, Right=39, Down=40, Delete=46, A=65..Z=90).
//
// We use a switch (no implicit conversion) so the AYDevice enum can grow
// without silently mapping to the wrong UIKeyCode. Unknown / unmapped
// inputs return UIKey_Unknown so UIManager.onKeyDown returns false.
UIKeyCode fromDeviceKey(ayt::device::KeyCode kc) {
    using ayt::device::KeyCode;
    switch (kc) {
    case KeyCode::Backspace:  return UIKey_Backspace;
    case KeyCode::Tab:        return UIKey_Tab;
    case KeyCode::Enter:      return UIKey_Enter;
    case KeyCode::Escape:     return UIKey_Escape;
    case KeyCode::Space:      return UIKey_Space;
    case KeyCode::Insert:     return UIKey_Insert;

    case KeyCode::End:        return UIKey_End;
    case KeyCode::Home:       return UIKey_Home;
    case KeyCode::Left:       return UIKey_Left;
    case KeyCode::Up:         return UIKey_Up;
    case KeyCode::Right:      return UIKey_Right;
    case KeyCode::Down:       return UIKey_Down;

    case KeyCode::PageUp:     return UIKey_PageUp;
    case KeyCode::PageDown:   return UIKey_PageDown;
    case KeyCode::Delete:     return UIKey_Delete;

    // Modifiers — UIManager.onKeyDown intercepts Shift/Ctrl/Alt before
    // forwarding. Map both halves of each modifier pair to a single
    // UIKeyCode; UIManager's bitmask handles the rest.
    case KeyCode::LeftShift:
    case KeyCode::RightShift:     return UIKey_Shift;
    case KeyCode::LeftControl:
    case KeyCode::RightControl:   return UIKey_Control;
    case KeyCode::LeftAlt:
    case KeyCode::RightAlt:       return UIKey_Alt;
    case KeyCode::LeftSuper:      return UIKey_LeftSuper;
    case KeyCode::RightSuper:     return UIKey_RightSuper;

    case KeyCode::A: return UIKey_A; case KeyCode::B: return UIKey_B;
    case KeyCode::C: return UIKey_C; case KeyCode::D: return UIKey_D;
    case KeyCode::E: return UIKey_E; case KeyCode::F: return UIKey_F;
    case KeyCode::G: return UIKey_G; case KeyCode::H: return UIKey_H;
    case KeyCode::I: return UIKey_I; case KeyCode::J: return UIKey_J;
    case KeyCode::K: return UIKey_K; case KeyCode::L: return UIKey_L;
    case KeyCode::M: return UIKey_M; case KeyCode::N: return UIKey_N;
    case KeyCode::O: return UIKey_O; case KeyCode::P: return UIKey_P;
    case KeyCode::Q: return UIKey_Q; case KeyCode::R: return UIKey_R;
    case KeyCode::S: return UIKey_S; case KeyCode::T: return UIKey_T;
    case KeyCode::U: return UIKey_U; case KeyCode::V: return UIKey_V;
    case KeyCode::W: return UIKey_W; case KeyCode::X: return UIKey_X;
    case KeyCode::Y: return UIKey_Y; case KeyCode::Z: return UIKey_Z;

    case KeyCode::Num0: return UIKey_Num0; case KeyCode::Num1: return UIKey_Num1;
    case KeyCode::Num2: return UIKey_Num2; case KeyCode::Num3: return UIKey_Num3;
    case KeyCode::Num4: return UIKey_Num4; case KeyCode::Num5: return UIKey_Num5;
    case KeyCode::Num6: return UIKey_Num6; case KeyCode::Num7: return UIKey_Num7;
    case KeyCode::Num8: return UIKey_Num8; case KeyCode::Num9: return UIKey_Num9;

    case KeyCode::F1: return UIKey_F1; case KeyCode::F2: return UIKey_F2;
    case KeyCode::F3: return UIKey_F3; case KeyCode::F4: return UIKey_F4;
    case KeyCode::F5: return UIKey_F5; case KeyCode::F6: return UIKey_F6;
    case KeyCode::F7: return UIKey_F7; case KeyCode::F8: return UIKey_F8;
    case KeyCode::F9: return UIKey_F9; case KeyCode::F10: return UIKey_F10;
    case KeyCode::F11: return UIKey_F11; case KeyCode::F12: return UIKey_F12;

    case KeyCode::Minus:       return UIKey_Minus;
    case KeyCode::Equal:       return UIKey_Equal;
    case KeyCode::LeftBracket: return UIKey_LeftBracket;
    case KeyCode::RightBracket:return UIKey_RightBracket;
    case KeyCode::Backslash:   return UIKey_Backslash;
    case KeyCode::Semicolon:   return UIKey_Semicolon;
    case KeyCode::Apostrophe:  return UIKey_Apostrophe;
    case KeyCode::Comma:       return UIKey_Comma;
    case KeyCode::Period:      return UIKey_Period;
    case KeyCode::Slash:       return UIKey_Slash;
    case KeyCode::GraveAccent: return UIKey_GraveAccent;

    case KeyCode::Kp0: return UIKey_Kp0; case KeyCode::Kp1: return UIKey_Kp1;
    case KeyCode::Kp2: return UIKey_Kp2; case KeyCode::Kp3: return UIKey_Kp3;
    case KeyCode::Kp4: return UIKey_Kp4; case KeyCode::Kp5: return UIKey_Kp5;
    case KeyCode::Kp6: return UIKey_Kp6; case KeyCode::Kp7: return UIKey_Kp7;
    case KeyCode::Kp8: return UIKey_Kp8; case KeyCode::Kp9: return UIKey_Kp9;
    case KeyCode::KpDecimal:  return UIKey_KpDecimal;
    case KeyCode::KpDivide:   return UIKey_KpDivide;
    case KeyCode::KpMultiply: return UIKey_KpMultiply;
    case KeyCode::KpSubtract: return UIKey_KpSubtract;
    case KeyCode::KpAdd:      return UIKey_KpAdd;
    case KeyCode::KpEnter:    return UIKey_KpEnter;

    case KeyCode::CapsLock:   return UIKey_CapsLock;
    case KeyCode::NumLock:    return UIKey_NumLock;
    case KeyCode::ScrollLock: return UIKey_ScrollLock;

    default:
        return UIKey_Unknown;
    }
}

} // namespace ayt::ui
