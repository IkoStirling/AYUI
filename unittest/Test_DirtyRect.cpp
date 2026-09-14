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

    // Audit B-NEW-2 / M-R-9: markDirty(rect) accepts a rect in the
    // widget's LOCAL paint frame. Propagation translates by
    // child._position so the same region lands in the parent's local
    // frame. With child at (20, 30), the parent's stored rect should
    // equal the child-local rect shifted by (20, 30).
    const FRectangle firstChildLocal(4.0f, 4.0f, 20.0f, 20.0f);
    const FRectangle secondChildLocal(30.0f, 15.0f, 56.0f, 36.0f);
    child.markDirty(firstChildLocal);
    child.markDirty(secondChildLocal);

    CHECK_FALSE(parent.isDirtyThis());
    CHECK_TRUE(parent.hasDirtyRect());
    CHECK(parent.getDirtyRect().minX == 24.0f);
    CHECK(parent.getDirtyRect().minY == 34.0f);
    CHECK(parent.getDirtyRect().maxX == 76.0f);
    CHECK(parent.getDirtyRect().maxY == 66.0f);
}

// Audit B-NEW-2 / M-R-9 regression: when the parent itself is offset
// inside its own parent (i.e. grandparent → parent → child, every link
// with a non-zero local position), explicit damage expressed in the
// child's local paint frame must be translated at every hop so the
// rect lands in the grandparent's local frame in the right place.
//
// Pre-fix behaviour: the rect was propagated unchanged, so a rect that
// was correct in child-local became wrong in grandparent-local once any
// ancestor had a non-zero position. The retained layer cache then
// repainted the wrong region of the root.
TEST_CASE(DirtyRect_OffsetParentAndChildTranslateDamageThroughChain) {
    PainterWidget grandparent;
    grandparent.setSize(FVector2(400.0f, 300.0f));
    grandparent.setPosition(FVector2(100.0f, 50.0f)); // origin inside root

    PainterWidget parent;
    parent.setSize(FVector2(200.0f, 150.0f));
    parent.setPosition(FVector2(40.0f, 25.0f)); // inside grandparent

    PainterWidget child;
    child.setSize(FVector2(80.0f, 60.0f));
    child.setPosition(FVector2(10.0f, 8.0f)); // inside parent

    grandparent.addChildExternal(&parent);
    parent.addChildExternal(&child);

    MockRenderer renderer;
    grandparent.render(renderer);
    CHECK_FALSE(grandparent.isDirtyThis());
    CHECK_FALSE(parent.isDirtyThis());
    CHECK_FALSE(child.isDirtyThis());

    // Damage in CHILD local paint frame, e.g. text cursor region.
    const FRectangle childLocal(4.0f, 6.0f, 32.0f, 28.0f);
    child.markDirty(childLocal);

    // Parent sees the rect translated by child's position (10, 8).
    CHECK_FALSE(parent.isDirtyThis());
    CHECK_TRUE(parent.hasDirtyRect());
    CHECK(parent.getDirtyRect().minX == 14.0f);
    CHECK(parent.getDirtyRect().minY == 14.0f);
    CHECK(parent.getDirtyRect().maxX == 42.0f);
    CHECK(parent.getDirtyRect().maxY == 36.0f);

    // Grandparent sees the rect translated by (child._position +
    // parent._position) = (10+40, 8+25) = (50, 33), expressed in
    // grandparent's own local frame.
    CHECK_FALSE(grandparent.isDirtyThis());
    CHECK_TRUE(grandparent.hasDirtyRect());
    CHECK(grandparent.getDirtyRect().minX == 54.0f);
    CHECK(grandparent.getDirtyRect().minY == 39.0f);
    CHECK(grandparent.getDirtyRect().maxX == 82.0f);
    CHECK(grandparent.getDirtyRect().maxY == 61.0f);

    // A second markDirty on the same child union-merges correctly into
    // the parent's accumulated rect (no double-translation).
    const FRectangle secondChildLocal(40.0f, 30.0f, 70.0f, 50.0f);
    child.markDirty(secondChildLocal);
    CHECK(parent.getDirtyRect().minX == 14.0f);
    CHECK(parent.getDirtyRect().minY == 14.0f);
    // The second child-local rect (40,30,70,50) becomes (50,38,80,58)
    // in parent local. Union with the first parent-local rect
    // (14,14,42,36) yields min(14,50)=14, min(14,38)=14,
    // max(42,80)=80, max(36,58)=58.
    CHECK(parent.getDirtyRect().maxX == 80.0f);
    CHECK(parent.getDirtyRect().maxY == 58.0f);
}

TEST_SUITE_END
