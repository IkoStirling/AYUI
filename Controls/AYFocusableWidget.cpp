#include "AYUI/FocusableWidget.h"
#include "AYUI/UIManager.h"

namespace ayt::ui {

FocusableWidget::FocusableWidget() = default;

// FocusableWidget's default destructor is intentional. Calling
// UIManager::get() from a base-class destructor is unsafe: the static
// `s_uninitializedFallback` is initialized lazily and torn down at
// program exit, and cross-instance state references during destruction
// are UB-adjacent. Cleanup paths (shutdown / loadLayout / loadFromString
// / hot-reload / UIManager::closeModal) already use clearFocusNoDispatch
// when they need to drop focus — base-class destructor is intentionally
// empty. Derived dtors (Modal, Menu, TextInput, TextArea) that care
// about preserving focus already route through closeModal()/UIManager
// during their body (R3 / Phase A / Phase C pattern).
FocusableWidget::~FocusableWidget() = default;

void FocusableWidget::setFocus(bool focus) {
    if (_hasFocus == focus) {
        return;
    }
    _hasFocus = focus;
    if (focus) {
        onFocusGained();
    } else {
        onFocusLost();
    }
    // Focused controls commonly change border, caret, selection, or hint.
    // Re-arm the dirty-render gate after the state transition.
    markDirty();
}

bool FocusableWidget::onKeyDown(int keyCode) {
    AYUNREFERENCED_PARAM(keyCode);
    // Default: do not consume. Subclasses that need keyboard routing
    // (TextInput) override and return true on handled keys.
    return false;
}

bool FocusableWidget::onKeyUp(int keyCode) {
    AYUNREFERENCED_PARAM(keyCode);
    return false;
}

bool FocusableWidget::onTextInput(wchar_t ch) {
    AYUNREFERENCED_PARAM(ch);
    return false;
}

} // namespace ayt::ui
