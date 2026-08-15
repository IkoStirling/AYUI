#include "AYTest.h"
#include "AYMath/MathUtils.h"
#include "AYI18n.h"
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

TEST_SUITE_END