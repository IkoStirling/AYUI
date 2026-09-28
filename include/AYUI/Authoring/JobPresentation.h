#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ayt::ui::authoring {
enum class JobState { Idle, Queued, Running, Succeeded, CacheHit, Failed, Cancelled };
struct JobStatusSnapshot {
    std::uint64_t generation = 0;
    JobState state = JobState::Idle;
    float progress = 0;
    std::wstring message;
    std::vector<std::wstring> outputs;
    bool cancellable = false;
    bool finished() const {
        return state == JobState::Succeeded || state == JobState::CacheHit
            || state == JobState::Failed || state == JobState::Cancelled;
    }
};

class JobProgressPresentation {
public:
    static int percent(float progress) {
        if (!std::isfinite(progress)) return 0;
        return static_cast<int>(std::round(std::clamp(progress, 0.0f, 1.0f) * 100.0f));
    }
    std::optional<int> consume(float progress) {
        const int next = percent(progress);
        if (_percent && *_percent == next) return {};
        _percent = next;
        return next;
    }
    void reset() { _percent.reset(); }
private:
    std::optional<int> _percent;
};
struct JobPresentationChange {
    bool accepted = false;
    bool changed = false;
    bool completed = false;
};

/** @brief UI-free job-to-view binding; never runs work or commits its results.
 * Hosts explicitly begin a project-scoped generation, poll on the UI thread,
 * and validate domain results before applying them. Old generations and terminal
 * regressions are ignored. Cancellation uses a freshly polled snapshot and an
 * optional host callback; reset before detaching the source/project.
 */
class JobPresentation {
public:
    void begin(std::uint64_t generation) {
        reset();
        _generation = generation;
    }
    void reset() {
        _generation = 0;
        _snapshot.reset();
        _completed = false;
        _cancelRequested = false;
    }
    std::uint64_t generation() const { return _generation; }
    JobPresentationChange observe(JobStatusSnapshot next) {
        if (_generation == 0 || next.generation != _generation || next.state == JobState::Idle) return {};
        if (_snapshot && _snapshot->finished() && next.state != _snapshot->state) return {};
        next.progress = std::isfinite(next.progress) ? std::clamp(next.progress, 0.0f, 1.0f) : 0.0f;
        const bool changed = !_snapshot || _snapshot->state != next.state
            || JobProgressPresentation::percent(_snapshot->progress) != JobProgressPresentation::percent(next.progress)
            || _snapshot->message != next.message || _snapshot->outputs != next.outputs
            || _snapshot->cancellable != next.cancellable;
        const bool completed = next.finished() && !_completed;
        _completed = _completed || completed;
        _snapshot = std::move(next);
        return {true, changed, completed};
    }
    bool requestCancel(const JobStatusSnapshot& fresh,
        const std::function<bool(std::uint64_t)>& cancel) {
        if (_generation == 0 || fresh.generation != _generation || _completed
            || _cancelRequested || !fresh.cancellable
            || (fresh.state != JobState::Running && fresh.state != JobState::Queued) || !cancel) return false;
        _cancelRequested = true;
        try {
            if (cancel(_generation)) return true;
        } catch (...) {
            _cancelRequested = false;
            throw;
        }
        _cancelRequested = false;
        return false;
    }
    const std::optional<JobStatusSnapshot>& snapshot() const { return _snapshot; }
private:
    std::uint64_t _generation = 0;
    std::optional<JobStatusSnapshot> _snapshot;
    bool _completed = false, _cancelRequested = false;
};

inline std::wstring formatJobReport(const JobStatusSnapshot& snapshot,
    const std::wstring& heading, bool showProgress, const std::wstring& separator = L"\n") {
    std::wstring report = heading;
    if (showProgress) report += L" " + std::to_wstring(JobProgressPresentation::percent(snapshot.progress)) + L"%";
    if (!snapshot.message.empty()) report += separator + snapshot.message;
    for (const auto& output : snapshot.outputs) report += L"\n" + output;
    return report;
}
} // namespace ayt::ui::authoring
