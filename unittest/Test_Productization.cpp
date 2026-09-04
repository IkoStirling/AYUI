#include "AYTest.h"
#include "AYUI/Accessibility.h"
#include "AYUI/AccessibilityAdapter.h"
#include "AYUI/Button.h"
#include "AYUI/CheckBox.h"
#include "AYUI/ComboBox.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/RadioButton.h"
#include "AYUI/RichText.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/Style.h"
#include "AYUI/TabStrip.h"
#include "AYUI/Theme.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetSerializer.h"

#include <cmath>
#include <cstring>
#include <limits>

using namespace ayt::ui;
using namespace ayt::math;

namespace {
const AccessibilityNode* findSemanticNode(const AccessibilityNode& node,
                                          uint64_t id) {
    if (node.id == id) return &node;
    for (const AccessibilityNode& child : node.children) {
        if (const AccessibilityNode* found = findSemanticNode(child, id)) return found;
    }
    return nullptr;
}
}

TEST_SUITE(AYUI_Productization)

TEST_CASE(dpi_and_ui_scale_keep_layout_in_logical_dip) {
    MockRenderer renderer;
    UIManager ui;
    ui.initialize(&renderer);
    ui.setClientSize(1920.0f, 1080.0f);
    ui.setDpiScale(1.5f);
    ui.setUiScale(1.25f);

    CHECK_FLOAT_EQ(ui.getEffectiveScale(), 1.875f, 1e-5f);
    CHECK_FLOAT_EQ(ui.getClientSize().x, 1024.0f, 1e-4f);
    CHECK_FLOAT_EQ(ui.getClientSize().y, 576.0f, 1e-4f);
    CHECK_FLOAT_EQ(ui.root()->getWidth(), 1024.0f, 1e-4f);
    CHECK_FLOAT_EQ(ui.logicalToPhysical(FVector2(10, 20)).x, 18.75f, 1e-4f);

    ui.render();
    CHECK_FLOAT_EQ(renderer.getUiScale(), 1.875f, 1e-5f);
    ui.shutdown();
}

TEST_CASE(dpi_scale_converts_physical_pointer_to_logical_hit_test) {
    MockRenderer renderer;
    UIManager ui;
    ui.initialize(&renderer);
    ui.setClientSize(400.0f, 200.0f);
    ui.setDpiScale(2.0f);
    Button button;
    button.setPosition(FVector2(10, 10));
    button.setSize(FVector2(80, 30));
    bool clicked = false;
    button.setOnClicked([&] { clicked = true; });
    ui.root()->addChildExternal(&button);

    ui.onMouseMove(40.0f, 40.0f);
    ui.onMouseButtonDown(40.0f, 40.0f, 0);
    ui.onMouseButtonUp(40.0f, 40.0f, 0);
    CHECK(clicked);
    CHECK_FLOAT_EQ(ui.getMousePos().x, 20.0f, 1e-5f);
    ui.shutdown();
}

TEST_CASE(dpi_and_ui_scale_reject_non_finite_host_values) {
    UIManager ui;
    ui.setDpiScale(std::numeric_limits<float>::quiet_NaN());
    ui.setUiScale(std::numeric_limits<float>::infinity());
    ui.setClientSize(std::numeric_limits<float>::infinity(), 720.0f);
    CHECK_FLOAT_EQ(ui.getDpiScale(), 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(ui.getUiScale(), 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(ui.getPhysicalClientSize().x, 0.0f, 1e-5f);
}

TEST_CASE(accessibility_tree_infers_roles_labels_states_and_actions) {
    MockRenderer renderer;
    UIManager ui;
    ui.initialize(&renderer);
    ui.setClientSize(400, 240);
    Button button;
    button.setText(L"Save");
    button.setPosition(FVector2(10, 10));
    button.setSize(FVector2(80, 30));
    bool pressed = false;
    button.setOnClicked([&] { pressed = true; });
    CheckBox check;
    check.setText(L"Autosave");
    check.setPosition(FVector2(10, 50));
    check.setSize(FVector2(120, 24));
    ScrollBar scroll;
    scroll.setRange(0.0f, 100.0f);
    scroll.setValue(50.0f);
    scroll.setPosition(FVector2(140, 10));
    scroll.setSize(FVector2(12, 120));
    ui.root()->addChildExternal(&button);
    ui.root()->addChildExternal(&check);
    ui.root()->addChildExternal(&scroll);

    const AccessibilityNode tree = ui.buildAccessibilityTree();
    const AccessibilityNode* buttonNode = findSemanticNode(tree, button.getAccessibilityId());
    const AccessibilityNode* checkNode = findSemanticNode(tree, check.getAccessibilityId());
    const AccessibilityNode* scrollNode = findSemanticNode(tree, scroll.getAccessibilityId());
    CHECK_NOT_NULL(buttonNode);
    CHECK_NOT_NULL(checkNode);
    CHECK_NOT_NULL(scrollNode);
    CHECK(buttonNode->role == AccessibilityRole::Button);
    CHECK(buttonNode->label == L"Save");
    CHECK((buttonNode->actions & accessibilityActionMask(AccessibilityAction::Press)) != 0);
    CHECK(checkNode->role == AccessibilityRole::CheckBox);
    CHECK(scrollNode->hasNumericRange);
    CHECK_FLOAT_EQ(static_cast<float>(scrollNode->numericValue), 50.0f, 1e-5f);

    CHECK(ui.performAccessibilityAction(button.getAccessibilityId(), AccessibilityAction::Press));
    CHECK(pressed);
    CHECK(ui.performAccessibilityAction(check.getAccessibilityId(), AccessibilityAction::Toggle));
    CHECK(check.isChecked());
    CHECK(ui.performAccessibilityAction(scroll.getAccessibilityId(), AccessibilityAction::Increment));
    CHECK(scroll.getValue() > 50.0f);
    CHECK(ui.setAccessibilityNumericValue(scroll.getAccessibilityId(), 37.5));
    CHECK_FLOAT_EQ(scroll.getValue(), 37.5f, 1e-5f);
    ui.shutdown();
}

TEST_CASE(accessibility_adapter_tracks_semantic_diffs_without_a_native_window) {
    MockRenderer renderer;
    UIManager ui;
    ui.initialize(&renderer);
    ui.setClientSize(320, 180);
    Button button;
    button.setText(L"Before");
    button.setSize(FVector2(80, 24));
    ui.root()->addChildExternal(&button);

    auto adapter = createNativeAccessibilityAdapter(ui, nullptr);
    CHECK_NOT_NULL(adapter.get());
    adapter->update();
    CHECK(adapter->changes().empty());

    button.setText(L"After");
    adapter->update();
    size_t nameChangeCount = 0;
    size_t unrelatedChangeCount = 0;
    for (const AccessibilityChange& change : adapter->changes()) {
        if (change.nodeId == button.getAccessibilityId()
            && change.kind == AccessibilityChangeKind::Name) {
            ++nameChangeCount;
        } else if (change.nodeId == button.getAccessibilityId()) {
            ++unrelatedChangeCount;
        }
    }
    const AccessibilityNode* node = findSemanticNode(
        adapter->snapshot(), button.getAccessibilityId());
    CHECK(nameChangeCount == 1u);
    CHECK(unrelatedChangeCount == 0u);
    CHECK_NOT_NULL(node);
    CHECK(node->label == L"After");
    adapter.reset();
    ui.shutdown();
}

TEST_CASE(accessibility_selection_and_expand_actions_match_native_patterns) {
    MockRenderer renderer;
    UIManager ui;
    ui.initialize(&renderer);
    ui.setClientSize(640, 320);

    RadioButton radio;
    radio.setText(L"Preferred");
    radio.setSize(FVector2(120, 24));
    ComboBox combo;
    combo.setItems({L"First", L"Second"});
    combo.setSelectedIndex(0);
    combo.setPosition(FVector2(0, 40));
    combo.setSize(FVector2(160, 28));
    TabStrip tabs;
    tabs.addTab(L"Overview");
    tabs.addTab(L"Details");
    tabs.setPosition(FVector2(0, 90));
    tabs.setSize(FVector2(240, 28));
    tabs.performLayout();
    ui.root()->addChildExternal(&radio);
    ui.root()->addChildExternal(&combo);
    ui.root()->addChildExternal(&tabs);

    AccessibilityNode tree = ui.buildAccessibilityTree();
    const AccessibilityNode* radioNode = findSemanticNode(tree, radio.getAccessibilityId());
    const AccessibilityNode* comboNode = findSemanticNode(tree, combo.getAccessibilityId());
    CHECK_NOT_NULL(radioNode);
    CHECK_NOT_NULL(comboNode);
    CHECK((radioNode->actions & accessibilityActionMask(AccessibilityAction::Select)) != 0);
    CHECK((comboNode->actions & accessibilityActionMask(AccessibilityAction::Expand)) != 0);
    CHECK((comboNode->actions & accessibilityActionMask(AccessibilityAction::Collapse)) != 0);

    CHECK(ui.performAccessibilityAction(radio.getAccessibilityId(), AccessibilityAction::Select));
    CHECK(radio.isChecked());
    CHECK(ui.performAccessibilityAction(combo.getAccessibilityId(), AccessibilityAction::Expand));
    CHECK(combo.isPopupOpen());
    CHECK(ui.performAccessibilityAction(combo.getAccessibilityId(), AccessibilityAction::Collapse));
    CHECK(!combo.isPopupOpen());

    const auto& tabButtons = tabs.getChildren();
    CHECK(tabButtons.size() == 2u);
    if (tabButtons.size() == 2u) {
        tree = ui.buildAccessibilityTree();
        const AccessibilityNode* firstTab = findSemanticNode(
            tree, tabButtons[0]->getAccessibilityId());
        const AccessibilityNode* secondTab = findSemanticNode(
            tree, tabButtons[1]->getAccessibilityId());
        CHECK_NOT_NULL(firstTab);
        CHECK_NOT_NULL(secondTab);
        if (firstTab != nullptr && secondTab != nullptr) {
            CHECK((firstTab->states & AccessibilityState_Selected) != 0);
            CHECK((secondTab->states & AccessibilityState_Selected) == 0);
            CHECK((secondTab->actions
                & accessibilityActionMask(AccessibilityAction::Select)) != 0);
        }
        CHECK(ui.performAccessibilityAction(
            tabButtons[1]->getAccessibilityId(), AccessibilityAction::Select));
        CHECK(tabs.getSelectedIndex() == 1);
    }
    ui.shutdown();
}

TEST_CASE(accessibility_metadata_round_trips_through_serializer) {
    AccessibilityRole parsedRole = AccessibilityRole::Generic;
    CHECK(accessibilityRoleFromName("button", parsedRole));
    CHECK(parsedRole == AccessibilityRole::Button);
    Button original;
    original.setAccessibilityRole(AccessibilityRole::MenuItem);
    original.setAccessibilityLabel(L"Open recent");
    original.setAccessibilityDescription(L"Opens the recent project list");
    const std::string json = WidgetSerializer::serializeWidget(&original);
    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    CHECK(restored->hasExplicitAccessibilityRole());
    CHECK(restored->getAccessibilityRole() == AccessibilityRole::MenuItem);
    CHECK(restored->getAccessibilityLabel() == L"Open recent");
    CHECK(restored->getAccessibilityDescription() == L"Opens the recent project list");
    destroyWidgetTree(restored);
}

TEST_CASE(theme_inherits_parent_tokens_and_child_overrides) {
    Theme parent;
    parent.setColorToken("product.surface", FVector4(0.1f, 0.2f, 0.3f, 1));
    parent.setFloatToken("product.gap", 8.0f);
    StyleSheet inheritedSheet;
    parent.addSheetFragment("product.controls", inheritedSheet);
    Theme child;
    child.setParentThemeName("product_parent");
    child.setColorToken("product.surface", FVector4(0.7f, 0.2f, 0.3f, 1));
    ThemeManager::get().registerTheme("product_parent", parent);
    ThemeManager::get().registerTheme("product_child", child);
    ThemeManager::get().setActiveTheme("product_child");
    const Theme* active = ThemeManager::get().getActiveTheme();
    CHECK_NOT_NULL(active);
    CHECK_FLOAT_EQ(active->getColorToken("product.surface").x, 0.7f, 1e-5f);
    CHECK_FLOAT_EQ(active->getFloatToken("product.gap"), 8.0f, 1e-5f);
    CHECK(active->hasSheetFragment("product.controls"));
    // H-3 follow-up: release the explicit-activation latch so this
    // case doesn't leak the "product_child" theme into later suites.
    ThemeManager::get().clearActiveThemeForTest();
}

TEST_CASE(widget_theme_override_cascades_to_descendants) {
    Theme theme;
    theme.setColorToken("product.cascade", FVector4(0.1f, 0.1f, 0.1f, 1));
    ThemeManager::get().registerTheme("product_cascade_theme", theme);
    ThemeManager::get().setActiveTheme("product_cascade_theme");
    auto* sheet = new StyleSheet();
    const char* json = R"({"styles":{"cascade":{"backgroundColor":"$product.cascade"}}})";
    sheet->loadFromString(json, std::strlen(json));
    StyleManager::get().setStyleSheet(sheet);
    CompoundWidget parent;
    Button child;
    child.setStyleId("cascade");
    parent.addChildExternal(&child);
    parent.setStyleTokenOverride("product.cascade", FVector4(0.8f, 0.0f, 0.0f, 1));
    const ResolvedStyle resolved = resolveStyle("cascade", &child);
    CHECK(resolved.hasStyle);
    CHECK_FLOAT_EQ(resolved.backgroundColor.x, 0.8f, 1e-5f);
    MockRenderer renderer;
    child.setSize(FVector2(100, 28));
    child.render(renderer);
    bool inheritedFillRendered = false;
    for (const auto& call : renderer.getDrawCalls()) {
        if (call.type == MockRenderer::DrawCall::Rect
            && std::abs(call.color.x - 0.8f) < 1e-5f) {
            inheritedFillRendered = true;
        }
    }
    CHECK(inheritedFillRendered);
    StyleManager::get().setStyleSheet(nullptr);
    delete sheet;
    // H-3 follow-up: release the explicit-activation latch so this
    // case doesn't leak the "product_cascade_theme" into later suites.
    ThemeManager::get().clearActiveThemeForTest();
}

TEST_CASE(tabstrip_scroll_overflow_and_selected_visibility) {
    TabStrip strip;
    strip.setSize(FVector2(180, 28));
    for (int i = 0; i < 5; ++i) strip.addTab(L"Long tab");
    strip.performLayout();
    CHECK(strip.isOverflown());
    CHECK(strip.getMaxScrollOffset() > 0.0f);
    CHECK(strip.onMouseWheel(UIMouseWheelEvent(FVector2(50, 10), 1.0f)));
    CHECK(strip.getScrollOffset() > 0.0f);
    strip.setSelectedIndex(4);
    CHECK_FLOAT_EQ(strip.getScrollOffset(), strip.getMaxScrollOffset(), 1e-4f);

    MockRenderer renderer;
    strip.render(renderer);
    CHECK(renderer.isClipStackBalanced());
}

TEST_CASE(tabstrip_restored_selection_is_visible_after_first_layout) {
    TabStrip strip;
    strip.setSize(FVector2(180, 28));
    for (int i = 0; i < 5; ++i) strip.addTab(L"Long tab");
    strip.setSelectedIndex(4);
    strip.performLayout();
    CHECK_FLOAT_EQ(strip.getScrollOffset(), strip.getMaxScrollOffset(), 1e-4f);
    strip.clearTabs();
    CHECK_FLOAT_EQ(strip.getScrollOffset(), 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(strip.getMaxScrollOffset(), 0.0f, 1e-5f);
}

TEST_CASE(tabstrip_compress_overflow_distributes_tabs_without_scroll) {
    TabStrip strip;
    strip.setSize(FVector2(200, 28));
    strip.setOverflowMode(TabStrip::OverflowMode::Compress);
    for (int i = 0; i < 4; ++i) strip.addTab(L"Wide tab");
    strip.performLayout();
    int outOfBounds = 0;
    for (Widget* child : strip.getChildren()) {
        if (child->getPosition().x < -0.001f
            || child->getPosition().x + child->getWidth() > 200.001f) ++outOfBounds;
    }
    CHECK(outOfBounds == 0);
    CHECK_FLOAT_EQ(strip.getMaxScrollOffset(), 0.0f, 1e-5f);
}

TEST_CASE(richtext_layout_wraps_newlines_aligns_and_hit_tests) {
    RichText text;
    text.setSize(FVector2(100, 100));
    text.setWrapWidth(60);
    text.setAlignment(RichTextAlignment::Center);
    text.addRun(L"alpha beta\ngamma", FVector4(1, 1, 1, 1), 14);
    MockRenderer renderer;
    const RichTextLayout layout = text.layout(renderer);
    CHECK(layout.lines.size() >= 2u);
    CHECK(layout.fragments.size() >= 2u);
    CHECK(layout.fragments.front().bounds.minX >= 0.0f);
    const size_t index = text.hitTestTextIndex(renderer, FVector2(5, 5));
    CHECK(index <= text.getPlainText().size());
    const FRectangle caret = text.getCaretRect(renderer, index);
    CHECK(caret.maxX > caret.minX);
}

TEST_CASE(richtext_justification_materializes_inter_word_advance) {
    RichText text;
    text.setSize(FVector2(120, 100));
    text.setWrapWidth(60);
    text.setAlignment(RichTextAlignment::Justify);
    text.addRun(L"aa bb cc dd", FVector4(1, 1, 1, 1), 14);
    MockRenderer renderer;
    const RichTextLayout layout = text.layout(renderer);
    CHECK(layout.lines.size() >= 2u);
    CHECK_FLOAT_EQ(layout.lines.front().width, 60.0f, 1e-4f);
    CHECK(layout.lines.front().fragmentCount > 1u);
}

TEST_CASE(richtext_short_viewport_clips_instead_of_dropping_first_line) {
    RichText text;
    text.setSize(FVector2(100, 5));
    text.addRun(L"visible through clip", FVector4(1, 1, 1, 1), 18);
    MockRenderer renderer;
    const RichTextLayout layout = text.layout(renderer);
    CHECK(layout.lines.size() == 1u);
    CHECK(!layout.fragments.empty());
    text.render(renderer);
    CHECK(renderer.isClipStackBalanced());
}

TEST_CASE(richtext_max_lines_ellipsis_and_run_decoration_render) {
    RichText text;
    text.setSize(FVector2(70, 30));
    text.setWrapWidth(70);
    text.setMaxLines(1);
    text.setOverflow(RichTextOverflow::Ellipsis);
    RichRun run;
    run.text = L"one two three four";
    run.color = FVector4(0.8f, 0.2f, 0.1f, 1);
    run.fontSize = 14;
    run.bold = true;
    run.italic = true;
    run.underline = true;
    run.strikethrough = true;
    text.addRun(run);
    MockRenderer renderer;
    const RichTextLayout layout = text.layout(renderer);
    CHECK(layout.truncated);
    CHECK(layout.lines.size() == 1u);
    text.render(renderer);
    int textCalls = 0;
    int decorationCalls = 0;
    bool stylePreserved = false;
    for (const auto& call : renderer.getDrawCalls()) {
        if (call.type == MockRenderer::DrawCall::Text) {
            ++textCalls;
            stylePreserved = stylePreserved || (call.textStyle.bold && call.textStyle.italic);
        } else if (call.type == MockRenderer::DrawCall::Rect) {
            ++decorationCalls;
        }
    }
    CHECK(textCalls >= 1);
    CHECK(decorationCalls >= 2);
    CHECK(stylePreserved);
}

TEST_CASE(richtext_caret_stops_never_split_extended_graphemes) {
    RichText text;
    text.setSize(FVector2(160, 40));
    text.addRun(L"e\u0301x", FVector4(1, 1, 1, 1), 14);
    MockRenderer renderer;
    const RichTextLayout layout = text.layout(renderer);
    size_t illegalCaretStopCount = 0;
    size_t legalCaretStopCount = 0;
    for (const RichTextFragment& fragment : layout.fragments) {
        for (size_t index : fragment.caretTextIndices) {
            if (index == 1u) ++illegalCaretStopCount;
            if (index == 0u || index == 2u || index == 3u) ++legalCaretStopCount;
        }
    }
    CHECK(illegalCaretStopCount == 0u);
    CHECK(legalCaretStopCount >= 3u);
    CHECK(text.hitTestTextIndex(renderer, FVector2(1, 5)) != 1u);
}

TEST_CASE(richtext_rtl_fragment_exposes_descending_logical_caret_order) {
    RichText text;
    text.setSize(FVector2(240, 50));
    text.addRun(L"abc \u05E9\u05DC\u05D5\u05DD 12", FVector4(1, 1, 1, 1), 14);
    MockRenderer renderer;
    const RichTextLayout layout = text.layout(renderer);
    size_t rtlFragmentCount = 0;
    size_t rtlCaretOrderViolationCount = 0;
    for (const RichTextFragment& fragment : layout.fragments) {
        if (!fragment.rightToLeft) continue;
        ++rtlFragmentCount;
        for (size_t i = 1; i < fragment.caretTextIndices.size(); ++i) {
            if (fragment.caretTextIndices[i - 1] < fragment.caretTextIndices[i]) {
                ++rtlCaretOrderViolationCount;
            }
        }
    }
    CHECK(rtlFragmentCount >= 1u);
    CHECK(rtlCaretOrderViolationCount == 0u);
}

TEST_CASE(richtext_serializer_round_trips_shaping_style) {
    RichText original;
    original.setTextDirection(TextDirection::RightToLeft);
    RichRun run;
    run.text = L"\u0645\u0631\u062D\u0628\u0627";
    run.fontFamily = L"Segoe UI";
    run.fontWeight = 700;
    run.language = "ar";
    original.addRun(run);
    const std::string json = WidgetSerializer::serializeWidget(&original);
    Widget* restoredWidget = WidgetSerializer::deserialize(json);
    auto* restored = dynamic_cast<RichText*>(restoredWidget);
    CHECK_NOT_NULL(restored);
    CHECK(restored->getTextDirection() == TextDirection::RightToLeft);
    CHECK(restored->getRunCount() == 1u);
    CHECK(restored->getRun(0).fontFamily == L"Segoe UI");
    CHECK(restored->getRun(0).fontWeight == 700);
    CHECK(restored->getRun(0).language == "ar");
    destroyWidgetTree(restoredWidget);
}

TEST_SUITE_END
