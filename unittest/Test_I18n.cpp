#include "AYTest.h"
#include "AYMath/MathUtils.h"
#include "AYUI/I18n.h"
#include <iostream>

using namespace ayt::ui;

TEST_SUITE(AYUI_I18n)

TEST_CASE(test_i18n_key_detection) {
    CHECK(isI18nKey("ui.menu.resume"));
    CHECK(isI18nKey("ui.btn.confirm"));
    CHECK(!isI18nKey("直接文本"));
    CHECK(!isI18nKey("ab"));
    CHECK(!isI18nKey(""));
}

TEST_CASE(test_i18n_singleton) {
    I18n& i18n = I18n::get();
    I18n& i18n2 = I18n::get();

    CHECK(&i18n == &i18n2);
}

TEST_CASE(test_i18n_language_switch) {
    I18n& i18n = I18n::get();

    i18n.setCurrentLanguage("en");
    CHECK(i18n.getCurrentLanguage() == "en");

    i18n.setCurrentLanguage("zh");
    CHECK(i18n.getCurrentLanguage() == "zh");
}

TEST_CASE(test_i18n_resolve_fallback) {
    I18n& i18n = I18n::get();
    i18n.setCurrentLanguage("zh");

    std::wstring result = i18n.resolve("ui.menu.resume");
    CHECK(result == L"ui.menu.resume");
}

// AYUI-Audit-2026-08-26 DoS: 64 MiB JSON cap on I18n::loadFromString.
// Pre-fix a hostile language table could grow nlohmann's parse to
// multi-GiB. The fix in i18n/AYI18n.cpp returns false + logs when
// the payload exceeds 64 MiB.
TEST_CASE(i18n_rejects_huge_json) {
    I18n& i18n = I18n::get();

    // Snapshot the previous table so we can restore it after the
    // rejection — I18n is a process-wide singleton and we don't want
    // to pollute later tests' state.
    const std::string prevLang = i18n.getCurrentLanguage();

    // 65 MiB of well-formed but oversized JSON: 32-byte key/value
    // pairs wrapped in a top-level object. The cap fires BEFORE the
    // parse attempt.
    constexpr size_t kPayloadBytes = 65ULL * 1024 * 1024;
    std::string huge(kPayloadBytes, 'a');

    const bool ok = i18n.loadFromString(huge.data(), huge.size());
    CHECK_FALSE(ok);

    // Singleton state must be untouched by the failed load.
    CHECK(i18n.getCurrentLanguage() == prevLang);

    // Resolve must still hand back the fallback for an unknown key
    // (the rejection did not clear() the singleton).
    std::wstring fallback = i18n.resolve("ui.menu.resume");
    CHECK(fallback == L"ui.menu.resume");
}

TEST_SUITE_END