// =============================================================================
// AYUI-DirtyRect-2026-08-26: per-widget paint-gate regression tests.
// =============================================================================
//
// Before this cut, populateFrame() walked every widget every frame. Static
// TextLabels ("clicks: 0"), idle Buttons, un-focused TextInputs all repainted
// 60 times a second. With 80-400 widgets per frame, that was 5k-24k wasted
// item submissions/sec.
//
// The dirty-rect system puts a short-circuit at the top of Widget::render:
// if both `_dirtyThis` is false AND `_dirtyRect` is empty (default
// ctor = zero area), the widget skips. Setters / animation ticks call
// markDirty() to re-arm. The contract:
//
//   - New widget: _dirtyThis = true (set by default member init) -> renders
//     once -> flag clears -> won't re-render until something marks dirty.
//   - markDirty(r): sets _dirtyThis=true; if r is non-empty, also unions
//     into _dirtyRect (partial damage, rarely used).
//   - render(): if !_visible || (!_dirtyThis && _dirtyRect.empty()) return;
//     at end of paint: _dirtyThis=false; _dirtyRect=empty.
//
// Tests assert the OBSERVABLE behavior: the same renderer instance's
// draw-call count must NOT grow when a static widget is re-rendered. The
// MockRenderer's getDrawCalls() list is the canonical "did this widget
// paint?" oracle for these tests.
// =============================================================================

#include "AYTest.h"
#include "AYUI/Widget.h"
#include "AYUI/TextLabel.h"
#include "AYUI/Button.h"
#include "AYUI/ScrollView.h"
#include "AYUI/MockRenderer.h"

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
// Core contract: a freshly-added widget renders ONCE on first frame, then
// stops repainting until something marks it dirty.
// =============================================================================
TEST_CASE(DirtyRect_FreshWidgetRendersOnceThenStops) {
    MockRenderer r;
    PainterWidget w;
    w.setSize(FVector2(100.0f, 50.0f));

    // Frame 1: _dirtyThis=true (from default member init) -> must render.
    w.render(r);
    CHECK(rectCount(r) == 1u);
    CHECK(w.renderCount == 1);

    // Frame 2: no mutations -> must short-circuit, zero new draw calls.
    w.render(r);
    CHECK(rectCount(r) == 1u);
    CHECK(w.renderCount == 1);

    // Frames 3-5: still clean -- accumulating frames doesn't grow the
    // draw-call list. This is the load-bearing perf assertion from the
    // audit: a static label doesn't repaint at 60 fps.
    w.render(r);
    w.render(r);
    w.render(r);
    CHECK(rectCount(r) == 1u);
    CHECK(w.renderCount == 1);
}

// =============================================================================
// Audit brief test #1: build a TextLabel, render once, render again without
// changes. The widget's _dirtyThis must clear after the first render so
// the second pass is a no-op.
// =============================================================================
TEST_CASE(DirtyRect_StaticLabelNoRerender) {
    TextLabel label;
    label.setText(L"clicks: 0");
    label.setSize(FVector2(100.0f, 20.0f));

    MockRenderer r;
    // Frame 1 -- fresh widget paints the text label.
    label.render(r);
    const size_t after1 = r.getDrawCalls().size();
    CHECK(after1 >= 1u);
    // _dirtyThis must have been cleared by the first render.
    CHECK_FALSE(label.isDirtyThis());
    CHECK_FALSE(label.hasDirtyRect());

    // Frame 2 -- no changes, must short-circuit (no new draw calls).
    label.render(r);
    CHECK(r.getDrawCalls().size() == after1);
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

    // Hide the widget -- the render short-circuit handles hiding for free.
    label.setVisible(false);
    label.render(r);
    const size_t afterHide = r.getDrawCalls().size();

    // Show the widget -- setVisible(true) must mark dirty.
    label.setVisible(true);
    CHECK_TRUE(label.isDirtyThis());

    label.render(r);
    CHECK(r.getDrawCalls().size() > afterHide);
    CHECK_FALSE(label.isDirtyThis());

    // Now render again -- clean, no new draws.
    label.render(r);
    CHECK(r.getDrawCalls().size() > afterHide); // unchanged from previous frame
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
// markDirty() propagation -- a child markDirty() must propagate to parent so
// the ancestor's render short-circuit doesn't block the descendant paint.
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
// Union markDirty(rect): passing a valid rect should NOT set _dirtyThis
// (partial damage only -- the rect alone is enough for the render to
// fire because hasDirtyRect() will be true). It MUST add the rect to
// _dirtyRect so the render short-circuit doesn't drop the call.
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
    // _dirtyThis stays false (per the implementation -- the rect alone is
    // enough for the render to fire because hasDirtyRect() will be true).
    w.markDirty(FRectangle(5.0f, 5.0f, 20.0f, 20.0f));
    CHECK_FALSE(w.isDirtyThis());
    CHECK_TRUE(w.hasDirtyRect());

    w.render(r);
    // Render fired (because hasDirtyRect was true); now clean again.
    CHECK_FALSE(w.hasDirtyRect());
}

TEST_SUITE_END