#include "AYTest.h"
#include <AYUI/Authoring/CurveCanvas.h>
#include <AYUI/Authoring/DopeSheet.h>
#include <AYUI/MockRenderer.h>
#include <AYMath/CurveMath.h>
#include <AYUI/Authoring/PlaybackControls.h>
#include <AYUI/Authoring/NumericFields.h>
#include <AYUI/Authoring/PreviewViewport.h>
#include <AYUI/Authoring/ResourceReferenceField.h>
#include <AYUI/Authoring/TimelineSelectionOps.h>
#include <AYUI/Authoring/DiagnosticsPanel.h>
#include <AYUI/Authoring/PropertyField.h>
#include <AYUI/Authoring/JobPresentation.h>
#include <AYUI/TextLabel.h>
#include <AYUI/TextInput.h>
#include <AYUI/Button.h>
#include <AYUI/ComboBox.h>
#include <AYUI/Slider.h>
#include <cmath>
#include <limits>

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

TEST_CASE(numeric_fields_preserve_ids_dimensions_readonly_and_atomic_validation)
{
    using namespace ayt::ui::authoring;
    CHECK(!parseFiniteFloat(L"nan"));
    CHECK(!parseFiniteFloat(L"inf"));
    CHECK(!parseFiniteFloat(L"1.25junk"));
    CHECK(!parseFiniteFloat(L""));
    CHECK(parseFiniteFloat(L"1.25") == 1.25f);
    auto* fields = new NumericFields({"x", "y", "z", "w"}, {L"X", L"Y", L"Z", L"W"});
    int submissions = 0;
    fields->setOnSubmitted([&] { ++submissions; });
    CHECK(fields->setValues({1, 2, 3}));
    CHECK(fields->componentCount() == 3u);
    CHECK(fields->inputs()[0]->getId() == "x");
    CHECK(fields->inputs()[0]->getPlaceholder() == L"X");
    CHECK(!fields->inputs()[3]->isVisible());
    std::vector<float> output{99};
    CHECK(fields->readValues(3, output));
    CHECK(output == std::vector<float>({1, 2, 3}));
    fields->inputs()[1]->setText(L"bad");
    output = {99};
    CHECK(!fields->readValues(3, output));
    CHECK(output == std::vector<float>({99}));
    CHECK(!fields->setValues({1, 2, 3, 4, 5}));
    CHECK(fields->componentCount() == 3u);
    CHECK(fields->inputs()[1]->getText() == L"bad");
    CHECK(fields->setValues({4, 5}, true));
    CHECK(fields->inputs()[0]->isReadOnly());
    CHECK(!fields->readValues(2, output));
    CHECK(fields->setValues({4, 5}, false));
    CHECK(!fields->inputs()[0]->isReadOnly());
    CHECK(!fields->readValues(3, output));
    fields->setUnit(L"value/s");
    CHECK(submissions == 0);
    CHECK(fields->setValues({}));
    CHECK(!fields->readValues(0, output));
    ayt::ui::destroyWidgetTree(fields);
}

TEST_CASE(playback_controls_do_not_seek_while_refreshing_and_hide_optional_capabilities)
{
    using namespace ayt::ui::authoring;
    struct Owner final : IPlaybackSource {
        PlaybackState state{true, false, 0.5, 1.0, {}, {}};
        int plays = 0, seeks = 0, rates = 0;
        PlaybackState playbackState() const override { return state; }
        void play() override { ++plays; state.playing = true; }
        void pause() override { state.playing = false; }
        void stop() override { state.positionSeconds = 0; state.playing = false; }
        bool seek(double seconds) override { ++seeks; state.positionSeconds = seconds; return true; }
        bool setRate(float value) override { ++rates; state.rate = value; return true; }
        bool setLooping(bool value) override { state.looping = value; return true; }
    };
    auto owner = std::make_shared<Owner>();
    auto* view = new PlaybackControls(owner, {true, true, true, true, true, true});
    view->refresh();
    CHECK(owner->seeks == 0);
    CHECK(owner->rates == 0);
    CHECK(!view->loopButton()->isVisible());
    CHECK(!view->rateInput()->isVisible());
    owner->state.positionSeconds = 0.8;
    view->refresh();
    CHECK(owner->seeks == 0);
    CHECK(std::fabs(view->seekInput()->getValue() - 0.8f) < 1e-6f);
    owner->state.looping = true;
    owner->state.rate = 1.5f;
    view->refresh();
    CHECK(view->loopButton()->isVisible());
    CHECK(view->rateInput()->getSelectedIndex() == 3);
    CHECK(owner->rates == 0);
    view->rateInput()->setSelectedIndexAndNotify(4);
    CHECK(owner->rates == 1);
    CHECK(owner->state.rate == 2.0f);
    owner->state.available = false;
    view->refresh();
    CHECK(!view->playButton()->isEnabled());
    CHECK(!view->seekInput()->isEnabled());
    ayt::ui::destroyWidgetTree(view);
}

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
    const TimelineRowLayout subpixelRows{0, 0.5, 0.25};
    CHECK(subpixelRows.rowCenter(2) == 1.0);
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
TEST_CASE(preview_projection_orbit_and_cache_are_resource_neutral)
{
    using namespace ayt::ui::authoring;
    PreviewOrbit orbit;
    CHECK(!orbit.move({10, 10}));
    orbit.begin({0, 0});
    CHECK(orbit.move({10, 1000}));
    CHECK(orbit.pitch == 1.5f);
    CHECK(orbit.end());
    CHECK(!orbit.end());
    for (int i = 0; i < 100; ++i) orbit.wheel(1);
    CHECK(orbit.zoom == 8.0f);
    orbit.reset();
    CHECK(orbit.zoom == 1.0f);
    CHECK(!orbit.rotating());
    const auto zoom = orbit.zoom;
    orbit.wheel(0);
    CHECK(orbit.zoom == zoom);
    PreviewBounds points;
    points.include({-1, -1, -1}); points.include({1, 1, 1});
    const PreviewProjection project(points, {0, 0, 100, 100}, orbit, 0.72f);
    const auto center = project({0, 0, 0});
    CHECK(center.x == 50.0f);
    CHECK(center.y == 50.0f);
    CHECK(std::isfinite(project({1, 1, 1}).depth));
    PreviewProjectionCache cache;
    PreviewProjectionStamp stamp{1, 2, {0, 0, 100, 100}, orbit.yaw, orbit.pitch, orbit.zoom};
    CHECK(cache.consume(stamp));
    CHECK(!cache.consume(stamp));
    ++stamp.pose; CHECK(cache.consume(stamp));
    ++stamp.content; CHECK(cache.consume(stamp));
    ++stamp.bounds.maxX; CHECK(cache.consume(stamp));
    ++stamp.yaw; CHECK(cache.consume(stamp));
    cache.invalidate(); CHECK(cache.consume(stamp));
}
TEST_CASE(resource_reference_refresh_pick_rejection_and_readonly)
{
    using namespace ayt::ui::authoring;
    auto* field = new ResourceReferenceField({L"Resource", L"opaque path", L"Apply", "ref_input"});
    int loads = 0, picks = 0;
    CHECK(!field->loadButton()->isEnabled());
    CHECK(!field->pickButton()->isVisible());
    field->setOnLoad([&](const auto& path) {
        ++loads; return ResourceReferenceResult{path == L"valid", L"Result details"};
    });
    field->setPath(L"bad"); CHECK(loads == 0);
    CHECK(field->input()->getId() == "ref_input");
    CHECK(!field->requestLoad()); CHECK(loads == 1);
    CHECK(field->input()->getText() == L"bad");
    CHECK(field->statusLabel()->getText() == L"Failed");
    field->setPicker([&] { ++picks; return std::wstring{}; });
    CHECK(!field->requestPick()); CHECK(loads == 1); CHECK(picks == 1);
    CHECK(field->input()->getText() == L"bad");
    field->setPicker([&] {
        CHECK(!field->requestPick()); CHECK(!field->requestLoad());
        return std::wstring{};
    });
    CHECK(!field->requestPick()); CHECK(loads == 1);
    field->setPicker([] { return std::wstring{L"valid"}; });
    CHECK(field->requestPick()); CHECK(loads == 2);
    CHECK(field->statusLabel()->getText() == L"Ready");
    field->setReadOnly(true);
    CHECK(field->input()->isReadOnly());
    CHECK(!field->requestLoad()); CHECK(!field->requestPick()); CHECK(loads == 2);
    field->setPath(L"refresh"); CHECK(loads == 2);
    ayt::ui::destroyWidgetTree(field);
}
TEST_CASE(selection_operations_normalize_preserve_remap_and_notify_empty)
{
    using namespace ayt::ui::authoring;
    TimelineSelection selection;
    int notifications = 0;
    const auto notify = [&](const auto&) { ++notifications; };
    CHECK(TimelineSelectionOps::keys(selection, "track", {"a", "b", "a", ""}, 2, notify));
    CHECK(selection.keyIds == std::vector<std::string>({"a", "b"}));
    CHECK(selection.primaryKeyId == "a");
    CHECK(!TimelineSelectionOps::single(selection, "track", "a", 2, true, notify));
    CHECK(notifications == 1);
    CHECK(TimelineSelectionOps::single(selection, "track", "b", 1, true, notify));
    CHECK(selection.keyIds.size() == 2u);
    CHECK(!TimelineSelectionOps::remap(selection, {"a", "b"}, {"new"}, notify));
    CHECK(selection.primaryKeyId == "b");
    CHECK(TimelineSelectionOps::remap(selection, {"a", "b"}, {"renamed-a", "renamed-b"}, notify));
    CHECK(selection.primaryKeyId == "renamed-b");
    CHECK(TimelineSelectionOps::clear(selection, notify));
    CHECK(selection.primaryKeyId.empty()); CHECK(selection.keyIds.empty());
    CHECK(!TimelineSelectionOps::clear(selection, notify));
    CHECK(notifications == 4);
    auto source = std::make_shared<TestCurveOwner>();
    CurveCanvas view(source);
    int cleared = 0;
    view.setOnSelectionChanged([&](const std::string& key, std::size_t) { if (key.empty()) ++cleared; });
    view.setTrackId("gain"); view.selectAllKeys(); view.clearSelection();
    CHECK(cleared == 1);
}
TEST_CASE(diagnostics_filter_expand_locate_and_plain_reports_are_safe)
{
    using namespace ayt::ui::authoring;
    auto* panel = new DiagnosticsPanel(2);
    int located = 0;
    std::string target;
    panel->setOnLocate([&](const auto& id) { ++located; target = id; });
    const std::vector<DiagnosticEntry> entries{
        {DiagnosticSeverity::Error, L"E", L"broken", "opaque-one"},
        {DiagnosticSeverity::Warning, L"W", L"attention", {}},
        {DiagnosticSeverity::Info, L"I", L"ready", "opaque-three"}};
    panel->setEntries(entries, L"Owner heading");
    CHECK(located == 0);
    CHECK(panel->reportText().find(L"1 more issue(s)") != std::wstring::npos);
    CHECK(!panel->locateVisible(2));
    CHECK(panel->locateVisible(0)); CHECK(target == "opaque-one");
    CHECK(!panel->locateVisible(1));
    panel->setExpanded(true);
    CHECK(panel->locateVisible(2)); CHECK(target == "opaque-three");
    CHECK(panel->reportText().find(L"more issue(s)") == std::wstring::npos);
    panel->filterControl()->setSelectedIndexAndNotify(1);
    CHECK(panel->reportText().find(L"broken") != std::wstring::npos);
    CHECK(panel->reportText().find(L"attention") == std::wstring::npos);
    CHECK(!panel->locateVisible(1));
    CHECK(located == 2);
    panel->locationControl()->setSelectedIndexAndNotify(0);
    panel->locationControl()->setSelectedIndexAndNotify(0);
    CHECK(located == 4);
    panel->setReport(L"Unmodified bake progress report");
    CHECK(panel->reportText() == L"Unmodified bake progress report");
    CHECK(!panel->locateVisible(0));
    panel->setEntries(entries); CHECK(located == 4);
    panel->setOnLocate({}); CHECK(!panel->locateVisible(0));
    ayt::ui::destroyWidgetTree(panel);
}
TEST_SUITE_END

TEST_SUITE(AYUI_JobPresentation)
TEST_CASE(job_presentation_ignores_stale_results_and_notifies_completion_once)
{
    using namespace ayt::ui::authoring;
    JobPresentation view;
    JobStatusSnapshot job{7, JobState::Running, 0.1f, L"working", {}, true};
    CHECK(!view.observe(job).accepted);
    view.begin(7);
    auto change = view.observe(job);
    CHECK(change.accepted); CHECK(change.changed); CHECK(!change.completed);
    CHECK(!view.observe(job).changed);
    job.progress = 0.101f; CHECK(!view.observe(job).changed);
    job.message = L"step two"; CHECK(view.observe(job).changed);
    job.generation = 6; CHECK(!view.observe(job).accepted);
    job.generation = 7; job.state = JobState::Succeeded;
    job.progress = 1; job.outputs = {L"owner/output"};
    change = view.observe(job); CHECK(change.changed); CHECK(change.completed);
    CHECK(!view.observe(job).completed);
    job.state = JobState::Running; CHECK(!view.observe(job).accepted);
    view.begin(8); job.generation = 7; CHECK(!view.observe(job).accepted);
    job.generation = 8; job.state = JobState::Failed; CHECK(view.observe(job).completed);
    CHECK(formatJobReport(job, L"FAILED", false).find(L"owner/output") != std::wstring::npos);
    view.reset(); CHECK(!view.observe(job).accepted);
}
TEST_CASE(job_progress_and_cancel_use_bounded_values_and_fresh_capabilities)
{
    using namespace ayt::ui::authoring;
    JobProgressPresentation progress;
    CHECK(progress.consume(-1) == 0); CHECK(!progress.consume(0));
    CHECK(progress.consume(2) == 100);
    CHECK(progress.consume(std::numeric_limits<float>::quiet_NaN()) == 0);
    progress.reset(); CHECK(progress.consume(0) == 0);
    JobPresentation view; view.begin(2);
    JobStatusSnapshot job{2, JobState::Running, 0.4f, L"", {}, true};
    int cancels = 0;
    auto cancel = [&](std::uint64_t id) { CHECK(id == 2u); ++cancels; return true; };
    auto stale = job; stale.generation = 1;
    CHECK(!view.requestCancel(stale, cancel));
    stale = job; stale.cancellable = false; CHECK(!view.requestCancel(stale, cancel));
    stale = job; stale.state = JobState::Succeeded; CHECK(!view.requestCancel(stale, cancel));
    CHECK(!view.requestCancel(job, {})); CHECK(cancels == 0);
    CHECK(!view.requestCancel(job, [](std::uint64_t) { return false; }));
    CHECK(view.requestCancel(job, cancel)); CHECK(cancels == 1);
    CHECK(!view.requestCancel(job, cancel));
    view.begin(2); job.state = JobState::Cancelled; CHECK(view.observe(job).completed);
    job.state = JobState::Running; CHECK(!view.requestCancel(job, cancel));
    CHECK(formatJobReport(job, L"Importing", true) == L"Importing 40%");
}
TEST_SUITE_END

TEST_SUITE(AYUI_PropertyField)
TEST_CASE(property_field_refresh_validation_readonly_and_enum_preservation)
{
    using namespace ayt::ui::authoring;
    PropertyFieldOptions options;
    options.inputId = "owner-value";
    auto* field = new PropertyField(options);
    int edits = 0, submits = 0;
    field->setOnEdited([&] { ++edits; });
    field->setOnSubmitted([&] { ++submits; CHECK(!field->requestSubmit()); });
    field->setTextValue(L"Name", L"old");
    CHECK(field->input()->getId() == "owner-value");
    CHECK(edits == 0); CHECK(submits == 0);
    field->input()->setText(L"draft");
    CHECK(edits == 1);
    field->setValidator([](const std::wstring& value, std::wstring& error) {
        if (value == L"draft") { error = L"owner rejected"; return false; }
        return true;
    });
    CHECK(!field->requestSubmit()); CHECK(submits == 0);
    CHECK(field->validationError() == L"owner rejected");
    field->setTextValue(L"Name", L"good", true);
    CHECK(!field->requestSubmit());
    field->setChoiceValue(L"Type", L"unknown", {L"A", L"B", L"A"}, true);
    CHECK(field->value() == L"unknown"); CHECK(submits == 0);
    field->choice()->setSelectedIndexAndNotify(1);
    CHECK(field->value() == L"A"); CHECK(submits == 1);
    field->setChoiceValue(L"Type", L"B", {L"A", L"B"}, false, true);
    CHECK(!field->choice()->isEnabled()); CHECK(!field->requestSubmit());
    field->setTextValue(L"", L"ignored");
    CHECK(!field->isVisible()); CHECK(field->value() == L"B");
    CHECK(!field->requestSubmit());
    field->setTextValue(L"Name", L"good");
    CHECK(field->requestSubmit()); CHECK(submits == 2);
    ayt::ui::destroyWidgetTree(field);
}
TEST_SUITE_END

int main() { return ayt::test::runAllTests("AYUI curve authoring"); }
