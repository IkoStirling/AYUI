#include "AYTest.h"
#include <AYUI/Authoring/CurveCanvas.h>
#include <AYUI/Authoring/DopeSheet.h>
#include <AYUI/MockRenderer.h>
#include <AYMath/CurveMath.h>
#include <cmath>

namespace {
// A non-animation owner with arbitrary event IDs. This test target links only
// AYUI authoring controls, proving there is no editor/animation resource coupling.
class TestCurveOwner final : public ayt::ui::authoring::ICurveEditorSource {
public:
    TestCurveOwner() { publish(); }
    void publish() {
        auto curve = std::make_shared<ayt::ui::authoring::CurveTrack>();
        curve->id = "gain";
        curve->keys = {{"a", 0.0, {0.0f}, {}, {}}, {"b", 1.0, {1.0f}, {}, {}}};
        curve->sample = [this](std::size_t, double seconds) {
            ++sampleCount;
            return static_cast<float>(seconds);
        };
        currentCurve = curve;
        auto timeline = std::make_shared<ayt::ui::authoring::TimelineSnapshot>();
        timeline->tracks = {{"cue-row", "Cue", ayt::ui::authoring::TimelineTrackKind::Event}};
        timeline->keys = {{"cue-opaque", "cue-row", eventTime}};
        currentTimeline = timeline;
        ++version;
    }
    std::uint64_t revision() const noexcept override { return version; }
    std::shared_ptr<const ayt::ui::authoring::CurveTrack> curveTrack(const std::string&) const override { return currentCurve; }
    std::shared_ptr<const ayt::ui::authoring::TimelineSnapshot> timelineSnapshot() const override { return currentTimeline; }
    double durationSeconds() const noexcept override { return 1.0; }
    double positionSeconds() const noexcept override { return position; }
    bool seek(double time) override { position = time; return true; }
    double snapTime(const std::string&, double time) const override { return std::round(time * 10.0) / 10.0; }
    bool beginEdit(const std::string&) override {
        if (active) return false;
        savedEventTime = eventTime;
        active = true;
        ++begins;
        return true;
    }
    bool endEdit(bool cancel) override {
        if (!active) return false;
        active = false;
        if (cancel) { eventTime = savedEventTime; ++cancels; publish(); }
        else ++commits;
        return true;
    }
    bool moveTimelineKey(std::string& row, std::string& key, double seconds) override {
        if (row != "cue-row" || key != "cue-opaque" || seconds < 0.0 || seconds > 1.0) return false;
        eventTime = seconds;
        publish();
        return true;
    }
    std::shared_ptr<const ayt::ui::authoring::CurveTrack> currentCurve;
    std::shared_ptr<const ayt::ui::authoring::TimelineSnapshot> currentTimeline;
    std::uint64_t version = 0;
    double eventTime = 0.5, savedEventTime = 0.5, position = 0.0;
    bool active = false;
    int begins = 0, commits = 0, cancels = 0, sampleCount = 0;
};
}

TEST_SUITE(AYUI_CurveEditor)

TEST_CASE(shared_ticks_rows_snap_and_units)
{
    using namespace ayt::ui::authoring;
    const auto ticks = timelineTicks({-0.1, 1.0}, 500.0);
    CHECK(ticks.size() == 5u);
    CHECK(std::fabs(ticks[1] - 0.2) < 1e-9);
    CHECK(timelineTicks({0, 0}, 500).empty());
    CHECK(timelineTicks({0, 1}, 0).empty());
    CHECK(timelineTicks({0, 1e300}, 500).size() <= 1024u);
    CHECK(formatTime(0.25, TimeDisplay::Adaptive) == L"250ms");
    CHECK(formatTime(2.0, TimeDisplay::Frames, 3, 24) == L"48f");
    CHECK(std::fabs(snapTimeToInterval(0.26, 0.1) - 0.3) < 1e-9);
    CHECK(snapTimeToInterval(0.26, 0) == 0.26);
    TimelineRowLayout rows{30, 28, 42};
    const auto range = rows.visibleRows(10, 56);
    CHECK(range.first == 1u);
    CHECK(range.second == 4u);
    CHECK(rows.rowCenter(2) == 58.0);
    CHECK(rows.visibleRows(0, 100).second == 0u);
}

TEST_CASE(refresh_gate_separates_content_selection_pose_and_transport)
{
    using namespace ayt::ui::authoring;
    AuthoringRefreshGate gate;
    AuthoringStateStamp stamp{1, 2, 3, 0, false};
    CHECK(gate.consume(stamp).content);
    CHECK(!gate.consume(stamp).any());
    stamp.positionSeconds = 0.1;
    auto changed = gate.consume(stamp);
    CHECK(changed.transport && !changed.content && !changed.pose && !changed.selection);
    ++stamp.selection;
    changed = gate.consume(stamp);
    CHECK(changed.selection && !changed.content && !changed.transport);
    ++stamp.pose;
    CHECK(gate.consume(stamp).pose);
    ++stamp.content;
    CHECK(gate.consume(stamp).content);
    stamp.playing = true;
    CHECK(gate.consume(stamp).transport);
    gate.acknowledge({5, 6, 7, 1, false});
    CHECK(!gate.consume({5, 6, 7, 1, false}).any());
}

TEST_CASE(scoped_gesture_retries_failed_finish_and_cancels_on_destruction)
{
    using namespace ayt::ui::authoring;
    int begins = 0, ends = 0;
    bool rejectEnd = true, cancelled = false;
    auto selection = std::make_shared<TimelineSelection>();
    selection->primaryKeyId = "before";
    {
        EditGestureSession gesture([&](const auto&) { ++begins; return true; },
            [&](bool cancel) { ++ends; cancelled = cancel; return !rejectEnd; }, selection);
        CHECK(gesture.begin("edit"));
        CHECK(!gesture.begin("nested"));
        CHECK(begins == 1);
        selection->primaryKeyId = "after";
        CHECK(!gesture.finish(false));
        CHECK(gesture.active());
        rejectEnd = false;
    }
    CHECK(ends == 2);
    CHECK(cancelled);
    CHECK(selection->primaryKeyId == "before");
}

TEST_CASE(seconds_viewport_preserves_zoom_anchor_and_tangent_units)
{
    ayt::ui::authoring::TimeViewport view{2.0, 4.0};
    const double anchor = view.timeAt(0.25);
    view.zoomAt(0.25, 0.5);
    CHECK(std::fabs(view.timeAt(0.25) - anchor) < 1.0e-9);
    CHECK(std::fabs(view.normalizedAt(anchor) - 0.25) < 1.0e-9);
    view.pan(0.5);
    CHECK(std::fabs(view.startSeconds - 1.5) < 1.0e-9);
    // Equal endpoint slopes make a linear segment independent of its span.
    CHECK(std::fabs(ayt::math::sampleCubicHermite(0, 4, 2, 2, 0.25f, 2) - 1) < 1e-6f);
    CHECK(std::fabs(ayt::math::sampleCubicHermite(0, 4, 2, 2, 1, 2) - 4) < 1e-6f);
}

TEST_CASE(curve_surface_reuses_samples_on_playhead_updates)
{
    auto source = std::make_shared<TestCurveOwner>();
    ayt::ui::authoring::CurveCanvas view(source);
    view.setSize({640, 240});
    view.setTrackId("gain");
    ayt::ui::MockRenderer renderer;
    view.render(renderer);
    const int sampled = source->sampleCount;
    CHECK(sampled == 161);
    source->seek(0.7);
    view.markDirty();
    view.render(renderer);
    CHECK(source->sampleCount == sampled);
    source->publish();
    view.markDirty();
    view.render(renderer);
    CHECK(source->sampleCount == sampled * 2);
    view.selectAllKeys();
    CHECK(view.selectedKeyCount() == 2u);
    ayt::ui::authoring::DopeSheet sheet(source);
    sheet.setSelection("gain", "a");
    CHECK(view.selectedKeyCount() == 2u); // Inspector refresh preserves multi-selection.
    sheet.setSelection("gain", "b");
    CHECK(view.selectedKeyCount() == 1u);
    CHECK(source->selectionState()->primaryKeyId == "b");
    CHECK(!view.deleteSelectedKeys()); // Owner may reject an unsupported command.
}

TEST_CASE(opaque_event_drag_uses_owner_snap_commit_and_cancel)
{
    auto source = std::make_shared<TestCurveOwner>();
    ayt::ui::authoring::DopeSheet view(source);
    view.setSize({640, 180});
    ayt::ui::MockRenderer renderer;
    view.render(renderer);
    // Public owner-independent row geometry: first row at y=34, time=.5.
    CHECK(view.onMouseButtonDown(ayt::ui::UIMouseEvent({404, 34}, 0)));
    CHECK(view.onMouseMove(ayt::ui::UIMouseEvent({522, 34}, 0)));
    CHECK(std::fabs(source->eventTime - 0.8) < 1e-9);
    CHECK(view.onMouseButtonUp(ayt::ui::UIMouseEvent({522, 34}, 0)));
    CHECK(source->begins == 1);
    CHECK(source->commits == 1);
    CHECK(source->cancels == 0);
    CHECK(view.onMouseButtonDown(ayt::ui::UIMouseEvent({545.6f, 34}, 0)));
    CHECK(view.onMouseMove(ayt::ui::UIMouseEvent({310, 34}, 0)));
    view.onCaptureCancelled();
    CHECK(source->cancels == 1);
    CHECK(std::fabs(source->eventTime - 0.8) < 1e-9);
}

TEST_CASE(view_destruction_cancels_only_its_own_edit)
{
    auto source = std::make_shared<TestCurveOwner>();
    {
        ayt::ui::authoring::DopeSheet view(source);
        view.setSize({640, 180});
        ayt::ui::MockRenderer renderer;
        view.render(renderer);
        CHECK(view.onMouseButtonDown(ayt::ui::UIMouseEvent({404, 34}, 0)));
        CHECK(view.onMouseMove(ayt::ui::UIMouseEvent({522, 34}, 0)));
    }
    CHECK(source->cancels == 1);
    CHECK(std::fabs(source->eventTime - 0.5) < 1e-9);
    CHECK(source->beginEdit("external"));
    {
        ayt::ui::authoring::DopeSheet view(source);
        view.setSize({640, 180});
        ayt::ui::MockRenderer renderer;
        view.render(renderer);
        bool selected = false;
        view.setOnSelectionChanged([&](const auto&, const auto&) { selected = true; });
        CHECK(view.onMouseButtonDown(ayt::ui::UIMouseEvent({404, 34}, 0)));
        CHECK(selected); // A denied edit still permits selection.
        CHECK(!view.onMouseButtonUp(ayt::ui::UIMouseEvent({404, 34}, 0)));
    }
    { ayt::ui::authoring::CurveCanvas view(source); }
    CHECK(source->active);
    CHECK(source->endEdit(true));
}
TEST_SUITE_END

int main() { return ayt::test::runAllTests("AYUI curve authoring"); }
