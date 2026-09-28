#pragma once

#include "AYUI/Authoring/TimelineModel.h"
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <utility>

namespace ayt::ui::authoring {

enum class TimeDisplay { Seconds, Frames, Adaptive };

// Full-string, finite float parsing; owners retain dimensional/Quaternion validation.
inline std::optional<float> parseFiniteFloat(const std::wstring& text) {
    try {
        std::size_t consumed = 0u;
        const float value = std::stof(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value)) return std::nullopt;
        return value;
    } catch (...) { return std::nullopt; }
}
inline std::wstring formatNumber(float value, int precision = 7) {
    std::wostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(std::clamp(precision, 1, 9)) << value;
    return text.str();
}

inline std::wstring formatTime(double seconds, TimeDisplay display = TimeDisplay::Seconds,
                               int precision = 3, double framesPerSecond = 30.0) {
    if (!std::isfinite(seconds)) seconds = 0.0;
    std::wostringstream text;
    text.imbue(std::locale::classic());
    if (display == TimeDisplay::Frames && framesPerSecond > 0.0
        && std::isfinite(framesPerSecond) && std::isfinite(seconds * framesPerSecond)) {
        text << std::fixed << std::setprecision(0) << std::round(seconds * framesPerSecond) << L"f";
    } else if (display == TimeDisplay::Adaptive && std::fabs(seconds) < 1.0) {
        text << std::fixed << std::setprecision(0) << std::round(seconds * 1000.0) << L"ms";
    } else {
        const int digits = display == TimeDisplay::Adaptive ? (std::fabs(seconds) >= 10.0 ? 0 : 1)
                                                          : std::clamp(precision, 0, 9);
        text << std::fixed << std::setprecision(digits) << seconds << L"s";
    }
    return text.str();
}

inline double snapTimeToInterval(double seconds, double interval) noexcept {
    if (!std::isfinite(seconds) || !std::isfinite(interval) || interval <= 0.0
        || !std::isfinite(seconds / interval)) return seconds;
    return std::round(seconds / interval) * interval;
}

// Pixel-density-aware 1/2/5 ticks, bounded even for enormous or invalid ranges.
inline std::vector<double> timelineTicks(TimeViewport view, double width, double spacing = 84.0) {
    if (!std::isfinite(view.startSeconds) || !std::isfinite(view.durationSeconds)
        || view.durationSeconds <= 0.0 || !std::isfinite(width) || width <= 0.0
        || !std::isfinite(spacing) || spacing <= 0.0) return {};
    const double target = std::max(1.0e-9, view.durationSeconds * spacing / width);
    if (!std::isfinite(target)) return {};
    const double decade = std::pow(10.0, std::floor(std::log10(target)));
    const double ratio = target / decade;
    const double step = decade * (ratio <= 1.0 ? 1.0 : ratio <= 2.0 ? 2.0 : ratio <= 5.0 ? 5.0 : 10.0);
    const double first = std::ceil(view.startSeconds / step) * step;
    const double end = view.startSeconds + view.durationSeconds;
    if (!std::isfinite(first) || !std::isfinite(end)) return {};
    std::vector<double> ticks;
    for (std::size_t i = 0; i < 1024u; ++i) {
        const double time = first + static_cast<double>(i) * step;
        if (!std::isfinite(time) || time > end + step * 1.0e-8) break;
        ticks.push_back(time);
    }
    return ticks;
}

struct TimelineRowLayout {
    double top = 0.0, height = 24.0, scrollOffset = 0.0;
    double rowTop(std::size_t row) const noexcept {
        return top + static_cast<double>(row) * std::max(1.0e-9, height) - scrollOffset;
    }
    double rowCenter(std::size_t row) const noexcept { return rowTop(row) + std::max(1.0e-9, height) * 0.5; }
    std::pair<std::size_t, std::size_t> visibleRows(std::size_t count, double viewportHeight) const noexcept {
        if (!std::isfinite(height) || height <= 0.0 || !std::isfinite(scrollOffset)
            || !std::isfinite(viewportHeight) || viewportHeight <= 0.0) return {0u, 0u};
        const double start = std::clamp(std::floor(scrollOffset / height), 0.0, static_cast<double>(count));
        const double end = std::clamp(std::ceil((scrollOffset + viewportHeight) / height), start,
                                      static_cast<double>(count));
        return {static_cast<std::size_t>(start), static_cast<std::size_t>(end)};
    }
};

/** @brief Scoped owner gesture; cancels only a transaction it successfully began.
 * End callbacks must be atomic and nonthrowing. A rejected finish retains ownership
 * so a later cancellation can restore the model. Do not create another history.
 */
class EditGestureSession {
public:
    using Begin = std::function<bool(const std::string&)>;
    using End = std::function<bool(bool)>;
    EditGestureSession(Begin begin, End end, std::shared_ptr<TimelineSelection> selection = {})
        : _begin(std::move(begin)), _end(std::move(end)), _selection(std::move(selection)) {}
    explicit EditGestureSession(std::shared_ptr<ICurveEditorSource> source)
        : EditGestureSession([source](const auto& label) { return source && source->beginEdit(label); },
                             [source](bool cancel) { return source && source->endEdit(cancel); },
                             source ? source->selectionState() : nullptr) {}
    ~EditGestureSession() { (void)finish(true); }
    EditGestureSession(const EditGestureSession&) = delete;
    EditGestureSession& operator=(const EditGestureSession&) = delete;
    bool begin(const std::string& label) {
        if (_active || !_begin || !_end || !_begin(label)) return false;
        _active = true;
        if (_selection) _savedSelection = *_selection;
        return true;
    }
    bool finish(bool cancel) {
        if (!_active) return true;
        if (!_end(cancel)) return false;
        _active = false;
        if (cancel && _selection) *_selection = _savedSelection;
        return true;
    }
    bool active() const noexcept { return _active; }
private:
    Begin _begin;
    End _end;
    std::shared_ptr<TimelineSelection> _selection;
    TimelineSelection _savedSelection;
    bool _active = false;
};

struct AuthoringStateStamp {
    std::uint64_t content = 0u, selection = 0u, pose = 0u;
    double positionSeconds = 0.0;
    bool playing = false;
};
struct AuthoringChanges {
    bool content = false, selection = false, pose = false, transport = false;
    bool any() const noexcept { return content || selection || pose || transport; }
};
// Owner-supplied stamps: this helper neither polls resources nor rebuilds UI.
class AuthoringRefreshGate {
public:
    AuthoringChanges consume(AuthoringStateStamp stamp) noexcept {
        AuthoringChanges result{!_valid || stamp.content != _last.content,
                                !_valid || stamp.selection != _last.selection,
                                !_valid || stamp.pose != _last.pose,
                                !_valid || stamp.positionSeconds != _last.positionSeconds
                                    || stamp.playing != _last.playing};
        acknowledge(stamp);
        return result;
    }
    void acknowledge(AuthoringStateStamp stamp) noexcept { _last = stamp; _valid = true; }
    void reset() noexcept { _valid = false; }
private:
    AuthoringStateStamp _last;
    bool _valid = false;
};
} // namespace ayt::ui::authoring
