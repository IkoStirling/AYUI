#include "AYTest.h"
#include "AYClipboard.h"
#include "AYTextInput.h"
#include "AYTextArea.h"
#include "AYUIManager.h"
#include "UIKeyCode.h"
#include <string>

// =============================================================================
// PR-A2 — IClipboard abstraction tests
// -----------------------------------------------------------------------------
// Pre-PR: TextInput had Win32 clipboard code in its anonymous namespace.
// TextArea had no clipboard support at all. PR-A2 introduces IClipboard
// + getClipboard() / setClipboardImpl() so both widgets share the same
// interface and tests can inject a deterministic mock (no flakiness from
// the real Windows clipboard, no cross-test pollution).
//
// Cases pin:
//   1. setText + getText roundtrip via the mock
//   2. TextInput Ctrl+C/X/V with a single-line paste (existing v1 behaviour
//      strips \r\n; we re-pin this against the new interface)
//   3. TextArea Ctrl+V with a multi-line paste — produces N source lines
//      (the \n → insertChar(linebreak) round-trip)
//   4. Empty clipboard is a no-op for both widgets
// =============================================================================

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Minimal in-process IClipboard for tests. Holds a single wstring;
// setText stores, getText returns + clears (mirrors a real OS clipboard
// where one app's paste overwrites another's).
class MockClipboard : public IClipboard {
public:
    bool setText(const std::wstring& t) override {
        m_text = t;
        return true;
    }
    bool getText(std::wstring& out) override {
        out = m_text;
        return !m_text.empty();
    }
    void clear() { m_text.clear(); }
    const std::wstring& text() const { return m_text; }
private:
    std::wstring m_text;
};

}  // namespace

TEST_SUITE(AYUI_Clipboard_A2)

TEST_CASE(clipboard_set_then_get_roundtrip) {
    auto* mock = new MockClipboard();
    setClipboardImpl(mock);

    CHECK(getClipboard().setText(L"hello"));
    std::wstring out;
    CHECK(getClipboard().getText(out));
    CHECK(out == L"hello");

    setClipboardImpl(nullptr);
}

TEST_CASE(clipboard_textinput_ctrl_c_x_v) {
    auto* mock = new MockClipboard();
    setClipboardImpl(mock);

    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    TextInput ti;
    ti.setText(L"hello world");
    ti.setSize(FVector2(200.0f, 24.0f));
    ti.setPosition(FVector2(10.0f, 10.0f));
    um.root()->addChildExternal(&ti);
    ti.setFocus(true);  // TextInput early-returns on onKeyDown if !_hasFocus

    // Select "world" (cols 6..11), copy, paste over a different range.
    ti.setCaret(11);
    ti.setSelection(6, 11);
    um.onKeyDown(UIKey_Control);  // prime the modifier bit
    CHECK(ti.onKeyDown(UIKey_C));
    CHECK(mock->text() == L"world");

    // Move caret to start, paste: text becomes "worldhello world".
    ti.setCaret(0);
    ti.setSelection(0, 0);
    CHECK(ti.onKeyDown(UIKey_V));
    CHECK(ti.getText() == L"worldhello world");

    // Cut: select "hello" (cols 0..5) at the start, Ctrl+X.
    ti.setCaret(5);
    ti.setSelection(0, 5);
    CHECK(ti.onKeyDown(UIKey_X));
    CHECK(mock->text() == L"world");
    CHECK(ti.getText() == L"hello world");
    um.onKeyUp(UIKey_Control);

    um.shutdown();
    setClipboardImpl(nullptr);
}

TEST_CASE(clipboard_textarea_multiline_paste_inserts_newlines) {
    auto* mock = new MockClipboard();
    setClipboardImpl(mock);

    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    TextArea ta;
    ta.setSize(FVector2(200.0f, 100.0f));
    ta.setText(L"");
    ta.getDocumentAsFocusable()->setFocus(true);

    // Pre-load the clipboard with 3 lines + \r\n on the middle line.
    mock->setText(L"line one\r\nline two\nline three");
    ta.setCaret(0, 0);
    um.onKeyDown(UIKey_Control);  // prime modifier bit
    CHECK(ta.getDocumentAsFocusable()->onKeyDown(UIKey_V));
    CHECK(ta.getText() == L"line one\nline two\nline three");
    um.onKeyUp(UIKey_Control);
    // 3 source lines, caret at end of last line.
    CHECK(ta.getCaretLine() == 2);
    CHECK(ta.getCaretCol() == static_cast<int>(std::wstring(L"line three").size()));

    um.shutdown();
    setClipboardImpl(nullptr);
}

TEST_CASE(clipboard_empty_clip_is_noop) {
    auto* mock = new MockClipboard();
    mock->clear();  // empty
    setClipboardImpl(mock);

    MockRenderer backend;
    UIManager um;
    um.initialize(&backend);

    // TextInput: empty paste must not crash and must not change text.
    TextInput ti;
    ti.setText(L"untouched");
    ti.setSize(FVector2(200.0f, 24.0f));
    ti.setPosition(FVector2(10.0f, 10.0f));
    um.root()->addChildExternal(&ti);
    ti.setFocus(true);
    um.onKeyDown(UIKey_Control);
    CHECK(ti.onKeyDown(UIKey_V));
    CHECK(ti.getText() == L"untouched");
    um.onKeyUp(UIKey_Control);

    // TextArea: empty paste is a no-op.
    TextArea ta;
    ta.setText(L"still here");
    ta.getDocumentAsFocusable()->setFocus(true);
    um.onKeyDown(UIKey_Control);
    CHECK(ta.getDocumentAsFocusable()->onKeyDown(UIKey_V));
    CHECK(ta.getText() == L"still here");
    um.onKeyUp(UIKey_Control);

    um.shutdown();
    setClipboardImpl(nullptr);
}

TEST_SUITE_END
