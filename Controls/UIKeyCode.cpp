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

    default:
        return UIKey_Unknown;
    }
}

} // namespace ayt::ui
