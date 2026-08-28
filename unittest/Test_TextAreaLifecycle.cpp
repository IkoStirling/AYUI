#include "AYTest.h"
#include "AYUI/TextArea.h"
#include "AYUI/LayoutLoader.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/Widget.h"
#include <cstdio>

// =============================================================================
// AYUI-Audit-2026-08-26 Bug1: TextArea::~TextArea leaked the TextDocument.
//
// Background: TextArea::ensureChildrenCreated() builds `_scrollView` and
// `_document`, then hands `_document` to ScrollView::setContent(). That
// setter uses addChildExternal (host-lifetime semantics), not addChild.
// destroyWidgetTree() therefore skips `_document` and leaks it. The
// fix in Controls/AYTextArea.cpp `delete _document`s it from ~TextArea.
//
// These tests exercise the loader path (which is the production path
// that triggers the leak) and assert no crash + no exception. Under
// /fsanitize=address the build will additionally flag any remaining
// leak in the TextDocument allocation. Without ASan we at minimum
// catch the visible UAF when `_document` was double-deleted.
// =============================================================================

using namespace ayt::ui;

TEST_SUITE(AYUI_TextAreaLifecycle)

// Loader-path lifecycle: build a TextArea through UILayoutLoader, then
// destroyWidgetTree it. Pre-fix this left a TextDocument leaked; post-fix
// the TextDocument is reclaimed by ~TextArea.
TEST_CASE(textarea_loader_build_and_destroy_no_crash) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "TextArea",
        "id": "ta_lifecycle",
        "size": { "w": 200, "h": 100 },
        "text": "hello world"
    })";

    Widget* root = loader.loadFromString(json);
    CHECK_NOT_NULL(root);

    auto* ta = dynamic_cast<TextArea*>(root);
    CHECK_NOT_NULL(ta);
    CHECK_NOT_NULL(ta->getDocument());
    CHECK_NOT_NULL(ta->getScrollView());
    CHECK(ta->getText() == L"hello world");

    // destroyWidgetTree must not throw and must not UAF. Pre-fix this
    // exited cleanly but leaked the TextDocument; ASan would catch it.
    destroyWidgetTree(root);
}

// Stack-scoped lifecycle (no loader). TextArea is owned by stack scope;
// both _scrollView and _document leak under UI-OWN-1, but the audit
// specifically closed the destroyWidgetTree path — verify it does not
// crash on construction or destruction.
TEST_CASE(textarea_stack_build_and_destroy_no_crash) {
    TextArea ta;
    CHECK_NOT_NULL(ta.getDocument());
    CHECK_NOT_NULL(ta.getScrollView());
    ta.setText(L"stack scoped text");
    CHECK(ta.getText() == L"stack scoped text");
    // ~TextArea runs at end of scope. Pre-fix would have double-faulted
    // here on systems that clear freed memory; ASan would have caught
    // the dangling _document reference during ~Widget's child walk.
}

// Multiple-loaders-back-to-back stress: verify the _document delete path
// is idempotent across many loader instances. Catches any "delete twice"
// pathology where ~TextArea fires twice (e.g. via re-entrant dtor chain).
TEST_CASE(textarea_repeated_load_and_destroy) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "TextArea",
        "id": "ta_repeat",
        "size": { "w": 100, "h": 50 }
    })";

    int nullRootCount = 0;
    int wrongTypeCount = 0;
    for (int i = 0; i < 5; ++i) {
        Widget* root = loader.loadFromString(json);
        if (root == nullptr) {
            ++nullRootCount;
            continue;
        }
        auto* ta = dynamic_cast<TextArea*>(root);
        if (ta == nullptr) {
            ++wrongTypeCount;
        } else {
            ta->setText(L"iteration");
        }
        destroyWidgetTree(root);
    }
    CHECK(nullRootCount == 0);
    CHECK(wrongTypeCount == 0);
}

TEST_SUITE_END
