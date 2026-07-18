#include "AYTest.h"
#include "AYUIManager.h"
#include "AYMockRenderer.h"
#include "AYButton.h"
#include "AYWindow.h"

#include <cstdio>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_UIManager)

TEST_CASE(test_uimanager_load_and_render) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.bindEvent("btn_ok", "onClick", []() {});

    const char* json = R"({
        "type": "VBox",
        "id": "root",
        "size": { "w": 320, "h": 240 },
        "children": [
            { "type": "Button", "id": "btn_ok", "text": "OK", "size": { "w": 80, "h": 32 }, "onClick": "ok" }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(320.0f, 240.0f);
    ui.layout();
    ui.render();
    CHECK(backend.getDrawCallCount() > 0);
    ui.shutdown();
}

TEST_CASE(test_uimanager_mouse_click) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    bool clicked = false;
    ui.bindEvent("btn_ok", "onClick", [&clicked]() { clicked = true; });

    const char* json = R"({
        "type": "Button",
        "id": "btn_ok",
        "text": "OK",
        "position": { "x": 10, "y": 10 },
        "size": { "w": 100, "h": 32 },
        "onClick": "ok"
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(200.0f, 100.0f);
    ui.layout();

    CHECK(ui.onMouseButtonDown(50.0f, 20.0f, 0));
    CHECK(ui.onMouseButtonUp(50.0f, 20.0f, 0));
    CHECK(clicked);
    ui.shutdown();
}

TEST_CASE(test_uimanager_window_drag_with_capture) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "VBox",
        "id": "root",
        "size": { "w": 640, "h": 480 },
        "children": [
            {
                "type": "Window",
                "id": "panel",
                "text": "Panel",
                "position": { "x": 40, "y": 40 },
                "size": { "w": 120, "h": 100 },
                "minSize": { "w": 80, "h": 60 }
            }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(640.0f, 480.0f);
    ui.layout();

    Window* panel = dynamic_cast<Window*>(ui.findById("panel"));
    CHECK(panel != nullptr);
    CHECK(ui.onMouseButtonDown(80.0f, 50.0f, 0));
    CHECK(panel->isDragging());
    ui.onMouseLeave();
    CHECK(ui.onMouseMove(180.0f, 120.0f));
    CHECK(panel->getPosition().x > 40.0f);
    CHECK(ui.onMouseButtonUp(180.0f, 120.0f, 0));
    CHECK(!panel->isDragging());
    ui.shutdown();
}

TEST_CASE(test_uimanager_cancel_capture) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "Window",
        "id": "panel",
        "text": "Panel",
        "position": { "x": 40, "y": 40 },
        "size": { "w": 220, "h": 160 },
        "minSize": { "w": 120, "h": 80 }
    })";

    CHECK(ui.loadFromString(json));
    // Root is the panel itself, so setClientSize would resize it to the
    // viewport. Skip that step to preserve the JSON-declared 220x160 size.

    Window* panel = dynamic_cast<Window*>(ui.findById("panel"));
    CHECK(panel != nullptr);

    CHECK(ui.onMouseButtonDown(80.0f, 50.0f, 0));
    CHECK(ui.isCapturing());
    CHECK(panel->isDragging());

    ui.cancelCapture();
    CHECK(!ui.isCapturing());
    CHECK(!panel->isDragging());
    ui.shutdown();
}

// R-7: a hot reload triggered while the UIManager has a captured widget
// (mid-drag on a Window) must release the capture BEFORE destroying the
// tree. The captured widget pointer would otherwise dangle after the
// tree swap, and a subsequent onMouseButtonUp dereferences freed memory.
//
// Setup: write a layout JSON with a draggable Window to a temp file,
// load it, capture the window via onMouseButtonDown, edit the file,
// then call update() — which triggers the watcher's tryReload path.
// After update, isCapturing() must be false and a subsequent
// onMouseButtonUp must not crash.
TEST_CASE(test_uimanager_reload_during_capture) {
    namespace fs = std::filesystem;

    fs::path tmpDir = fs::temp_directory_path() / "ayui_r7_reload_capture";
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir);
    fs::path jsonPath = tmpDir / "panel.json";

    {
        std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
        out << R"({
            "type": "Window",
            "id": "panel",
            "text": "v1",
            "position": { "x": 40, "y": 40 },
            "size": { "w": 220, "h": 160 },
            "minSize": { "w": 120, "h": 80 }
        })";
    }

    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    CHECK(ui.loadLayout(jsonPath.string()));

    Window* panel = dynamic_cast<Window*>(ui.findById("panel"));
    CHECK(panel != nullptr);

    // Begin a drag — this sets _capturedWidget = panel inside UIManager.
    CHECK(ui.onMouseButtonDown(80.0f, 50.0f, 0));
    CHECK(ui.isCapturing());
    CHECK(panel->isDragging());

    // Edit the file to trigger a reload on the next update().
    {
        std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
        out << R"({
            "type": "Window",
            "id": "panel",
            "text": "v2",
            "position": { "x": 40, "y": 40 },
            "size": { "w": 220, "h": 160 },
            "minSize": { "w": 120, "h": 80 }
        })";
    }

    // Allow the watcher thread to enqueue the change event.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // R-7 fix path: update() detects a pending reload, calls
    // cancelCapture() (synthesizing a mouse-up so the old Window's
    // _isDragging clears), clears _hoverWidget, then destroys the tree.
    ui.update(0.0f);

    // After reload the capture must be gone. The previous _capturedWidget
    // pointer is now dangling; reading it would be UB.
    CHECK(!ui.isCapturing());

    // Subsequent input handling must not crash on the dangling pointer.
    // If cancelCapture wasn't called, this onMouseButtonUp would
    // dereference the freed Window — R-7 prevents that.
    ui.onMouseButtonUp(80.0f, 50.0f, 0);

    ui.shutdown();
    fs::remove_all(tmpDir, ec);
}

// Bug repro: a splitter between two Window panels must go back to
// invisible once the cursor leaves it onto an adjacent panel. The flow
// is: hover in → tick past reveal delay → revealed (2 draw calls) →
// mouse leaves onto the right panel → must be invisible (0 draw calls).
//
// This drives the public UIManager::onMouseMove path (not direct
// splitter calls) so the test exercises the same code the runtime
// editor uses.
TEST_CASE(test_uimanager_splitter_returns_to_invisible_after_leave) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "HBox",
        "id": "root",
        "size": { "w": 800, "h": 400 },
        "spacing": 0,
        "children": [
            { "type": "Window", "id": "left",  "position": { "x": 0,   "y": 0 }, "size": { "w": 220, "h": 400 } },
            { "type": "SplitterHandle", "id": "split", "size": { "w": 4, "h": 400 } },
            { "type": "Window", "id": "right", "position": { "x": 0, "y": 0 }, "size": { "w": 280, "h": 400 } }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(800.0f, 400.0f);
    ui.layout();

    SplitterHandle* split = dynamic_cast<SplitterHandle*>(ui.findById("split"));
    CHECK(split != nullptr);

    // After layout the splitter sits at x ∈ [224, 228) — left panel is
    // 220 wide + 4-wide title bar inset + spacing puts the splitter band
    // here. Hover at x=226 to land squarely inside it.
    CHECK(split->getWorldBounds().minX == 224.0f);
    CHECK(split->getWorldBounds().maxX == 228.0f);

    // Step 1: hover into the splitter.
    ui.onMouseMove(226.0f, 200.0f);
    // Step 2: tick past the 150ms reveal delay.
    ui.update(0.20f);
    CHECK(split->isRevealed());

    // Sanity: at reveal time, splitter emits 2 rects with accent fill +
    // grab handle. Filter by the splitter accent color (0.40, 0.48, 0.62)
    // so we don't conflate Window's own draws.
    auto countSplitterRects = [&backend]() {
        int n = 0;
        for (const auto& dc : backend.getDrawCalls()) {
            if (dc.type == MockRenderer::DrawCall::Rect &&
                dc.color.x > 0.35f && dc.color.x < 0.45f &&
                dc.color.y > 0.43f && dc.color.y < 0.53f) {
                ++n;
            }
        }
        return n;
    };

    backend.clear();
    ui.render();
    int revealedRects = countSplitterRects();
    std::cerr << "=== DIAG: revealedRects = " << revealedRects
              << " (expected 1: fill rect only — grab handle is whiter 0.85, 0.88, 0.92)"
              << std::endl;
    // After fix: at least 1 splitter rect (the accent fill).
    CHECK(revealedRects >= 1);

    // Step 3: mouse moves from splitter onto the right panel.
    ui.onMouseMove(400.0f, 200.0f);
    ui.update(0.0f);
    CHECK(!split->isRevealed());

    // Step 4: splitter must be invisible — 0 splitter-accent rects.
    backend.clear();
    ui.render();
    int afterLeaveRects = countSplitterRects();
    std::cerr << "=== DIAG: afterLeaveRects = " << afterLeaveRects
              << " (expected 0)" << std::endl;
    CHECK(afterLeaveRects == 0);

    ui.shutdown();
}

// Drag scenario: complete drag cycle then leave splitter. After release,
// the splitter must return to invisible once the cursor moves off it.
TEST_CASE(test_uimanager_splitter_drag_then_leave_returns_to_invisible) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "HBox",
        "id": "root",
        "size": { "w": 800, "h": 400 },
        "spacing": 0,
        "children": [
            { "type": "Window", "id": "left",  "size": { "w": 220, "h": 400 } },
            { "type": "SplitterHandle", "id": "split" },
            { "type": "Window", "id": "right", "size": { "w": 280, "h": 400 } }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(800.0f, 400.0f);
    ui.layout();

    SplitterHandle* split = dynamic_cast<SplitterHandle*>(ui.findById("split"));
    Window* left = dynamic_cast<Window*>(ui.findById("left"));
    CHECK(split != nullptr);
    CHECK(left != nullptr);

    auto countSplitterRects = [&backend]() {
        int n = 0;
        for (const auto& dc : backend.getDrawCalls()) {
            if (dc.type == MockRenderer::DrawCall::Rect &&
                dc.color.x > 0.35f && dc.color.x < 0.45f &&
                dc.color.y > 0.43f && dc.color.y < 0.53f) {
                ++n;
            }
        }
        return n;
    };

    // Step 1: hover into splitter — band is at [224, 228).
    ui.onMouseMove(226.0f, 200.0f);
    ui.update(0.20f);
    CHECK(split->isRevealed());

    // Step 2: drag. Press, move (drag), release — all inside the splitter band.
    CHECK(ui.onMouseButtonDown(226.0f, 200.0f, 0));
    ui.onMouseMove(280.0f, 200.0f);
    CHECK(left->getWidth() > 220.0f);  // drag worked
    ui.onMouseButtonUp(280.0f, 200.0f, 0);
    CHECK(!ui.isCapturing());
    CHECK(!split->isDragging());

    // Step 3: move off the splitter band after release.
    ui.onMouseMove(500.0f, 200.0f);
    ui.update(0.0f);

    // Step 4: render and confirm splitter produced 0 accent rects.
    backend.clear();
    ui.render();
    int afterDragRects = countSplitterRects();
    std::cerr << "=== DIAG drag-then-leave: afterDragRects=" << afterDragRects
              << " (expected 0)" << std::endl;
    CHECK(afterDragRects == 0);

    ui.shutdown();
}

// Cross-splitter transition: hover into splitter A, reveal it, then move
// horizontally across to splitter B in the same HBox. Both splitters must
// end up in the correct state (A hidden, B just entered).
TEST_CASE(test_uimanager_splitter_to_sibling_splitter) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "HBox",
        "id": "root",
        "size": { "w": 800, "h": 400 },
        "spacing": 0,
        "children": [
            { "type": "Window", "id": "left",   "size": { "w": 220, "h": 400 } },
            { "type": "SplitterHandle", "id": "splitA" },
            { "type": "Window", "id": "mid",    "size": { "w": 220, "h": 400 } },
            { "type": "SplitterHandle", "id": "splitB" },
            { "type": "Window", "id": "right",  "size": { "w": 280, "h": 400 } }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(800.0f, 400.0f);
    ui.layout();

    SplitterHandle* a = dynamic_cast<SplitterHandle*>(ui.findById("splitA"));
    SplitterHandle* b = dynamic_cast<SplitterHandle*>(ui.findById("splitB"));
    CHECK(a != nullptr);
    CHECK(b != nullptr);

    std::cerr << "=== DIAG: splitA worldX = ["
              << a->getWorldBounds().minX << ", " << a->getWorldBounds().maxX << ")"
              << " splitB worldX = ["
              << b->getWorldBounds().minX << ", " << b->getWorldBounds().maxX << ")" << std::endl;

    // Hover into splitA.
    ui.onMouseMove(a->getWorldBounds().minX + 1.0f, 200.0f);
    ui.update(0.20f);
    CHECK(a->isRevealed());
    CHECK(!b->isRevealed());

    // Move directly across to splitB (skip the mid window by jumping).
    ui.onMouseMove(b->getWorldBounds().minX + 1.0f, 200.0f);
    ui.update(0.0f);

    // A must be hidden, B must be hovered but not yet revealed (no tick).
    CHECK(!a->isRevealed());
    CHECK(!b->isRevealed());  // not yet past delay

    // Tick past delay — only B reveals.
    ui.update(0.20f);
    CHECK(!a->isRevealed());
    CHECK(b->isRevealed());

    // Move off into mid window — B must hide.
    ui.onMouseMove(300.0f, 200.0f);  // well inside mid window
    ui.update(0.0f);
    CHECK(!a->isRevealed());
    CHECK(!b->isRevealed());

    ui.shutdown();
}

// Bug repro: editor_shell.ui.json structure (VBox root + VBox/HBox toolbar
// + HBox main_row containing Window/Splitter/Image/Splitter/Window).
// After hovering into a splitter and revealing it, the cursor must
// release the splitter visual when it leaves onto an adjacent panel.
// This drives the exact UIManager API EditorSession uses (so any layer
// mismatch with the real editor surfaces here).
TEST_CASE(test_uimanager_editor_shell_splitter_leave_through_panel) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    // Replicates editor_shell.ui.json main_row (cropped):
    const char* json = R"({
        "type": "VBox",
        "id": "editor_root",
        "size": { "w": 1280, "h": 720 },
        "spacing": 4,
        "children": [
            { "type": "HBox", "id": "toolbar", "size": { "h": 44 } },
            {
                "type": "HBox",
                "id": "main_row",
                "spacing": 0,
                "children": [
                    { "type": "Window", "id": "panel_hierarchy",
                      "size": { "w": 220, "h": 0 } },
                    { "type": "SplitterHandle", "id": "split_hv" },
                    { "type": "Image", "id": "panel_viewport",
                      "size": { "w": 0, "h": 0 },
                      "color": [0.08, 0.09, 0.11, 1.0] },
                    { "type": "SplitterHandle", "id": "split_vi" },
                    { "type": "Window", "id": "panel_inspector",
                      "size": { "w": 280, "h": 0 } }
                ]
            }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(1280.0f, 720.0f);
    ui.layout();

    SplitterHandle* split = dynamic_cast<SplitterHandle*>(ui.findById("split_hv"));
    CHECK(split != nullptr);

    std::cerr << "=== DIAG editor_shell: split_hv worldY = ["
              << split->getWorldBounds().minY << ", " << split->getWorldBounds().maxY
              << ") worldX = ["
              << split->getWorldBounds().minX << ", " << split->getWorldBounds().maxX << ")" << std::endl;

    // Hover into the first splitter.
    const float splitX = (split->getWorldBounds().minX + split->getWorldBounds().maxX) * 0.5f;
    const float splitY = (split->getWorldBounds().minY + split->getWorldBounds().maxY) * 0.5f;
    ui.onMouseMove(splitX, splitY);
    ui.update(0.20f);
    CHECK(split->isRevealed());

    // Move off the splitter into the panel_viewport (Image, no children).
    // This is the "leave" path the user reported as broken.
    ui.onMouseMove(splitX + 200.0f, splitY);
    ui.update(0.0f);
    CHECK(!split->isRevealed());

    // Move onto the hierarchy panel (left of the splitter) — also a leave.
    ui.onMouseMove(splitX, splitY);
    ui.update(0.20f);
    CHECK(split->isRevealed());
    ui.onMouseMove(50.0f, splitY);
    ui.update(0.0f);
    CHECK(!split->isRevealed());

    ui.shutdown();
}

// Bug repro: drag-release on a non-splitter surface leaves the splitter
// in a stuck-revealed state.
//
// Sequence:
//   1. hover into splitter, tick past delay → revealed
//   2. press button → splitter._dragging = true
//   3. drag away onto a panel
//   4. release on the panel — at this point UIManager::onMouseButtonUp
//      calls updateHoverWidget BEFORE delivering onMouseButtonUp to the
//      captured widget. The leave fires while _dragging is still true
//      so SplitterHandle::onMouseLeave skips the hover reset.
//   5. splitter._dragging is now false but _hover is still true, with
//      _hoverElapsed already past the delay. isRevealed() returns true
//      every frame until the next onMouseMove lands.
//
// This is the user-reported "splitter stays highlighted after leaving".
TEST_CASE(test_uimanager_splitter_release_off_band_stuck_revealed) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "HBox",
        "id": "root",
        "size": { "w": 800, "h": 400 },
        "spacing": 0,
        "children": [
            { "type": "Window", "id": "left",  "size": { "w": 220, "h": 400 } },
            { "type": "SplitterHandle", "id": "split" },
            { "type": "Window", "id": "right", "size": { "w": 280, "h": 400 } }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(800.0f, 400.0f);
    ui.layout();

    SplitterHandle* split = dynamic_cast<SplitterHandle*>(ui.findById("split"));
    CHECK(split != nullptr);

    // 1. hover into splitter, tick past reveal delay.
    ui.onMouseMove(split->getWorldBounds().minX + 1.0f, 200.0f);
    ui.update(0.20f);
    CHECK(split->isRevealed());

    // 2. press.
    CHECK(ui.onMouseButtonDown(split->getWorldBounds().minX + 1.0f, 200.0f, 0));
    CHECK(split->isDragging());

    // 3. drag away onto the right panel (no release yet).
    ui.onMouseMove(500.0f, 200.0f);
    CHECK(split->isDragging());  // sanity — still dragging

    // 4. release while off the splitter band. THIS is where the bug
    //    fires: UIManager::onMouseButtonUp computes the new hover
    //    BEFORE delivering mouse-up to the captured widget, so
    //    onMouseLeave fires while _dragging is still true.
    ui.onMouseButtonUp(500.0f, 200.0f, 0);
    CHECK(!split->isDragging());

    // Splitter must hide on release.
    CHECK(!split->isRevealed());

    // Render and confirm: zero splitter-accent rects.
    backend.clear();
    ui.render();
    int splitterAccent = 0;
    for (const auto& dc : backend.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect &&
            dc.color.x > 0.35f && dc.color.x < 0.45f &&
            dc.color.y > 0.43f && dc.color.y < 0.53f) {
            ++splitterAccent;
        }
    }
    CHECK(splitterAccent == 0);

    // After release, moving the cursor back INTO the splitter band
    // must restart the reveal flow from scratch (delay counter 0).
    const float newSplitMinX = split->getWorldBounds().minX;
    ui.onMouseMove(newSplitMinX + 1.0f, 200.0f);
    CHECK(!split->isRevealed());  // not yet past the delay
    ui.update(0.20f);
    CHECK(split->isRevealed());

    ui.shutdown();
}

// Bug repro: tick after leave must NOT re-reveal. The tick cascade runs
// every frame regardless of mouse state; the splitter must honor the
// leave even as tick() keeps firing.
TEST_CASE(test_uimanager_splitter_leave_holds_through_continuous_tick) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "HBox",
        "id": "root",
        "size": { "w": 800, "h": 400 },
        "spacing": 0,
        "children": [
            { "type": "Window", "id": "left",  "size": { "w": 220, "h": 400 } },
            { "type": "SplitterHandle", "id": "split" },
            { "type": "Window", "id": "right", "size": { "w": 280, "h": 400 } }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(800.0f, 400.0f);
    ui.layout();

    SplitterHandle* split = dynamic_cast<SplitterHandle*>(ui.findById("split"));
    CHECK(split != nullptr);

    ui.onMouseMove(split->getWorldBounds().minX + 1.0f, 200.0f);
    ui.update(0.20f);
    CHECK(split->isRevealed());

    // Leave.
    ui.onMouseMove(500.0f, 200.0f);
    ui.update(0.0f);
    CHECK(!split->isRevealed());

    // Keep ticking for 5 seconds of sim time — splitter must stay hidden.
    for (int i = 0; i < 50; ++i) {
        ui.update(0.10f);
        CHECK(!split->isRevealed());
    }

    ui.shutdown();
}

TEST_SUITE_END