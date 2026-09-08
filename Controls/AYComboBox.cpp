#include "AYUI/ComboBox.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/PopupAnchor.h"
#include "AYUI/Style.h"
#include "AYUI/TextMeasure.h"
#include "AYUI/UIManager.h"
#include "AYUI/UIKeyCode.h"
#include "AYMath/MathUtils.h"

#include <algorithm>

namespace ayt::ui {

ComboBox::ComboBox() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    // Keep layoutPositionManaged=true so VBox/HBox flow can place us.
    // (Previously false left ComboBox stuck at 0,0 inside Gallery pages.)

    _display = new TextLabel();
    _display->setText(L"");
    _display->setVerticalAlignment(TextLabel::VAlignment::Center);
    addChildExternal(_display);
}

ComboBox::~ComboBox() {
    // Phase A (A2): _display is still a child of ComboBox; destroyWidgetTree
    // frees it when the ComboBox tree is destroyed. _popup may be mounted
    // on the overlay OR held unmounted after a soft closePopup — either
    // way we must not leave it on the overlay, and if it is unmounted we
    // still own the allocation and must free it.
    if (_popup != nullptr) {
        if (UIManager* ui = UIManager::tryGet()) {
            if (_popup->getParent() != nullptr) {
                ui->closePopup(_popup, /*destroy=*/true);
            } else {
                destroyWidgetTree(_popup);
            }
        } else if (_popup->getParent() == nullptr) {
            // Manager already shut down; mounted popups were torn down with
            // the overlay (and onPopupDismissedByManager nulls _popup when
            // we were the anchor). Only free a soft-closed unmounted popup.
            destroyWidgetTree(_popup);
        }
        _popup = nullptr;
    }
    _display = nullptr;
}

void ComboBox::setEnabled(bool enabled) {
    if (_enabled == enabled) return;
    _enabled = enabled;
    if (!enabled && _popupOpen) closePopup();
    markDirty();
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
    // PR-C2 — programmatic selection also invalidates the typeahead
    // buffer because the user (or host) has committed to a value. The
    // next letter starts a fresh prefix.
    _typeaheadBuffer.clear();
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

void ComboBox::setSelectedIndexAndNotify(int index) {
    const int previous = _selectedIndex;
    setSelectedIndex(index);
    if (_selectedIndex != previous && _onSelectionChanged) {
        _onSelectionChanged(_selectedIndex);
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
    // G1 — lock the popup to single-selection mode so ComboBox's existing
    // single-select contract (popup click → close + fire host callback)
    // is unchanged after ListView's multi-select API landed. Hosts wanting
    // a multi-select popup should build their own ListView with
    // setSelectionMode(Extended) rather than going through ComboBox.
    _popup->setSelectionMode(ListView::SelectionMode::Single);
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
        //
        // PR-C2: a real popup-row click (NOT silent sync) invalidates
        // the typeahead buffer because the user has committed to a
        // selection — clear it so a subsequent letter starts fresh.
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
        // Mouse-driven selection (popup row click). Drop the typeahead
        // buffer; the popup is about to close anyway.
        _typeaheadBuffer.clear();
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
    // Programmatic mirror into the popup must NOT look like a row click:
    // ListView::setSelectedIndex fires onSelectionChanged, which would
    // closePopup() + invoke the host callback (applyProperty mid-sync).
    const bool wasSilent = _silentPopupSync;
    _silentPopupSync = true;
    _popup->setSelectedIndex(_selectedIndex);
    _silentPopupSync = wasSilent;
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
    if (!_enabled) return;
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
    //
    // Phase D §5.3 — tryGet() guards against the batch SEGV: when this
    // ComboBox opens during a test that has already torn its UIManager
    // down, get()'s static fallback would pin the popup on a process-
    // lifetime static and crash when that static dtor runs.
    UIManager* uiPtr = UIManager::tryGet();
    if (uiPtr == nullptr) return;
    UIManager& ui = *uiPtr;
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

    // Flip upward if it would overflow the viewport bottom.
    // PR-Container-Shared-Contract: route through popupAnchorPlacement so
    // the flip+clamp math is shared with Tooltip (which had the missing
    // x-clamp bug). helper is at Controls/AYPopupAnchor.cpp.
    const math::FVector2 popupSize(popupW, popupH);
    const math::FVector2 belowPos(anchor.minX, anchor.maxY + kPopupGap);
    const math::FVector2 abovePos(anchor.minX,
                                  anchor.minY - popupH - kPopupGap);
    const math::FVector2 pos = popupAnchorPlacement(
        belowPos, abovePos, popupSize, client);

    _popup->setPosition(pos);
    _popup->markBoundsDirty();
    // Overlay is a plain Widget (not CompoundWidget) so UIManager::layout
    // does not cascade into it. Row hit-rects stay at the ListView's
    // pre-mount size unless we lay out here — without this, popup row
    // clicks miss and selection never fires.
    _popup->performLayout();

    // UI animation lane: popup fade-in (mirrors Menu::open's pop-in).
    _popup->setOpacity(0.0f);
    _popup->animateOpacity(1.0f, 140.0f, AnimationCurve::EaseOut);
    // UI-anim cut 2: slide-in from 8px above the anchored position.
    _popup->setPosition(pos + math::FVector2(0.0f, -8.0f));
    _popup->animatePositionTo(pos, 160.0f, AnimationCurve::EaseOut);
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

    if (_popup->getParent() != nullptr) {
        // UI animation lane: UX-driven close — the popup fades out on the
        // overlay, then unmounts (destroy=false; the popup tree stays
        // alive for reuse on the next open). _popupOpen is already false
        // so the popup is logically closed; the fade is purely visual.
        // Selection / row-click callbacks run on the ListView stack —
        // destroying here would free `this` mid-callback (0xC0000005).
        // Foreign dismiss paths (other popup open, click-outside) still
        // destroy via UIManager::closePopup(p, true) or
        // beginPopupFadeOut(p, true) + onPopupDismissedByManager.
        //
        // Code-review 2026-08-02 #10: prefer tryGet() over get(). get()'s
        // static-fallback bootstrap would pin the popup onto the
        // process-lifetime static UIManager (R3 landmine from Phase A2)
        // when this ComboBox lives in a test fixture whose manager has
        // already been torn down. Safe no-op when no manager is active.
        if (UIManager* ui = UIManager::tryGet()) {
            ui->beginPopupFadeOut(_popup, /*destroy=*/false);
        }
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
        if (UIManager* ui = UIManager::tryGet()) {
            ui->setFocus(this);
        }
    }
    return false;
}

bool ComboBox::onKeyDown(int keyCode) {
    // ComboBox owns ALL its keys — does NOT delegate to the popup
    // ListView. See DECISION 3 in the header note for the state table.
    //
    // PR-C2 — typeahead letter handling. UIKey_A..UIKey_Z (and digits
    // 0..9 if you want to extend) become characters in the typeahead
    // buffer. We treat each letter as: append to buffer (lowercased),
    // find next match starting from current selection+1, jump there,
    // and ensure the popup is open so the user sees the match. The
    // timer is reset so the next letter within kTypeaheadTimeout
    // extends the prefix; after the timeout the buffer clears.

    if (_items.empty()) return false;
    const int n = static_cast<int>(_items.size());

    // PR-C2 — typeahead: A-Z (case-insensitive). Each match is a single
    // character so we accept exactly one VK per press. The buffer can
    // grow if the user keeps typing within TypeaheadBuffer::kTimeout.
    if (keyCode >= UIKey_A && keyCode <= UIKey_Z) {
        // Append the lowercase letter (TypeaheadBuffer resets its own
        // timer; tick() may have already cleared an expired buffer).
        const wchar_t ch = static_cast<wchar_t>(
            L'a' + (keyCode - UIKey_A));
        _typeaheadBuffer.append(ch);
        const int startFrom = (_selectedIndex < 0) ? 0
                                                  : (_selectedIndex + 1) % n;
        auto getter = [this](int i) -> const std::wstring& {
            return _items[static_cast<size_t>(i)];
        };
        int match = _typeaheadBuffer.findMatch(startFrom, n, getter);
        if (match < 0 && _typeaheadBuffer.size() > 1) {
            // Letter-switch recovery: "ab" miss → restart with 'b' (not
            // typo-pop back to 'a', which blocked A→B highlight jumps).
            match = _typeaheadBuffer.recoverFromLetterSwitch(startFrom, n, getter);
        }
        if (match < 0) {
            _typeaheadBuffer.clear();
            return false;
        }
        // Match found — jump there silently (no popup close + no
        // _onSelectionChanged fire, matching the arrow-key path).
        _silentPopupSync = true;
        setSelectedIndex(match);
        if (_popup != nullptr) {
            _popup->setSelectedIndex(match);
            _popup->scrollToIndex(match);
        }
        _silentPopupSync = false;
        if (!isPopupOpen()) {
            openPopup();
        }
        return true;
    }

    const bool isOpen = isPopupOpen();

    switch (keyCode) {
    case UIKey_Down:
    case UIKey_Up: {
        // PR-C2 — arrow keys invalidate the typeahead buffer because
        // the user has switched to navigation. Matches Windows: once
        // you press an arrow, the next letter starts a fresh prefix.
        _typeaheadBuffer.clear();
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
        // PR-C2 — Enter invalidates the typeahead buffer.
        _typeaheadBuffer.clear();
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
        // PR-C2 — Escape invalidates the typeahead buffer.
        _typeaheadBuffer.clear();
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

void ComboBox::tick(float dt) {
    // Preserve the compound cascade: this advances Widget opacity/position
    // tweens on the ComboBox itself and ticks the display child. Omitting
    // it made animateOpacity/animatePositionTo silently stall on ComboBox.
    CompoundFocusableWidget::tick(dt);
    // PR-TypeaheadBuffer: timer + auto-clear live on the struct.
    // Driven by UIManager::update → _root->tick which recurses via
    // CompoundWidget::tick (see AYWidget.cpp:37).
    _typeaheadBuffer.tick(dt);
}

void ComboBox::performLayout() {
    layoutChildren();
}

void ComboBox::layoutChildren() {
    if (_display != nullptr) {
        // Inset text from the left border; reserve the right edge for the
        // dropdown arrow (previously text sat flush against the chrome).
        _display->setPosition(math::FVector2(kTextPadX, 0.0f));
        const float textW = std::max(0.0f, getWidth() - kArrowWidth - kTextPadX);
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
    const ResolvedStyle style = resolveStyle(getStyleId(), this);
    math::FVector4 bg = style.hasStyle
        ? style.backgroundColor
        : math::FVector4(0.16f, 0.16f, 0.18f, 1.0f);
    math::FVector4 border = style.hasStyle
        ? style.borderColor
        : math::FVector4(0.45f, 0.45f, 0.5f, 1.0f);
    float bw = style.hasStyle ? style.borderWidth : 1.0f;
    const float cornerRadius = style.hasStyle ? style.cornerRadius : 2.0f;

    renderer.drawRoundedRect(bounds, bg, cornerRadius);
    renderer.drawBorderRect(bounds, border, bw, cornerRadius);

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

void ComboBox::renderChildren(IRenderBackend& renderer) {
    if (_display == nullptr) return;

    const int fontSize = resolveTextFontSize(
        getStyleId(), kDefaultTextFontSize);
    const math::FVector4 textColor = resolveTextColor(
        getStyleId(), this, math::FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    if (_display->getFontSize() != fontSize) {
        _display->setFontSize(fontSize);
    }
    if (_display->getTextColor() != textColor) {
        _display->setTextColor(textColor);
    }

    // The display label's text shaping bounds do not imply a backend clip.
    // Keep long selected values inside the text slot so they cannot paint
    // across the arrow or a neighboring property-panel action button.
    renderer.pushClip(_display->getWorldBounds());
    _display->render(renderer);
    renderer.popClip();
}

Widget* createComboBoxWidget() {
    return new ComboBox();
}

} // namespace ayt::ui
