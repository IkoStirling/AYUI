#include "AYMenu.h"
#include "IAYRenderBackend.h"
#include "AYSeparator.h"
#include "AYUIManager.h"
#include "UIKeyCode.h"
#include <algorithm>

namespace ayt::ui {

Menu::Menu() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    setVisible(false);
}

Menu::~Menu() {
    // R3 — break the _ownerHost back-pointer BEFORE any compound dtor
    // work runs. If we're being destroyed while still open on the overlay
    // (e.g. the host MenuBar died first and left us alive as a zombie),
    // close() / dismissFromManager() would route _ownerHost->addChild()
    // into freed memory. Idempotent; safe to call even when already null.
    //
    // We do NOT call close() here — dtor order is the caller's problem
    // (MenuBar's dtor walks open menus and calls close() on each BEFORE
    // clearing its own state; standalone Menu destruction skips that
    // because there's no host to coordinate with).
    clearOwnerHost();

    // _submenus are owned; CompoundWidget destructor frees our children
    // (which includes MenuItems). Submenu pointers themselves were added
    // as children of this menu (via attachSubmenu), so they self-clean.
    // Just null our pointers so dtor runs cleanly if base hasn't yet.
    for (auto* m : _submenus) {
        (void)m;
    }
    _items.clear();
    _submenus.clear();
}

MenuItem* Menu::addItem(const std::wstring& text) {
    auto* item = new MenuItem();
    item->setText(text);
    item->setSize(math::FVector2(kDefaultWidth - 2.0f * kDefaultPad,
                                  kDefaultHeight));
    addChild(item);   // owning
    _items.push_back(item);
    // Forward click → menu-close hand-off.
    item->setOnActivate([this, idx = _items.size() - 1]() {
        _lastActivatedIndex = static_cast<int>(idx);
        if (_onItemActivated) _onItemActivated(static_cast<int>(idx));
        close();
    });
    layoutItems();
    return item;
}

MenuItem* Menu::addItem(const std::wstring& text,
                        const std::wstring& shortcut) {
    auto* item = addItem(text);
    item->setShortcut(shortcut);
    return item;
}

MenuItem* Menu::addSeparator() {
    auto* sep = new Separator();
    sep->setSize(math::FVector2(kDefaultWidth - 2.0f * kDefaultPad, 1.0f));
    addChild(sep);
    // We treat Separator as a non-MenuItem special; keep _items clear
    // of separators by stuffing a nullptr in their slot would break
    // getItem semantics. Easier: stash separators in a separate vector.
    // For v1, we don't use separators in unit tests; full support would
    // need an Items union model. Until then, just orphan the separator
    // (it'll be deleted but not reach _items).
    // NOTE: items not added to _items nor visible via getItem — we just
    // own them and lay them out.
    sep->setVisible(true);
    layoutItems();
    return nullptr;
}

MenuItem* Menu::getItem(size_t index) const {
    if (index >= _items.size()) return nullptr;
    return _items[index];
}

void Menu::clearItems() {
    _items.clear();
    // Snapshot children before deleting — CompoundWidget owns by default;
    // we delete each MenuItem (and submenu) and remove them from the
    // parent's children list so re-adding more items doesn't double-free.
    // Copy first: getChildren() returns a reference; removeChild mutates it.
    std::vector<Widget*> kids = getChildren();
    for (auto* kid : kids) {
        if (kid != nullptr) {
            removeChild(kid);
            delete kid;
        }
    }
    _submenus.clear();
    layoutItems();
}

void Menu::attachSubmenu(MenuItem* item, Menu* sub) {
    if (item == nullptr || sub == nullptr) return;
    item->setSubmenu(sub);
    addChild(sub);   // owning — freeing this menu frees the submenu.
    _submenus.push_back(sub);
}

void Menu::open(Widget* host, const math::FVector2& anchorPos) {
    if (host == nullptr) return;
    _ownerHost = host;
    setVisible(true);
    setPosition(anchorPos);
    // Phase A (A2): mount on UIManager's overlay root instead of the host.
    // This lets the menu render + hit-test above any nested layout (e.g.
    // a MenuBar inside a Window inside a VBox) and survive host destruction
    // cleanly via the overlay's destroyWidgetTree path.
    //
    // Phase D §5.3 — tryGet guards against batch SEGV: opening a Menu in
    // a test whose UIManager is already shut down would otherwise pin
    // the Menu on the static fallback and crash on fallback dtor.
    UIManager* uiPtr = UIManager::tryGet();
    if (uiPtr == nullptr) return;
    UIManager& ui = *uiPtr;
    ui.openPopup(host, this);
    _open = true;
    // Phase B (B3) R3: save the focused widget BEFORE we steal focus,
    // so close() can restore it. Order matters: openPopup may close a
    // different active dropdown which fires onPopupDismissedByManager,
    // but UIManager's own _focusedWidget is untouched by that — so we
    // can capture it here, after the overlay is mounted, and the saved
    // pointer is the live widget the user was working with.
    _focusedWidgetBefore = ui.getFocusedWidget();
    ui.setFocus(this);
    performLayout();

    // PR-anim: pop-in fade. The menu tree renders at full alpha a frame
    // later (Widget::render pushes the tweened opacity), so first paint
    // is already the start of the fade — no first-frame pop. Fade-out on
    // close is deliberately NOT animated: close() destroys the tree
    // synchronously, and a fade-out would need a delayed-destroy dance.
    setOpacity(0.0f);
    animateOpacity(1.0f, 140.0f, AnimationCurve::EaseOut);
}

void Menu::detachForHostDestruction()
{
    _open = false;
    setVisible(false);
    _focusedWidgetBefore = nullptr;
    if (UIManager* ui = UIManager::tryGet()) {
        // Soft bookkeeping only — closePopup walks isDescendantOf /
        // removeChild and is unsafe while destroyWidgetTree is tearing
        // down parents (Reload JSON / shutdown crashes).
        ui->abandonPopup(this);
    }
}

void Menu::close() {
    if (!_open) return;
    _open = false;
    // PR-C3 hotfix — clear keyboard-hover flag on the previously-highlighted
    // row so a re-open starts from a clean slate (otherwise the same row
    // would still appear highlighted when the user reopens the menu, even
    // though no typeahead letter was pressed).
    if (_hoveredIndex >= 0 && _hoveredIndex < static_cast<int>(_items.size())) {
        if (auto* prev = _items[static_cast<size_t>(_hoveredIndex)]) {
            prev->setKeyboardHovered(false);
        }
    }
    _hoveredIndex = -1;
    setVisible(false);
    // Soft unmount: MenuBar keeps a durable Menu* for the session.
    // closePopup(..., destroy=true) used to free the tree here, which
    // left MenuBar::_menus[i].menu dangling after the first item click
    // / Escape — subsequent opens and hit-tests then stuck or AV'd.
    if (UIManager* ui = UIManager::tryGet()) {
        // Shutdown / tree teardown: `_focusedWidgetBefore` (and any
        // previous focus target) may already be destroyed. Skip all
        // virtual setFocus dispatch — UIManager::shutdown already
        // cleared focus via clearFocusNoDispatch.
        if (!ui->isShuttingDown()) {
            if (_focusedWidgetBefore != nullptr) {
                ui->setFocus(_focusedWidgetBefore);
                _focusedWidgetBefore = nullptr;
            }
            if (_onClose) _onClose();
            if (ui->getFocusedWidget() == this) {
                ui->setFocus(nullptr);
            }
        } else {
            _focusedWidgetBefore = nullptr;
        }
        ui->closePopup(this, /*destroy=*/false);
    }
    // Reparent under the owning MenuBar so MenuBar still tracks us.
    // Must be External — MenuBar::_menus owns delete in ~MenuBar;
    // addChild would let destroyWidgetTree free us first → UAF.
    if (_ownerHost != nullptr && getParent() == nullptr) {
        _ownerHost->addChildExternal(this);
    }
}

Widget* Menu::hitTest(const math::FVector2& worldPos) {
    if (!_open) return nullptr;
    const math::FRectangle b = getWorldBounds();
    if (!b.contains(worldPos)) return nullptr;
    // Descend into children the normal way — CompoundFocusableWidget::hitTest
    // does this for us.
    return CompoundFocusableWidget::hitTest(worldPos);
}

void Menu::layoutItems() {
    const float h = kDefaultHeight;
    const float w = kDefaultWidth - 2.0f * kDefaultPad;
    float y = kDefaultPad;
    for (size_t i = 0; i < _items.size(); ++i) {
        if (_items[i] != nullptr) {
            _items[i]->setPosition(math::FVector2(kDefaultPad, y));
            _items[i]->setSize(math::FVector2(w, h));
            y += h;
        }
    }
    setSize(math::FVector2(kDefaultWidth, y + kDefaultPad));
}

// PR-C3 feedback — single mutation point for _hoveredIndex so the
// _onHoverChanged callback always fires when the highlight moves.
// Before this method, every direct `_hoveredIndex = X` call had to
// remember to fire the callback by hand — easy to miss on the
// typeahead-letter path. Centralizing keeps the contract uniform.
//
// PR-C3 hotfix — also flip MenuItem::_isKeyboardHovered on the matching
// row (and clear it on the previous one) so the highlight bar actually
// PAINTS at the new index. Without this, typeahead was logically correct
// but visually silent — the user saw no feedback when pressing 'A' on
// an open menu.
void Menu::setHoveredIndex(int index) {
    if (_hoveredIndex == index) return;
    // Clear keyboard-hover flag on the outgoing row.
    if (_hoveredIndex >= 0 && _hoveredIndex < static_cast<int>(_items.size())) {
        if (auto* prev = _items[static_cast<size_t>(_hoveredIndex)]) {
            prev->setKeyboardHovered(false);
        }
    }
    _hoveredIndex = index;
    // Set keyboard-hover flag on the incoming row.
    if (_hoveredIndex >= 0 && _hoveredIndex < static_cast<int>(_items.size())) {
        if (auto* next = _items[static_cast<size_t>(_hoveredIndex)]) {
            next->setKeyboardHovered(true);
        }
    }
    if (_onHoverChanged) {
        _onHoverChanged(_hoveredIndex);
    }
}

// PR-S3 — typeahead-aware hover setter. Same as setHoveredIndex but
// also fires _onHoverChanged when the new index equals the current
// one (setHoveredIndex's identity-check early-returns). Without this,
// a typeahead letter that resolves to the already-hovered item (e.g.
// colors menu {Red, Green, Blue}, user types 'R' on a fresh menu
// where _hoveredIndex is already 0 = Red) produces no feedback for
// the host's status label.
void Menu::setHoveredIndexFromTypeahead(int index) {
    setHoveredIndex(index);
    if (index == _hoveredIndex && _onHoverChanged) {
        _onHoverChanged(_hoveredIndex);
    }
}

void Menu::performLayout() {
    CompoundFocusableWidget::performLayout();
    layoutItems();
}

void Menu::onRender(IRenderBackend& renderer) {
    if (!_open || !isVisible()) {
        return;
    }
    // Use local position + size to avoid a stale getWorldBounds when the
    // menu has never been laid out (caller might call render() directly
    // without first wiring into a host). For a popup later this is the
    // right answer too — the host pushes the menu into the widget tree
    // and CompoundWidget::performLayout stays a no-op for popup, leaving
    // position/size intact.
    const math::FVector2 pos = getPosition();
    const math::FVector2 sz = getSize();
    if (sz.x <= 0.0f || sz.y <= 0.0f) return;
    math::FRectangle b(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);
    // B4: floating-popup drop shadow — first so the plate covers the
    // blurred edge (matches Window B2, tighter offset/blur for menus).
    renderer.drawRectShadow(b, IRenderBackend::ShadowStyle{
        math::FVector4(0.0f, 0.0f, 0.0f, 0.40f),  // color
        math::FVector2(0.0f, 3.0f),               // offset
        8.0f,                                     // blur
        3.0f                                      // cornerRadius
    });
    // B4: rounded plate + single border ring — the old 4-strip square
    // border (4 draw calls) becomes one SDF ring at radius 3, matching
    // Tooltip's B1 plate (which copied the menu palette).
    constexpr float kRadius = 3.0f;
    renderer.drawRoundedRect(b, math::FVector4(0.13f, 0.14f, 0.17f, 0.96f), kRadius);
    renderer.drawBorderRect(b, math::FVector4(0.35f, 0.35f, 0.40f, 1.0f), 1.0f, kRadius);
}

Widget* createMenuWidget() { return new Menu(); }

// =============================================================================
// Phase B (B3) — keyboard navigation + click focus grab
// =============================================================================

bool Menu::onMouseButtonDown(const UIMouseEvent& e) {
    // Grab focus on press — open() already saved the previous focus, so
    // if the user later closes via Escape or item activation, focus
    // returns cleanly. Returning false lets the click flow up to the
    // item hit-test path so row clicks still select.
    if (e.mouseButton == 0) {
        if (UIManager* ui = UIManager::tryGet()) {
            ui->setFocus(this);
        }
    }
    return false;
}

bool Menu::onKeyDown(int keyCode) {
    // Menu owns Up/Down/Enter/Escape. Tab is intentionally not consumed —
    // UIManager intercepts Tab before this method sees it (R2 contract).
    // Tab-while-menu-open focuses the NEXT focusable widget, leaving the
    // menu — Escape is the menu-internal dismiss path.
    //
    // PR-C3: extended with first-letter typeahead (UIKey_A..UIKey_Z).
    // Behavior mirrors Windows native menus:
    //   - Single-letter jump: highlight advances to the next item whose
    //     first character matches.
    //   - Multi-letter prefix within kTypeaheadTimeout: extends the match.
    //   - Typeahead does NOT close the menu or activate the item — the
    //     user still presses Enter to confirm. This matches the OS
    //     convention and keeps the menu predictable for keyboard users.

    if (_items.empty()) {
        if (keyCode == UIKey_Escape) { close(); return true; }
        return false;
    }
    const int n = static_cast<int>(_items.size());

    // PR-C3 — typeahead letter handling. Same buffer + timeout shape as
    // ComboBox::onKeyDown's typeahead block; same buffer semantics on
    // Down/Up/Enter/Escape below (each clears the buffer).
    //
    // PR-C3 hotfix — wrap-to-first behavior on repeat-letter press. When
    // the user types the same letter twice and only ONE item matches
    // (e.g. menu = {Apple, Apricot, Banana, ...}, type 'B' twice, only
    // "Banana" matches), the second 'B' must wrap back to "Banana"
    // (Windows convention) — NOT clear the buffer and fall through.
    // Without this, single-match letters feel broken: 'A' switches
    // between Apple/Apricot on repeat, 'B' sits at Banana with no
    // feedback for the second keystroke.
    if (keyCode >= UIKey_A && keyCode <= UIKey_Z) {
        const wchar_t ch = static_cast<wchar_t>(
            L'a' + (keyCode - UIKey_A));
        _typeaheadBuffer.append(ch);
        // PR-S3 fix: with _hoveredIndex now defaulting to -1 (see AYMenu.h),
        // the standard (_hoveredIndex + 1) % n formula correctly resolves
        // startFrom to 0 on a fresh menu (first-letter typeahead includes
        // idx 0) and to _hoveredIndex + 1 on subsequent presses (skip self,
        // which is the Windows native cycle behavior). The setHoveredIndex
        // call in this branch goes through setHoveredIndexFromTypeahead so
        // a 'R' that lands on the already-hovered idx 0 still fires
        // _onHoverChanged and the host's status label gets feedback.
        const int startFrom = (_hoveredIndex < 0) ? 0 : (_hoveredIndex + 1) % n;
        // Lambda getter lets us skip separator/nullptr items: an empty
        // wstring fails the size check inside findMatch, so those
        // entries are treated as no-match naturally.
        auto getter = [this](int i) -> const std::wstring& {
            const MenuItem* item = _items[static_cast<size_t>(i)];
            if (item == nullptr) {
                static const std::wstring empty;
                return empty;
            }
            return item->getText();
        };
        int match = _typeaheadBuffer.findMatch(startFrom, n, getter);
        if (match < 0) {
            // Letter-switch recovery: "ab" miss → restart with 'b'.
            if (_typeaheadBuffer.size() > 1) {
                match = _typeaheadBuffer.recoverFromLetterSwitch(startFrom, n, getter);
                if (match >= 0) {
                    setHoveredIndexFromTypeahead(match);
                    return true;
                }
            } else if (_typeaheadBuffer.size() == 1) {
                // Menu-only single-letter wrap (Windows listbox
                // convention): user types the same letter twice and
                // only ONE item matches; second press must wrap back to
                // that match. ComboBox does NOT do this.
                const int wrapMatch = _typeaheadBuffer.findMatch(0, n, getter);
                if (wrapMatch >= 0) {
                    setHoveredIndexFromTypeahead(wrapMatch);
                    return true;
                }
            }
            _typeaheadBuffer.clear();
            return false;
        }
        setHoveredIndexFromTypeahead(match);
        return true;
    }

    switch (keyCode) {
    case UIKey_Down:
        // PR-C3 — arrow key invalidates the typeahead buffer.
        _typeaheadBuffer.clear();
        setHoveredIndex((_hoveredIndex + 1) % n);
        return true;
    case UIKey_Up:
        // PR-C3 — arrow key invalidates the typeahead buffer.
        _typeaheadBuffer.clear();
        setHoveredIndex((_hoveredIndex <= 0) ? n - 1 : _hoveredIndex - 1);
        return true;
    case UIKey_Enter:
        // PR-C3 — Enter invalidates the typeahead buffer.
        _typeaheadBuffer.clear();
        if (_hoveredIndex >= 0 && _hoveredIndex < n) {
            activateItem(_hoveredIndex);
            return true;
        }
        return false;
    case UIKey_Escape:
        // PR-C3 — Escape invalidates the typeahead buffer.
        _typeaheadBuffer.clear();
        close();
        return true;
    default:
        return false;
    }
}

void Menu::tick(float dt) {
    // PR-anim: chain the base FIRST — opacity tweens live in
    // Widget::tick. compoundDescendTick forces the base for nodes it
    // reaches as `self`, but the overlay cascade reaches the Menu as a
    // CHILD (virtual dispatch → this override), so without the chain
    // the pop-in fade would never advance.
    Widget::tick(dt);
    // PR-TypeaheadBuffer: timer + auto-clear live on the struct.
    // Driven by UIManager::update → _root->tick cascade when the Menu
    // is in the _root subtree (the common case after open() reparents
    // onto the overlay — the overlay is mounted in _root, so the
    // cascade still reaches the Menu through the overlay's children).
    _typeaheadBuffer.tick(dt);
}

void Menu::activateItem(int index) {
    // Mirror the same side effects MenuItem's onMouseButtonUp callback
    // triggers (see the lambda installed in addItem): record index,
    // fire _onItemActivated, close. Centralized here so the keyboard
    // path and the mouse path stay byte-identical without exposing
    // MenuItem's protected handleClick().
    if (index < 0 || index >= static_cast<int>(_items.size())) return;
    _lastActivatedIndex = index;
    if (_onItemActivated) {
        _onItemActivated(index);
    }
    close();
}

void Menu::dismissFromManager() {
    // UIManager click-outside / swap-dropdown path. Same soft contract
    // as close(): keep Menu* alive for MenuBar, clear _open, and let
    // closePopup(false) clear UIManager::_activeDropdown.
    if (!_open && getParent() == nullptr) {
        return;
    }
    _open = false;
    setVisible(false);
    if (UIManager* ui = UIManager::tryGet()) {
        if (!ui->isShuttingDown()) {
            if (_focusedWidgetBefore != nullptr) {
                ui->setFocus(_focusedWidgetBefore);
                _focusedWidgetBefore = nullptr;
            }
            if (_onClose) _onClose();
            if (ui->getFocusedWidget() == this) {
                ui->setFocus(nullptr);
            }
        } else {
            _focusedWidgetBefore = nullptr;
        }
        ui->closePopup(this, /*destroy=*/false);
    }
    if (_ownerHost != nullptr && getParent() == nullptr) {
        _ownerHost->addChildExternal(this);
    }
}

} // namespace ayt::ui
