#pragma once

#include "AYWidget.h"
#include <string>

namespace ayt::ui {

// C-3 FocusableWidget: the v1 base for any widget that owns text/IME
// routing. Widgets that can hold keyboard focus (TextInput, ComboBox
// in the future, MenuItem when its submenu is open, etc.) extend this
// instead of Widget directly.
//
// Why a separate base instead of InteractiveWidget:
//   - InteractiveWidget's Hovered/Pressed/Disabled state machine
//     conflates pointer presence with semantic state. A focused
//     TextInput that the cursor has moved away from is NOT "Pressed";
//     it's focused + Idle. Reusing InteractiveWidget would force us
//     to either invent a new state (Focused) or overload Pressed.
//   - Cursor management: an unfocused TextInput has a `Default` cursor;
//     a focused TextInput has a `Beam` (text caret) cursor. The
//     focus-in/out lifecycle is independent of pointer hover.
//
// UIManager owns the focused widget reference (single source of truth)
// and routes keyDown / keyUp / textInput events to it. FocusableWidget
// itself does NOT receive hover or press events from UIManager's
// existing pointer pipeline — focus is acquired by user click on the
// widget area (TextInput's onMouseButtonDown calls UIManager's focus
// setter) or by programmatic setFocus() from a host.
//
// The "Beam" cursor hint is added to UiCursorHint (in AYWidget.h) so
// UIManager::getCursorHint can report it when the focused widget has
// its hint customized to Beam. Other focusable widgets can override.
class FocusableWidget : public Widget {
public:
    FocusableWidget();
    ~FocusableWidget() override;

    bool hasFocus() const { return _hasFocus; }

    // Programmatically request focus. The host (UIManager or app code)
    // is responsible for plumbing this through the focus pipeline —
    // setFocus just updates local state and fires callbacks.
    void setFocus(bool focus);

    // True when this widget is the one UIManager should route keyboard
    // events to. FocusableWidget itself does NOT track this; UIManager
    // calls into the focused widget via Widget::onKeyDown /
    // onKeyUp / onTextInput. FocusableWidget overrides these to
    // delegate to subclasses that need the input.
    //
    // Subclasses override ONLY if they need custom keyboard handling.
    // Default base returns false (don't consume the event); subclass
    // overrides return true to indicate "I ate this".
    bool onKeyDown(int keyCode) override;
    bool onKeyUp(int keyCode) override;
    bool onTextInput(wchar_t ch) override;

    // =================================================================
    // Phase C (S4): IME composition hooks. UIManager routes device-side
    // onDeviceCompositionStart/Update/End into these. Default returns
    // false (does not consume); subclass opt-in: TextInput +
    // TextArea::TextDocument (PR-2). All three receive UTF-8 chunks; the
    // subclass is responsible for decoding to its internal text encoding.
    //
    // State machine contract (mirrored from UIManager's bridge):
    //   Start  — first non-empty preview. Subclass sets _composing=true,
    //            stores preview, draws underline.
    //   Update — replaces preview; caret moves.
    //   End    — committed text arrived. Subclass replaces selection with
    //            committed; clears _composing. `committed` may be empty if
    //            the IME only sent the End sentinel (some Linux IBuses);
    //            callers re-pump via onDeviceChar in that case (UIManager
    //            handles this).
    // =================================================================
    virtual bool onImeCompositionStart(const std::string& /*text*/, int /*caret*/) { return false; }
    virtual bool onImeCompositionUpdate(const std::string& /*text*/, int /*caret*/) { return false; }
    virtual bool onImeCompositionEnd(const std::string& /*committed*/) { return false; }

protected:
    // Subclass-overridable hooks.
    virtual void onFocusGained() {}
    virtual void onFocusLost() {}

    bool _hasFocus = false;
};

} // namespace ayt::ui
