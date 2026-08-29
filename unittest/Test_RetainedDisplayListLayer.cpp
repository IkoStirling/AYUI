#include "AYTest.h"
#include "AYUI/DisplayList.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/UIManager.h"
#include "AYUI/Widget.h"

#include <array>
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

TEST_CASE(DamageRegions_PreserveDistantRectsMergeOverlapAndCapComplexity) {
    Widget parent;
    CountingPainter child;
    parent.setSize(FVector2(320.0f, 180.0f));
    child.setSize(FVector2(300.0f, 160.0f));
    parent.addChildExternal(&child);

    MockRenderer renderer;
    parent.render(renderer);
    CHECK_FALSE(parent.isDirtyThis());

    const FRectangle first(8.0f, 10.0f, 28.0f, 30.0f);
    const FRectangle second(220.0f, 100.0f, 250.0f, 130.0f);
    child.markDirty(first);
    child.markDirty(second);
    CHECK(parent.getDirtyRegions().size() == 2u);
    CHECK(parent.getDirtyRect().minX == first.minX);
    CHECK(parent.getDirtyRect().maxY == second.maxY);

    child.markDirty(FRectangle(20.0f, 20.0f, 36.0f, 38.0f));
    CHECK(parent.getDirtyRegions().size() == 2u);
    int mergedFirstRegions = 0;
    for (const FRectangle& region : parent.getDirtyRegions()) {
        if (region.minX == 8.0f && region.minY == 10.0f
            && region.maxX == 36.0f && region.maxY == 38.0f) {
            ++mergedFirstRegions;
        }
    }
    CHECK(mergedFirstRegions == 1);

    renderer.beginFrame();
    parent.render(renderer);
    constexpr float kSpacing = 24.0f;
    for (size_t i = 0; i < Widget::kMaxDamageRegions; ++i) {
        const float x = static_cast<float>(i) * kSpacing;
        child.markDirty(FRectangle(x, 60.0f, x + 8.0f, 68.0f));
    }
    CHECK(parent.getDirtyRegions().size() == Widget::kMaxDamageRegions);

    child.markDirty(FRectangle(220.0f, 60.0f, 228.0f, 68.0f));
    CHECK(parent.getDirtyRegions().size() == 1u);
    CHECK(parent.getDirtyRegions()[0].minX == 0.0f);
    CHECK(parent.getDirtyRegions()[0].maxX == 228.0f);
}

TEST_CASE(WidgetLayer_AlwaysCachesSubtreeAndUsesPartialDamage) {
    CountingPainter painter;
    painter.setSize(FVector2(160.0f, 80.0f));
    painter.setLayerCachePolicy(LayerCachePolicy::Always);
    MockRenderer renderer;

    renderer.beginFrame();
    painter.render(renderer);
    CHECK_TRUE(painter.hasActiveLayerCache());
    CHECK(painter.paintCount == 1);
    auto stats = renderer.getLayerCacheStats();
    CHECK(stats.fullPaints == 1u);
    CHECK(stats.composites == 1u);
    CHECK(stats.cacheHits == 0u);

    renderer.beginFrame();
    painter.render(renderer);
    CHECK(painter.paintCount == 1);
    CHECK(renderer.getDrawCalls().size() == 1u);
    CHECK(renderer.getDrawCalls()[0].type == MockRenderer::DrawCall::Layer);
    stats = renderer.getLayerCacheStats();
    CHECK(stats.composites == 2u);
    CHECK(stats.cacheHits == 1u);

    const FRectangle damage(12.0f, 14.0f, 42.0f, 34.0f);
    painter.markDirty(damage);
    renderer.beginFrame();
    painter.render(renderer);
    CHECK(painter.paintCount == 2);
    stats = renderer.getLayerCacheStats();
    CHECK(stats.partialPaints == 1u);
    CHECK(stats.repaintPixelArea == 160u * 80u + 30u * 20u);
    CHECK(renderer.isClipStackBalanced());

    painter.setLayerCachePolicy(LayerCachePolicy::Disabled);
    CHECK_FALSE(painter.hasActiveLayerCache());
    CHECK(renderer.getLayerCacheStats().layerReleases == 1u);
}

TEST_CASE(WidgetLayer_AutoPromotesStableComplexSubtreeAndDemotesChurn) {
    Widget parent;
    parent.setSize(FVector2(240.0f, 120.0f));
    parent.setLayerCachePolicy(LayerCachePolicy::Auto);
    std::array<CountingPainter, 12> children;
    for (size_t i = 0; i < children.size(); ++i) {
        children[i].setPosition(FVector2(
            static_cast<float>((i % 6u) * 36u),
            static_cast<float>((i / 6u) * 44u)));
        children[i].setSize(FVector2(28.0f, 28.0f));
        parent.addChildExternal(&children[i]);
    }

    MockRenderer renderer;
    for (int frame = 0; frame < 4; ++frame) {
        renderer.beginFrame();
        parent.render(renderer);
    }
    CHECK_TRUE(parent.hasActiveLayerCache());
    CHECK(renderer.getLayerCacheStats().layerCreates == 1u);

    for (int frame = 0; frame < 3; ++frame) {
        parent.markDirty();
        renderer.beginFrame();
        parent.render(renderer);
    }
    CHECK_FALSE(parent.hasActiveLayerCache());
    CHECK(renderer.getLayerCacheStats().layerReleases == 1u);
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

TEST_CASE(UIManager_ProductionRootLayerCachesMainTreeButKeepsOverlayImmediate) {
    MockRenderer renderer;
    UIManager ui;
    ui.initialize(&renderer);
    ui.setClientSize(320.0f, 180.0f);
    ui.setRootLayerCachingEnabled(true);

    auto* rootPainter = new CountingPainter();
    rootPainter->setSize(FVector2(120.0f, 80.0f));
    rootPainter->setDisplayListPolicy(DisplayListPolicy::Immediate);
    ui.root()->addChild(rootPainter);

    auto* overlayPainter = new CountingPainter();
    overlayPainter->setPosition(FVector2(16.0f, 12.0f));
    overlayPainter->setSize(FVector2(48.0f, 24.0f));
    overlayPainter->setDisplayListPolicy(DisplayListPolicy::Immediate);
    ui.getOverlayRoot()->addChild(overlayPainter);

    ui.render();
    CHECK(rootPainter->paintCount == 1);
    CHECK(overlayPainter->paintCount == 1);
    CHECK(renderer.getDrawCalls().size() == 3u);
    CHECK(renderer.getDrawCalls()[1].type == MockRenderer::DrawCall::Layer);

    int firstFramePaintEvents = 0;
    int firstFrameCompositeEvents = 0;
    for (const auto& event : renderer.getLayerEvents()) {
        if (event.type == MockRenderer::LayerEvent::PaintBegan) {
            ++firstFramePaintEvents;
        } else if (event.type == MockRenderer::LayerEvent::Composited) {
            ++firstFrameCompositeEvents;
        }
    }
    CHECK(firstFramePaintEvents == 1);
    CHECK(firstFrameCompositeEvents == 1);

    // A clean root is represented by one layer composite. The overlay is
    // deliberately outside that layer, so it keeps its per-frame path.
    ui.render();
    CHECK(rootPainter->paintCount == 1);
    CHECK(overlayPainter->paintCount == 2);
    CHECK(renderer.getDrawCalls().size() == 2u);
    CHECK(renderer.getDrawCalls()[0].type == MockRenderer::DrawCall::Layer);

    int cleanFramePaintEvents = 0;
    int cleanFrameCompositeEvents = 0;
    for (const auto& event : renderer.getLayerEvents()) {
        if (event.type == MockRenderer::LayerEvent::PaintBegan) {
            ++cleanFramePaintEvents;
        } else if (event.type == MockRenderer::LayerEvent::Composited) {
            ++cleanFrameCompositeEvents;
        }
    }
    CHECK(cleanFramePaintEvents == 0);
    CHECK(cleanFrameCompositeEvents == 1);

    // Explicit damage stays rectangular through the widget ancestry and
    // drives a Preserve-outside / clear-inside partial layer repaint.
    const FRectangle damage(8.0f, 10.0f, 42.0f, 36.0f);
    rootPainter->markDirty(damage);
    ui.render();
    CHECK(rootPainter->paintCount == 2);
    CHECK(overlayPainter->paintCount == 3);
    int partialPaints = 0;
    int partialInvalidations = 0;
    for (const auto& event : renderer.getLayerEvents()) {
        if (event.type == MockRenderer::LayerEvent::PaintBegan
            && !event.fullRedraw && event.damage.minX == damage.minX
            && event.damage.minY == damage.minY
            && event.damage.maxX == damage.maxX
            && event.damage.maxY == damage.maxY) {
            ++partialPaints;
        } else if (event.type == MockRenderer::LayerEvent::Invalidated
                   && !event.fullRedraw) {
            ++partialInvalidations;
        }
    }
    CHECK(partialPaints == 1);
    CHECK(partialInvalidations == 1);
    CHECK(renderer.isClipStackBalanced());

    // Distant invalidations stay independent all the way to the root layer.
    // This avoids repainting the large untouched rectangle between them.
    const FRectangle leftDamage(4.0f, 6.0f, 20.0f, 18.0f);
    const FRectangle rightDamage(88.0f, 54.0f, 112.0f, 72.0f);
    rootPainter->markDirty(leftDamage);
    rootPainter->markDirty(rightDamage);
    ui.render();
    int multiRegionPaints = 0;
    int matchingRegions = 0;
    for (const auto& event : renderer.getLayerEvents()) {
        if (event.type != MockRenderer::LayerEvent::PaintBegan
            || event.fullRedraw) {
            continue;
        }
        ++multiRegionPaints;
        if ((event.damage.minX == leftDamage.minX
             && event.damage.minY == leftDamage.minY
             && event.damage.maxX == leftDamage.maxX
             && event.damage.maxY == leftDamage.maxY)
            || (event.damage.minX == rightDamage.minX
                && event.damage.minY == rightDamage.minY
                && event.damage.maxX == rightDamage.maxX
                && event.damage.maxY == rightDamage.maxY)) {
            ++matchingRegions;
        }
    }
    CHECK(multiRegionPaints == 2);
    CHECK(matchingRegions == 2);
    CHECK(rootPainter->paintCount == 4);
    CHECK(overlayPainter->paintCount == 4);

    rootPainter->color = FVector4(0.1f, 0.7f, 0.3f, 1.0f);
    rootPainter->markDirty();
    ui.render();
    CHECK(rootPainter->paintCount == 5);
    CHECK(overlayPainter->paintCount == 5);

    // The feature remains opt-in and has an immediate-mode escape hatch.
    ui.setRootLayerCachingEnabled(false);
    ui.render();
    CHECK(rootPainter->paintCount == 6);
    CHECK(overlayPainter->paintCount == 6);
    CHECK(renderer.getDrawCalls().size() == 2u);
    CHECK(renderer.getDrawCalls()[0].type == MockRenderer::DrawCall::Rect);

    ui.shutdown();
}

TEST_SUITE_END
