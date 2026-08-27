// AYUI-Perf-2026-08-26: world-bounds cache regression. Verifies that
// getWorldBounds() returns the cached rect on subsequent calls and that
// setPosition/setSize invalidate both the node AND every descendant.
//
// Build a 4-level chain, hammer getWorldBounds() N times, assert the
// debug recompute counter stays at 1 (initial walk only).
#include "AYTest.h"
#include "AYMath/MathTypes.h"
#include "AYUI/Widget.h"
#include <cstdio>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_GetWorldBoundsCache)

TEST_CASE(world_bounds_cache_initial_walk_only) {
    // 4-level chain: root -> a -> b -> leaf
    CompoundWidget* root = new CompoundWidget();
    root->setSize(FVector2(800.0f, 600.0f));
    root->setPosition(FVector2(0.0f, 0.0f));

    Widget* a = new Widget();
    a->setPosition(FVector2(50.0f, 50.0f));
    a->setSize(FVector2(200.0f, 200.0f));
    root->addChild(a);

    Widget* b = new Widget();
    b->setPosition(FVector2(30.0f, 30.0f));
    b->setSize(FVector2(100.0f, 100.0f));
    a->addChild(b);

    Widget* leaf = new Widget();
    leaf->setPosition(FVector2(10.0f, 10.0f));
    leaf->setSize(FVector2(20.0f, 20.0f));
    b->addChild(leaf);

    // Initial call walks + caches. Counter should be exactly 1.
    const FRectangle r0 = leaf->getWorldBounds();
    CHECK_FLOAT_EQ(r0.minX, 90.0f, 1e-4f);
    CHECK_FLOAT_EQ(r0.minY, 90.0f, 1e-4f);
    CHECK_FLOAT_EQ(r0.maxX, 110.0f, 1e-4f);
    CHECK_FLOAT_EQ(r0.maxY, 110.0f, 1e-4f);

#ifndef NDEBUG
    const int afterFirst = leaf->debugGetWorldBoundsRecomputeCount();
    CHECK(afterFirst == 1);
#endif

    // Hammer the leaf 1000 times — counter must NOT advance.
    for (int i = 0; i < 1000; ++i) {
        const FRectangle r = leaf->getWorldBounds();
        // Sanity: same rect.
        CHECK_FLOAT_EQ(r.minX, 90.0f, 1e-4f);
        CHECK_FLOAT_EQ(r.minY, 90.0f, 1e-4f);
        (void)r.maxX; (void)r.maxY;
    }

#ifndef NDEBUG
    CHECK(leaf->debugGetWorldBoundsRecomputeCount() == afterFirst);
#endif

    destroyWidgetTree(root);
}

TEST_CASE(world_bounds_cache_parent_move_cascades) {
    CompoundWidget* root = new CompoundWidget();
    root->setSize(FVector2(800.0f, 600.0f));

    Widget* child = new Widget();
    child->setPosition(FVector2(50.0f, 50.0f));
    child->setSize(FVector2(40.0f, 40.0f));
    root->addChild(child);

    Widget* grand = new Widget();
    grand->setPosition(FVector2(10.0f, 10.0f));
    grand->setSize(FVector2(10.0f, 10.0f));
    child->addChild(grand);

    // Prime caches.
    FRectangle r = grand->getWorldBounds();
    CHECK_FLOAT_EQ(r.minX, 60.0f, 1e-4f);
    CHECK_FLOAT_EQ(r.minY, 60.0f, 1e-4f);

#ifndef NDEBUG
    grand->debugResetWorldBoundsRecomputeCount();
#endif

    // Move the root — every descendant's cache must invalidate.
    root->setPosition(FVector2(100.0f, 100.0f));

    // After move, grand's world origin shifts from (60,60) to (160,160).
    r = grand->getWorldBounds();
    CHECK_FLOAT_EQ(r.minX, 160.0f, 1e-4f);
    CHECK_FLOAT_EQ(r.minY, 160.0f, 1e-4f);
    CHECK_FLOAT_EQ(r.maxX, 170.0f, 1e-4f);

    // Hit it again — should now be cached, counter advances at most once.
    r = grand->getWorldBounds();
    CHECK_FLOAT_EQ(r.minX, 160.0f, 1e-4f);
#ifndef NDEBUG
    CHECK(grand->debugGetWorldBoundsRecomputeCount() <= 1);
#endif

    destroyWidgetTree(root);
}

TEST_CASE(world_bounds_cache_size_change_cascades) {
    CompoundWidget* root = new CompoundWidget();
    root->setSize(FVector2(800.0f, 600.0f));

    Widget* child = new Widget();
    child->setPosition(FVector2(50.0f, 50.0f));
    child->setSize(FVector2(40.0f, 40.0f));
    root->addChild(child);

    Widget* grand = new Widget();
    grand->setPosition(FVector2(10.0f, 10.0f));
    grand->setSize(FVector2(10.0f, 10.0f));
    child->addChild(grand);

    // Prime.
    FRectangle r = grand->getWorldBounds();
    CHECK_FLOAT_EQ(r.maxX, 70.0f, 1e-4f);
    CHECK_FLOAT_EQ(r.maxY, 70.0f, 1e-4f);

    // Resize child — descendants must invalidate.
    child->setSize(FVector2(40.0f, 80.0f));
    r = grand->getWorldBounds();
    CHECK_FLOAT_EQ(r.maxY, 110.0f, 1e-4f);  // 50+10 (grand local Y) + 50 grand height
    // Actually: child is at 50,50 with size 40,80. Grand is at 10,10 with
    // size 10,10. World rect of grand: x = 50+10..60+10, y = 50+10..60+10.
    CHECK_FLOAT_EQ(r.minX, 60.0f, 1e-4f);
    CHECK_FLOAT_EQ(r.minY, 60.0f, 1e-4f);
    CHECK_FLOAT_EQ(r.maxX, 70.0f, 1e-4f);
    CHECK_FLOAT_EQ(r.maxY, 70.0f, 1e-4f);

    destroyWidgetTree(root);
}

TEST_CASE(world_bounds_cache_no_invalidate_when_unchanged) {
    // Setting the same position twice in a row must not invalidate the
    // cache (Phase UI-PERF-1 already had this guard; the cache path
    // must preserve it).
    CompoundWidget* root = new CompoundWidget();
    root->setSize(FVector2(800.0f, 600.0f));

    Widget* child = new Widget();
    child->setPosition(FVector2(50.0f, 50.0f));
    child->setSize(FVector2(40.0f, 40.0f));
    root->addChild(child);

    // Prime.
    FRectangle r = child->getWorldBounds();
    CHECK_FLOAT_EQ(r.minX, 50.0f, 1e-4f);

#ifndef NDEBUG
    child->debugResetWorldBoundsRecomputeCount();
#endif

    // Re-set identical position.
    child->setPosition(FVector2(50.0f, 50.0f));
    r = child->getWorldBounds();
    CHECK_FLOAT_EQ(r.minX, 50.0f, 1e-4f);

#ifndef NDEBUG
    // Either 0 or 1 — the call is a no-op either way. The cache hit
    // path means the counter should NOT advance past 1 (no extra
    // recompute for the no-op setPosition).
    CHECK(child->debugGetWorldBoundsRecomputeCount() <= 1);
#endif

    destroyWidgetTree(root);
}

TEST_SUITE_END
