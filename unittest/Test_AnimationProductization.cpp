#include "AYTest.h"
#include "AYUI/Animation.h"
#include "AYUI/Button.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/ScrollableWidget.h"
#include "AYUI/Style.h"
#include "AYUI/Widget.h"

#include <cmath>
#include <cstring>

using namespace ayt::math;
using namespace ayt::ui;

TEST_SUITE(AYUI_AnimationProductization)

TEST_CASE(global_duration_scale_changes_widget_playback_speed) {
    AnimationSettings::get().reset();
    AnimationSettings::get().setDurationScale(2.0f);

    Widget widget;
    widget.setOpacity(0.0f);
    widget.animateOpacity(1.0f, 100.0f, AnimationCurve::Linear);
    widget.tick(0.10f);
    CHECK_FLOAT_EQ(widget.getOpacity(), 0.5f, 1e-5f);
    CHECK(widget.isOpacityAnimating());
    widget.tick(0.10f);
    CHECK_FLOAT_EQ(widget.getOpacity(), 1.0f, 1e-5f);
    CHECK_FALSE(widget.isOpacityAnimating());

    AnimationSettings::get().reset();
}

TEST_CASE(reduced_motion_snaps_decorative_but_preserves_essential) {
    AnimationSettings::get().reset();
    AnimationSettings::get().setReducedMotion(true);

    Widget widget;
    int decorativeCompleted = 0;
    AnimationOptions decorative;
    decorative.durationMs = 200.0f;
    decorative.callbacks.onCompleted = [&] { ++decorativeCompleted; };
    widget.animatePositionTo(FVector2(20.0f, 30.0f), decorative);
    CHECK_FALSE(widget.isPositionAnimating());
    CHECK(widget.getPosition() == FVector2(20.0f, 30.0f));
    CHECK(decorativeCompleted == 1);

    AnimationOptions essential;
    essential.durationMs = 100.0f;
    essential.curve = AnimationCurve::Linear;
    essential.importance = AnimationImportance::Essential;
    widget.setOpacity(0.0f);
    widget.animateOpacity(1.0f, essential);
    CHECK(widget.isOpacityAnimating());
    widget.tick(0.05f);
    CHECK_FLOAT_EQ(widget.getOpacity(), 0.5f, 1e-5f);

    AnimationSettings::get().reset();
}

TEST_CASE(reduced_motion_stops_scroll_momentum_after_immediate_wheel_step) {
    AnimationSettings::get().reset();
    ScrollableWidget scroll;
    scroll.setContentSize(FVector2(100.0f, 1000.0f));
    const FVector2 viewport(100.0f, 100.0f);
    CHECK(scroll.applyWheel(FVector2(0.0f, 80.0f), viewport));
    CHECK_FLOAT_EQ(scroll.getScrollOffset().y, 80.0f, 1e-5f);

    AnimationSettings::get().setReducedMotion(true);
    FVector2 delta;
    CHECK_FALSE(scroll.advanceMomentum(0.016f, viewport, delta));
    CHECK_FLOAT_EQ(delta.y, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(scroll.getScrollOffset().y, 80.0f, 1e-5f);
    AnimationSettings::get().reset();
}

TEST_CASE(widget_callbacks_pause_resume_and_cancel_are_deterministic) {
    AnimationSettings::get().reset();
    Widget widget;
    widget.setOpacity(0.0f);
    int completed = 0;
    int cancelled = 0;
    AnimationOptions options;
    options.durationMs = 100.0f;
    options.curve = AnimationCurve::Linear;
    options.callbacks.onCompleted = [&] { ++completed; };
    options.callbacks.onCancelled = [&] { ++cancelled; };

    widget.animateOpacity(1.0f, options);
    widget.tick(0.04f);
    CHECK_FLOAT_EQ(widget.getOpacity(), 0.4f, 1e-5f);
    widget.pauseAnimations();
    CHECK(widget.areAnimationsPaused());
    widget.tick(1.0f);
    CHECK_FLOAT_EQ(widget.getOpacity(), 0.4f, 1e-5f);
    widget.resumeAnimations();
    widget.tick(0.06f);
    CHECK(completed == 1);
    CHECK(cancelled == 0);

    widget.animateOpacity(0.0f, options);
    widget.cancelAnimations(true);
    CHECK_FALSE(widget.isOpacityAnimating());
    CHECK_FLOAT_EQ(widget.getOpacity(), 0.0f, 1e-5f);
    CHECK(completed == 1);
    CHECK(cancelled == 1);

    widget.animateOpacity(1.0f, options);
    widget.setOpacity(widget.getOpacity());
    CHECK_FALSE(widget.isOpacityAnimating());
    CHECK(cancelled == 2);
}

TEST_CASE(timeline_samples_synchronized_keyframe_tracks) {
    AnimationSettings::get().reset();
    float scalar = -1.0f;
    FVector2 position;
    FVector4 color;
    int completed = 0;

    AnimationTimeline timeline;
    timeline.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 10.0f, AnimationCurve::Linear}},
        [&](float value) { scalar = value; });
    timeline.addVec2Track(
        {{0.0f, FVector2(0.0f, 0.0f)},
         {100.0f, FVector2(20.0f, -10.0f), AnimationCurve::Linear}},
        [&](const FVector2& value) { position = value; });
    timeline.addVec4Track(
        {{0.0f, FVector4(0.0f, 0.0f, 0.0f, 0.0f)},
         {100.0f, FVector4(1.0f, 0.5f, 0.25f, 1.0f),
          AnimationCurve::Linear}},
        [&](const FVector4& value) { color = value; });
    AnimationCallbacks callbacks;
    callbacks.onCompleted = [&] { ++completed; };
    timeline.setCallbacks(std::move(callbacks));

    timeline.play();
    timeline.tick(0.05f);
    CHECK_FLOAT_EQ(scalar, 5.0f, 1e-5f);
    CHECK_FLOAT_EQ(position.x, 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(position.y, -5.0f, 1e-5f);
    CHECK_FLOAT_EQ(color.w, 0.5f, 1e-5f);
    timeline.pause();
    timeline.tick(1.0f);
    CHECK_FLOAT_EQ(scalar, 5.0f, 1e-5f);
    timeline.resume();
    timeline.tick(0.05f);
    CHECK(timeline.getState() == AnimationPlaybackState::Completed);
    CHECK(completed == 1);
    CHECK_FLOAT_EQ(scalar, 10.0f, 1e-5f);
}

TEST_CASE(sequence_then_plays_timelines_serially) {
    AnimationSettings::get().reset();
    float value = -1.0f;
    int completed = 0;

    AnimationTimeline first;
    first.addFloatTrack(
        {{0.0f, 0.0f}, {10.0f, 1.0f, AnimationCurve::Linear}},
        [&](float v) { value = v; });
    AnimationTimeline second;
    second.addFloatTrack(
        {{0.0f, 1.0f}, {10.0f, 3.0f, AnimationCurve::Linear}},
        [&](float v) { value = v; });

    AnimationCallbacks callbacks;
    callbacks.onCompleted = [&] { ++completed; };
    AnimationSequence sequence;
    sequence.append(std::move(first)).then(std::move(second));
    sequence.setCallbacks(std::move(callbacks));
    sequence.play();
    sequence.tick(0.01f);
    CHECK_FLOAT_EQ(value, 1.0f, 1e-5f);
    CHECK(sequence.getCurrentIndex() == 1u);
    sequence.tick(0.01f);
    CHECK_FLOAT_EQ(value, 3.0f, 1e-5f);
    CHECK(sequence.getState() == AnimationPlaybackState::Completed);
    CHECK(completed == 1);
}

TEST_CASE(timeline_repeat_and_yoyo_preserve_direction_and_callbacks) {
    AnimationSettings::get().reset();
    float value = -1.0f;
    int completed = 0;
    AnimationTimeline timeline;
    timeline.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 10.0f, AnimationCurve::Linear}},
        [&](float v) { value = v; });
    timeline.setRepeatCount(1).setYoyo(true);
    AnimationCallbacks callbacks;
    callbacks.onCompleted = [&] { ++completed; };
    timeline.setCallbacks(std::move(callbacks));

    CHECK(timeline.getRepeatCount() == 1);
    CHECK(timeline.isYoyo());
    CHECK_FLOAT_EQ(timeline.getTotalDurationMs(), 200.0f, 1e-5f);
    timeline.play();
    timeline.tick(0.10f);
    CHECK(timeline.getCurrentIteration() == 1u);
    CHECK(timeline.isPlayingReverse());
    CHECK_FLOAT_EQ(value, 10.0f, 1e-5f);
    timeline.tick(0.025f);
    CHECK_FLOAT_EQ(value, 7.5f, 1e-5f);
    timeline.tick(0.075f);
    CHECK(timeline.getState() == AnimationPlaybackState::Completed);
    CHECK_FLOAT_EQ(value, 0.0f, 1e-5f);
    CHECK(completed == 1);
    timeline.tick(1.0f);
    CHECK(completed == 1);
}

TEST_CASE(timeline_large_delta_crosses_repeats_without_losing_time) {
    AnimationSettings::get().reset();
    float value = -1.0f;
    AnimationTimeline timeline;
    timeline.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 1.0f, AnimationCurve::Linear}},
        [&](float v) { value = v; });
    timeline.setRepeatCount(2);
    timeline.play();
    timeline.tick(0.25f);
    CHECK(timeline.getCurrentIteration() == 2u);
    CHECK_FALSE(timeline.isPlayingReverse());
    CHECK_FLOAT_EQ(value, 0.5f, 1e-5f);
    CHECK(timeline.getState() == AnimationPlaybackState::Running);
    timeline.tick(0.05f);
    CHECK(timeline.getState() == AnimationPlaybackState::Completed);
    CHECK_FLOAT_EQ(value, 1.0f, 1e-5f);
}

TEST_CASE(infinite_yoyo_timeline_runs_until_cancelled) {
    AnimationSettings::get().reset();
    float value = -1.0f;
    int cancelled = 0;
    AnimationTimeline timeline;
    timeline.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 1.0f, AnimationCurve::Linear}},
        [&](float v) { value = v; });
    timeline.setRepeatCount(AnimationTimeline::RepeatForever).setYoyo(true);
    AnimationCallbacks callbacks;
    callbacks.onCancelled = [&] { ++cancelled; };
    timeline.setCallbacks(std::move(callbacks));

    timeline.play();
    CHECK(std::isinf(timeline.getTotalDurationMs()));
    timeline.tick(0.225f);
    CHECK(timeline.getCurrentIteration() == 2u);
    CHECK_FALSE(timeline.isPlayingReverse());
    CHECK_FLOAT_EQ(value, 0.25f, 1e-5f);
    timeline.cancel();
    CHECK(timeline.getState() == AnimationPlaybackState::Cancelled);
    CHECK(cancelled == 1);
}

TEST_CASE(reduced_motion_uses_stable_final_pose_for_looping_timelines) {
    AnimationSettings::get().reset();
    AnimationSettings::get().setReducedMotion(true);
    float finiteValue = -1.0f;
    AnimationTimeline finite;
    finite.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 1.0f, AnimationCurve::Linear}},
        [&](float v) { finiteValue = v; });
    finite.setRepeatCount(1).setYoyo(true);
    finite.play();
    CHECK(finite.getState() == AnimationPlaybackState::Completed);
    CHECK_FLOAT_EQ(finiteValue, 0.0f, 1e-5f);

    float infiniteValue = -1.0f;
    AnimationTimeline infinite;
    infinite.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 1.0f, AnimationCurve::Linear}},
        [&](float v) { infiniteValue = v; });
    infinite.setRepeatCount(AnimationTimeline::RepeatForever).setYoyo(true);
    infinite.play();
    CHECK(infinite.getState() == AnimationPlaybackState::Completed);
    CHECK_FLOAT_EQ(infiniteValue, 1.0f, 1e-5f);
    AnimationSettings::get().reset();
}

TEST_CASE(physical_spring_parameters_control_overshoot) {
    AnimationSettings::get().reset();
    SpringParameters loose;
    loose.mass = 1.0f;
    loose.stiffness = 100.0f;
    loose.damping = 1.0f;

    SpringParameters clamped = loose;
    clamped.clampOvershoot = true;

    float looseValue = 0.0f;
    float clampedValue = 0.0f;
    AnimationTimeline looseTimeline;
    looseTimeline.addFloatTrack(
        {AnimationKeyframe<float>(0.0f, 0.0f),
         AnimationKeyframe<float>(500.0f, 1.0f, loose)},
        [&](float v) { looseValue = v; });
    AnimationTimeline clampedTimeline;
    clampedTimeline.addFloatTrack(
        {AnimationKeyframe<float>(0.0f, 0.0f),
         AnimationKeyframe<float>(500.0f, 1.0f, clamped)},
        [&](float v) { clampedValue = v; });

    looseTimeline.play();
    clampedTimeline.play();
    looseTimeline.tick(0.25f);
    clampedTimeline.tick(0.25f);
    CHECK(looseValue > 1.0f);
    CHECK_FLOAT_EQ(clampedValue, 1.0f, 1e-5f);
    looseTimeline.tick(0.25f);
    clampedTimeline.tick(0.25f);
    CHECK_FLOAT_EQ(looseValue, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(clampedValue, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(evaluateSpring(0.0f, 500.0f, loose), 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(evaluateSpring(1.0f, 500.0f, loose), 1.0f, 1e-6f);
}

TEST_CASE(cubic_bezier_parameters_drive_timeline_sampling) {
    AnimationSettings::get().reset();
    CubicBezierParameters easeIn;
    easeIn.x1 = 0.42f;
    easeIn.y1 = 0.0f;
    easeIn.x2 = 1.0f;
    easeIn.y2 = 1.0f;

    float value = 0.0f;
    AnimationTimeline timeline;
    timeline.addFloatTrack(
        {AnimationKeyframe<float>(0.0f, 0.0f),
         AnimationKeyframe<float>(1000.0f, 1.0f, easeIn)},
        [&](float sampled) { value = sampled; });
    timeline.seek(500.0f);
    CHECK(value > 0.30f);
    CHECK(value < 0.33f);
    CHECK_FLOAT_EQ(evaluateCubicBezier(0.0f, easeIn), 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(evaluateCubicBezier(1.0f, easeIn), 1.0f, 1e-6f);
}

TEST_CASE(reduced_motion_provider_bridges_host_platform_preference) {
    AnimationSettings& settings = AnimationSettings::get();
    settings.reset();
    settings.setReducedMotionProvider([]() -> std::optional<bool> {
        return true;
    });
    CHECK(settings.refreshReducedMotionPreference());
    CHECK(settings.isReducedMotion());
    settings.setReducedMotionProvider([]() -> std::optional<bool> {
        return false;
    });
    CHECK(settings.refreshReducedMotionPreference());
    CHECK_FALSE(settings.isReducedMotion());
    settings.reset();
}

TEST_CASE(sequence_carries_large_delta_into_the_next_step) {
    AnimationSettings::get().reset();
    float value = -1.0f;
    AnimationTimeline first;
    first.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 1.0f, AnimationCurve::Linear}},
        [&](float v) { value = v; });
    AnimationTimeline second;
    second.addFloatTrack(
        {{0.0f, 1.0f}, {100.0f, 3.0f, AnimationCurve::Linear}},
        [&](float v) { value = v; });

    AnimationSequence sequence;
    sequence.append(std::move(first)).then(std::move(second));
    sequence.play();
    sequence.tick(0.15f);
    CHECK(sequence.getCurrentIndex() == 1u);
    CHECK_FLOAT_EQ(value, 2.0f, 1e-5f);
    sequence.tick(0.05f);
    CHECK(sequence.getState() == AnimationPlaybackState::Completed);
    CHECK_FLOAT_EQ(value, 3.0f, 1e-5f);
}

TEST_CASE(timeline_and_sequence_pause_cancel_callbacks_fire_once) {
    AnimationSettings::get().reset();
    float timelineValue = 0.0f;
    int timelineCompleted = 0;
    int timelineCancelled = 0;
    AnimationTimeline timeline;
    timeline.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 1.0f, AnimationCurve::Linear}},
        [&](float value) { timelineValue = value; });
    AnimationCallbacks timelineCallbacks;
    timelineCallbacks.onCompleted = [&] { ++timelineCompleted; };
    timelineCallbacks.onCancelled = [&] { ++timelineCancelled; };
    timeline.setCallbacks(std::move(timelineCallbacks));
    timeline.play();
    timeline.tick(0.02f);
    CHECK_FLOAT_EQ(timelineValue, 0.2f, 1e-5f);
    timeline.pause();
    CHECK(timeline.getState() == AnimationPlaybackState::Paused);
    timeline.tick(1.0f);
    CHECK_FLOAT_EQ(timelineValue, 0.2f, 1e-5f);
    timeline.cancel();
    timeline.cancel();
    CHECK(timeline.getState() == AnimationPlaybackState::Cancelled);
    CHECK(timelineCancelled == 1);
    CHECK(timelineCompleted == 0);

    float sequenceValue = 0.0f;
    int sequenceCancelled = 0;
    AnimationTimeline step;
    step.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 1.0f, AnimationCurve::Linear}},
        [&](float value) { sequenceValue = value; });
    AnimationCallbacks sequenceCallbacks;
    sequenceCallbacks.onCancelled = [&] { ++sequenceCancelled; };
    AnimationSequence sequence;
    sequence.append(std::move(step));
    sequence.setCallbacks(std::move(sequenceCallbacks));
    sequence.play();
    sequence.tick(0.025f);
    CHECK_FLOAT_EQ(sequenceValue, 0.25f, 1e-5f);
    sequence.pause();
    CHECK(sequence.getState() == AnimationPlaybackState::Paused);
    sequence.tick(1.0f);
    CHECK_FLOAT_EQ(sequenceValue, 0.25f, 1e-5f);
    sequence.cancel();
    CHECK(sequence.getState() == AnimationPlaybackState::Cancelled);
    CHECK(sequenceCancelled == 1);
}

TEST_CASE(reduced_motion_timeline_completes_synchronously) {
    AnimationSettings::get().reset();
    AnimationSettings::get().setReducedMotion(true);
    float value = 0.0f;
    int completed = 0;
    AnimationTimeline timeline;
    timeline.addFloatTrack(
        {{0.0f, 0.0f}, {500.0f, 1.0f, AnimationCurve::EaseOut}},
        [&](float v) { value = v; });
    AnimationCallbacks callbacks;
    callbacks.onCompleted = [&] { ++completed; };
    timeline.setCallbacks(std::move(callbacks));
    timeline.play();
    CHECK(timeline.getState() == AnimationPlaybackState::Completed);
    CHECK_FLOAT_EQ(value, 1.0f, 1e-5f);
    CHECK(completed == 1);
    AnimationSettings::get().reset();
}

TEST_CASE(timeline_seek_scrubs_without_changing_playback_state) {
    float value = -1.0f;
    AnimationTimeline timeline;
    timeline.addFloatTrack(
        {{0.0f, 0.0f}, {100.0f, 10.0f, AnimationCurve::Linear}},
        [&](float sampled) { value = sampled; });

    CHECK(timeline.getState() == AnimationPlaybackState::Idle);
    timeline.seek(25.0f);
    CHECK_FLOAT_EQ(value, 2.5f, 1e-5f);
    CHECK(timeline.getState() == AnimationPlaybackState::Idle);
    CHECK_FLOAT_EQ(timeline.getCurrentTimeMs(), 25.0f, 1e-5f);

    timeline.play();
    timeline.pause();
    timeline.seek(250.0f);
    CHECK_FLOAT_EQ(value, 10.0f, 1e-5f);
    CHECK(timeline.getState() == AnimationPlaybackState::Paused);
    CHECK(timeline.getCurrentIteration() == 0u);
    CHECK_FALSE(timeline.isPlayingReverse());
}

TEST_CASE(stylesheet_parses_state_colors_and_declarative_transition) {
    StyleSheet sheet;
    const char* json = R"({
        "styles": {
            "animated_button": {
                "backgroundColor": [0.1, 0.1, 0.1, 1.0],
                "states": {
                    "hovered": { "backgroundColor": [0.9, 0.5, 0.2, 1.0] },
                    "pressed": { "backgroundColor": [0.2, 0.4, 0.9, 1.0] },
                    "disabled": { "backgroundColor": [0.2, 0.2, 0.2, 0.5] }
                },
                "transition": {
                    "backgroundColor": { "durationMs": 120, "curve": "linear" }
                }
            }
        }
    })";
    CHECK(sheet.loadFromString(json, std::strlen(json)));
    const WidgetStyle* style = sheet.getStyle("animated_button");
    CHECK_NOT_NULL(style);
    CHECK(style->backgroundStates.enabled);
    CHECK_FLOAT_EQ(style->backgroundStates.hovered.x, 0.9f, 1e-5f);
    CHECK(style->backgroundTransition.enabled);
    CHECK_FLOAT_EQ(style->backgroundTransition.durationMs, 120.0f, 1e-5f);
    CHECK(style->backgroundTransition.curve == AnimationCurve::Linear);
}

TEST_CASE(stylesheet_accepts_cubic_bezier_transition_curve_name) {
    StyleSheet sheet;
    const char* json = R"({
        "styles": {
            "bezier_button": {
                "transition": {
                    "backgroundColor": {
                        "durationMs": 180,
                        "curve": "cubic-bezier"
                    }
                }
            }
        }
    })";
    CHECK(sheet.loadFromString(json, std::strlen(json)));
    const WidgetStyle* style = sheet.getStyle("bezier_button");
    CHECK_NOT_NULL(style);
    CHECK(style->backgroundTransition.curve == AnimationCurve::CubicBezier);
}

TEST_CASE(button_honors_stateful_style_transition_and_reduced_motion) {
    AnimationSettings::get().reset();
    StyleManager::get().setStyleSheet(nullptr);
    StyleSheet sheet;
    WidgetStyle style = StyleBuilder::makeButton();
    style.backgroundColor = FVector4(0.1f, 0.1f, 0.1f, 1.0f);
    style.backgroundStates.enabled = true;
    style.backgroundStates.normal = style.backgroundColor;
    style.backgroundStates.hovered = FVector4(0.9f, 0.9f, 0.9f, 1.0f);
    style.backgroundStates.pressed = FVector4(0.3f, 0.3f, 0.3f, 1.0f);
    style.backgroundStates.disabled = FVector4(0.2f, 0.2f, 0.2f, 0.5f);
    style.backgroundTransition.enabled = true;
    style.backgroundTransition.durationMs = 100.0f;
    style.backgroundTransition.curve = AnimationCurve::Linear;
    sheet.setStyle("stateful", style);
    StyleManager::get().setStyleSheet(&sheet);

    Button button;
    button.setStyleId("stateful");
    button.setSize(FVector2(100.0f, 30.0f));
    MockRenderer initial;
    button.render(initial);
    CHECK_FLOAT_EQ(initial.getDrawCalls().front().color.x, 0.1f, 1e-5f);

    button.onMouseMove(UIMouseEvent(FVector2(10.0f, 10.0f)));
    MockRenderer armed;
    button.render(armed);
    button.tick(0.05f);
    MockRenderer halfway;
    button.render(halfway);
    CHECK_FLOAT_EQ(halfway.getDrawCalls().front().color.x, 0.5f, 1e-4f);

    AnimationSettings::get().setReducedMotion(true);
    button.onMouseLeave();
    MockRenderer reduced;
    button.render(reduced);
    CHECK_FLOAT_EQ(reduced.getDrawCalls().front().color.x, 0.1f, 1e-5f);

    StyleManager::get().setStyleSheet(nullptr);
    AnimationSettings::get().reset();
}

TEST_SUITE_END
