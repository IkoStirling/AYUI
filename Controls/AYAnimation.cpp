#include "AYUI/Animation.h"
#include "AYUI/Tween.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

AnimationSettings& AnimationSettings::get() {
    static AnimationSettings settings;
    return settings;
}

void AnimationSettings::setDurationScale(float scale) {
    _durationScale = std::isfinite(scale) ? std::max(0.0f, scale) : 1.0f;
}

bool AnimationSettings::shouldAnimate(AnimationImportance importance) const {
    return _durationScale > 0.0f
        && (!_reducedMotion || importance == AnimationImportance::Essential);
}

float AnimationSettings::playbackDelta(
    float dt, AnimationImportance importance) const {
    if (!shouldAnimate(importance) || dt <= 0.0f) return 0.0f;
    return dt / _durationScale;
}

void AnimationSettings::reset() {
    _durationScale = 1.0f;
    _reducedMotion = false;
}

namespace {

template <typename T>
void normalizeKeyframes(std::vector<AnimationKeyframe<T>>& keyframes) {
    for (auto& key : keyframes) key.timeMs = std::max(0.0f, key.timeMs);
    std::stable_sort(keyframes.begin(), keyframes.end(),
        [](const AnimationKeyframe<T>& a, const AnimationKeyframe<T>& b) {
            return a.timeMs < b.timeMs;
        });
}

template <typename T>
T sampleKeyframes(const std::vector<AnimationKeyframe<T>>& keys, float timeMs) {
    if (keys.empty()) return T{};
    if (timeMs <= keys.front().timeMs) return keys.front().value;
    if (timeMs >= keys.back().timeMs) return keys.back().value;

    const auto right = std::upper_bound(keys.begin(), keys.end(), timeMs,
        [](float time, const AnimationKeyframe<T>& key) {
            return time < key.timeMs;
        });
    const auto left = right - 1;
    const float span = right->timeMs - left->timeMs;
    if (span <= 0.0f) return right->value;
    const float linear = (timeMs - left->timeMs) / span;
    return tweenLerp(left->value, right->value,
                     easeCurve(linear, right->curve));
}

template <typename Track>
float trackDuration(const Track& track) {
    return track.keyframes.empty() ? 0.0f : track.keyframes.back().timeMs;
}

} // namespace

AnimationTimeline& AnimationTimeline::addFloatTrack(
    std::vector<AnimationKeyframe<float>> keyframes, FloatApply apply) {
    normalizeKeyframes(keyframes);
    _floatTracks.push_back({std::move(keyframes), std::move(apply)});
    recomputeDuration();
    return *this;
}

AnimationTimeline& AnimationTimeline::addVec2Track(
    std::vector<AnimationKeyframe<math::FVector2>> keyframes, Vec2Apply apply) {
    normalizeKeyframes(keyframes);
    _vec2Tracks.push_back({std::move(keyframes), std::move(apply)});
    recomputeDuration();
    return *this;
}

AnimationTimeline& AnimationTimeline::addVec4Track(
    std::vector<AnimationKeyframe<math::FVector4>> keyframes, Vec4Apply apply) {
    normalizeKeyframes(keyframes);
    _vec4Tracks.push_back({std::move(keyframes), std::move(apply)});
    recomputeDuration();
    return *this;
}

void AnimationTimeline::recomputeDuration() {
    _durationMs = 0.0f;
    for (const auto& track : _floatTracks)
        _durationMs = std::max(_durationMs, trackDuration(track));
    for (const auto& track : _vec2Tracks)
        _durationMs = std::max(_durationMs, trackDuration(track));
    for (const auto& track : _vec4Tracks)
        _durationMs = std::max(_durationMs, trackDuration(track));
}

void AnimationTimeline::sample(float timeMs) {
    for (const auto& track : _floatTracks) {
        if (track.apply && !track.keyframes.empty())
            track.apply(sampleKeyframes(track.keyframes, timeMs));
    }
    for (const auto& track : _vec2Tracks) {
        if (track.apply && !track.keyframes.empty())
            track.apply(sampleKeyframes(track.keyframes, timeMs));
    }
    for (const auto& track : _vec4Tracks) {
        if (track.apply && !track.keyframes.empty())
            track.apply(sampleKeyframes(track.keyframes, timeMs));
    }
}

void AnimationTimeline::play() {
    _currentTimeMs = 0.0f;
    _state = AnimationPlaybackState::Running;
    sample(0.0f);
    if (_durationMs <= 0.0f
        || !AnimationSettings::get().shouldAnimate(_importance)) {
        _currentTimeMs = _durationMs;
        sample(_durationMs);
        complete();
    }
}

void AnimationTimeline::pause() {
    if (_state == AnimationPlaybackState::Running)
        _state = AnimationPlaybackState::Paused;
}

void AnimationTimeline::resume() {
    if (_state == AnimationPlaybackState::Paused)
        _state = AnimationPlaybackState::Running;
}

void AnimationTimeline::cancel() {
    if (!isRunning()) return;
    _state = AnimationPlaybackState::Cancelled;
    auto callback = std::move(_callbacks.onCancelled);
    _callbacks.onCompleted = {};
    if (callback) callback();
}

void AnimationTimeline::complete() {
    if (_state == AnimationPlaybackState::Completed) return;
    _state = AnimationPlaybackState::Completed;
    auto callback = std::move(_callbacks.onCompleted);
    _callbacks.onCancelled = {};
    if (callback) callback();
}

void AnimationTimeline::tick(float dt) {
    if (_state != AnimationPlaybackState::Running) return;
    if (!AnimationSettings::get().shouldAnimate(_importance)) {
        _currentTimeMs = _durationMs;
    } else {
        _currentTimeMs += AnimationSettings::get().playbackDelta(dt, _importance)
                        * 1000.0f;
        _currentTimeMs = std::min(_currentTimeMs, _durationMs);
    }
    sample(_currentTimeMs);
    if (_currentTimeMs >= _durationMs) complete();
}

AnimationSequence& AnimationSequence::append(AnimationTimeline timeline) {
    _steps.push_back(std::move(timeline));
    return *this;
}

void AnimationSequence::play() {
    _currentIndex = 0;
    if (_steps.empty()) {
        _state = AnimationPlaybackState::Completed;
        auto callback = std::move(_callbacks.onCompleted);
        _callbacks.onCancelled = {};
        if (callback) callback();
        return;
    }
    _state = AnimationPlaybackState::Running;
    startCurrent();
}

void AnimationSequence::startCurrent() {
    if (_currentIndex >= _steps.size()) {
        complete();
        return;
    }
    _steps[_currentIndex].play();
    // Zero-duration/reduced-motion steps complete synchronously. Advance all
    // such steps now so a reduced-motion sequence reaches its final state in
    // the same call to play().
    while (_currentIndex < _steps.size()
           && _steps[_currentIndex].getState() == AnimationPlaybackState::Completed) {
        ++_currentIndex;
        if (_currentIndex < _steps.size()) _steps[_currentIndex].play();
    }
    if (_currentIndex >= _steps.size()) complete();
}

void AnimationSequence::pause() {
    if (_state != AnimationPlaybackState::Running) return;
    _state = AnimationPlaybackState::Paused;
    if (_currentIndex < _steps.size()) _steps[_currentIndex].pause();
}

void AnimationSequence::resume() {
    if (_state != AnimationPlaybackState::Paused) return;
    _state = AnimationPlaybackState::Running;
    if (_currentIndex < _steps.size()) _steps[_currentIndex].resume();
}

void AnimationSequence::cancel() {
    if (_state != AnimationPlaybackState::Running
        && _state != AnimationPlaybackState::Paused) return;
    if (_currentIndex < _steps.size()) _steps[_currentIndex].cancel();
    _state = AnimationPlaybackState::Cancelled;
    auto callback = std::move(_callbacks.onCancelled);
    _callbacks.onCompleted = {};
    if (callback) callback();
}

void AnimationSequence::complete() {
    if (_state == AnimationPlaybackState::Completed) return;
    _state = AnimationPlaybackState::Completed;
    auto callback = std::move(_callbacks.onCompleted);
    _callbacks.onCancelled = {};
    if (callback) callback();
}

void AnimationSequence::tick(float dt) {
    if (_state != AnimationPlaybackState::Running
        || _currentIndex >= _steps.size()) return;
    _steps[_currentIndex].tick(dt);
    if (_steps[_currentIndex].getState() == AnimationPlaybackState::Completed) {
        ++_currentIndex;
        startCurrent();
    }
}

} // namespace ayt::ui
