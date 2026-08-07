#include "AYTest.h"
#include "AYTypeaheadBuffer.h"

#include <string>
#include <vector>

using namespace ayt::ui;

// ============================================================================
// PR-TypeaheadBuffer: pure-struct tests for the shared prefix-match
// accumulator. Exercises the API without a UIManager or any widget, so
// regressions here are isolated to the helper itself.
// ============================================================================

TEST_SUITE(AYUI_TypeaheadBuffer)

// ---- buffer / timer basics ----

TEST_CASE(typeahead_buffer_append_accumulates_lowercase_letters) {
    TypeaheadBuffer b;
    b.append(L'a');
    b.append(L'p');
    b.append(L'p');
    CHECK(b.size() == 3);
    CHECK(b.peek() == L"app");
    CHECK_FALSE(b.empty());
}

TEST_CASE(typeahead_buffer_tick_clears_after_timeout) {
    TypeaheadBuffer b;
    b.append(L'a');
    b.tick(TypeaheadBuffer::kTimeout * 2.0f);
    CHECK(b.empty());
    CHECK(b.peek() == L"");
}

TEST_CASE(typeahead_buffer_tick_keeps_buffer_under_timeout) {
    TypeaheadBuffer b;
    b.append(L'a');
    b.tick(TypeaheadBuffer::kTimeout * 0.5f);
    CHECK(b.size() == 1);
    CHECK(b.peek() == L"a");
}

TEST_CASE(typeahead_buffer_clear_resets_buffer_and_timer) {
    TypeaheadBuffer b;
    b.append(L'a');
    b.tick(0.1f);
    CHECK_FALSE(b.empty());
    b.clear();
    CHECK(b.empty());
    // After clear(), isExpired must read false because the buffer is
    // empty (no in-flight timer).
    CHECK_FALSE(b.isExpired());
}

TEST_CASE(typeahead_buffer_empty_after_clear) {
    TypeaheadBuffer b;
    b.append(L'a');
    b.clear();
    CHECK(b.empty());
    CHECK(b.size() == 0);
}

TEST_CASE(typeahead_buffer_is_expired_reflects_timer) {
    TypeaheadBuffer b;
    CHECK_FALSE(b.isExpired());
    b.append(L'a');
    CHECK_FALSE(b.isExpired());
    b.tick(TypeaheadBuffer::kTimeout + 1e-4f);
    // After clear, isExpired returns false because empty() short-circuits.
    CHECK(b.empty());
    CHECK_FALSE(b.isExpired());
}

// ---- findMatch ----

static const std::vector<std::wstring> kFruits = {
    L"Apple", L"Apricot", L"Banana", L"Blueberry", L"Cherry", L"Coconut"
};

static const std::vector<std::wstring> kEmpty = {};

TEST_CASE(typeahead_buffer_find_match_empty_buffer_returns_minus_one) {
    TypeaheadBuffer b;
    auto getter = [](int i) -> const std::wstring& {
        return kFruits[static_cast<size_t>(i)];
    };
    CHECK(b.findMatch(0, static_cast<int>(kFruits.size()), getter) == -1);
}

TEST_CASE(typeahead_buffer_find_match_single_letter_case_insensitive) {
    TypeaheadBuffer b;
    b.append(L'b');
    auto getter = [](int i) -> const std::wstring& {
        return kFruits[static_cast<size_t>(i)];
    };
    // 'b' → first match starting at 0 is Banana (index 2).
    CHECK(b.findMatch(0, static_cast<int>(kFruits.size()), getter) == 2);
}

TEST_CASE(typeahead_buffer_find_match_multi_letter_prefix) {
    TypeaheadBuffer b;
    b.append(L'b');
    b.append(L'l');
    auto getter = [](int i) -> const std::wstring& {
        return kFruits[static_cast<size_t>(i)];
    };
    // "bl" → first match is Blueberry (index 3).
    CHECK(b.findMatch(0, static_cast<int>(kFruits.size()), getter) == 3);
}

TEST_CASE(typeahead_buffer_find_match_wraps_around) {
    TypeaheadBuffer b;
    b.append(L'a');
    auto getter = [](int i) -> const std::wstring& {
        return kFruits[static_cast<size_t>(i)];
    };
    // Starting at 1, should find Apricot (index 1) — not Apple (index 0).
    CHECK(b.findMatch(1, static_cast<int>(kFruits.size()), getter) == 1);
    // Starting at 2, wraps to Apple (index 0).
    CHECK(b.findMatch(2, static_cast<int>(kFruits.size()), getter) == 0);
}

TEST_CASE(typeahead_buffer_find_match_separator_skip) {
    // Items with a separator (empty string) interspersed. The lambda
    // returns an empty wstring for the separator index, which fails the
    // size check inside findMatch and is treated as no-match.
    const std::vector<std::wstring> items = { L"Apple", L"", L"Apricot" };
    TypeaheadBuffer b;
    b.append(L'a');
    auto getter = [&items](int i) -> const std::wstring& {
        return items[static_cast<size_t>(i)];
    };
    CHECK(b.findMatch(0, static_cast<int>(items.size()), getter) == 0);
}

TEST_CASE(typeahead_buffer_find_match_start_from_minus_one_treats_as_zero) {
    TypeaheadBuffer b;
    b.append(L'a');
    auto getter = [](int i) -> const std::wstring& {
        return kFruits[static_cast<size_t>(i)];
    };
    // Caller passes -1 to mean "nothing selected yet" — findMatch
    // normalizes any startFrom into [0, count) so the lambda lookup
    // is safe regardless of caller arithmetic. Real Menu/ComboBox
    // callers already do the `(_hoveredIndex < 0) ? 0 : (...) % n`
    // guard themselves, but the helper stays robust.
    CHECK(b.findMatch(-1, static_cast<int>(kFruits.size()), getter) == 0);
}

TEST_CASE(typeahead_buffer_find_match_start_from_negative_does_not_underflow) {
    // Regression test: previously passing a negative startFrom caused
    // vector::operator[] underflow inside the caller's getter. The
    // helper now normalizes startFrom into [0, count) so any caller
    // arithmetic is safe.
    TypeaheadBuffer b;
    b.append(L'b');
    auto getter = [](int i) -> const std::wstring& {
        return kFruits[static_cast<size_t>(i)];
    };
    // -5 normalizes to 1 (mod 6) → first 'b' match from index 1 is
    // Banana at index 2.
    CHECK(b.findMatch(-5, static_cast<int>(kFruits.size()), getter) == 2);
}

// ---- recoverFromLetterSwitch ----

TEST_CASE(typeahead_buffer_recover_from_letter_switch_returns_match_with_last_letter_only) {
    TypeaheadBuffer b;
    b.append(L'a');
    b.append(L'b');   // "ab" — no match
    auto getter = [](int i) -> const std::wstring& {
        return kFruits[static_cast<size_t>(i)];
    };
    // recover: temp "b" → Banana (index 2).
    const int r = b.recoverFromLetterSwitch(0,
        static_cast<int>(kFruits.size()), getter);
    CHECK(r == 2);
}

TEST_CASE(typeahead_buffer_recover_from_letter_switch_returns_minus_one_when_no_match) {
    TypeaheadBuffer b;
    b.append(L'z');
    b.append(L'q');
    auto getter = [](int i) -> const std::wstring& {
        return kFruits[static_cast<size_t>(i)];
    };
    // recover: temp "q" — no item starts with 'q'.
    const int r = b.recoverFromLetterSwitch(0,
        static_cast<int>(kFruits.size()), getter);
    CHECK(r == -1);
}

TEST_CASE(typeahead_buffer_letter_case_invariant_lowercase_only_appended) {
    // The struct does NOT fold case on append — caller is responsible
    // for lowercasing (matches the pre-PR ComboBox/Menu behavior of
    // `L'a' + (keyCode - UIKey_A)` and the original findTypeaheadMatch
    // which only folded the item-side character, not the buffer side).
    //
    // The case-insensitive fold happens on the ITEM side during
    // findMatch; the buffer stores its letters verbatim. So a caller
    // that forgets to lowercase will NOT match — the buffer 'A' never
    // equals the item 'A' folded to 'a'. ComboBox/Menu both use
    // lowercase on append, so this never mattered in production, but
    // the contract is pinned here for future callers.
    TypeaheadBuffer b;
    b.append(L'A');   // uppercase, no fold inside append
    auto getter = [](int i) -> const std::wstring& {
        return kFruits[static_cast<size_t>(i)];
    };
    // Buffer letter 'A' does NOT match item 'A'pple folded to 'a'.
    // This is identical to pre-PR ComboBox::findTypeaheadMatch.
    CHECK(b.findMatch(0, static_cast<int>(kFruits.size()), getter) == -1);

    // Sanity: a lowercase buffer letter folds correctly via item side.
    TypeaheadBuffer b2;
    b2.append(L'a');
    CHECK(b2.findMatch(0, static_cast<int>(kFruits.size()), getter) == 0);
}

TEST_SUITE_END