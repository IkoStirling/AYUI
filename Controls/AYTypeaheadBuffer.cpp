#include "AYUI/TypeaheadBuffer.h"

namespace ayt::ui {

void TypeaheadBuffer::append(wchar_t ch) {
    _buffer.push_back(ch);
    _timer = 0.0f;
}

void TypeaheadBuffer::tick(float dt) {
    if (_buffer.empty()) return;
    _timer += dt;
    if (_timer > kTimeout) {
        _buffer.clear();
        _timer = 0.0f;
    }
}

void TypeaheadBuffer::clear() {
    _buffer.clear();
    _timer = 0.0f;
}

int TypeaheadBuffer::findMatch(int startFrom, int count,
                               const std::function<const std::wstring&(int)>& getItemText) const {
    if (_buffer.empty() || count <= 0) return -1;
    // Normalize startFrom to [0, count) so callers that pass -1 (e.g.
    // a "nothing selected yet" sentinel) or out-of-range values can't
    // underflow the lambda's vector::operator[] lookup. Off-by-one
    // callers from Menu/ComboBox compute startFrom via
    // `(_hoveredIndex < 0) ? 0 : (_hoveredIndex + 1) % n`, but we keep
    // the helper robust to a raw -1 to avoid crashing tests / future
    // callers that forget the guard.
    int i = ((startFrom % count) + count) % count;
    for (int checked = 0; checked < count; ++checked) {
        const std::wstring& item = getItemText(i);
        if (item.size() >= _buffer.size()) {
            bool match = true;
            for (size_t k = 0; k < _buffer.size(); ++k) {
                // Case-insensitive ASCII fold (matches the pre-PR
                // behavior of ComboBox/Menu — CJK items whose first
                // character matches exactly still pass via the default
                // else branch).
                wchar_t a = item[k];
                if (a >= L'A' && a <= L'Z') {
                    a = static_cast<wchar_t>(a + (L'a' - L'A'));
                }
                if (a != _buffer[k]) {
                    match = false;
                    break;
                }
            }
            if (match) return i;
        }
        i = (i + 1) % count;
    }
    return -1;
}

int TypeaheadBuffer::recoverFromLetterSwitch(int startFrom, int count,
                                              const std::function<const std::wstring&(int)>& getItemText) const {
    if (_buffer.empty()) return -1;
    // Build a temporary 1-letter buffer from the last character.
    TypeaheadBuffer tmp;
    tmp.append(_buffer.back());
    int r = tmp.findMatch(startFrom, count, getItemText);
    if (r < 0) r = tmp.findMatch(0, count, getItemText);
    return r;
}

} // namespace ayt::ui