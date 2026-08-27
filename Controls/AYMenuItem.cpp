#include "AYUI/MenuItem.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Menu.h"
#include "AYUI/TextMeasure.h"
#include "AYUI/UIKeyCode.h"
#include <algorithm>
#include <cctype>
#include <vector>

namespace ayt::ui {

// ============================================================================
// Polish (P3) — accelerator parser.
// ============================================================================
// Splits "Ctrl+Shift+Z" into (kAccelControl|kAccelShift, UIKey_Z).
// Modifiers are recognized case-insensitively as full names ("Ctrl",
// "Shift", "Alt") or aliases ("Control"="Ctrl", "Option"="Alt"). Token
// separator is '+'; leading/trailing whitespace tolerated. Unknown
// modifier names are silently dropped (no error return — we want
// setShortcut to never throw, matching the existing display-only API).
//
// Key token: a single char mapped to UIKeyCode. Recognized:
//   - 'a'..'z' (case-insensitive) → UIKey_A..UIKey_Z
//   - 0 / 1 special tokens today:
//       "Enter"   → UIKey_Enter
//       "Tab"     → UIKey_Tab
//       "Esc"     → UIKey_Escape
//   F1..F12 / arrow keys not parsed in P3 — accelerators for those are
//   rare and risk clashing with existing B-phase keyboard nav
//   (Up/Down/Left/Right inside ListView/Menu/TabControl). Out of scope.
// ============================================================================

namespace {

// Case-insensitive equality on std::wstring. Returns true iff `s` equals
// `prefix` ignoring ASCII case. Used by the parser to recognize modifier
// names + key aliases without pulling in <cwctype> or locale tricks.
bool iequals(const std::wstring& s, const char* ascii) {
    size_t i = 0;
    for (; ascii[i] != '\0'; ++i) {
        if (i >= s.size()) return false;
        const wchar_t a = s[i];
        const wchar_t b = static_cast<wchar_t>(ascii[i]);
        if (std::tolower(static_cast<unsigned char>(a)) !=
            std::tolower(static_cast<unsigned char>(b))) {
            return false;
        }
    }
    return i == s.size();
}

uint8_t modifierBit(const std::wstring& tok) {
    if (iequals(tok, "ctrl") || iequals(tok, "control")) return MenuItem::kAccelControl;
    if (iequals(tok, "shift"))                            return MenuItem::kAccelShift;
    if (iequals(tok, "alt") || iequals(tok, "option"))    return MenuItem::kAccelAlt;
    return 0;
}

int keyTokenToCode(const std::wstring& tok) {
    // Single-char keys: A..Z (case-insensitive). Maps to UIKey_A..Z.
    if (tok.size() == 1) {
        const wchar_t c = tok[0];
        if (c >= L'A' && c <= L'Z') return static_cast<int>(UIKey_A + (c - L'A'));
        if (c >= L'a' && c <= L'z') return static_cast<int>(UIKey_A + (c - L'a'));
        return 0;
    }
    if (iequals(tok, "enter") || iequals(tok, "return")) return UIKey_Enter;
    if (iequals(tok, "tab"))                            return UIKey_Tab;
    if (iequals(tok, "esc") || iequals(tok, "escape"))   return UIKey_Escape;
    return 0;   // Unrecognized — P3 out-of-scope keys fail closed.
}

} // namespace

bool MenuItem::parseShortcut(const std::wstring& s,
                             uint8_t& outMods,
                             int& outKey) {
    outMods = 0;
    outKey  = 0;
    if (s.empty()) return false;

    // Split on '+'. Build a small vector of trimmed tokens. Cap at 4
    // tokens (3 modifiers + 1 key) — anything longer is malformed.
    std::vector<std::wstring> toks;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L'+') {
            if (!cur.empty()) toks.push_back(cur);
            cur.clear();
        } else if (c == L' ' || c == L'\t') {
            // Drop whitespace; tokens are tight.
            continue;
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) toks.push_back(cur);
    if (toks.empty()) return false;

    // All but the last token must be modifiers; last is the key.
    for (size_t i = 0; i + 1 < toks.size(); ++i) {
        const uint8_t bit = modifierBit(toks[i]);
        if (bit == 0) {
            // Unknown modifier — discard the whole binding. Hosts that
            // type "Cmd+S" will get display-only, no dispatch, rather
            // than binding a confused "Cmd" prefix to Ctrl-equivalent.
            return false;
        }
        outMods |= bit;
    }
    outKey = keyTokenToCode(toks.back());
    if (outKey == 0) {
        // Last token wasn't a recognized key. We still might have
        // collected modifiers; that's fine for display but means no
        // dispatch. Return false so the caller knows NOT to register
        // it; the field _accelKey stays at its 0 default so future
        // getAccelKey()==0 reads as "unparseable".
        return false;
    }
    return true;
}

MenuItem::MenuItem() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    setLayoutPositionManaged(false);   // Menu positions items itself
    setLayoutSizeManaged(false);
}

MenuItem::~MenuItem() {
    // Polish (P3): when an item is destroyed, the MenuBar's accelerator
    // registry would otherwise have a dangling pointer to this freed
    // object. We could either hold a back-pointer to MenuBar and
    // self-unregister, OR let UIManager's dispatch path null-check.
    // Phase A precedent (clearDragStateNoDispatch pattern) argues for
    // the latter — single ownership of mutable state stays in UIManager
    // + MenuBar; widgets just announce their kind. UIManager's onKeyDown
    // already has tryGet() guards (Phase A + Drag-Drop R3). We therefore
    // leave this dtor empty: MenuBar::clearItems / owner destruction
    // walks the registry and purges entries that match this item.
    //
    // Implementation: MenuBar's destructor (and clearItems) iterates
    // _accelRegistry and removes entries pointing to a known set of
    // items. For an item destroyed through CompoundWidget::dtor
    // (unusual — items live inside Menu which MenuBar owns), MenuBar
    // sees the deletion first. Items not behind a MenuBar never end up
    // in the registry, so no leak.
}

void MenuItem::setShortcut(const std::wstring& s) {
    // Polish (P3): this is the single point that owns (mods, key) for
    // a MenuItem. Re-callable — the next parse overrides any prior
    // binding. The display string is always updated even when parsing
    // fails (so hosts see "⌘S" or "F1" even when those don't dispatch).
    _shortcut = s;
    uint8_t mods = 0;
    int key = 0;
    parseShortcut(s, mods, key);
    _accelMods = mods;
    _accelKey  = key;
    markBoundsDirty();
    // AYUI-DirtyRect-2026-08-26: shortcut text changes the rendered row
    // (right-aligned hint).
    markDirty();
}

bool MenuItem::handleClick() {
    // Fire the activate callback. We do NOT toggle selection — menus
    // are not persistent single-select; the row visually highlights while
    // hovered and disappears when the menu closes.
    if (_onActivate) _onActivate();
    return true;
}

bool MenuItem::onMouseButtonUp(const UIMouseEvent& e) {
    if (!isEnabled() || e.mouseButton != 0) return false;
    if (!getWorldBounds().contains(e.mousePos)) return false;
    return handleClick();
}

void MenuItem::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;

    // Hover / press highlight. UI animation lane: the bar target fades in
    // and out through resolveTransitionColor (90ms default); the gate
    // keeps painting while a fade-out is still running, so the bar
    // disappears smoothly instead of vanishing on the leave frame.
    //
    // `_color.w > 0.001f` covers the gap between "fade-out finished" and
    // "leave frame": right after onMouseLeave the tween has NOT restarted
    // yet (it is render-driven), so _colorAnim.active alone would skip the
    // bar for one frame and the leave would look like an instant vanish.
    // The flag is read BEFORE resolveTransitionColor below starts the
    // retarget tween, so a completed hover fade (active=false, color still
    // bright) still enters the paint branch on the leave frame.
    const bool highlighted = (isMouseOver() || _isKeyboardHovered) && isEnabled();
    const bool barFadingOut = _colorInitialized && _color.w > 0.001f;
    if (highlighted || _colorAnim.active || barFadingOut) {
        const math::FVector4 target = highlighted
            ? math::FVector4(0.18f, 0.45f, 0.78f, 0.55f)
            : math::FVector4(0.18f, 0.45f, 0.78f, 0.0f);
        renderer.drawRect(b, resolveTransitionColor(target));
    } else if (!_colorInitialized) {
        // Prime the transition state without painting (alpha-0 target
        // snaps in) so the FIRST hover fades in from transparent instead
        // of jumping to full highlight.
        resolveTransitionColor(math::FVector4(0.18f, 0.45f, 0.78f, 0.0f));
    }

    const float padL = 16.0f;
    const float padR = 12.0f;
    const math::FVector4 textColor = isEnabled()
        ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
        : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);

    if (!_text.empty()) {
        math::FRectangle textBounds(
            b.minX + padL, b.minY + 4.0f,
            b.maxX - padR, b.maxY - 4.0f);
        renderer.drawText(textBounds, _text, 14, textColor);
    }
    if (!_shortcut.empty()) {
        math::FRectangle scBounds(
            b.minX, b.minY + 4.0f,
            b.maxX - padR, b.maxY - 4.0f);
        const math::FVector4 scColor = isEnabled()
            ? math::FVector4(0.70f, 0.70f, 0.74f, 1.0f)
            : math::FVector4(0.40f, 0.40f, 0.42f, 1.0f);
        // Right-align by drawing the text near the right edge.
        // PR-B2: backend-aware measurePrefixWidth — produces true glyph
        // width when a real font backend is wired (PR-A1 header). Falls
        // back to 7px/char when no backend (tests / MockRenderer).
        const float approxW = measurePrefixWidth(_shortcut, _shortcut.size(), nullptr, 13);
        scBounds.minX = b.maxX - approxW - padR;
        scBounds.maxX = b.maxX - padR;
        renderer.drawText(scBounds, _shortcut, 13, scColor);
    }
    if (_submenu != nullptr) {
        // Submenu chevron.
        renderer.drawText(
            math::FRectangle(b.maxX - 14.0f, b.minY + 4.0f,
                              b.maxX - 4.0f,  b.maxY - 4.0f),
            L"›",     // ›
            14, textColor);
    }
}

Widget* createMenuItemWidget() { return new MenuItem(); }

} // namespace ayt::ui
