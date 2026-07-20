#include "AYRadioGroup.h"
#include "AYRadioButton.h"

namespace ayt::ui {

RadioGroup::~RadioGroup() {
    // Restore every member's pre-add callbacks so they don't dangle
    // into a destroyed group. If host already destroyed a radio, the
    // pointer is stale — we can't safely touch it; bail.
    for (auto& h : _members) {
        if (h.rb == nullptr) continue;
        h.rb->setOnToggled(std::move(h.prevToggled));
        h.rb->setOnSelected(std::move(h.prevSelected));
    }
    _members.clear();
    _selected = nullptr;
}

bool RadioGroup::add(RadioButton* rb) {
    if (rb == nullptr) return false;
    // Dedup — same radio twice is a no-op.
    for (const auto& h : _members) {
        if (h.rb == rb) return true;
    }
    Hook h;
    h.rb = rb;
    // Capture existing callbacks so we can restore them on remove/dt
    // AND so the host's pre-existing toggled/selected observers keep
    // firing (we wrap, not replace).
    h.prevToggled = rb->_onToggled;
    h.prevSelected = rb->_onSelected;
    rb->setOnToggled([this, rb](bool nowChecked) {
        onMemberToggled(rb, nowChecked);
    });
    rb->setOnSelected([this, rb]() {
        onMemberSelected(rb);
    });
    _members.push_back(std::move(h));
    return true;
}

void RadioGroup::remove(RadioButton* rb) {
    for (size_t i = 0; i < _members.size(); ++i) {
        if (_members[i].rb == rb) {
            // Restore callbacks. Then erase.
            if (rb != nullptr) {
                rb->setOnToggled(std::move(_members[i].prevToggled));
                rb->setOnSelected(std::move(_members[i].prevSelected));
            }
            _members.erase(_members.begin() + i);
            if (_selected == rb) _selected = nullptr;
            return;
        }
    }
}

int RadioGroup::getSelectedIndex() const {
    if (_selected == nullptr) return -1;
    for (size_t i = 0; i < _members.size(); ++i) {
        if (_members[i].rb == _selected) return static_cast<int>(i);
    }
    return -1;
}

RadioButton* RadioGroup::getMember(size_t index) const {
    if (index >= _members.size()) return nullptr;
    return _members[index].rb;
}

size_t RadioGroup::indexOf(RadioButton* rb) const {
    for (size_t i = 0; i < _members.size(); ++i) {
        if (_members[i].rb == rb) return i;
    }
    return static_cast<size_t>(-1);   // sentinel — host checks size_t max
}

void RadioGroup::setSelectedIndex(int index) {
    // Early-out when the new selection equals the current one.
    // Mirrors the click-pipeline's natural "second click on already-
    // checked radio is a no-op" (AYRadioButton.cpp onMouseButtonUp
    // doesn't double-fire). Hosts that wire change-detection
    // shouldn't see redundant events.
    const int currentIdx = getSelectedIndex();
    if (index == currentIdx) return;

    if (index < 0) {
        // Clear: uncheck current selection only.
        if (_selected != nullptr) {
            RadioButton* prev = _selected;
            _selected = nullptr;
            // Direct setChecked path bypasses our hook re-entry.
            _suppressHook = true;
            prev->setChecked(false);
            _suppressHook = false;
        }
    } else {
        if (static_cast<size_t>(index) >= _members.size()) return;
        RadioButton* target = _members[index].rb;
        // Deselect previous + select new in one transactional pass.
        _suppressHook = true;
        if (_selected != nullptr) _selected->setChecked(false);
        _selected = target;
        target->setChecked(true);
        _suppressHook = false;
    }
    if (_onSelectionChanged) _onSelectionChanged(getSelectedIndex());
}

void RadioGroup::onMemberToggled(RadioButton* rb, bool nowChecked) {
    // Re-fire the radio's pre-add callback so existing observers
    // (e.g. host's setOnToggled) still see the event. Skip when we
    // initiated this setChecked (avoids double-firing).
    if (!_suppressHook) {
        for (const auto& h : _members) {
            if (h.rb == rb && h.prevToggled) {
                h.prevToggled(nowChecked);
                break;
            }
        }
    }
    if (!nowChecked) {
        // Deselect event — clear our pointer if this was the selected.
        if (_selected == rb) _selected = nullptr;
        // Skip the host callback when we initiated this deselect
        // (mutex pass during setSelectedIndex / selection swap). The
        // outer caller will fire _onSelectionChanged with the FINAL
        // selected index, so a -1 mid-flight would be a lie.
        if (!_suppressHook && _onSelectionChanged) {
            _onSelectionChanged(-1);
        }
        return;
    }
    // Selection event — deselect previous peer. Suppress the inner
    // setChecked so its _onToggled(false) re-entry doesn't fire a
    // spurious _onSelectionChanged(-1). The outer caller (whether
    // click or setSelectedIndex) fires _onSelectionChanged with the
    // FINAL index exactly once after this function returns.
    RadioButton* prev = _selected;
    _selected = rb;
    if (prev != nullptr && prev != rb) {
        _suppressHook = true;
        prev->setChecked(false);
        _suppressHook = false;
    }
    // Skip the outer fire when WE initiated (setSelectedIndex has its
    // own outer fire after the whole transaction). For the click
    // path, _suppressHook is false here and we fire normally.
    if (!_suppressHook && _onSelectionChanged) {
        _onSelectionChanged(getSelectedIndex());
    }
}

void RadioGroup::onMemberSelected(RadioButton* rb) {
    // _onSelected fires ONLY on uncheck → check transition (see
    // AYRadioButton.cpp:16-28 setChecked path). The mutex logic lives
    // in onMemberToggled; here we just re-fire the previous callback.
    for (const auto& h : _members) {
        if (h.rb == rb && h.prevSelected) {
            h.prevSelected();
            break;
        }
    }
}

} // namespace ayt::ui