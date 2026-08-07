#pragma once

// =============================================================================
// PR-TypeaheadBuffer: shared prefix-match accumulator for ComboBox + Menu.
//
// No Widget base — owned by value. Both callers embed one `TypeaheadBuffer`
// member and drive `tick(dt)` from their existing `tick()` override.
//
// Owns:
//   - buffer (lowercase-ASCII wchar string)
//   - timer accumulator (seconds since last append)
//   - timeout constant (shared 0.5f, matches Windows native dropdown/menu)
//
// Does NOT own (caller responsibility):
//   - currentIndex / selection side effects (caller calls setSelectedIndex
//     or setHoveredIndex after findMatch returns)
//   - auto-open behavior on match (ComboBox-only)
//   - single-letter wrap (Menu-only; caller branches in onKeyDown)
//   - separator / nullptr-skip (caller's lambda getter returns empty wstring)
//   - invalidation keys (Down/Up/Enter/Escape call clear())
//   - non-ASCII case folding (kept at "ASCII fold only" — matches the
//     pre-PR behavior of both callers)
// =============================================================================

#include <functional>
#include <string>

namespace ayt::ui {

struct TypeaheadBuffer {
    static constexpr float kTimeout = 0.5f;

    // Append a lowercase ASCII letter; resets the timer. Caller is
    // responsible for folding A-Z to a-z before calling.
    void append(wchar_t lowerAsciiLetter);

    // Drive the timer forward. When the timer crosses `kTimeout` with
    // a non-empty buffer, the buffer is cleared so the next letter
    // starts a fresh prefix. Idempotent on an empty buffer.
    void tick(float dt);

    // Manual reset (called by invalidation keys: arrow / Enter / Escape,
    // and by programmatic selection).
    void clear();

    // Linear wrap-around match starting at `startFrom`. `count` is the
    // total item count. `getItemText(i)` returns the text of the i-th
    // item (or empty wstring for skip-this entries — the empty string
    // fails the size check naturally and is treated as "no match").
    // Returns -1 if no item's first N chars (case-insensitive ASCII
    // fold) match the buffer.
    int findMatch(int startFrom, int count,
                  const std::function<const std::wstring&(int)>& getItemText) const;

    // Letter-switch recovery: build a temporary 1-letter buffer from
    // the buffer's last character and retry from `startFrom`, then
    // from 0. Returns the recovered match index, or -1.
    int recoverFromLetterSwitch(int startFrom, int count,
                  const std::function<const std::wstring&(int)>& getItemText) const;

    bool        empty()     const { return _buffer.empty(); }
    size_t      size()      const { return _buffer.size(); }
    const std::wstring& peek() const { return _buffer; }
    bool        isExpired() const { return _timer > kTimeout; }

private:
    std::wstring _buffer;
    float        _timer = 0.0f;
};

} // namespace ayt::ui