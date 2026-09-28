#pragma once
#include <AYMath/MathTypes.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace ayt::ui::authoring {
struct PreviewPoint { float x = 0, y = 0, depth = 0; };

/** @brief Resource-neutral orbit and fitted orthographic preview primitives.
 * Hosts provide geometry/revisions and retain rendering, picking and pose ownership.
 * No world camera, document, ticking or history is created here.
 */
class PreviewOrbit {
public:
    float yaw = 0.55f, pitch = -0.18f, zoom = 1.0f;
    void reset() noexcept { yaw = 0.55f; pitch = -0.18f; zoom = 1; cancel(); }
    void begin(ayt::math::FVector2 point) noexcept { _last = point; _rotating = true; }
    bool move(ayt::math::FVector2 point) noexcept {
        if (!_rotating || !std::isfinite(point.x) || !std::isfinite(point.y)) return false;
        yaw += (point.x - _last.x) * 0.012f;
        pitch = std::clamp(pitch + (point.y - _last.y) * 0.012f, -1.5f, 1.5f);
        _last = point; return true;
    }
    bool end() noexcept { const bool was = _rotating; cancel(); return was; }
    void cancel() noexcept { _rotating = false; }
    bool rotating() const noexcept { return _rotating; }
    void wheel(float delta) noexcept {
        if (std::isfinite(delta) && delta != 0) zoom = std::clamp(zoom * (delta > 0 ? 1.12f : 0.89f), 0.2f, 8.0f);
    }
private:
    ayt::math::FVector2 _last{};
    bool _rotating = false;
};

struct PreviewProjectionStamp {
    std::uint64_t content = 0, pose = 0;
    ayt::math::FRectangle bounds{};
    float yaw = 0, pitch = 0, zoom = 1;
    bool operator==(const PreviewProjectionStamp& rhs) const noexcept {
        return content == rhs.content && pose == rhs.pose && yaw == rhs.yaw && pitch == rhs.pitch
            && zoom == rhs.zoom && bounds.minX == rhs.bounds.minX && bounds.minY == rhs.bounds.minY
            && bounds.maxX == rhs.bounds.maxX && bounds.maxY == rhs.bounds.maxY;
    }
};
class PreviewProjectionCache {
public:
    bool consume(PreviewProjectionStamp stamp) noexcept {
        if (_last && *_last == stamp) return false;
        _last = stamp; return true;
    }
    void invalidate() noexcept { _last.reset(); }
private:
    std::optional<PreviewProjectionStamp> _last;
};

struct PreviewBounds {
    ayt::math::FVector3 minimum{}, maximum{};
    bool populated = false;
    void include(ayt::math::FVector3 point) noexcept {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return;
        if (!populated) { minimum = maximum = point; populated = true; return; }
        minimum.x = std::min(minimum.x, point.x); maximum.x = std::max(maximum.x, point.x);
        minimum.y = std::min(minimum.y, point.y); maximum.y = std::max(maximum.y, point.y);
        minimum.z = std::min(minimum.z, point.z); maximum.z = std::max(maximum.z, point.z);
    }
};
class PreviewProjection {
public:
    PreviewProjection(const PreviewBounds& points, ayt::math::FRectangle viewport,
                      const PreviewOrbit& orbit, float coverage)
        : _center((points.minimum + points.maximum) * 0.5f), _viewport(viewport),
          _cy(std::cos(orbit.yaw)), _sy(std::sin(orbit.yaw)),
          _cp(std::cos(orbit.pitch)), _sp(std::sin(orbit.pitch)) {
        const float extent = std::max({points.maximum.x - points.minimum.x,
            points.maximum.y - points.minimum.y, points.maximum.z - points.minimum.z, 0.01f});
        _scale = std::min(std::max(1.0f, viewport.maxX - viewport.minX),
            std::max(1.0f, viewport.maxY - viewport.minY)) * coverage / extent * orbit.zoom;
    }
    PreviewPoint operator()(ayt::math::FVector3 point) const noexcept {
        point -= _center;
        const float x = _cy * point.x + _sy * point.z;
        const float z = -_sy * point.x + _cy * point.z;
        return {(_viewport.minX + _viewport.maxX) * 0.5f + x * _scale,
            (_viewport.minY + _viewport.maxY) * 0.5f - (_cp * point.y - _sp * z) * _scale,
            _sp * point.y + _cp * z};
    }
private:
    ayt::math::FVector3 _center;
    ayt::math::FRectangle _viewport;
    float _cy, _sy, _cp, _sp, _scale;
};
} // namespace ayt::ui::authoring
