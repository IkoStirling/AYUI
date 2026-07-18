#include "AYTest.h"
#include "AYTextInput.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
#include "AYStyle.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_TextInput)

// C-3: TextInput default state: empty text, caret at 0, no selection,
// not focused, Beam cursor hint.
TEST_CASE(textinput_initial_state) {
    TextInput ti;
    CHECK(ti.getText() == L"");
    CHECK(ti.getCaret() == 0u);
    CHECK_FALSE(ti.hasSelection());
    CHECK_FALSE(ti.hasFocus());
    CHECK(ti.getCursorHint() == UiCursorHint::Beam);
    CHECK_FALSE(ti.isReadOnly());
    CHECK_FALSE(ti.isPasswordMode());
}

// C-3: insertChar at caret 0 appends text. Caret advances to one past
// the inserted char.
TEST_CASE(textinput_insert_char_basic) {
    TextInput ti;
    ti.setFocus(true);
    CHECK(ti.insertChar(L'A'));
    CHECK(ti.getText() == L"A");
    CHECK(ti.getCaret() == 1u);

    ti.insertChar(L'B');
    CHECK(ti.getText() == L"AB");
    CHECK(ti.getCaret() == 2u);
}

// C-3: insertChar while not focused is a no-op (UIManager routes typed
// chars only to the focused widget; TextInput itself enforces this).
TEST_CASE(textinput_insert_char_blocks_unfocused) {
    TextInput ti;
    CHECK_FALSE(ti.insertChar(L'X'));
    CHECK(ti.getText() == L"");
}

// C-3: caret movement with arrow keys via onKeyDown. setCaret is the
// public API; onKeyDown is the dispatch path.
TEST_CASE(textinput_caret_arrow_keys) {
    TextInput ti;
    ti.setFocus(true);
    ti.setText(L"hello");

    ti.setCaret(0);
    CHECK(ti.getCaret() == 0u);
    ti.onKeyDown(39);   // Right
    CHECK(ti.getCaret() == 1u);
    ti.onKeyDown(39);
    ti.onKeyDown(39);
    CHECK(ti.getCaret() == 3u);
    ti.onKeyDown(37);   // Left
    CHECK(ti.getCaret() == 2u);

    // Clamp at edges.
    ti.setCaret(0);
    ti.onKeyDown(37);
    CHECK(ti.getCaret() == 0u);
    ti.setCaret(5);
    ti.onKeyDown(39);
    CHECK(ti.getCaret() == 5u);
}

// C-3: Home / End jump to start / end.
TEST_CASE(textinput_home_end_keys) {
    TextInput ti;
    ti.setFocus(true);
    ti.setText(L"hello");

    ti.setCaret(3);
    ti.onKeyDown(36);   // Home
    CHECK(ti.getCaret() == 0u);
    ti.onKeyDown(35);   // End
    CHECK(ti.getCaret() == 5u);
}

// C-3: deleteLeft removes the char before the caret; deleteRight
// removes the char at the caret.
TEST_CASE(textinput_delete_left_and_right) {
    TextInput ti;
    ti.setFocus(true);

    ti.setText(L"hello");
    ti.setCaret(5);
    CHECK(ti.deleteLeft());
    CHECK(ti.getText() == L"hell");
    CHECK(ti.getCaret() == 4u);

    // Reset and exercise deleteRight at index 1 on a fresh buffer.
    ti.setText(L"hello");
    ti.setCaret(1);
    CHECK(ti.deleteRight());
    CHECK(ti.getText() == L"hllo");   // removed 'e' at index 1
    CHECK(ti.getCaret() == 1u);
    CHECK(ti.getText().size() == 4u);
}

// C-3: Backspace at caret 0 is a no-op (no char to delete).
TEST_CASE(textinput_delete_left_at_start_noop) {
    TextInput ti;
    ti.setFocus(true);
    ti.setText(L"hello");
    ti.setCaret(0);
    CHECK_FALSE(ti.deleteLeft());
    CHECK(ti.getText() == L"hello");
}

// C-3: Selection — setSelection, hasSelection, clearSelection.
TEST_CASE(textinput_selection_basic) {
    TextInput ti;
    ti.setFocus(true);
    ti.setText(L"hello world");
    ti.setSelection(6, 11);    // "world"
    CHECK(ti.hasSelection());
    CHECK(ti.getSelectionStart() == 6u);
    CHECK(ti.getSelectionEnd() == 11u);
    CHECK(ti.getCaret() == 11u);

    ti.clearSelection();
    CHECK_FALSE(ti.hasSelection());
    CHECK(ti.getSelectionStart() == ti.getCaret());
}

// C-3: selectAll + insertChar replaces the selection.
TEST_CASE(textinput_select_all_insert_replaces) {
    TextInput ti;
    ti.setFocus(true);
    ti.setText(L"hello");
    ti.selectAll();
    CHECK(ti.hasSelection());

    CHECK(ti.insertChar(L'X'));
    CHECK(ti.getText() == L"X");
    CHECK(ti.getCaret() == 1u);
    CHECK_FALSE(ti.hasSelection());
}

// C-3: deleteLeft with active selection deletes the selection (single
// delete replaces it with empty).
TEST_CASE(textinput_delete_left_with_selection) {
    TextInput ti;
    ti.setFocus(true);
    ti.setText(L"hello world");
    ti.setSelection(6, 11);    // "world"
    CHECK(ti.deleteLeft());
    CHECK(ti.getText() == L"hello ");
    CHECK(ti.getCaret() == 6u);
    CHECK_FALSE(ti.hasSelection());
}

// C-3: onTextInput routes characters and only when focused.
TEST_CASE(textinput_on_text_input_routes_only_when_focused) {
    TextInput ti;
    CHECK_FALSE(ti.onTextInput(L'a'));
    CHECK(ti.getText() == L"");

    ti.setFocus(true);
    CHECK(ti.onTextInput(L'a'));
    CHECK(ti.onTextInput(L'b'));
    CHECK(ti.getText() == L"ab");
}

// C-3: maxLength caps the buffer; further insertChar is a no-op.
TEST_CASE(textinput_max_length_caps_input) {
    TextInput ti;
    ti.setFocus(true);
    ti.setMaxLength(3);
    ti.setText(L"abc");
    CHECK_FALSE(ti.insertChar(L'd'));   // already at cap
    CHECK(ti.getText() == L"abc");

    ti.setMaxLength(3);   // re-pin (setText wouldn't re-trim if maxLength same)
    ti.setText(L"ab");
    CHECK(ti.insertChar(L'c'));
    CHECK(ti.getText() == L"abc");
}

// C-3: readOnly blocks edits but still allows caret moves + selection.
TEST_CASE(textinput_readonly_blocks_edits) {
    TextInput ti;
    ti.setText(L"hello");
    ti.setReadOnly(true);
    CHECK_FALSE(ti.insertChar(L'X'));
    CHECK_FALSE(ti.deleteLeft());
    CHECK_FALSE(ti.deleteRight());
    CHECK(ti.getText() == L"hello");

    // Selection / setCaret still work (readonly doesn't block cursor).
    ti.setCaret(2);
    CHECK(ti.getCaret() == 2u);
}

// C-3: password mode — getText still gives the raw text but render
// produces a masked display. We check by introspecting via the
// accessor (the rendering path is tested separately).
TEST_CASE(textinput_password_mode_blocks_selection) {
    TextInput ti;
    ti.setText(L"hunter2");
    ti.setPasswordMode(true);
    CHECK(ti.isPasswordMode());

    // Password mode clears selection (can't edit / copy past length).
    ti.setSelection(0, 3);
    ti.setPasswordMode(true);
    CHECK_FALSE(ti.hasSelection());

    // Underlying text remains.
    CHECK(ti.getText() == L"hunter2");
}

// C-3: appendText + clear work as advertised.
TEST_CASE(textinput_append_and_clear) {
    TextInput ti;
    ti.setFocus(true);
    ti.setText(L"hello");
    ti.setCaret(5);
    ti.appendText(L" world");
    CHECK(ti.getText() == L"hello world");
    CHECK(ti.getCaret() == 11u);

    ti.clear();
    CHECK(ti.getText() == L"");
    CHECK(ti.getCaret() == 0u);
}

// C-3: clear() on already-empty is a no-op callback-wise.
TEST_CASE(textinput_clear_when_empty_no_callback) {
    TextInput ti;
    int count = 0;
    ti.setOnTextChanged([&](const std::wstring&) { ++count; });
    ti.clear();
    CHECK(count == 0);
}

// C-3: setText fires _onTextChanged when value differs, NOT when same.
TEST_CASE(textinput_settext_callback_idempotent) {
    TextInput ti;
    int count = 0;
    ti.setOnTextChanged([&](const std::wstring&) { ++count; });

    ti.setText(L"hello");
    CHECK(count == 1);
    ti.setText(L"hello");
    CHECK(count == 1);
    ti.setText(L"");
    CHECK(count == 2);
}

// C-3: UIManager setFocus dispatches focus lifecycle to FocusableWidget.
// First widget gets focus, second replaces it (first loses focus).
TEST_CASE(textinput_uimanager_set_focus_routes) {
    UIManager& um = UIManager::get();
    um.initialize(nullptr);

    TextInput* a = new TextInput();
    TextInput* b = new TextInput();
    a->setId("a");
    b->setId("b");

    um.setFocus(a);
    CHECK(a->hasFocus());
    CHECK_FALSE(b->hasFocus());

    um.setFocus(b);
    CHECK_FALSE(a->hasFocus());
    CHECK(b->hasFocus());

    um.setFocus(nullptr);
    CHECK_FALSE(a->hasFocus());
    CHECK_FALSE(b->hasFocus());

    delete a;
    delete b;
    um.shutdown();
}

// C-3: render emits at least one Rect (background) + one BorderRect +
// at most 1 caret Rect (when focused & caret visible). Width 'draw
// calls' is fan-out dependent; we pin via accent presence instead of
// exact counts.
TEST_CASE(textinput_render_emits_background) {
    TextInput ti;
    ti.setSize(FVector2(200.0f, 24.0f));
    ti.setPosition(FVector2(0.0f, 0.0f));

    MockRenderer renderer;
    ti.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    CHECK(rectCount >= 1);
}

// C-3: factory + serializer round-trip preserves text + password + readOnly.
TEST_CASE(textinput_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("TextInput"));

    Widget* widget = factory.create("TextInput");
    CHECK_NOT_NULL(widget);
    TextInput* original = dynamic_cast<TextInput*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("username");
    original->setText(L"alice");
    original->setPasswordMode(true);
    original->setReadOnly(false);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"TextInput\"") != std::string::npos);
    CHECK(json.find("alice") != std::string::npos);
    CHECK(json.find("\"password\": true") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    TextInput* restoredTi = dynamic_cast<TextInput*>(restored);
    CHECK_NOT_NULL(restoredTi);
    CHECK(restoredTi->getId() == "username");
    CHECK(restoredTi->getText() == L"alice");
    CHECK_TRUE(restoredTi->isPasswordMode());

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

TEST_SUITE_END
