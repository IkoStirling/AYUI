// =============================================================================
// AYUI invalidation + per-frame submission regression tests.
// =============================================================================
//
// UIRenderBackend owns a frame-local command buffer: beginFrame() clears all
// UiItems and bgfx transient submissions cannot be replayed implicitly. The
// retained Widget display list must therefore replay every visible widget's
// high-level commands every frame, even when onRender itself is skipped.
//
// Dirty markers remain useful as cache/damage invalidation metadata: setters
// mark the affected widget, invalidation propagates to ancestors, and render
// consumes the markers. They do NOT suppress per-frame submission until AYUI
// invalidate Widget-local display lists and future render-target layers.
// =============================================================================

#include "AYTest.h"
#include "AYUI/Widget.h"
#include "AYUI/TextLabel.h"
#include "AYUI/Button.h"
#include "AYUI/ScrollView.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/UIManager.h"

#include <cstddef>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_DirtyRect)

// -----------------------------------------------------------------------------
// Helper: a leaf widget that emits a single drawRect on every render. Lets us
// count renders via MockRenderer's draw-call list without depending on the
// internals of any concrete widget class (TextLabel / Button / ScrollView
// all already cover their own render paths in dedicated suites).
// -----------------------------------------------------------------------------
namespace {

struct PainterWidget : Widget {
    int renderCount = 0;
    void onRender(IRenderBackend& renderer) override {
        renderer.drawRect(getWorldBounds(),
            FVector4(1.0f, 1.0f, 1.0f, 1.0f));
        ++renderCount;
    }
};

size_t rectCount(MockRenderer& r) {
    // Only count drawRect calls (skip drawText, drawRoundedRect, ...) so
    // PainterWidget's per-render call surface is exactly one.
    size_t n = 0;
    for (const auto& c : r.getDrawCalls()) {
        if (c.type == MockRenderer::DrawCall::Rect) {
            ++n;
        }
    }
    return n;
}

} // namespace

// =============================================================================
// Core contract: direct render calls replay cached visible presentation.
// =============================================================================
TEST_CASE(DirtyRect_CleanWidgetReplaysDisplayListEveryFrame) {
    MockRenderer r;
    PainterWidget w;
    w.setSize(FVector2(100.0f, 50.0f));

    w.render(r);
    CHECK(rectCount(r) == 1u);
    CHECK(w.renderCount == 1);
    CHECK_FALSE(w.isDirtyThis());

    // The second call is clean and appends a replayed submission without
    // rerunning authored Widget code.
    w.render(r);
    CHECK(rectCount(r) == 2u);
    CHECK(w.renderCount == 1);

    w.render(r);
    w.render(r);
    w.render(r);
    CHECK(rectCount(r) == 5u);
    CHECK(w.renderCount == 1);
    CHECK_TRUE(w.hasCachedDisplayList());
    CHECK(w.getCachedDisplayCommandCount() == 1u);
}

// =============================================================================
// Static labels replay their retained commands after invalidation is consumed.
// =============================================================================
TEST_CASE(DirtyRect_StaticLabelReplaysWhenClean) {
    TextLabel label;
    label.setText(L"clicks: 0");
    label.setSize(FVector2(100.0f, 20.0f));

    MockRenderer r;
    label.render(r);
    const size_t after1 = r.getDrawCalls().size();
    CHECK(after1 >= 1u);
    CHECK_FALSE(label.isDirtyThis());
    CHECK_FALSE(label.hasDirtyRect());

    label.render(r);
    CHECK(r.getDrawCalls().size() > after1);
}

// =============================================================================
// Production-shaped regression: UIManager calls beginFrame(), which clears
// the backend command list. A clean static label must be regenerated into
// the second frame or it disappears after the back-buffer swap.
// =============================================================================
TEST_CASE(DirtyRect_UIManagerStaticLabelSurvivesFrameClear) {
    MockRenderer r;
    UIManager ui;
    ui.initialize(&r);
    ui.setClientSize(320.0f, 120.0f);
    CHECK(ui.loadFromString(
        R"({"type":"VBox","id":"static_root","size":{"w":320,"h":120},"children":[{"type":"TextLabel","id":"static_label","text":"persistent","size":{"w":160,"h":24}}]})"));

    ui.render();
    TextLabel* label = dynamic_cast<TextLabel*>(ui.findById("static_label"));
    CHECK_NOT_NULL(label);
    const size_t frame1 = r.getDrawCalls().size();
    CHECK(frame1 >= 1u);
    CHECK_FALSE(label->isDirtyThis());

    ui.render();
    const size_t frame2 = r.getDrawCalls().size();
    CHECK(frame2 >= 1u);
    CHECK(frame2 == frame1);
    CHECK_FALSE(label->isDirtyThis());

    ui.shutdown();
}

// =============================================================================
// Audit brief test #2: setText() must mark the widget dirty so the next
// render paints the new label.
// =============================================================================
TEST_CASE(DirtyRect_SetTextTriggersRerender) {
    TextLabel label;
    label.setText(L"clicks: 0");
    label.setSize(FVector2(100.0f, 20.0f));

    MockRenderer r;
    label.render(r);
    const size_t after1 = r.getDrawCalls().size();
    CHECK_FALSE(label.isDirtyThis());

    label.setText(L"clicks: 1");
    // setText must dirty the widget.
    CHECK_TRUE(label.isDirtyThis());

    label.render(r);
    // New draw calls happened (the new text glyph run).
    CHECK(r.getDrawCalls().size() > after1);
    // And the flag cleared again.
    CHECK_FALSE(label.isDirtyThis());
}

// =============================================================================
// Audit brief test #3: setVisible(true) must mark dirty so a hidden widget
// paints when revealed.
// =============================================================================
TEST_CASE(DirtyRect_VisibilityChangeTriggersRerender) {
    TextLabel label;
    label.setText(L"hi");
    label.setSize(FVector2(50.0f, 20.0f));

    MockRenderer r;

    // Hidden widgets never submit.
    label.setVisible(false);
    label.render(r);
    const size_t afterHide = r.getDrawCalls().size();

    // Show the widget -- setVisible(true) must mark dirty.
    label.setVisible(true);
    CHECK_TRUE(label.isDirtyThis());

    label.render(r);
    CHECK(r.getDrawCalls().size() > afterHide);
    CHECK_FALSE(label.isDirtyThis());

    // A clean visible widget still submits on the next frame/call.
    label.render(r);
    CHECK(r.getDrawCalls().size() > afterHide);
}

// =============================================================================
// Audit brief test #4: ScrollView -- when the scroll offset actually changes,
// the content widget must be repainted (it moved on screen).
// =============================================================================
TEST_CASE(DirtyRect_ScrollOffsetChangeTriggersRerender) {
    ScrollView sv;
    sv.setSize(FVector2(200.0f, 100.0f));
    auto* content = new Widget();
    content->setSize(FVector2(200.0f, 400.0f));
    sv.setContent(content);
    sv.setContentSize(FVector2(200.0f, 400.0f));
    sv.performLayout();

    MockRenderer r;
    sv.render(r);
    const size_t afterFrame1 = r.getDrawCalls().size();
    // First-frame paint happened -- content was rendered.
    CHECK(afterFrame1 >= 1u);
    // After the first render, content should be clean.
    CHECK_FALSE(content->isDirtyThis());

    // scrollBy to (0, 0) -> no-op on the offset, no markDirty.
    sv.scrollBy(FVector2(0.0f, 0.0f));
    CHECK_FALSE(content->isDirtyThis());

    // scrollBy to (0, 10) -> content moved, must be dirty.
    sv.scrollBy(FVector2(0.0f, 10.0f));
    CHECK_TRUE(content->isDirtyThis());

    sv.render(r);
    // New draw calls because content's position changed.
    CHECK(r.getDrawCalls().size() > afterFrame1);
    CHECK_FALSE(content->isDirtyThis());
}

// =============================================================================
// Button: setEnabled(false) -> state=Disabled -> must repaint to show the
// greyed fill. Exercises the InteractiveWidget hook.
// =============================================================================
TEST_CASE(DirtyRect_SetEnabledTriggersRerender) {
    Button btn;
    btn.setText(L"OK");
    btn.setSize(FVector2(80.0f, 32.0f));

    MockRenderer r;
    btn.render(r);
    const size_t after1 = r.getDrawCalls().size();
    CHECK_FALSE(btn.isDirtyThis());

    btn.setEnabled(false);
    // setEnabled must dirty because the rendered fill changes.
    CHECK_TRUE(btn.isDirtyThis());
    btn.render(r);
    CHECK(r.getDrawCalls().size() > after1);
}

// =============================================================================
// setOpacity(0.5) -> fills change -> must repaint this frame.
// =============================================================================
TEST_CASE(DirtyRect_SetOpacityTriggersRerender) {
    PainterWidget w;
    w.setSize(FVector2(20.0f, 20.0f));

    MockRenderer r;
    w.render(r);
    const size_t after1 = r.getDrawCalls().size();
    CHECK_FALSE(w.isDirtyThis());

    w.setOpacity(0.5f);
    CHECK_TRUE(w.isDirtyThis());
    w.render(r);
    CHECK(r.getDrawCalls().size() > after1);
}

// =============================================================================
// markDirty() propagation remains the invalidation contract for a future
// retained subtree cache.
// =============================================================================
TEST_CASE(DirtyRect_ChildDirtyPropagatesToParent) {
    auto* parent = new Widget();
    auto* child = new Widget();
    parent->addChild(child);
    parent->setSize(FVector2(100.0f, 100.0f));
    child->setSize(FVector2(50.0f, 50.0f));

    // Frame 1: both dirty, both render. Parent then becomes clean.
    MockRenderer r;
    parent->render(r);
    // After the first frame, parent should be clean.
    CHECK_FALSE(parent->isDirtyThis());

    // Now dirty the child. The markDirty() propagation must mark parent.
    child->markDirty();
    CHECK_TRUE(child->isDirtyThis());
    CHECK_TRUE(parent->isDirtyThis());

    delete parent;
}

// =============================================================================
// Union markDirty(rect): passing a valid rect should NOT set _dirtyThis.
// It records partial damage for a future retained cache.
// =============================================================================
TEST_CASE(DirtyRect_MarkDirtyWithRectSetsDirtyRect) {
    PainterWidget w;
    w.setSize(FVector2(100.0f, 50.0f));

    MockRenderer r;
    w.render(r);
    // After the first render, fully clean.
    CHECK_FALSE(w.isDirtyThis());
    CHECK_FALSE(w.hasDirtyRect());

    // markDirty with a non-empty rect: _dirtyRect becomes that rect, and
    // _dirtyThis stays false.
    w.markDirty(FRectangle(5.0f, 5.0f, 20.0f, 20.0f));
    CHECK_FALSE(w.isDirtyThis());
    CHECK_TRUE(w.hasDirtyRect());

    w.render(r);
    // Submission consumes the damage marker.
    CHECK_FALSE(w.hasDirtyRect());
}

TEST_CASE(DirtyRect_ExplicitDamagePropagatesWithoutBecomingFullTreeDirty) {
    PainterWidget parent;
    PainterWidget child;
    parent.setSize(FVector2(200.0f, 120.0f));
    child.setPosition(FVector2(20.0f, 30.0f));
    child.setSize(FVector2(60.0f, 40.0f));
    parent.addChildExternal(&child);

    MockRenderer renderer;
    parent.render(renderer);
    CHECK_FALSE(parent.isDirtyThis());
    CHECK_FALSE(parent.hasDirtyRect());

    const FRectangle first(24.0f, 34.0f, 40.0f, 50.0f);
    const FRectangle second(50.0f, 45.0f, 76.0f, 66.0f);
    child.markDirty(first);
    child.markDirty(second);

    CHECK_FALSE(parent.isDirtyThis());
    CHECK_TRUE(parent.hasDirtyRect());
    CHECK(parent.getDirtyRect().minX == 24.0f);
    CHECK(parent.getDirtyRect().minY == 34.0f);
    CHECK(parent.getDirtyRect().maxX == 76.0f);
    CHECK(parent.getDirtyRect().maxY == 66.0f);
}

TEST_SUITE_END
