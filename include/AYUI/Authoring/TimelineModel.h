#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ayt::ui::authoring {

enum class EditGesturePhase { Begin, Update, End, Cancel };
enum class TimelineTrackKind { Value, Audio, Event };

struct TimelineKey {
    std::string id;
    std::string trackId;
    double timeSeconds = 0.0;
};
struct TimelineTrack {
    std::string id;
    std::string name;
    TimelineTrackKind kind = TimelineTrackKind::Value;
};
struct TimelineSnapshot {
    std::vector<TimelineTrack> tracks;
    std::vector<TimelineKey> keys;
};
struct TimelineSelection {
    std::string trackId;
    std::string primaryKeyId;
    std::vector<std::string> keyIds;
    std::size_t component = 0u;
};
struct CurveKey {
    std::string id;
    double timeSeconds = 0.0;
    std::vector<float> values;
    std::vector<float> inTangents;
    std::vector<float> outTangents;
};
struct CurveTrack {
    std::string id;
    // Input and output tangents are derivatives in value/second. Bezier and
    // spring owners supply their own sampler without advertising slope handles.
    bool editableTangents = false;
    double snapIntervalSeconds = 0.0;
    std::vector<CurveKey> keys;
    std::function<float(std::size_t, double)> sample;
};

// UI-free time transform shared by curve, dope sheet and layout animation.
// Values are seconds; adapters convert resource ticks or UI milliseconds.
struct TimeViewport {
    double startSeconds = 0.0;
    double durationSeconds = 1.0;
    double timeAt(double normalized) const noexcept {
        return startSeconds + std::clamp(normalized, 0.0, 1.0)
            * std::max(1.0e-9, durationSeconds);
    }
    double normalizedAt(double seconds) const noexcept {
        return (seconds - startSeconds) / std::max(1.0e-9, durationSeconds);
    }
    void pan(double normalizedDelta) noexcept {
        if (std::isfinite(normalizedDelta))
            startSeconds -= normalizedDelta * durationSeconds;
    }
    void zoomAt(double normalized, double factor,
                double minimum = 0.01, double maximum = 3600.0) noexcept {
        if (!std::isfinite(factor) || factor <= 0.0) return;
        normalized = std::clamp(normalized, 0.0, 1.0);
        const double anchor = timeAt(normalized);
        durationSeconds = std::clamp(durationSeconds * factor, minimum, maximum);
        startSeconds = anchor - normalized * durationSeconds;
    }
};

/** @brief Owner adapter for reusable curve and timeline authoring widgets.
 * Link AYUITimelineCore for this UI-free contract, AYUICurveEditor for views.
 * Immutable snapshots may outlive a document revision. Cache them until content
 * changes; playback position is intentionally separate from content revision.
 * IDs are opaque. Successful mutations return any remapped IDs after sorting.
 * Validation, persistence, history and typed sampling remain in the owner.
 * Use one shared source per document for coordinated selection. Hosts invalidate
 * attached views after owner/selection/playhead changes; use the view callbacks
 * to refresh sibling views and inspectors. All time values use seconds.
 * AuthoringPrimitives.h supplies optional ruler, row layout, scoped gesture and
 * change-gating helpers without adding domain responsibilities to this source.
 */
class ICurveEditorSource {
public:
    virtual ~ICurveEditorSource() = default;
    // Views attached to the same source share selection without owning data.
    const std::shared_ptr<TimelineSelection>& selectionState() const noexcept {
        return _selection;
    }
    virtual std::uint64_t revision() const noexcept = 0;
    virtual std::shared_ptr<const CurveTrack> curveTrack(const std::string&) const = 0;
    virtual std::shared_ptr<const TimelineSnapshot> timelineSnapshot() const = 0;
    virtual double durationSeconds() const noexcept = 0;
    virtual double positionSeconds() const noexcept = 0;
    virtual bool seek(double seconds) = 0;
    virtual double snapTime(const std::string& trackId, double seconds) const = 0;
    virtual bool beginEdit(const std::string& label) = 0;
    virtual bool endEdit(bool cancel) = 0;
    virtual bool updateKey(std::string&, double, const std::vector<float>&) { return false; }
    virtual bool transformKeys(std::vector<std::string>&, double, std::size_t, float) { return false; }
    virtual bool removeKeys(const std::vector<std::string>&) { return false; }
    virtual bool setTangents(const std::string&, const std::vector<float>&,
                             const std::vector<float>&) { return false; }
    virtual bool moveTimelineKey(std::string& trackId, std::string& keyId,
                                  double seconds) = 0;
private:
    std::shared_ptr<TimelineSelection> _selection = std::make_shared<TimelineSelection>();
};

} // namespace ayt::ui::authoring
