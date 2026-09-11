#include "AYUI/MenuItem.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Menu.h"
#include "AYUI/Style.h"
#include "AYUI/SvgIcon.h"
#include "AYUI/TextMeasure.h"
#include "AYUI/UIKeyCode.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>
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
//   - named keys:
//       "Enter"   → UIKey_Enter
//       "Tab"     → UIKey_Tab
//       "Esc"     → UIKey_Escape
//       "Delete", "Space", arrows, F1..F12
// ============================================================================

namespace {

struct MenuItemLeadingIconState {
    std::shared_ptr<const SvgDocument> document;
    math::FVector4 color{0.92f, 0.92f, 0.94f, 1.0f};
    float size = 14.0f;
};

std::unordered_map<const MenuItem*, MenuItemLeadingIconState>&
menuItemLeadingIcons()
{
    // Leading indicators are optional presentation state. Store them out of
    // line so adding the feature does not alter MenuItem's public layout.
    static auto* states = new std::unordered_map<
        const MenuItem*, MenuItemLeadingIconState>();
    return *states;
}

const MenuItemLeadingIconState* findLeadingIcon(const MenuItem* item)
{
    const auto& states = menuItemLeadingIcons();
    const auto found = states.find(item);
    return found != states.end() ? &found->second : nullptr;
}

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
        if (c >= L'0' && c <= L'9') return static_cast<int>(UIKey_Num0 + (c - L'0'));
        return 0;
    }
    if (tok.size() >= 2 && (tok[0] == L'F' || tok[0] == L'f')) {
        int number = 0;
        for (size_t i = 1; i < tok.size(); ++i) {
            if (tok[i] < L'0' || tok[i] > L'9') {
                number = 0;
                break;
            }
            number = number * 10 + static_cast<int>(tok[i] - L'0');
        }
        if (number >= 1 && number <= 12) return UIKey_F1 + number - 1;
    }
    if (iequals(tok, "enter") || iequals(tok, "return")) return UIKey_Enter;
    if (iequals(tok, "tab"))                            return UIKey_Tab;
    if (iequals(tok, "esc") || iequals(tok, "escape"))   return UIKey_Escape;
    if (iequals(tok, "delete") || iequals(tok, "del"))   return UIKey_Delete;
    if (iequals(tok, "space"))                           return UIKey_Space;
    if (iequals(tok, "left"))                            return UIKey_Left;
    if (iequals(tok, "right"))                           return UIKey_Right;
    if (iequals(tok, "up"))                              return UIKey_Up;
    if (iequals(tok, "down"))                            return UIKey_Down;
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
    menuItemLeadingIcons().erase(this);
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

void MenuItem::setLeadingIconDocument(
    std::shared_ptr<const SvgDocument> document) {
    auto& state = menuItemLeadingIcons()[this];
    if (state.document == document) return;
    state.document = std::move(document);
    markDirty();
}

std::shared_ptr<const SvgDocument> MenuItem::getLeadingIconDocument() const {
    const MenuItemLeadingIconState* state = findLeadingIcon(this);
    return state != nullptr ? state->document : nullptr;
}

void MenuItem::setLeadingIconColor(const math::FVector4& color) {
    auto& state = menuItemLeadingIcons()[this];
    if (state.color == color) return;
    state.color = color;
    markDirty();
}

math::FVector4 MenuItem::getLeadingIconColor() const {
    const MenuItemLeadingIconState* state = findLeadingIcon(this);
    return state != nullptr
        ? state->color : math::FVector4(0.92f, 0.92f, 0.94f, 1.0f);
}

void MenuItem::setLeadingIconSize(float size) {
    const float clamped = std::max(0.0f, size);
    auto& state = menuItemLeadingIcons()[this];
    if (state.size == clamped) return;
    state.size = clamped;
    markDirty();
}

float MenuItem::getLeadingIconSize() const {
    const MenuItemLeadingIconState* state = findLeadingIcon(this);
    return state != nullptr ? state->size : 14.0f;
}

bool MenuItem::handleClick() {
    // Settle popup/focus bookkeeping before invoking host code. Editor menu
    // actions can mutate scene and inspector state; doing that while the
    // popup is still active leaves UIManager routing input to a stale modal
    // layer. Copy the host callback first because closing may eventually
    // unmount (or, for a custom owner, destroy) this item.
    const std::function<void()> hostActivate = _onActivate;
    if (_onMenuActivate) _onMenuActivate();
    if (hostActivate) hostActivate();
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
            ? resolveAccentColor(0.55f)
            : resolveAccentColor(0.0f);
        renderer.drawRect(b, resolveTransitionColor(target));
    } else if (!_colorInitialized) {
        // Prime the transition state without painting (alpha-0 target
        // snaps in) so the FIRST hover fades in from transparent instead
        // of jumping to full highlight.
        resolveTransitionColor(resolveAccentColor(0.0f));
    }

    const MenuItemLeadingIconState* leading = findLeadingIcon(this);
    const bool hasLeadingIcon = leading != nullptr
        && leading->document != nullptr;
    const float padL = hasLeadingIcon ? 36.0f : 16.0f;
    const float padR = 12.0f;
    const math::FVector4 textColor = isEnabled()
        ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
        : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);

    if (hasLeadingIcon && leading->size > 0.0f) {
        const float size = std::min(leading->size,
            std::max(0.0f, b.maxY - b.minY - 8.0f));
        if (size > 0.0f) {
            const float x = b.minX + 14.0f;
            const float y = b.minY + (b.maxY - b.minY - size) * 0.5f;
            math::FVector4 color = leading->color;
            if (!isEnabled()) color.w *= 0.55f;
            leading->document->draw(renderer,
                math::FRectangle(x, y, x + size, y + size), color);
        }
    }

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
