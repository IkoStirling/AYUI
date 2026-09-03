#pragma once

#include "AYMath/MathTypes.h"
#include "AYUI/IRenderBackend.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace ayt::ui {

// Decorative motion is suppressed when reduced-motion is enabled. Essential
// motion remains available for hosts that must preserve an operational signal
// (for example a short progress acknowledgement), while still honoring the
// global duration scale.
enum class AnimationImportance : uint8_t {
    Decorative,
    Essential,
};

enum class AnimationPlaybackState : uint8_t {
    Idle,
    Running,
    Paused,
    Completed,
    Cancelled,
};

struct AnimationCallbacks {
    std::function<void()> onCompleted;
    std::function<void()> onCancelled;
};

struct AnimationOptions {
    float durationMs = 0.0f;
    AnimationCurve curve = AnimationCurve::EaseOut;
    AnimationImportance importance = AnimationImportance::Decorative;
    AnimationCallbacks callbacks;
};

// Optional second-order spring response for a timeline segment. Values use
// ordinary physical units and are evaluated over the authored keyframe span:
// mass must be positive, stiffness must be positive, damping is non-negative,
// and initialVelocity is expressed in normalized value units per second.
// Invalid values are sanitized by the sampler so data-driven authoring cannot
// introduce NaNs into a track.
struct SpringParameters {
    float mass = 1.0f;
    float stiffness = 170.0f;
    float damping = 18.0f;
    float initialVelocity = 0.0f;
    bool clampOvershoot = false;
};

// Process-wide UI animation policy. AYUI is driven from one UI thread, so the
// policy intentionally avoids locks. durationScale uses CSS-like semantics:
// 1 = authored speed, 2 = twice as long, 0 = finish finite tweens immediately.
class AnimationSettings {
public:
    static AnimationSettings& get();

    void setDurationScale(float scale);
    float getDurationScale() const { return _durationScale; }

    void setReducedMotion(bool enabled) { _reducedMotion = enabled; }
    bool isReducedMotion() const { return _reducedMotion; }

    bool shouldAnimate(AnimationImportance importance) const;
    float playbackDelta(float dt, AnimationImportance importance) const;

    // Restores production defaults. Primarily useful for isolated tests and
    // hosts switching profiles during shutdown/reinitialization.
    void reset();

private:
    float _durationScale = 1.0f;
    bool _reducedMotion = false;
};

template <typename T>
struct AnimationKeyframe {
    float timeMs = 0.0f;
    T value{};
    // Curve used while approaching this keyframe from the previous one.
    AnimationCurve curve = AnimationCurve::Linear;
    SpringParameters spring;
    bool hasSpringParameters = false;

    AnimationKeyframe() = default;
    AnimationKeyframe(float atMs, const T& v,
                      AnimationCurve c = AnimationCurve::Linear)
        : timeMs(atMs), value(v), curve(c) {}
    AnimationKeyframe(float atMs, const T& v, const SpringParameters& parameters)
        : timeMs(atMs), value(v), curve(AnimationCurve::Spring),
          spring(parameters), hasSpringParameters(true) {}
};

// A host-owned, CPU-side timeline. Tracks are sampled together from one clock,
// which keeps opacity/position/color keyframes synchronized without relying on
// renderer-owned animation handles. Call tick(dt) from a Widget override or a
// host update loop; Widget::tick remains the normal convenience path for its
// built-in opacity and position tweens.
class AnimationTimeline {
public:
    // A repeat count describes additional iterations after the first one.
    // 0 plays once, 2 plays three times, and RepeatForever never completes
    // without cancellation. Yoyo alternates direction on each iteration.
    static constexpr int RepeatForever = -1;

    using FloatApply = std::function<void(float)>;
    using Vec2Apply = std::function<void(const math::FVector2&)>;
    using Vec4Apply = std::function<void(const math::FVector4&)>;

    AnimationTimeline& addFloatTrack(
        std::vector<AnimationKeyframe<float>> keyframes, FloatApply apply);
    AnimationTimeline& addVec2Track(
        std::vector<AnimationKeyframe<math::FVector2>> keyframes, Vec2Apply apply);
    AnimationTimeline& addVec4Track(
        std::vector<AnimationKeyframe<math::FVector4>> keyframes, Vec4Apply apply);

    AnimationTimeline& setCallbacks(AnimationCallbacks callbacks) {
        _callbacks = std::move(callbacks);
        return *this;
    }
    AnimationTimeline& setImportance(AnimationImportance importance) {
        _importance = importance;
        return *this;
    }
    AnimationTimeline& setRepeatCount(int repeatCount);
    AnimationTimeline& setYoyo(bool enabled) {
        _yoyo = enabled;
        return *this;
    }

    void play();
    void pause();
    void resume();
    void cancel();
    void tick(float dt);

    AnimationPlaybackState getState() const { return _state; }
    bool isRunning() const {
        return _state == AnimationPlaybackState::Running
            || _state == AnimationPlaybackState::Paused;
    }
    bool isPaused() const { return _state == AnimationPlaybackState::Paused; }
    float getDurationMs() const { return _durationMs; }
    float getTotalDurationMs() const;
    float getCurrentTimeMs() const { return _currentTimeMs; }
    int getRepeatCount() const { return _repeatCount; }
    bool isYoyo() const { return _yoyo; }
    uint64_t getCurrentIteration() const { return _currentIteration; }
    bool isPlayingReverse() const { return _playingReverse; }

private:
    struct FloatTrack {
        std::vector<AnimationKeyframe<float>> keyframes;
        FloatApply apply;
    };
    struct Vec2Track {
        std::vector<AnimationKeyframe<math::FVector2>> keyframes;
        Vec2Apply apply;
    };
    struct Vec4Track {
        std::vector<AnimationKeyframe<math::FVector4>> keyframes;
        Vec4Apply apply;
    };

    void sample(float timeMs);
    void recomputeDuration();
    float advance(float dt);
    void updatePlaybackPosition(double elapsedMs);
    void finishImmediately();
    double totalPlaybackDurationMs() const;
    void complete();

    friend class AnimationSequence;

    std::vector<FloatTrack> _floatTracks;
    std::vector<Vec2Track> _vec2Tracks;
    std::vector<Vec4Track> _vec4Tracks;
    AnimationCallbacks _callbacks;
    AnimationImportance _importance = AnimationImportance::Decorative;
    AnimationPlaybackState _state = AnimationPlaybackState::Idle;
    float _durationMs = 0.0f;
    float _currentTimeMs = 0.0f;
    double _elapsedPlaybackMs = 0.0;
    int _repeatCount = 0;
    bool _yoyo = false;
    uint64_t _currentIteration = 0;
    bool _playingReverse = false;
};

// Owns timelines and plays them serially. `then` is intentionally an alias of
// append so call sites read naturally: sequence.append(fade).then(slide).
class AnimationSequence {
public:
    AnimationSequence& append(AnimationTimeline timeline);
    AnimationSequence& then(AnimationTimeline timeline) {
        return append(std::move(timeline));
    }
    AnimationSequence& setCallbacks(AnimationCallbacks callbacks) {
        _callbacks = std::move(callbacks);
        return *this;
    }

    void play();
    void pause();
    void resume();
    void cancel();
    void tick(float dt);

    AnimationPlaybackState getState() const { return _state; }
    size_t getCurrentIndex() const { return _currentIndex; }
    size_t size() const { return _steps.size(); }

private:
    void startCurrent();
    void complete();

    std::vector<AnimationTimeline> _steps;
    AnimationCallbacks _callbacks;
    AnimationPlaybackState _state = AnimationPlaybackState::Idle;
    size_t _currentIndex = 0;
};

} // namespace ayt::ui
