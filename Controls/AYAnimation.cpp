#include "AYUI/Animation.h"
#include "AYUI/Tween.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

    float factor = easeCurve(linear, right->curve);
    if (right->curve == AnimationCurve::Spring
        && right->hasSpringParameters) {
        const SpringParameters& authored = right->spring;
        const auto finiteOr = [](float value, float fallback) {
            return std::isfinite(value) ? value : fallback;
        };
        const double mass = std::clamp(
            static_cast<double>(finiteOr(authored.mass, 1.0f)), 0.001, 1000.0);
        const double stiffness = std::clamp(
            static_cast<double>(finiteOr(authored.stiffness, 170.0f)),
            0.001, 1000000.0);
        const double damping = std::clamp(
            static_cast<double>(finiteOr(authored.damping, 18.0f)),
            0.0, 100000.0);
        const double initialVelocity = std::clamp(
            static_cast<double>(finiteOr(authored.initialVelocity, 0.0f)),
            -100000.0, 100000.0);
        const double seconds = static_cast<double>(linear * span) * 0.001;
        const double omega0 = std::sqrt(stiffness / mass);
        const double zeta = damping / (2.0 * std::sqrt(stiffness * mass));
        double displacement = 0.0;

        if (zeta < 1.0 - 1e-6) {
            const double omegaD = omega0 * std::sqrt(1.0 - zeta * zeta);
            const double a = -1.0;
            const double b = (initialVelocity - zeta * omega0) / omegaD;
            displacement = std::exp(-zeta * omega0 * seconds)
                * (a * std::cos(omegaD * seconds)
                   + b * std::sin(omegaD * seconds));
        } else if (zeta <= 1.0 + 1e-6) {
            const double a = -1.0;
            const double b = initialVelocity - omega0;
            displacement = (a + b * seconds) * std::exp(-omega0 * seconds);
        } else {
            const double root = std::sqrt(zeta * zeta - 1.0);
            const double r1 = -omega0 * (zeta - root);
            const double r2 = -omega0 * (zeta + root);
            const double c1 = (initialVelocity + r2) / (r1 - r2);
            const double c2 = -1.0 - c1;
            displacement = c1 * std::exp(r1 * seconds)
                + c2 * std::exp(r2 * seconds);
        }

        const double response = 1.0 + displacement;
        factor = std::isfinite(response)
            ? static_cast<float>(response) : linear;
        if (authored.clampOvershoot)
            factor = std::clamp(factor, 0.0f, 1.0f);
    }
    return tweenLerp(left->value, right->value, factor);
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

AnimationTimeline& AnimationTimeline::setRepeatCount(int repeatCount) {
    _repeatCount = std::max(RepeatForever, repeatCount);
    return *this;
}

float AnimationTimeline::getTotalDurationMs() const {
    if (_repeatCount == RepeatForever && _durationMs > 0.0f)
        return std::numeric_limits<float>::infinity();
    return static_cast<float>(totalPlaybackDurationMs());
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
    _elapsedPlaybackMs = 0.0;
    _currentTimeMs = 0.0f;
    _currentIteration = 0;
    _playingReverse = false;
    _state = AnimationPlaybackState::Running;
    sample(0.0f);
    if (_durationMs <= 0.0f
        || !AnimationSettings::get().shouldAnimate(_importance)) {
        finishImmediately();
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

double AnimationTimeline::totalPlaybackDurationMs() const {
    if (_repeatCount == RepeatForever)
        return std::numeric_limits<double>::infinity();
    return static_cast<double>(_durationMs)
        * static_cast<double>(_repeatCount + 1);
}

void AnimationTimeline::updatePlaybackPosition(double elapsedMs) {
    if (_durationMs <= 0.0f) {
        _currentIteration = 0;
        _playingReverse = false;
        _currentTimeMs = 0.0f;
        sample(0.0f);
        return;
    }

    const double duration = static_cast<double>(_durationMs);
    const bool finite = _repeatCount != RepeatForever;
    const double total = totalPlaybackDurationMs();
    uint64_t iteration = 0;
    double within = 0.0;

    if (finite && elapsedMs >= total - 1e-6) {
        iteration = static_cast<uint64_t>(_repeatCount);
        within = duration;
    } else {
        const double rawIteration = std::floor(std::max(0.0, elapsedMs) / duration);
        const double maxIteration = static_cast<double>(
            std::numeric_limits<uint64_t>::max());
        iteration = static_cast<uint64_t>(std::min(rawIteration, maxIteration));
        within = std::fmod(std::max(0.0, elapsedMs), duration);
    }

    _currentIteration = iteration;
    _playingReverse = _yoyo && ((iteration & 1u) != 0u);
    _currentTimeMs = static_cast<float>(
        _playingReverse ? duration - within : within);
    sample(_currentTimeMs);
}

void AnimationTimeline::finishImmediately() {
    // An infinite decorative loop has no finite parity. Reduced motion uses
    // the authored end pose as its stable replacement and completes once.
    if (_repeatCount == RepeatForever) {
        _elapsedPlaybackMs = static_cast<double>(_durationMs);
        _currentIteration = 0;
        _playingReverse = false;
        _currentTimeMs = _durationMs;
        sample(_currentTimeMs);
    } else {
        _elapsedPlaybackMs = totalPlaybackDurationMs();
        updatePlaybackPosition(_elapsedPlaybackMs);
    }
    complete();
}

float AnimationTimeline::advance(float dt) {
    if (_state != AnimationPlaybackState::Running) return 0.0f;
    if (dt <= 0.0f) return 0.0f;
    if (!AnimationSettings::get().shouldAnimate(_importance)) {
        finishImmediately();
        return dt;
    }

    const double requestedMs = static_cast<double>(
        AnimationSettings::get().playbackDelta(dt, _importance)) * 1000.0;
    if (requestedMs <= 0.0) return 0.0f;

    if (_repeatCount == RepeatForever) {
        _elapsedPlaybackMs += requestedMs;
        updatePlaybackPosition(_elapsedPlaybackMs);
        return 0.0f;
    }

    const double total = totalPlaybackDurationMs();
    const double remainingMs = std::max(0.0, total - _elapsedPlaybackMs);
    const double consumedMs = std::min(requestedMs, remainingMs);
    _elapsedPlaybackMs += consumedMs;
    updatePlaybackPosition(_elapsedPlaybackMs);

    if (_elapsedPlaybackMs >= total - 1e-6) {
        const float consumedHostSeconds = static_cast<float>(
            static_cast<double>(dt) * (consumedMs / requestedMs));
        complete();
        return std::max(0.0f, dt - consumedHostSeconds);
    }
    return 0.0f;
}

void AnimationTimeline::tick(float dt) {
    (void)advance(dt);
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

    float remaining = std::max(0.0f, dt);
    while (_state == AnimationPlaybackState::Running
           && _currentIndex < _steps.size()) {
        remaining = _steps[_currentIndex].advance(remaining);
        if (_steps[_currentIndex].getState()
            != AnimationPlaybackState::Completed) return;
        ++_currentIndex;
        startCurrent();
        if (remaining <= 0.0f) return;
    }
}

} // namespace ayt::ui
