#include "AYFocusableWidget.h"

namespace ayt::ui {

FocusableWidget::FocusableWidget() = default;
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
