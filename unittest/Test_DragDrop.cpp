#include "AYTest.h"
#include "AYUI/UIManager.h"
#include "AYUI/DragDrop.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/InteractiveWidget.h"
#include "AYUI/UIKeyCode.h"

using namespace ayt::ui;
using namespace ayt::math;

// =============================================================================
// G12 — Drag & Drop API tests.
//
// Test fixture convention (per Test_UIManager.cpp idiom):
//   MockRenderer backend; UIManager ui; ui.initialize(&backend);
//   setClientSize + layout; mouse event sequences via the public API;
//   ui.shutdown().
//
// For drag tests we don't need JSON — we use stack-allocated widgets
// wired into a CompoundWidget root attached to the manager's _root
// (so CompoundWidget::hitTest can descend into captor / source /
// target). The drag source / target must have a parent (beginDrag
// rejects orphans).
// =============================================================================

TEST_SUITE(AYUI_DragDrop)

// Helper: UIManager's _root is a plain Widget — for hit-test descent we
// need widgets attached as siblings of the overlay root, or we wrap the
// test in a CompoundWidget root inserted as a child. Test fixture:
// attach captor / source / target directly to _overlayRoot (which is
// also a plain Widget but lives at the same level). Actually simpler:
// use _root directly with addChildExternal — even though it's plain
// Widget, child widgets' hitTest is called via the pickTopmostWidget's
// child-iteration (the descender at line 41 of AYWidget.cpp walks
// _children on every node, regardless of leaf/compound).
//
// Actually checking: pickTopmostWidget walks _overlayRoot's children
// then falls back to _root which is a plain Widget — plain Widget's
// hitTest only checks self bounds, NOT descend. So to get descent we
// must either (a) wrap our test widgets in a CompoundWidget that becomes
// _root, or (b) attach them as overlay children (overlay path manually
// descends via pickWidgetAt).
//
// We choose (b) — attach test widgets directly to _overlayRoot. This
// mirrors how ComboBox popups + Modal land in the overlay and how
// pickTopmostWidget descends into them.

// -----------------------------------------------------------------------------
// 1. beginDrag creates a session
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_begin_drag_creates_session) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(80.0f, 24.0f));
    source.setPosition(FVector2(10.0f, 10.0f));
    DragPayload p;
    p.kind = "FilePath";
    p.text = L"C:/foo.txt";
    source.setDragPayload(p);
    bool dragStarted = false;
    source.setOnDragStart([&dragStarted]() { dragStarted = true; });
    ui.getOverlayRoot()->addChildExternal(&source);

    CHECK_FALSE(ui.isDragging());
    CHECK(ui.beginDrag(&source));
    CHECK(ui.isDragging());
    CHECK(ui.getDragSource() == &source);
    CHECK(ui.getDragPayload().kind == "FilePath");
    CHECK(dragStarted);

    ui.cancelDrag();
    CHECK_FALSE(ui.isDragging());
    ui.shutdown();
}

// -----------------------------------------------------------------------------
// 2. beginDrag rejects when _capturedWidget is non-null
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_begin_drag_rejects_when_captured) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    // InteractiveWidget onMouseButtonDown returns true → UIManager sets
    // _capturedWidget. While captured, beginDrag for a different widget
    // must return false so the two channels don't collide.
    InteractiveWidget captor;
    captor.setSize(FVector2(50.0f, 30.0f));
    captor.setPosition(FVector2(10.0f, 10.0f));
    ui.getOverlayRoot()->addChildExternal(&captor);

    CHECK(ui.onMouseButtonDown(20.0f, 20.0f, 0));
    CHECK(ui.isCapturing());

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(50.0f, 30.0f));
    source.setPosition(FVector2(200.0f, 100.0f));
    ui.getOverlayRoot()->addChildExternal(&source);

    CHECK_FALSE(ui.beginDrag(&source));
    CHECK_FALSE(ui.isDragging());

    ui.onMouseButtonUp(20.0f, 20.0f, 0);
    ui.shutdown();
}

// -----------------------------------------------------------------------------
// 3. updateDrag finds the nearest accepting ancestor
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_update_drag_finds_drop_target) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(40.0f, 40.0f));
    source.setPosition(FVector2(0.0f, 0.0f));
    DragPayload p;
    p.kind = "item";
    p.text = L"item-7";
    source.setDragPayload(p);
    ui.getOverlayRoot()->addChildExternal(&source);

    Widget target;
    target.setAcceptDrops(true);
    target.setSize(FVector2(100.0f, 50.0f));
    target.setPosition(FVector2(200.0f, 200.0f));
    ui.getOverlayRoot()->addChildExternal(&target);

    CHECK(ui.beginDrag(&source));
    ui.updateDrag(250.0f, 220.0f);
    CHECK(ui.getCurrentDropTarget() == &target);
    CHECK(target.isCurrentDropTarget());

    ui.updateDrag(100.0f, 100.0f);   // off target
    CHECK(ui.getCurrentDropTarget() == nullptr);
    CHECK_FALSE(target.isCurrentDropTarget());

    ui.cancelDrag();
    ui.shutdown();
}

// -----------------------------------------------------------------------------
// 4. onDragEnter / onDragLeave fire on target transition
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_drag_enter_leave_fires_on_target_change) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(40.0f, 40.0f));
    source.setPosition(FVector2(0.0f, 0.0f));
    DragPayload p;
    p.kind = "item";
    p.text = L"hello";
    source.setDragPayload(p);
    ui.getOverlayRoot()->addChildExternal(&source);

    Widget target;
    target.setAcceptDrops(true);
    target.setSize(FVector2(80.0f, 60.0f));
    target.setPosition(FVector2(200.0f, 100.0f));
    int enterCount = 0;
    int leaveCount = 0;
    target.setOnDragEnter([&enterCount](const DragPayload&) { ++enterCount; });
    target.setOnDragLeave([&leaveCount]() { ++leaveCount; });
    ui.getOverlayRoot()->addChildExternal(&target);

    CHECK(ui.beginDrag(&source));
    ui.updateDrag(220.0f, 120.0f);    // enter
    CHECK(enterCount == 1);
    CHECK(leaveCount == 0);
    ui.updateDrag(230.0f, 130.0f);    // still over target
    CHECK(enterCount == 1);
    ui.updateDrag(50.0f, 50.0f);      // leave
    CHECK(leaveCount == 1);

    ui.cancelDrag();
    ui.shutdown();
}

// -----------------------------------------------------------------------------
// 5. endDrag fires onDrop on the target
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_end_drag_fires_on_drop) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(40.0f, 40.0f));
    source.setPosition(FVector2(0.0f, 0.0f));
    DragPayload p;
    p.kind = "color";
    p.userData = 0x12345678;
    source.setDragPayload(p);
    bool endAccepted = false;
    source.setOnDragEnd([&endAccepted](bool accepted) { endAccepted = accepted; });
    ui.getOverlayRoot()->addChildExternal(&source);

    Widget target;
    target.setAcceptDrops(true);
    target.setSize(FVector2(100.0f, 50.0f));
    target.setPosition(FVector2(200.0f, 200.0f));
    DragPayload droppedPayload;
    bool dropFired = false;
    target.setOnDrop([&](const DragPayload& payload) {
        droppedPayload = payload;
        dropFired = true;
    });
    ui.getOverlayRoot()->addChildExternal(&target);

    CHECK(ui.beginDrag(&source));
    ui.updateDrag(250.0f, 220.0f);
    CHECK(ui.endDrag(true));
    CHECK(dropFired);
    CHECK(droppedPayload.kind == "color");
    CHECK(droppedPayload.userData == 0x12345678);
    CHECK(endAccepted);
    CHECK_FALSE(ui.isDragging());

    ui.shutdown();
}

// -----------------------------------------------------------------------------
// 6. cancelDrag does NOT fire onDrop; source gets _onDragEnd(false)
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_cancel_drag_does_not_fire_on_drop) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(40.0f, 40.0f));
    source.setPosition(FVector2(0.0f, 0.0f));
    DragPayload p;
    p.kind = "x";
    source.setDragPayload(p);
    bool endAccepted = true;
    source.setOnDragEnd([&endAccepted](bool accepted) { endAccepted = accepted; });
    ui.getOverlayRoot()->addChildExternal(&source);

    Widget target;
    target.setAcceptDrops(true);
    target.setSize(FVector2(100.0f, 50.0f));
    target.setPosition(FVector2(200.0f, 200.0f));
    int dropFired = 0;
    target.setOnDrop([&](const DragPayload&) { ++dropFired; });
    ui.getOverlayRoot()->addChildExternal(&target);

    CHECK(ui.beginDrag(&source));
    ui.updateDrag(250.0f, 220.0f);
    ui.cancelDrag();
    CHECK(dropFired == 0);
    CHECK_FALSE(endAccepted);
    CHECK_FALSE(ui.isDragging());

    ui.shutdown();
}

// -----------------------------------------------------------------------------
// 6b. PR-Dock-TearOff: endDrag(true) with NO accepting target under the
//     cursor reports accepted=true to the source (a "void drop" — mouse
//     up in empty space), distinct from Esc (cancelDrag → false).
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_end_drag_void_drop_fires_on_drag_end_true) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(40.0f, 40.0f));
    source.setPosition(FVector2(0.0f, 0.0f));
    DragPayload p;
    p.kind = "x";
    source.setDragPayload(p);
    bool endAccepted = false;
    source.setOnDragEnd([&endAccepted](bool accepted) { endAccepted = accepted; });
    ui.getOverlayRoot()->addChildExternal(&source);

    CHECK(ui.beginDrag(&source));
    // Move over empty space — nothing accepts drops here.
    ui.updateDrag(380.0f, 280.0f);
    ui.endDrag(true);
    CHECK_FALSE(ui.isDragging());
    CHECK(endAccepted);   // void drop is still an accepted release
}

// -----------------------------------------------------------------------------
// 7. Escape cancels an active drag session
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_escape_key_cancels_drag) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(40.0f, 40.0f));
    source.setPosition(FVector2(0.0f, 0.0f));
    DragPayload p;
    p.kind = "x";
    source.setDragPayload(p);
    int startCount = 0;
    source.setOnDragStart([&startCount]() { ++startCount; });
    ui.getOverlayRoot()->addChildExternal(&source);

    CHECK(ui.beginDrag(&source));
    CHECK(startCount == 1);
    CHECK(ui.onKeyDown(UIKey_Escape));
    CHECK_FALSE(ui.isDragging());

    ui.shutdown();
}

// -----------------------------------------------------------------------------
// 8. Drop target renders accent border when current
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_drop_target_renders_accent_border) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget target;
    target.setAcceptDrops(true);
    target.setSize(FVector2(100.0f, 50.0f));
    target.setPosition(FVector2(100.0f, 100.0f));

    // Baseline: count Rect draw calls before flipping the drop-target flag.
    const int beforeRectCount = static_cast<int>(backend.getDrawCalls().size());

    target.setCurrentDropTarget(true);
    target.render(backend);
    const int afterRectCount = static_cast<int>(backend.getDrawCalls().size());
    CHECK(afterRectCount > beforeRectCount);

    ui.shutdown();
}

// -----------------------------------------------------------------------------
// 9. Widget dtor mid-drag does not UAF (R3 landmine guard)
// -----------------------------------------------------------------------------
TEST_CASE(dragdrop_widget_dtor_clears_drag_state_no_uaf) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    auto* source = new Widget();
    source->setDraggable(true);
    source->setSize(FVector2(40.0f, 40.0f));
    source->setPosition(FVector2(0.0f, 0.0f));
    DragPayload p;
    p.kind = "item";
    source->setDragPayload(p);
    ui.getOverlayRoot()->addChild(source);   // owning — destructor fires ~Widget
    source->setOnDragEnd([](bool) {});

    CHECK(ui.beginDrag(source));
    CHECK(ui.isDragging());
    // Mid-drag dtor: source is about to be freed. clearDragStateNoDispatch
    // is the R3-safe parallel of clearFocusNoDispatch / clearCaptureNoDispatch;
    // it drops the session slot without dispatching any virtual on the
    // about-to-be-freed widget.
    ui.getOverlayRoot()->removeChild(source);
    delete source;
    ui.clearDragStateNoDispatch(source);   // pointer now dangling; helper matches by value
    // Subsequent ops must be safe no-ops.
    ui.cancelDrag();
    CHECK_FALSE(ui.isDragging());

    ui.shutdown();
}

// =============================================================================
// AYUI-Audit-2026-08-26: per-kind acceptance API on Widget. setAcceptDrops()
// alone opts in to all kinds (legacy behavior). setAcceptDropKinds(...)
// filters by payload.kind via acceptsKind(). UIManager::updateDrag walks
// the ancestor chain with the kind filter, so a target tuned for one
// kind does not get drops of another kind.
// =============================================================================

// AYUI-Audit-2026-08-26: DragDrop_TargetRejectsForeignKind — target lists
// only "FileList"; the drag carries "TextBlock"; no enter/leave fires,
// no onDrop fires, currentDropTarget stays null.
TEST_CASE(DragDrop_TargetRejectsForeignKind) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(40.0f, 40.0f));
    source.setPosition(FVector2(0.0f, 0.0f));
    DragPayload p;
    p.kind = "TextBlock";
    source.setDragPayload(p);
    ui.getOverlayRoot()->addChildExternal(&source);

    Widget target;
    target.setAcceptDrops(true);
    target.setAcceptDropKinds({"FileList"});
    target.setSize(FVector2(100.0f, 50.0f));
    target.setPosition(FVector2(200.0f, 200.0f));
    int enterCount = 0;
    int dropCount = 0;
    target.setOnDragEnter([&enterCount](const DragPayload&) { ++enterCount; });
    target.setOnDrop([&dropCount](const DragPayload&) { ++dropCount; });
    ui.getOverlayRoot()->addChildExternal(&target);

    CHECK(ui.beginDrag(&source));
    // Move over target with kind="TextBlock" — target must be invisible
    // to this drag because acceptsKind("TextBlock") is false.
    ui.updateDrag(220.0f, 220.0f);
    CHECK(ui.getCurrentDropTarget() == nullptr);
    CHECK(enterCount == 0);
    CHECK_FALSE(target.isCurrentDropTarget());

    ui.endDrag(true);
    CHECK(dropCount == 0);   // no drop fired

    ui.shutdown();
}

// AYUI-Audit-2026-08-26: DragDrop_TargetAcceptsListedKind — target lists
// "FileList"; the drag carries "FileList"; enter + drop fire as normal.
TEST_CASE(DragDrop_TargetAcceptsListedKind) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(40.0f, 40.0f));
    source.setPosition(FVector2(0.0f, 0.0f));
    DragPayload p;
    p.kind = "FileList";
    p.text = L"a.txt";
    source.setDragPayload(p);
    ui.getOverlayRoot()->addChildExternal(&source);

    Widget target;
    target.setAcceptDrops(true);
    target.setAcceptDropKinds({"FileList", "Image"});
    target.setSize(FVector2(100.0f, 50.0f));
    target.setPosition(FVector2(200.0f, 200.0f));
    int enterCount = 0;
    int dropCount = 0;
    std::string droppedKind;
    target.setOnDragEnter([&enterCount](const DragPayload&) { ++enterCount; });
    target.setOnDrop([&](const DragPayload& payload) {
        ++dropCount;
        droppedKind = payload.kind;
    });
    ui.getOverlayRoot()->addChildExternal(&target);

    CHECK(ui.beginDrag(&source));
    ui.updateDrag(220.0f, 220.0f);
    CHECK(ui.getCurrentDropTarget() == &target);
    CHECK(target.isCurrentDropTarget());
    CHECK(enterCount == 1);
    ui.endDrag(true);
    CHECK(dropCount == 1);
    CHECK(droppedKind == "FileList");

    ui.shutdown();
}

// AYUI-Audit-2026-08-26: DragDrop_TargetAcceptsAnyWhenKindsEmpty — empty
// kinds list (default) preserves the pre-audit "accept any kind"
// contract. We drag multiple distinct kinds and verify every drop fires.
TEST_CASE(DragDrop_TargetAcceptsAnyWhenKindsEmpty) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(400.0f, 300.0f);

    Widget target;
    target.setAcceptDrops(true);
    // Default kinds list is empty => acceptsKind(any) == true.
    target.setSize(FVector2(100.0f, 50.0f));
    target.setPosition(FVector2(200.0f, 200.0f));
    int dropCount = 0;
    target.setOnDrop([&](const DragPayload&) { ++dropCount; });
    ui.getOverlayRoot()->addChildExternal(&target);

    // Round 1: a "TextBlock" drag with no kinds configured — should drop.
    {
        Widget source;
        source.setDraggable(true);
        source.setSize(FVector2(40.0f, 40.0f));
        source.setPosition(FVector2(0.0f, 0.0f));
        DragPayload p;
        p.kind = "TextBlock";
        source.setDragPayload(p);
        ui.getOverlayRoot()->addChildExternal(&source);

        CHECK(ui.beginDrag(&source));
        ui.updateDrag(220.0f, 220.0f);
        CHECK(ui.getCurrentDropTarget() == &target);   // empty kinds → any
        ui.endDrag(true);
        CHECK(dropCount == 1);
        ui.getOverlayRoot()->removeChild(&source);
    }

    // Round 2: an "Image" drag — also drops (kinds still empty).
    {
        Widget source2;
        source2.setDraggable(true);
        source2.setSize(FVector2(40.0f, 40.0f));
        source2.setPosition(FVector2(0.0f, 0.0f));
        DragPayload p;
        p.kind = "Image";
        source2.setDragPayload(p);
        ui.getOverlayRoot()->addChildExternal(&source2);

        CHECK(ui.beginDrag(&source2));
        ui.updateDrag(220.0f, 220.0f);
        CHECK(ui.getCurrentDropTarget() == &target);
        ui.endDrag(true);
        CHECK(dropCount == 2);
    }

    ui.shutdown();
}

TEST_SUITE_END