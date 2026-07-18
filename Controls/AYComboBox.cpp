#include "AYComboBox.h"
#include "AYScrollBar.h"
#include "AYIRenderBackend.h"
#include "AYStyle.h"
#include "AYUIManager.h"
#include "UIKeyCode.h"
#include "aymath/MathUtils.h"

#include <algorithm>

namespace ayt::ui {

ComboBox::ComboBox() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    setLayoutPositionManaged(false);

    _display = new TextLabel();
    _display->setText(L"");
    addChildExternal(_display);
}

ComboBox::~ComboBox() {
    // Phase A (A2): _display is still a child of ComboBox; destroyWidgetTree
    // frees it when the ComboBox tree is destroyed. _popup may be mounted
    // on the overlay OR held unmounted after a soft closePopup — either
    // way we must not leave it on the overlay, and if it is unmounted we
    // still own the allocation and must free it.
    if (_popup != nullptr) {
        if (_popup->getParent() != nullptr) {
            UIManager::get().closePopup(_popup, /*destroy=*/true);
        } else {
            destroyWidgetTree(_popup);
        }
        _popup = nullptr;
    }
    _display = nullptr;
}

void ComboBox::setItems(const std::vector<std::wstring>& items) {
    _items = items;
    if (_selectedIndex >= static_cast<int>(_items.size())) {
        _selectedIndex = -1;
    }
    ensurePopupCreated();
    if (_popup != nullptr) {
        _popup->setItems(_items);
        syncPopupSelection();
    }
    if (_display != nullptr) {
        _display->setText(
            _selectedIndex >= 0 && _selectedIndex < static_cast<int>(_items.size())
                ? _items[_selectedIndex]
                : L"");
    }
}

void ComboBox::addItem(const std::wstring& item) {
    _items.push_back(item);
    ensurePopupCreated();
    if (_popup != nullptr) {
        _popup->setItems(_items);
        syncPopupSelection();
    }
}

void ComboBox::clearItems() {
    _items.clear();
    _selectedIndex = -1;
    ensurePopupCreated();
    if (_popup != nullptr) {
        _popup->setItems(_items);
    }
    if (_display != nullptr) {
        _display->setText(L"");
    }
}

const std::wstring& ComboBox::getItem(size_t index) const {
    static const std::wstring kEmpty;
    if (index >= _items.size()) return kEmpty;
    return _items[index];
}

void ComboBox::setSelectedIndex(int index) {
    int clamped = index;
    if (clamped < -1) clamped = -1;
    if (clamped >= static_cast<int>(_items.size())) clamped = -1;
    _selectedIndex = clamped;
    if (_display != nullptr) {
        _display->setText(
            _selectedIndex >= 0
                ? _items[_selectedIndex]
                : L"");
    }
    // Honor the silent-sync guard: when onKeyDown's open-state path is
    // moving the selection, the popup's selection callback (wired in
    // ensurePopupCreated) would close the popup + fire _onSelectionChanged
    // — both wrong mid-arrows. Caller wraps both this call AND its own
    // explicit popup sync with _silentPopupSync, then resets it.
    if (!_silentPopupSync) {
        syncPopupSelection();
    }
}

const std::wstring& ComboBox::getSelectedItem() const {
    static const std::wstring kEmpty;
    if (_selectedIndex < 0 ||
        _selectedIndex >= static_cast<int>(_items.size())) {
        return kEmpty;
    }
    return _items[_selectedIndex];
}

void ComboBox::ensurePopupCreated() {
    if (_popup != nullptr) return;
    _popup = new ListView();
    _popup->setItems(_items);
    _popup->setVisible(false);
    // Wire popup's selection callback to fire ComboBox's own callback.
    // This is what makes a click on a popup row trigger the host's
    // setOnSelectionChanged lambda — without this forwarding, the popup's
    // internal ListView selection would be invisible to ComboBox listeners.
    _popup->setOnSelectionChanged([this](int idx) {
        // Phase B (B2): if ComboBox::onKeyDown is mirroring a keyboard
        // move into the popup ListView, that sync would re-enter here
        // and dismiss the popup mid-arrows. Suppress the side effects
        // (close + callback) in that case — the keyboard caller owns
        // the state machine. Display label still updates so the
        // highlight stays consistent.
        if (_silentPopupSync) {
            _selectedIndex = idx;
            if (_display != nullptr) {
                _display->setText(
                    _selectedIndex >= 0
                        ? _items[_selectedIndex]
                        : L"");
            }
            return;
        }
        // Mirror selection into ComboBox's own _selectedIndex so
        // getSelectedIndex / getSelectedItem report the new value, and
        // update the display label. setSelectedIndex closes the popup
        // (popups dismiss on selection, matching native dropdown UX).
        _selectedIndex = idx;
        if (_display != nullptr) {
            _display->setText(
                _selectedIndex >= 0
                    ? _items[_selectedIndex]
                    : L"");
        }
        closePopup();
        if (_onSelectionChanged) {
            _onSelectionChanged(idx);
        }
    });
    // Phase A (A2): the popup is NOT mounted as a child of ComboBox anymore.
    // It will be reparented onto UIManager's overlay root via openPopup(),
    // which lets the popup:
    //   - render + hit-test ABOVE any sibling/parent in the host tree
    //   - survive ComboBox destruction cleanly via the overlay's
    //     destroyWidgetTree in UIManager::closePopup
    //   - flip/clamp against the viewport (UIManager::getClientSize)
    // The popup pointer is non-owning from ComboBox's perspective — the
    // overlay owns the lifetime end-to-end. ensurePopupCreated only
    // allocates; openPopup does the mount.
}

void ComboBox::syncPopupSelection() {
    if (_popup == nullptr) return;
    _popup->setSelectedIndex(_selectedIndex);
}

math::FRectangle ComboBox::computePopupBounds() const {
    const math::FVector2 myPos = getWorldBounds().getMin();
    const int visibleRows = std::min(
        static_cast<int>(_items.size()), _maxPopupItems);
    const float h = std::max(
        kDefaultHeight,
        static_cast<float>(visibleRows) * _popup->getItemHeight() +
            ScrollBar::kDefaultBarWidth);
    return math::FRectangle(
        myPos.x,
        myPos.y + getHeight() + kPopupGap,
        myPos.x + getWidth(),
        myPos.y + getHeight() + kPopupGap + h);
}

void ComboBox::openPopup() {
    ensurePopupCreated();
    if (_items.empty()) {
        // Nothing to show — refuse to open.
        return;
    }
    _popupOpen = true;
    _popup->setVisible(true);
    syncPopupSelection();

    // Phase A (A2): mount the popup on UIManager's overlay root, NOT as
    // a child of this ComboBox. The overlay handles rendering + hit-test
    // ordering (popup above the main tree) and owns the popup lifetime.
    // DropdownManager enforces single-active-popup — if a different
    // popup is open it will be closed first.
    UIManager& ui = UIManager::get();
    ui.openPopup(this, _popup);

    // If no active UIManager has an overlay (unit tests that forget to
    // initialize, or get() fallback), the popup was not mounted — report
    // closed so callers don't think a dangling dropdown is live.
    if (_popup == nullptr || _popup->getParent() == nullptr) {
        _popupOpen = false;
        return;
    }

    // Phase A (A2 L4): flip + clamp against the viewport. Position the
    // popup below the anchor by default; if it would overflow the
    // viewport bottom AND there's room to flip above, place it above.
    // Always clamp x within [0, clientWidth - popup.width].
    const math::FVector2 client = ui.getClientSize();
    const math::FRectangle anchor = getWorldBounds();

    // Set popup size first so flip math has a real height.
    const int visibleRows = std::min(
        static_cast<int>(_items.size()), _maxPopupItems);
    const float popupH = std::max(
        kDefaultHeight,
        static_cast<float>(visibleRows) * _popup->getItemHeight() +
            ScrollBar::kDefaultBarWidth);
    _popup->setSize(math::FVector2(getWidth(), popupH));

    const float popupW = _popup->getSize().x;
    math::FVector2 pos(anchor.minX, anchor.maxY + kPopupGap);

    // Flip upward if it would overflow the viewport bottom.
    if (client.y > 0.0f && pos.y + popupH > client.y
        && anchor.minY - popupH - kPopupGap >= 0.0f) {
        pos.y = anchor.minY - popupH - kPopupGap;
    }

    // Clamp x within viewport. If popup is wider than viewport, pin to 0.
    if (client.x > 0.0f) {
        if (popupW > client.x) {
            pos.x = 0.0f;
        } else if (pos.x + popupW > client.x) {
            pos.x = client.x - popupW;
        } else if (pos.x < 0.0f) {
            pos.x = 0.0f;
        }
    }

    _popup->setPosition(pos);
    _popup->markBoundsDirty();
    // Overlay is a plain Widget (not CompoundWidget) so UIManager::layout
    // does not cascade into it. Row hit-rects stay at the ListView's
    // pre-mount size unless we lay out here — without this, popup row
    // clicks miss and selection never fires.
    _popup->performLayout();
}

void ComboBox::onPopupDismissedByManager() {
    _popupOpen = false;
    // Manager is about to destroyWidgetTree(_popup). Drop our non-owning
    // pointer so isPopupOpen() / closePopup() cannot touch freed memory.
    _popup = nullptr;
}

void ComboBox::closePopup() {
    _popupOpen = false;
    if (_popup == nullptr) return;

    _popup->setVisible(false);

    if (_popup->getParent() != nullptr) {
        // Unmount only (destroy=false). Selection / row-click callbacks
        // run on the ListView stack — destroyWidgetTree here would free
        // `this` mid-callback (0xC0000005). Foreign dismiss paths
        // (other popup open, click-outside) still destroy via
        // UIManager::closePopup(p, true) + onPopupDismissedByManager.
        UIManager::get().closePopup(_popup, /*destroy=*/false);
    }
}

Widget* ComboBox::hitTest(const math::FVector2& worldPos) {
    // Phase A (A2): the popup lives on the overlay, NOT as a child of
    // ComboBox. UIManager's overlay-first hit-test funnel already routes
    // clicks inside the popup's world bounds to the popup's rows. We just
    // hit-test self-bounds like a normal CompoundFocusableWidget.
    return CompoundFocusableWidget::hitTest(worldPos);
}

bool ComboBox::onMouseButtonUp(const UIMouseEvent& e) {
    if (!isEnabled() || e.mouseButton != 0) return false;

    // Click on the main ComboBox area: toggle popup.
    if (getWorldBounds().contains(e.mousePos)) {
        if (_popupOpen) {
            closePopup();
        } else {
            openPopup();
        }
        return true;
    }

    // Click outside while popup is open — UIManager's click-outside
    // detector already closed the popup in onMouseButtonDown. Don't
    // re-close here; just swallow the event.
    if (_popupOpen) {
        return true;
    }
    return false;
}

void ComboBox::onMouseLeave() {
    CompoundFocusableWidget::onMouseLeave();
    // Don't auto-close on leave: user may move the cursor toward the
    // popup. UIManager re-evaluates hover every move; if the cursor
    // ends up outside ComboBox AND outside popup on the next frame
    // and no button is held, the click-outside path in onMouseButtonUp
    // closes it. (Native OS dropdowns behave similarly.)
}

// =============================================================================
// Phase B (B2) — keyboard navigation + click focus grab
// =============================================================================

bool ComboBox::onMouseButtonDown(const UIMouseEvent& e) {
    // Phase B (B2): grab focus on press so the user can arrow-cycle
    // without needing an explicit click-then-Tab. The actual click →
    // toggle-popup path lives in onMouseButtonUp (returning false
    // here lets the event flow up unchanged).
    if (e.mouseButton == 0) {
        UIManager::get().setFocus(this);
    }
    return false;
}

bool ComboBox::onKeyDown(int keyCode) {
    // ComboBox owns ALL its keys — does NOT delegate to the popup
    // ListView. See DECISION 3 in the header note for the state table.

    if (_items.empty()) return false;
    const int n = static_cast<int>(_items.size());
    const bool isOpen = isPopupOpen();

    switch (keyCode) {
    case UIKey_Down:
    case UIKey_Up: {
        if (!isOpen) {
            // Closed → open popup, target depends on direction.
            // Down: target = current (or 0 if nothing selected).
            // Up:   target = last item.
            const int target = (keyCode == UIKey_Up)
                ? (n - 1)
                : (_selectedIndex < 0 ? 0
                                      : std::min(_selectedIndex, n - 1));
            setSelectedIndex(target);
            openPopup();
            return true;
        }
        // Open → arrows change selection. Wrap the whole popup-sync
        // sequence in _silentPopupSync: setSelectedIndex internally
        // calls syncPopupSelection, AND we then explicitly re-sync to
        // ensure the highlight tracks the keyboard move. Both writes
        // would otherwise fire the popup's _onSelectionChanged →
        // closePopup + fire host callback, both wrong mid-arrows.
        const int cur = (_selectedIndex < 0) ? 0 : _selectedIndex;
        const int next = (keyCode == UIKey_Down)
            ? (cur + 1) % n
            : (cur <= 0 ? n - 1 : cur - 1);
        _silentPopupSync = true;
        setSelectedIndex(next);
        if (_popup != nullptr) {
            _popup->setSelectedIndex(_selectedIndex);
        }
        _silentPopupSync = false;
        return true;
    }
    case UIKey_Enter:
        if (isOpen) {
            // Commit: close + fire callback.
            closePopup();
            if (_onSelectionChanged) {
                _onSelectionChanged(_selectedIndex);
            }
            return true;
        }
        return false;
    case UIKey_Escape:
        if (isOpen) {
            // Dismiss without committing — _selectedIndex unchanged.
            closePopup();
            return true;
        }
        return false;
    default:
        return false;
    }
}

void ComboBox::performLayout() {
    layoutChildren();
}

void ComboBox::layoutChildren() {
    if (_display != nullptr) {
        _display->setPosition(math::FVector2(0.0f, 0.0f));
        // Reserve the right edge for the dropdown arrow.
        const float textW = std::max(0.0f, getWidth() - kArrowWidth);
        _display->setSize(math::FVector2(textW, getHeight()));
    }
    // Phase A (A2): popup positioning moved to openPopup(), which uses
    // the live viewport metrics from UIManager for flip + clamp. The
    // popup is NOT a child of this ComboBox (it lives on the overlay),
    // so layoutChildren has nothing to do for it here.
}

void ComboBox::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    // Background — main ComboBox area only; popup renders itself.
    const ResolvedStyle style = resolveStyle(getStyleId());
    math::FVector4 bg = style.hasStyle
        ? style.backgroundColor
        : math::FVector4(0.16f, 0.16f, 0.18f, 1.0f);
    math::FVector4 border = style.hasStyle
        ? style.borderColor
        : math::FVector4(0.45f, 0.45f, 0.5f, 1.0f);
    float bw = style.hasStyle ? style.borderWidth : 1.0f;

    renderer.drawRect(bounds, bg);
    renderer.drawBorderRect(bounds, border, bw, 2.0f);

    // Arrow chevron — a small downward triangle on the right edge, drawn
    // as a quad with a notch to suggest a chevron without a path API.
    const float ax0 = bounds.maxX - kArrowWidth;
    const float ax1 = bounds.maxX;
    const float ay0 = bounds.minY;
    const float ay1 = bounds.maxY;
    const float midX = (ax0 + ax1) * 0.5f;
    const float midY = (ay0 + ay1) * 0.5f;
    const float inset = 4.0f;
    // Two filled triangles approximated as 3 rects (a chevron). We use a
    // simple inverted-V — top half darker than bottom half — for v1.
    const math::FVector4 arrowColor(0.75f, 0.75f, 0.8f, 1.0f);
    renderer.drawRect(
        math::FRectangle(midX - inset, midY - 1.0f,
                         midX, midY + 1.0f),
        arrowColor);
    renderer.drawRect(
        math::FRectangle(midX, midY - 1.0f,
                         midX + inset, midY + 1.0f),
        arrowColor);

    // Display label (TextLabel child) + popup (ListView child, when open)
    // render themselves via the standard CompoundWidget cascade after this
    // method returns. We render ONLY the main area + arrow + border here.
    // (Drawing the popup explicitly would double-render it because
    // CompoundWidget::render walks _children after calling onRender.)
}

Widget* createComboBoxWidget() {
    return new ComboBox();
}

} // namespace ayt::ui
