#include "AYTest.h"
#include "AYUI/DisplayList.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Widget.h"

#include <cstdint>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_RetainedDisplayListLayer)

namespace {

struct CountingPainter : Widget {
    int paintCount = 0;
    FVector4 color = FVector4(0.8f, 0.4f, 0.2f, 1.0f);

    void onRender(IRenderBackend& renderer) override {
        ++paintCount;
        renderer.drawRect(getWorldBounds(), color);
    }
};

struct ClipContainer : Widget {
    void renderChildren(IRenderBackend& renderer) override {
        renderer.pushClip(getWorldBounds());
        Widget::renderChildren(renderer);
        renderer.popClip();
    }
};

struct RetainedPathPainter : Widget {
    int paintCount = 0;

    void onRender(IRenderBackend& renderer) override {
        ++paintCount;
        const auto path = renderer.createPath();
        renderer.addPathRect(path, getWorldBounds());
        renderer.setPathFillColor(path, FVector4(0, 1, 0, 1));
        renderer.drawPath(path, PathFillMode::Fill);
        renderer.pushPathClip(path);
        renderer.drawRect(getWorldBounds(), FVector4(1, 0, 0, 1));
        renderer.popClip();
        renderer.releasePath(path);
    }
};

struct RichCommandPainter : Widget {
    int paintCount = 0;
    void* texture = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234));

    void onRender(IRenderBackend& renderer) override {
        ++paintCount;
        const FRectangle b = getWorldBounds();
        renderer.setBlendMode(BlendMode::Multiply);
        renderer.pushClip(b);
        renderer.drawGradientRect(
            b, FVector4(1, 0, 0, 1), FVector4(0, 1, 0, 1),
            FVector4(0, 0, 1, 1), FVector4(1, 1, 1, 1));
        renderer.drawRoundedRect(b, FVector4(0.2f, 0.3f, 0.4f, 1),
                                 IRenderBackend::CornerRadii(1, 2, 3, 4));
        IRenderBackend::TextStyle textStyle;
        textStyle.color = FVector4(0.7f, 0.8f, 0.9f, 1);
        textStyle.wrapToBounds = true;
        textStyle.align = IRenderBackend::TextStyle::Align::Center;
        renderer.drawText(b, L"retained", 16, textStyle);
        renderer.drawNinePatch(b, texture, FRectangle(0, 0, 1, 1),
                               FVector4(2, 2, 2, 2));
        renderer.popClip();
        renderer.setBlendMode(BlendMode::Normal);
    }
};

struct NestedChromeContainer : Widget {
    Widget* nested = nullptr;
    int paintCount = 0;

    void onRender(IRenderBackend& renderer) override {
        ++paintCount;
        const FRectangle bounds = getWorldBounds();
        renderer.drawRect(bounds, FVector4(0.1f, 0.1f, 0.1f, 1));
        renderer.pushClip(bounds);
        if (nested != nullptr) {
            nested->render(renderer);
        }
        renderer.popClip();
        renderer.drawBorderRect(bounds, FVector4(1, 1, 1, 1), 1.0f);
    }

    void renderChildren(IRenderBackend&) override {}
};

} // namespace

TEST_CASE(DisplayList_ComplexTreeReplaysClipOpacityAndInvalidatesWorldBounds) {
    ClipContainer parent;
    CountingPainter child;
    parent.setPosition(FVector2(10, 20));
    parent.setSize(FVector2(120, 80));
    parent.setOpacity(0.5f);
    child.setPosition(FVector2(5, 7));
    child.setSize(FVector2(30, 20));
    child.setOpacity(0.5f);
    parent.addChildExternal(&child);

    MockRenderer renderer;
    parent.render(renderer);
    CHECK(child.paintCount == 1);
    CHECK(renderer.getDrawCalls().size() == 1u);
    CHECK(renderer.getDrawCalls()[0].color.w == 0.25f);
    CHECK(renderer.getDrawCalls()[0].bounds.minX == 15.0f);
    CHECK(renderer.getDrawCalls()[0].bounds.minY == 27.0f);
    CHECK(renderer.isClipStackBalanced());

    renderer.beginFrame();
    parent.render(renderer);
    CHECK(child.paintCount == 1);
    CHECK(renderer.getDrawCalls().size() == 1u);
    CHECK(renderer.getDrawCalls()[0].color.w == 0.25f);
    CHECK(renderer.isClipStackBalanced());

    // Parent opacity is evaluated around the child replay; the child list
    // itself does not need rebuilding.
    parent.setOpacity(0.25f);
    renderer.beginFrame();
    parent.render(renderer);
    CHECK(child.paintCount == 1);
    CHECK(renderer.getDrawCalls()[0].color.w == 0.125f);

    // Commands currently store world-space geometry, so moving an ancestor
    // must invalidate every descendant list before the next replay.
    parent.setPosition(FVector2(40, 50));
    renderer.beginFrame();
    parent.render(renderer);
    CHECK(child.paintCount == 2);
    CHECK(renderer.getDrawCalls()[0].bounds.minX == 45.0f);
    CHECK(renderer.getDrawCalls()[0].bounds.minY == 57.0f);
    CHECK(renderer.isClipStackBalanced());
}

TEST_CASE(DisplayList_ExplicitImmediatePolicyPreservesLegacyPath) {
    CountingPainter painter;
    painter.setDisplayListPolicy(DisplayListPolicy::Immediate);
    MockRenderer renderer;

    painter.render(renderer);
    painter.render(renderer);
    CHECK(painter.paintCount == 2);
    CHECK_FALSE(painter.hasCachedDisplayList());
    CHECK(renderer.getDrawCalls().size() == 2u);
}

TEST_CASE(DisplayList_NestedChildRenderStaysDynamicInsideCachedParentOrder) {
    NestedChromeContainer parent;
    CountingPainter child;
    parent.setSize(FVector2(100, 60));
    child.setPosition(FVector2(10, 12));
    child.setSize(FVector2(20, 16));
    parent.nested = &child;
    parent.addChildExternal(&child);

    MockRenderer renderer;
    parent.render(renderer);
    CHECK(parent.paintCount == 1);
    CHECK(child.paintCount == 1);
    CHECK_TRUE(parent.hasCachedDisplayList());
    CHECK_TRUE(child.hasCachedDisplayList());
    const size_t fullDrawCount = renderer.getDrawCalls().size();
    CHECK(fullDrawCount >= 3u);
    CHECK(renderer.getDrawCalls()[1].color == child.color);
    CHECK(renderer.isClipStackBalanced());

    // A child-only presentation change must rebuild the child while the
    // parent's background/content/chrome command order remains cached.
    child.color = FVector4(0.2f, 0.7f, 0.3f, 1.0f);
    child.markDirty();
    renderer.beginFrame();
    parent.render(renderer);
    CHECK(parent.paintCount == 1);
    CHECK(child.paintCount == 2);
    CHECK(renderer.getDrawCalls().size() == fullDrawCount);
    CHECK(renderer.getDrawCalls()[1].color == child.color);
    CHECK(renderer.isClipStackBalanced());

    child.setVisible(false);
    renderer.beginFrame();
    parent.render(renderer);
    CHECK(parent.paintCount == 1);
    CHECK(child.paintCount == 2);
    CHECK(renderer.getDrawCalls().size() + 1u == fullDrawCount);

    child.setVisible(true);
    renderer.beginFrame();
    parent.render(renderer);
    CHECK(parent.paintCount == 1);
    CHECK(child.paintCount == 3);
    CHECK(renderer.getDrawCalls().size() == fullDrawCount);
    CHECK(renderer.getDrawCalls()[1].color == child.color);
}

TEST_CASE(DisplayList_VectorPathsReplayFromBackendIndependentRecipes) {
    RetainedPathPainter painter;
    MockRenderer renderer;

    painter.render(renderer);
    CHECK(painter.paintCount == 1);
    CHECK(painter.hasCachedDisplayList());
    CHECK(painter.getCachedDisplayCommandCount() == 4u);
    CHECK(renderer.getDrawCalls().size() == 2u);

    renderer.beginFrame();
    painter.render(renderer);
    CHECK(painter.paintCount == 1);
    CHECK(painter.hasCachedDisplayList());
    CHECK(renderer.getDrawCalls().size() == 2u);
    CHECK(renderer.isClipStackBalanced());
}

TEST_CASE(DisplayList_ReplayPreservesOptimizedHighLevelCommands) {
    RichCommandPainter painter;
    painter.setSize(FVector2(80, 32));
    MockRenderer renderer;

    painter.render(renderer);
    const auto first = renderer.getDrawCalls();
    const size_t commandCount = painter.getCachedDisplayCommandCount();
    CHECK(painter.paintCount == 1);
    CHECK_TRUE(painter.hasCachedDisplayList());
    CHECK(commandCount == 8u);

    renderer.beginFrame();
    painter.render(renderer);
    const auto& replay = renderer.getDrawCalls();
    CHECK(painter.paintCount == 1);
    CHECK(replay.size() == first.size());

    int mismatches = 0;
    for (size_t i = 0; i < first.size(); ++i) {
        const auto& a = first[i];
        const auto& b = replay[i];
        if (a.type != b.type || a.bounds.minX != b.bounds.minX
            || a.bounds.minY != b.bounds.minY || a.bounds.maxX != b.bounds.maxX
            || a.bounds.maxY != b.bounds.maxY || a.color != b.color
            || a.texture != b.texture || a.text != b.text
            || a.blendMode != b.blendMode) {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
    CHECK(renderer.isClipStackBalanced());
}

TEST_CASE(UILayer_MockLifecycleTracksDpiDamageAndComposite) {
    MockRenderer renderer;
    CHECK_TRUE(renderer.supportsRenderTargets());

    IRenderBackend::LayerDesc desc;
    desc.logicalBounds = FRectangle(10, 20, 110, 70);
    desc.dpiScale = 2.0f;
    desc.overlay = true;
    desc.clearMode = IRenderBackend::LayerClearMode::Preserve;
    const auto layer = renderer.createLayer(desc);
    CHECK_TRUE(layer.isValid());
    CHECK_TRUE(renderer.isLayerDirty(layer));

    const auto target = renderer.getLayerRenderTarget(layer);
    const auto targetDesc = renderer.getRenderTargetDesc(target);
    CHECK_TRUE(target.isValid());
    CHECK(targetDesc.width == 200);
    CHECK(targetDesc.height == 100);
    CHECK(targetDesc.dpiScale == 2.0f);
    CHECK_TRUE(targetDesc.preserveContents);

    // Dirty layers cannot be composited until a successful paint closes.
    renderer.compositeLayer(layer, desc.logicalBounds);
    CHECK(renderer.getDrawCalls().empty());

    IRenderBackend::LayerPaint fullPaint;
    fullPaint.fullRedraw = true;
    CHECK_TRUE(renderer.beginLayerPaint(layer, fullPaint));
    CHECK_FALSE(renderer.beginLayerPaint(layer, fullPaint));
    renderer.drawRect(desc.logicalBounds, FVector4(1, 1, 1, 1));
    renderer.endLayerPaint(layer);
    CHECK_FALSE(renderer.isLayerDirty(layer));

    renderer.compositeLayer(layer, desc.logicalBounds, 0.5f);
    CHECK(renderer.getDrawCalls().size() == 2u);
    CHECK(renderer.getDrawCalls().back().type == MockRenderer::DrawCall::Layer);
    CHECK(renderer.getDrawCalls().back().color.w == 0.5f);

    renderer.invalidateLayer(layer, FRectangle(20, 25, 40, 45));
    CHECK_TRUE(renderer.isLayerDirty(layer));
    const size_t beforeBlockedComposite = renderer.getDrawCalls().size();
    renderer.compositeLayer(layer, desc.logicalBounds);
    CHECK(renderer.getDrawCalls().size() == beforeBlockedComposite);

    IRenderBackend::LayerPaint partialPaint;
    partialPaint.damage = FRectangle(20, 25, 40, 45);
    partialPaint.fullRedraw = false;
    CHECK_TRUE(renderer.beginLayerPaint(layer, partialPaint));
    renderer.endLayerPaint(layer);

    desc.logicalBounds = FRectangle(0, 0, 50, 30);
    desc.dpiScale = 1.5f;
    CHECK_TRUE(renderer.updateLayer(layer, desc));
    const auto resizedDesc = renderer.getRenderTargetDesc(target);
    CHECK(resizedDesc.width == 75);
    CHECK(resizedDesc.height == 45);
    CHECK_TRUE(renderer.isLayerDirty(layer));

    renderer.releaseLayer(layer);
    CHECK_FALSE(renderer.getLayerRenderTarget(layer).isValid());
}

TEST_SUITE_END
