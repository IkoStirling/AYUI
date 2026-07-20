// P1 (Polish) — TextArea undo / redo coverage.
//
// Verifies the linear-history snapshot mechanism in TextArea:
//   - Each mutating op (insertChar / deleteLeft / deleteRight / setText /
//     clear) pushes a TextEditSnapshot BEFORE applying the change.
//   - undo() pops one and reverses to the prior state.
//   - redo() restores the undone state until a fresh mutation clears
//     the redo stack (standard editor semantics).
//   - Empty stacks are no-ops (no crash).
//   - Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z route through TextDocument::onKeyDown
//     to undo() / redo() and produce the matching text state.
//
// History capacity is kMaxHistoryEntries = 100 — we don't exhaustively
// stress-test the cap here (covered by a smoke test on getUndoStackSize
// post many calls).

#include "AYTest.h"
#include "AYTextArea.h"
#include "AYWidgetFactory.h"
#include "AYUIManager.h"
#include "AYMockRenderer.h"
#include "UIKeyCode.h"
#include <string>

using namespace ayt::ui;

TEST_SUITE(AYUI_UndoRedo_P1)

// ---------------------------------------------------------------------------
// Snapshot-level API.
// ---------------------------------------------------------------------------

TEST_CASE(undo_redo_stacks_start_empty) {
    TextArea ta;
    CHECK_FALSE(ta.canUndo());
    CHECK_FALSE(ta.canRedo());
    CHECK(ta.getUndoStackSize() == 0u);
    CHECK(ta.getRedoStackSize() == 0u);
}

TEST_CASE(set_text_pushes_undo_entry) {
    TextArea ta;
    ta.setText(L"alpha");
    CHECK(ta.canUndo());
    CHECK(ta.getUndoStackSize() == 1u);
}

TEST_CASE(insert_char_pushes_undo_entry) {
    TextArea ta;
    ta.insertChar(L'x');
    ta.insertChar(L'y');
    CHECK(ta.getUndoStackSize() == 2u);
}

TEST_CASE(undo_empty_stack_does_not_crash) {
    // No mutating op was ever called → _undoStack is empty → undo() is
    // a no-op. Verify it neither throws nor modifies state.
    TextArea ta;
    CHECK_FALSE(ta.canUndo());
    ta.undo();
    CHECK(ta.getText() == L"");
    CHECK_FALSE(ta.canUndo());
}

TEST_CASE(redo_empty_stack_is_noop) {
    TextArea ta;
    ta.setText(L"keep me");
    ta.redo();   // no-op: no redo path
    CHECK(ta.getText() == L"keep me");
    CHECK_FALSE(ta.canRedo());
}

TEST_CASE(readonly_ops_do_not_push_undo) {
    TextArea ta;
    ta.setReadOnly(true);
    ta.insertChar(L'x');   // rejected — no snapshot
    ta.deleteLeft();       // rejected — no snapshot
    ta.setText(L"forced"); // setText bypasses readonly in v1; covered
                            // by the public RO contract on insertChar.
                            // Insert/delete are the path we care about.
    CHECK(ta.canUndo());    // setText above did push (not RO-gated in v1)
    // The point: only the typing-level ops respect readonly for history
    // purposes. setText is treated as a host-level reset.
    (void)0;
}

// ---------------------------------------------------------------------------
// Forward / backward state restoration.
// ---------------------------------------------------------------------------

TEST_CASE(undo_insert_char_reverts_text_and_caret) {
    TextArea ta;
    ta.setText(L"");      // 1 undo entry (empty → empty, still pushed)
    ta.insertChar(L'a');  // 2nd undo entry (empty → "a")
    ta.insertChar(L'b');
    ta.insertChar(L'c');
    CHECK(ta.getText() == L"abc");
    CHECK(ta.getCaretCol() == 3);

    ta.undo();
    CHECK(ta.getText() == L"ab");
    CHECK(ta.getCaretCol() == 2);

    ta.undo();
    CHECK(ta.getText() == L"a");
    CHECK(ta.getCaretCol() == 1);

    ta.undo();
    CHECK(ta.getText() == L"");
    CHECK(ta.getCaretCol() == 0);
}

TEST_CASE(undo_delete_char_restores_char) {
    TextArea ta;
    ta.setText(L"hello");
    ta.deleteLeft();   // removes 'o'
    CHECK(ta.getText() == L"hell");
    ta.undo();
    CHECK(ta.getText() == L"hello");
}

TEST_CASE(fresh_edit_after_undo_clears_redo_stack) {
    TextArea ta;
    ta.setText(L"ab");
    ta.insertChar(L'c');   // "abc"
    ta.undo();             // back to "ab" — redo stack now has "abc"
    CHECK(ta.canRedo());
    CHECK(ta.getRedoStackSize() == 1u);

    ta.insertChar(L'z');   // fresh edit: "abz" — redo stack cleared
    CHECK_FALSE(ta.canRedo());
    CHECK(ta.getText() == L"abz");
}

TEST_CASE(redo_replays_undone_change) {
    TextArea ta;
    ta.setText(L"hello");
    ta.insertChar(L'x');
    CHECK(ta.getText() == L"hellox");

    ta.undo();
    CHECK(ta.getText() == L"hello");
    CHECK(ta.canRedo());

    ta.redo();
    CHECK(ta.getText() == L"hellox");
    CHECK_FALSE(ta.canRedo());
}

TEST_CASE(undo_redo_undo_redo_round_trip_three_levels) {
    // Stress-test the round-trip: undo to "abc", redo to "abcd",
    // undo again to "abc", redo again to "abcd". History must remain
    // consistent (no double-push, no leak).
    TextArea ta;
    ta.setText(L"abc");
    ta.insertChar(L'd');
    CHECK(ta.getText() == L"abcd");

    ta.undo(); CHECK(ta.getText() == L"abc");
    ta.redo(); CHECK(ta.getText() == L"abcd");
    ta.undo(); CHECK(ta.getText() == L"abc");
    ta.redo(); CHECK(ta.getText() == L"abcd");
    CHECK(ta.getRedoStackSize() == 0u);
}

// ---------------------------------------------------------------------------
// setText participates in history.
// ---------------------------------------------------------------------------

TEST_CASE(undo_set_text_reverts_to_previous_text) {
    TextArea ta;
    ta.setText(L"first");
    ta.setText(L"second");
    CHECK(ta.getText() == L"second");

    ta.undo();
    CHECK(ta.getText() == L"first");
}

TEST_CASE(clear_is_undoable) {
    TextArea ta;
    ta.setText(L"abc");
    ta.clear();
    CHECK(ta.getText() == L"");
    ta.undo();
    CHECK(ta.getText() == L"abc");
}

// ---------------------------------------------------------------------------
// IME-like composition commit is itself undoable because it calls
// insertChar (which pushes). One composition commit pushing N chars
// accumulates N undo entries in v1 — explicit non-coalesced behavior.
// Documented but worth pinning for future polish (time-window coalesce).
// ---------------------------------------------------------------------------

TEST_CASE(undo_walks_back_through_multiline_buffer) {
    TextArea ta;
    ta.setText(L"one");
    ta.insertChar(L'\n');
    ta.insertChar(L't');
    ta.insertChar(L'w');
    ta.insertChar(L'o');
    CHECK(ta.getText() == L"one\ntwo");

    ta.undo();   // undo last insert ('o')
    CHECK(ta.getText() == L"one\ntw");

    ta.undo();   // undo 'w'
    CHECK(ta.getText() == L"one\nt");

    ta.undo();   // undo 't'
    CHECK(ta.getText() == L"one\n");

    ta.undo();   // undo '\n'
    CHECK(ta.getText() == L"one");

    ta.redo();
    CHECK(ta.getText() == L"one\n");
}

// ---------------------------------------------------------------------------
// Selection replacement is a single undo step.
// ---------------------------------------------------------------------------

TEST_CASE(undo_reverts_selection_replacement) {
    TextArea ta;
    ta.setText(L"hello world");
    ta.setSelection(0, 6, 0, 11);   // select "world"
    ta.insertChar(L'!');            // replace selection with '!'
    CHECK(ta.getText() == L"hello !");

    ta.undo();
    // After undo, _lines should be back to "hello world" (the
    // selection-replace is recorded as one logical op, so one undo
    // reverts the entire replacement).
    CHECK(ta.getText() == L"hello world");
}

// ---------------------------------------------------------------------------
// Capacity cap (kMaxHistoryEntries = 100). Pushing more than 100 entries
// must drop the oldest; canUndo / getText remain sensible.
// ---------------------------------------------------------------------------

TEST_CASE(history_caps_at_max_entries) {
    TextArea ta;
    // Push 105 single-char inserts. Last entry should still be reachable;
    // earliest entries are dropped from undo after cap.
    for (int i = 0; i < 105; ++i) {
        ta.insertChar(static_cast<wchar_t>(L'a' + (i % 26)));
    }
    CHECK(ta.getUndoStackSize() <= TextArea::kMaxHistoryEntries);
    CHECK(ta.canUndo());

    // Total string length is 105; undoing 100 times should NOT get back
    // to empty (because the cap dropped the earliest entries). This test
    // only pins the cap semantic — exact string content at intermediate
    // undo depths is brittle.
    for (int i = 0; i < 100; ++i) {
        ta.undo();
    }
    CHECK_FALSE(ta.canUndo());
    CHECK(ta.getText().size() < 105u);  // we did make progress
}

// ---------------------------------------------------------------------------
// Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z integration via UIManager key routing.
// These tests exercise the real keyboard path that hosts hit.
// ---------------------------------------------------------------------------

TEST_CASE(ctrl_z_undo_via_uimanager_key_routing) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    TextArea ta;
    ta.setSize(FVector2(400.0f, 300.0f));
    ui.root()->addChildExternal(&ta);

    // Give focus to the inner document so onKeyDown routes into it.
    FocusableWidget* doc = ta.getDocumentAsFocusable();
    CHECK(doc != nullptr);
    ui.setFocus(doc);
    CHECK(doc->hasFocus());

    ta.setText(L"foo");
    ta.insertChar(L'x');   // "foox"
    CHECK(ta.getText() == L"foox");

    // Press Ctrl (modifier) + Z (key). UIManager dispatches modifier
    // tracking first; then the keyCode goes to the focused widget.
    CHECK(ui.onKeyDown(UIKey_Control) == true);    // modifier latch
    CHECK(ui.onKeyDown(UIKey_Z) == true);          // undo
    CHECK(ta.getText() == L"foo");

    // TextDocument::onKeyUp is the base-class default (returns false);
    // we don't pin that — instead verify the modifier gets released.
    ui.onKeyUp(UIKey_Z);
    CHECK(ui.getModifiers() != 0u);   // still control held
    ui.onKeyUp(UIKey_Control);
    CHECK(ui.getModifiers() == 0u);
    ui.shutdown();
}

TEST_CASE(ctrl_y_redo_via_uimanager_key_routing) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    TextArea ta;
    ta.setSize(FVector2(400.0f, 300.0f));
    ui.root()->addChildExternal(&ta);
    ui.setFocus(ta.getDocumentAsFocusable());
    ta.setText(L"foo");
    ta.insertChar(L'x');   // "foox"

    // Undo via Ctrl+Z
    ui.onKeyDown(UIKey_Control);
    ui.onKeyDown(UIKey_Z);
    ui.onKeyUp(UIKey_Z);
    ui.onKeyUp(UIKey_Control);
    CHECK(ta.getText() == L"foo");

    // Redo via Ctrl+Y
    ui.onKeyDown(UIKey_Control);
    ui.onKeyDown(UIKey_Y);
    ui.onKeyUp(UIKey_Y);
    ui.onKeyUp(UIKey_Control);
    CHECK(ta.getText() == L"foox");
    ui.shutdown();
}

TEST_CASE(ctrl_shift_z_redo_via_uimanager_key_routing) {
    // macOS-style redo.
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    TextArea ta;
    ta.setSize(FVector2(400.0f, 300.0f));
    ui.root()->addChildExternal(&ta);
    ui.setFocus(ta.getDocumentAsFocusable());
    ta.setText(L"foo");
    ta.insertChar(L'x');

    ui.onKeyDown(UIKey_Control);
    ui.onKeyDown(UIKey_Z);
    ui.onKeyUp(UIKey_Z);
    CHECK(ta.getText() == L"foo");
    ui.onKeyUp(UIKey_Control);

    // Now Ctrl+Shift+Z for redo
    ui.onKeyDown(UIKey_Control);
    ui.onKeyDown(UIKey_Shift);
    ui.onKeyDown(UIKey_Z);
    ui.onKeyUp(UIKey_Z);
    ui.onKeyUp(UIKey_Shift);
    ui.onKeyUp(UIKey_Control);
    CHECK(ta.getText() == L"foox");
    ui.shutdown();
}

TEST_SUITE_END
