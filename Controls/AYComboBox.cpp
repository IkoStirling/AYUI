#include "AYComboBox.h"
#include "AYScrollBar.h"
#include "AYIRenderBackend.h"
#include "AYStyle.h"
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
    // _display + _popup are children — destroyWidgetTree handles them
    // when the factory-owned tree root is deleted. Just NULL our pointers
    // so a stray dtor path can't double-free.
    _display = nullptr;
    _popup   = nullptr;
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
    syncPopupSelection();
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
    addChildExternal(_popup);
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
    bringToFront();   // popup should be on top among siblings
    syncPopupSelection();
    // Re-layout so popup gets its real bounds.
    layoutChildren();
}

void ComboBox::closePopup() {
    if (_popup != nullptr) {
        _popup->setVisible(false);
    }
    _popupOpen = false;
}

Widget* ComboBox::hitTest(const math::FVector2& worldPos) {
    // When popup is open, give it priority so clicks on popup rows are
    // routed to the popup list (rows are technically outside the
    // ComboBox's main bounds).
    if (_popupOpen && _popup != nullptr && _popup->isVisible()) {
        Widget* hit = _popup->hitTest(worldPos);
        if (hit != nullptr) return hit;
    }
    return CompoundWidget::hitTest(worldPos);
}

bool ComboBox::onMouseButtonUp(const UIMouseEvent& e) {
    if (!isEnabled() || e.mouseButton != 0) return false;

    // Popup open and click hits popup → forward to the popup's rows.
    if (_popupOpen && _popup != nullptr && _popup->isVisible()) {
        // First test whether the click is in the popup. We test against
        // the popup's WORLD bounds because the popup is positioned in
        // world coordinates when the ComboBox is laid out (its parent
        // chain puts it under ComboBox).
        if (_popup->getWorldBounds().contains(e.mousePos)) {
            // Route to the popup's normal CompoundWidget hit-test path
            // via its own onMouseButtonUp.
            Widget* hit = _popup->hitTest(e.mousePos);
            if (hit != nullptr) {
                // _popup is a ListView; ListView's row click handler will
                // fire and call setSelectedIndex via the popup's own
                // _onSelectionChanged, which we route below in
                // openPopup() wiring. The picked row's click also closes
                // the popup.
                hit->onMouseButtonUp(e);
                return true;
            }
        }
    }

    // Click on the main ComboBox area: toggle popup.
    if (getWorldBounds().contains(e.mousePos)) {
        if (_popupOpen) {
            closePopup();
        } else {
            openPopup();
        }
        return true;
    }

    // Click outside while popup is open → close.
    if (_popupOpen) {
        closePopup();
        return true;
    }
    return false;
}

void ComboBox::onMouseLeave() {
    CompoundWidget::onMouseLeave();
    // Don't auto-close on leave: user may move the cursor toward the
    // popup. UIManager re-evaluates hover every move; if the cursor
    // ends up outside ComboBox AND outside popup on the next frame
    // and no button is held, the click-outside path in onMouseButtonUp
    // closes it. (Native OS dropdowns behave similarly.)
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
    if (_popupOpen && _popup != nullptr) {
        const math::FRectangle pb = computePopupBounds();
        // The popup's parent is ComboBox; its world position must equal
        // its local position (parent world = own world pos for our flat
        // tree). Use setPosition to place it just below the main area.
        const math::FVector2 myPos = getWorldBounds().getMin();
        _popup->setPosition(math::FVector2(
            0.0f, getHeight() + kPopupGap));
        _popup->setSize(math::FVector2(
            getWidth(), pb.maxY - pb.minY));
        // Force the popup's bounds dirty so getWorldBounds recomputes
        // through the new local position.
        _popup->markBoundsDirty();
    }
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
