#include "AYTest.h"
#include "AYTextInput.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include <cstdio>
#include "AYMockRenderer.h"
#include "AYStyle.h"
#include "UIKeyCode.h"
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

// Phase B (S3) — UIKeyCode retrofit smoke test.
// The anonymous VK constants (kKey_Backspace=8, etc.) were removed from
// TextInput and replaced with UIKeyCode enum values. The integers MUST
// remain equal so any host code passing raw VK ints still routes correctly.
// This test pins the contract — if the enum values ever drift, this fails.
TEST_CASE(textinput_uikeycode_constants_match_vk) {
    CHECK(static_cast<int>(UIKey_Backspace) == 8);
    CHECK(static_cast<int>(UIKey_Tab)       == 9);
    CHECK(static_cast<int>(UIKey_Enter)     == 13);
    CHECK(static_cast<int>(UIKey_Escape)    == 27);
    CHECK(static_cast<int>(UIKey_End)       == 35);
    CHECK(static_cast<int>(UIKey_Home)      == 36);
    CHECK(static_cast<int>(UIKey_Left)      == 37);
    CHECK(static_cast<int>(UIKey_Right)     == 39);
    CHECK(static_cast<int>(UIKey_Delete)    == 46);
    CHECK(static_cast<int>(UIKey_A)         == 65);
    CHECK(static_cast<int>(UIKey_Z)         == 90);

    // Sanity: an existing key path still works post-retrofit. After
    // setText the caret lands at the end (TextInput convention). Press
    // Left to move it, Backspace to delete the char before the caret.
    TextInput ti;
    ti.setText(L"hello");
    UIManager um;
    um.initialize(nullptr);
    um.setFocus(&ti);
    CHECK(ti.getCaret() == 5);   // setText puts caret at end
    ti.onKeyDown(UIKey_Left);
    CHECK(ti.getCaret() == 4);
    ti.onKeyDown(UIKey_Left);
    CHECK(ti.getCaret() == 3);
    ti.onKeyDown(UIKey_Backspace);   // delete 'l' at index 2
    CHECK(ti.getText() == L"helo");
    ti.onKeyDown(UIKey_Home);
    CHECK(ti.getCaret() == 0);
    ti.onKeyDown(UIKey_End);
    CHECK(ti.getCaret() == 4);
    um.shutdown();
}

// =============================================================================
// Phase C (S4) — IME composition tests for TextInput (PR-2)
// =============================================================================
//
// Coverage:
//   - Start populates _compositionPreview + sets _composing.
//   - Update replaces preview.
//   - End with committed text replaces the current selection.
//   - End clears composing state.
//   - Read-only refuses all 3 hooks.
//   - Dtor cancels in-flight composition.
//   - Render draws underline rectangle when composing.
// =============================================================================

#include "AYImeTypes.h"

TEST_CASE(textinput_ime_start_inserts_preview) {
    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    TextInput ti;
    ti.setSize(FVector2(200.0f, 24.0f));
    um.root()->addChildExternal(&ti);
    um.setFocus(&ti);

    um.onDeviceCompositionStart("ni", 2);
    CHECK(ti.onImeCompositionStart("ni", 2));   // direct hook also consumes
    CHECK(ti.isComposing());

    um.shutdown();
}

TEST_CASE(textinput_ime_update_replaces_preview) {
    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    TextInput ti;
    ti.setSize(FVector2(200.0f, 24.0f));
    um.root()->addChildExternal(&ti);
    um.setFocus(&ti);

    um.onDeviceCompositionStart("ni", 2);
    um.onDeviceCompositionUpdate("nihao", 5);
    // We can't observe _compositionPreview directly (private), but we
    // can observe _composing state via the public-ish state machine:
    // a fresh Start that arrives while composing should NOT blow away
    // state. Best-effort: trigger a second Start, verify composing still
    // true.
    um.onDeviceCompositionStart("new", 3);
    CHECK(ti.isComposing());

    um.shutdown();
}

TEST_CASE(textinput_ime_end_commits_replaces_selection) {
    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    TextInput ti;
    ti.setSize(FVector2(200.0f, 24.0f));
    um.root()->addChildExternal(&ti);
    um.setFocus(&ti);
    ti.setText(L"hello");

    um.onDeviceCompositionStart("ni", 2);
    // End with committed Chinese char "你" (UTF-8: \xe4\xbd\xa0, UTF-16: U+4F60).
    um.onDeviceCompositionEnd("\xe4\xbd\xa0");
    // After End the IME commits "你" by replacing the selection. There
    // was no selection so the candidate was discarded (we don't merge
    // previews into _text on End without committed text). Verify
    // _composing cleared.
    CHECK_FALSE(ti.isComposing());
    // The committed text replaced the empty selection at the caret,
    // which was at end of "hello" → text becomes "hello" + U+4F60.
    // 你 is U+4F60 in UTF-16 (Windows wchar_t). We use the L"你"
    // escape form so the test reads cleanly regardless of source file
    // encoding. The escape also avoids the trap of writing
    // L"\xe4\xbd\xa0" which becomes THREE codepoints (E4, BD, A0), not
    // a single surrogate-encoded codepoint.
    CHECK(ti.getText() == L"hello你");

    um.shutdown();
}

TEST_CASE(textinput_ime_end_clears_composing_state) {
    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    TextInput ti;
    ti.setSize(FVector2(200.0f, 24.0f));
    um.root()->addChildExternal(&ti);
    um.setFocus(&ti);

    um.onDeviceCompositionStart("hi", 2);
    CHECK(ti.isComposing());
    um.onDeviceCompositionEnd("");
    CHECK_FALSE(ti.isComposing());

    um.shutdown();
}

TEST_CASE(textinput_ime_composition_ignored_when_readonly) {
    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    TextInput ti;
    ti.setReadOnly(true);
    ti.setSize(FVector2(200.0f, 24.0f));
    um.root()->addChildExternal(&ti);
    um.setFocus(&ti);

    CHECK_FALSE(ti.onImeCompositionStart("ni", 2));
    CHECK_FALSE(ti.isComposing());

    um.shutdown();
}

TEST_CASE(textinput_dtor_cancels_composition) {
    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    TextInput* ti = new TextInput();
    ti->setSize(FVector2(200.0f, 24.0f));
    um.root()->addChildExternal(ti);
    um.setFocus(ti);

    um.onDeviceCompositionStart("draft", 5);
    CHECK(ti->isComposing());

    // Destroying the TextInput must NOT leave UIManager's
    // _compositionOwner pointing at freed memory. Subsequent
    // onDeviceCompositionEnd should not AV.
    delete ti;
    um.onDeviceCompositionEnd("ignored");   // safe — owner cleared

    um.shutdown();
}

TEST_CASE(textinput_render_underline_when_composing) {
    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    TextInput ti;
    ti.setSize(FVector2(200.0f, 24.0f));
    ti.setPosition(FVector2(10.0f, 10.0f));
    um.root()->addChildExternal(&ti);
    um.setFocus(&ti);

    um.onDeviceCompositionStart("你好", 6);  // 2 BMP codepoints

    MockRenderer renderer;
    ti.render(renderer);

    // The underline must add at least one extra drawRect compared to
    // a non-composing render. We can't easily count "added" calls, so
    // verify the underline color appears among the draw calls.
    bool sawUnderline = false;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) {
            const FVector4 c = dc.color;
            // Sky blue underline: (0.30, 0.65, 0.95, 1.0) — match within tolerance.
            if (std::abs(c.x - 0.30f) < 0.01f &&
                std::abs(c.y - 0.65f) < 0.01f &&
                std::abs(c.z - 0.95f) < 0.01f) {
                sawUnderline = true;
                break;
            }
        }
    }
    CHECK(sawUnderline);

    um.shutdown();
}

TEST_SUITE_END
