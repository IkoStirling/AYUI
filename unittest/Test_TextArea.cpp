#include "AYTest.h"
#include "AYTextArea.h"
#include "AYScrollView.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
#include "AYStyle.h"
#include <iostream>

// =============================================================================
// Known-not-covered scenarios for C-10 TextArea v1
// =============================================================================
// See Controls/AYTextArea.h top-of-file "v1 design decisions" block for
// design rationale + upgrade paths. Pinned here as entry points.
//
// A1. Word wrap.
//     v1 does NOT wrap long lines (DECISION 1). A line wider than the
//     viewport scrolls horizontally; hbar is off by default. v1.1 fix:
//     add setWordWrap(bool) + re-measure lines on viewport width change
//     using a TextShaper.
//
// A2. IME / composition.
//     v1 routes plain text via insertChar; composition strings are NOT
//     tracked. v1.1: preedit buffer drawn near caret, Escape cancels.
//
// A3. Undo / redo.
//     v1 has no snapshot stack. v1.1: snapshot _lines + caret +
//     selection; Ctrl+Z / Ctrl+Y routed via onKeyDown.
//
// A4. Drag-to-select.
//     v1: single click moves caret; double-click selects a word;
//     Shift+arrow extends. Drag-to-select requires mouse capture (see
//     SplitterHandle drag-end pattern at the widget-state level).
//
// A5. Multi-line selection highlight.
//     v1: only single-line ranges get a visual highlight. Multi-line
//     selections are tracked in the data model but render as the active
//     line's range only. v1.1 fix: render N rects per line crossed.
//
// A6. Placeholder text.
//     v1 has no placeholder. v1.1: add setPlaceholder + render when
//     buffer is one empty line.
//
// A7. ScrollView horizontal scrollbar auto-enable on long lines.
//     v1 ships hbar off (DECISION 1). Long lines just clip. v1.1: enable
//     hbar when content width > viewport, mirror DECISION 1's wrap
//     upgrade.
// =============================================================================

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_TextArea)

// C-10: default state — one empty line, caret at (0,0), vbar enabled.
TEST_CASE(textarea_initial_state) {
    TextArea ta;
    CHECK(ta.getText() == L"");
    CHECK(ta.getCaretLine() == 0);
    CHECK(ta.getCaretCol() == 0);
    CHECK_FALSE(ta.hasSelection());
    CHECK_FALSE(ta.isReadOnly());
    CHECK(ta.getScrollView() != nullptr);
    CHECK(ta.getDocument() != nullptr);
    CHECK_FLOAT_EQ(ta.getLineHeight(), TextArea::kDefaultLineHeight, 1e-5f);
}

// C-10: setText splits on '\n'; getText joins with '\n'.
TEST_CASE(textarea_set_text_splits_and_joins) {
    TextArea ta;
    ta.setText(L"line one\nline two\nline three");
    CHECK(ta.getText() == L"line one\nline two\nline three");

    ta.setText(L"");
    CHECK(ta.getText() == L"");
    // Empty buffer still has one (empty) line — caret can sit at (0,0).
    CHECK(ta.getCaretLine() == 0);

    ta.setText(L"single");
    CHECK(ta.getText() == L"single");
}

// C-10: insertChar types into the buffer; '\n' splits lines; selection
// replacement works.
TEST_CASE(textarea_insert_char_and_newline) {
    TextArea ta;
    int changes = 0;
    ta.setOnTextChanged([&](const std::wstring&) { ++changes; });

    CHECK(ta.insertChar(L'a'));
    CHECK(ta.insertChar(L'b'));
    CHECK(ta.insertChar(L'c'));
    CHECK(ta.getText() == L"abc");
    CHECK(ta.getCaretLine() == 0);
    CHECK(ta.getCaretCol() == 3);
    CHECK(changes == 3);

    CHECK(ta.insertChar(L'\n'));
    CHECK(ta.getText() == L"abc\n");
    CHECK(ta.getCaretLine() == 1);
    CHECK(ta.getCaretCol() == 0);

    CHECK(ta.insertChar(L'd'));
    CHECK(ta.getText() == L"abc\nd");
    CHECK(changes == 5);
}

// C-10: deleteLeft / deleteRight handle backspace + delete + line join.
TEST_CASE(textarea_delete_ops) {
    TextArea ta;
    ta.setText(L"ab\ncd");
    ta.setCaret(1, 1);   // between 'c' and 'd'

    CHECK(ta.deleteRight());
    CHECK(ta.getText() == L"ab\nc");

    ta.setCaret(1, 1);   // at end of "ab\nc"
    CHECK(ta.deleteLeft());
    CHECK(ta.getText() == L"ab\n");
    CHECK(ta.getCaretLine() == 1);
    CHECK(ta.getCaretCol() == 0);

    CHECK(ta.deleteLeft());  // join line 1 with line 0 ("ab")
    CHECK(ta.getText() == L"ab");
    CHECK(ta.getCaretLine() == 0);
    CHECK(ta.getCaretCol() == 2);

    CHECK(ta.deleteLeft());  // delete 'b'
    CHECK(ta.getText() == L"a");
    CHECK(ta.getCaretCol() == 1);

    CHECK(ta.deleteLeft());  // delete 'a'
    CHECK(ta.getText() == L"");
    CHECK(ta.getCaretCol() == 0);

    CHECK_FALSE(ta.deleteLeft());   // at (0,0) — no-op
    CHECK(ta.getText() == L"");
}

// C-10: selection + selectAll.
TEST_CASE(textarea_selection) {
    TextArea ta;
    ta.setText(L"hello\nworld");

    ta.selectAll();
    CHECK(ta.hasSelection());
    CHECK(ta.getCaretLine() == 1);
    CHECK(ta.getCaretCol() == 5);

    ta.clearSelection();
    CHECK_FALSE(ta.hasSelection());
    CHECK(ta.getCaretLine() == 1);
    CHECK(ta.getCaretCol() == 5);

    ta.setSelection(0, 1, 0, 5);   // half-open [1,5) = "ello"
    CHECK(ta.hasSelection());
    CHECK(ta.getCaretLine() == 0);
    CHECK(ta.getCaretCol() == 5);

    // InsertChar with selection replaces the range.
    CHECK(ta.insertChar(L'X'));
    CHECK(ta.getText() == L"hX\nworld");
    CHECK(ta.getCaretLine() == 0);
    CHECK(ta.getCaretCol() == 2);
}

// C-10: setCaret clamps to valid ranges.
TEST_CASE(textarea_caret_clamps) {
    TextArea ta;
    ta.setText(L"a\nbb\nccc");

    ta.setCaret(99, 99);
    CHECK(ta.getCaretLine() == 2);
    CHECK(ta.getCaretCol() == 3);

    ta.setCaret(1, 99);    // line 1 has length 2
    CHECK(ta.getCaretLine() == 1);
    CHECK(ta.getCaretCol() == 2);

    ta.setCaret(-5, -5);
    CHECK(ta.getCaretLine() == 0);
    CHECK(ta.getCaretCol() == 0);
}

// C-10: readOnly blocks edits.
TEST_CASE(textarea_readonly) {
    TextArea ta;
    ta.setText(L"abc");
    ta.setReadOnly(true);

    CHECK_FALSE(ta.insertChar(L'x'));
    CHECK(ta.getText() == L"abc");

    CHECK_FALSE(ta.deleteLeft());
    CHECK(ta.getText() == L"abc");
}

// C-10: maxLength bounds total character count (combined across lines).
TEST_CASE(textarea_max_length) {
    TextArea ta;
    ta.setMaxLength(5);
    ta.setText(L"abc");
    CHECK(ta.insertChar(L'd'));
    CHECK(ta.insertChar(L'e'));
    CHECK_FALSE(ta.insertChar(L'f'));   // would push to 6
    CHECK(ta.getText() == L"abcde");
}

// C-10: setLineHeight triggers document re-size (longer document).
TEST_CASE(textarea_line_height_resizes_document) {
    TextArea ta;
    ta.setText(L"a\nb\nc\nd");
    ta.setSize(FVector2(200.0f, 200.0f));
    ta.setPosition(FVector2(0.0f, 0.0f));
    ta.performLayout();

    // The TextDocument is private to TextArea's .cpp. We can verify the
    // height bump indirectly through the ScrollView's contentSize (which
    // TextArea::syncDocumentSizeToContent pushes via setContentSize).
    ScrollView* sv = ta.getScrollView();
    CHECK_NOT_NULL(sv);
    const float h0 = sv->getContent()->getSize().y;
    CHECK_FLOAT_EQ(h0, 4.0f * TextArea::kDefaultLineHeight + 2.0f * TextArea::kPaddingY, 0.5f);

    ta.setLineHeight(36.0f);
    const float h1 = sv->getContent()->getSize().y;
    CHECK(h1 > h0);
    CHECK_FLOAT_EQ(h1, 4.0f * 36.0f + 2.0f * TextArea::kPaddingY, 0.5f);
}

// C-10: factory + serializer round-trip preserves text + readOnly +
// maxLength + lineHeight.
TEST_CASE(textarea_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("TextArea"));

    Widget* widget = factory.create("TextArea");
    CHECK_NOT_NULL(widget);
    TextArea* original = dynamic_cast<TextArea*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("ta_notes");
    original->setText(L"line one\nline two\nline three");
    original->setMaxLength(1000);
    original->setLineHeight(20.0f);
    original->setReadOnly(false);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"TextArea\"") != std::string::npos);
    CHECK(json.find("line one") != std::string::npos);
    CHECK(json.find("line three") != std::string::npos);
    CHECK(json.find("\\n") != std::string::npos || json.find("\"\\\\n\"") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    TextArea* restoredTa = dynamic_cast<TextArea*>(restored);
    CHECK_NOT_NULL(restoredTa);
    CHECK(restoredTa->getId() == "ta_notes");
    CHECK(restoredTa->getText() == L"line one\nline two\nline three");
    CHECK(restoredTa->getMaxLength() == 1000u);
    CHECK_FLOAT_EQ(restoredTa->getLineHeight(), 20.0f, 1e-5f);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// C-10: render emits at least the ScrollView's background + document rects.
TEST_CASE(textarea_render_emits_scrollview_and_document) {
    TextArea ta;
    ta.setText(L"hello\nworld");
    ta.setSize(FVector2(200.0f, 80.0f));
    ta.setPosition(FVector2(0.0f, 0.0f));
    ta.performLayout();

    MockRenderer renderer;
    ta.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    // ScrollView background + ScrollBar thumb + document selection rect
    // (none here) + caret rect (no focus) + line glyph rects. Just verify
    // the cascade reaches the document.
    CHECK(rectCount >= 2);
}

TEST_SUITE_END