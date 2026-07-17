#include "AYTest.h"
#include "AYTextLabel.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_TextLabel)

TEST_CASE(textlabel_initial_state) {
    TextLabel label;
    CHECK(label.getText() == L"");
    CHECK(label.getFontSize() == 14);  // default font size
    CHECK(label.getWordWrap() == false);
}

TEST_CASE(textlabel_set_text) {
    TextLabel label;
    label.setText(L"Hello World");
    CHECK(label.getText() == L"Hello World");
}

TEST_CASE(textlabel_font_size) {
    TextLabel label;
    label.setFontSize(24);
    CHECK(label.getFontSize() == 24);
}

TEST_CASE(textlabel_text_color) {
    TextLabel label;
    FVector4 color(1.0f, 0.0f, 0.0f, 1.0f);
    label.setTextColor(color);
    CHECK(label.getTextColor().x == 1.0f);
    CHECK(label.getTextColor().y == 0.0f);
}

TEST_CASE(textlabel_alignment) {
    TextLabel label;
    label.setHorizontalAlignment(TextLabel::HAlignment::Center);
    label.setVerticalAlignment(TextLabel::VAlignment::Center);
    CHECK(label.getWordWrap() == false);

    label.setWordWrap(true);
    CHECK(label.getWordWrap() == true);

    label.setWrapWidth(200.0f);
}

TEST_CASE(textlabel_content_change) {
    TextLabel label;
    label.setText(L"Initial");
    CHECK(label.getText() == L"Initial");

    label.setText(L"Updated");
    CHECK(label.getText() == L"Updated");
}

TEST_CASE(textlabel_no_button_inheritance) {
    // R-3 (2026-07-17): proves B1 is fixed at runtime. Pre-R-3 a TextLabel
    // would dynamic_cast<Button*> to non-null because it extended Button.
    TextLabel label;
    CHECK(dynamic_cast<Button*>(&label) == nullptr);
    CHECK(dynamic_cast<InteractiveWidget*>(&label) == nullptr);
    CHECK(dynamic_cast<LeafWidget*>(&label) != nullptr);
}

TEST_SUITE_END