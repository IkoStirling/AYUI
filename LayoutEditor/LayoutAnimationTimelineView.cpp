#include "AYUI/LayoutEditor/LayoutAnimationTimelineView.h"

#include "AYUI/Tween.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ayt::ui {

namespace {

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

std::wstring timeLabel(float timeMs) {
    std::wostringstream text;
    if (timeMs >= 1000.0f) {
        text << std::fixed << std::setprecision(timeMs >= 10000.0f ? 0 : 1)
             << timeMs / 1000.0f << L"s";
    } else {
        text << static_cast<int>(std::lround(timeMs)) << L"ms";
    }
    return text.str();
}

} // namespace

LayoutAnimationTimelineView::LayoutAnimationTimelineView() {
    setId("__le_animation_timeline_view");
    setDisplayListPolicy(DisplayListPolicy::Retained);
    setLayoutPositionManaged(false);
    setLayoutSizeManaged(false);
}

void LayoutAnimationTimelineView::setTracks(
    std::vector<LayoutAnimationTimelineTrackView> tracks) {
    _tracks = std::move(tracks);
    if (_selectedTrack >= static_cast<int>(_tracks.size())) {
        _selectedTrack = -1;
        _selectedKey = -1;
    } else if (_selectedTrack >= 0 &&
               _selectedKey >= static_cast<int>(
                   _tracks[static_cast<size_t>(_selectedTrack)].keyTimesMs.size())) {
        _selectedKey = -1;
    }
    markDirty();
}

void LayoutAnimationTimelineView::setDurationMs(float durationMs) {
    const float next = std::max(1.0f, finiteOr(durationMs, 1000.0f));
    if (std::fabs(next - _durationMs) < 0.001f) return;
    _durationMs = next;
    _viewStartMs = clampViewStart(_viewStartMs);
    _currentTimeMs = std::clamp(_currentTimeMs, 0.0f, _durationMs);
    markDirty();
}

void LayoutAnimationTimelineView::setCurrentTimeMs(float timeMs) {
    const float next = std::clamp(
        finiteOr(timeMs, 0.0f), 0.0f, _durationMs);
    if (std::fabs(next - _currentTimeMs) < 0.001f) return;
    _currentTimeMs = next;
    markDirty();
}

void LayoutAnimationTimelineView::setSelection(int trackIndex, int keyIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(_tracks.size())) {
        trackIndex = -1;
        keyIndex = -1;
    } else if (keyIndex < 0 || keyIndex >= static_cast<int>(
                   _tracks[static_cast<size_t>(trackIndex)].keyTimesMs.size())) {
        keyIndex = -1;
    }
    if (_selectedTrack == trackIndex && _selectedKey == keyIndex) return;
    _selectedTrack = trackIndex;
    _selectedKey = keyIndex;
    markDirty();
}

void LayoutAnimationTimelineView::setSelectedCurve(AnimationCurve curve) {
    if (_selectedCurve == curve) return;
    _selectedCurve = curve;
    markDirty();
}

void LayoutAnimationTimelineView::clearCallbacks() {
    _onSeek = {};
    _onSelectionChanged = {};
    _onKeyDragged = {};
}

math::FRectangle LayoutAnimationTimelineView::plotBounds() const {
    const math::FRectangle bounds = getWorldBounds();
    return math::FRectangle(
        std::min(bounds.maxX, bounds.minX + kLabelWidth),
        std::min(bounds.maxY, bounds.minY + kHeaderHeight),
        bounds.maxX, bounds.maxY);
}

float LayoutAnimationTimelineView::visibleDurationMs() const {
    return _durationMs / std::max(1.0f, _zoom);
}

float LayoutAnimationTimelineView::clampViewStart(float value) const {
    return std::clamp(finiteOr(value, 0.0f), 0.0f,
                      std::max(0.0f, _durationMs - visibleDurationMs()));
}

float LayoutAnimationTimelineView::worldXForTime(float timeMs) const {
    const math::FRectangle plot = plotBounds();
    const float width = std::max(1.0f, plot.maxX - plot.minX);
    return plot.minX + (timeMs - _viewStartMs) /
        std::max(1.0f, visibleDurationMs()) * width;
}

float LayoutAnimationTimelineView::timeAtWorldX(float worldX) const {
    const math::FRectangle plot = plotBounds();
    const float width = std::max(1.0f, plot.maxX - plot.minX);
    const float normalized = std::clamp(
        (worldX - plot.minX) / width, 0.0f, 1.0f);
    return std::clamp(
        _viewStartMs + normalized * visibleDurationMs(),
        0.0f, _durationMs);
}

math::FRectangle LayoutAnimationTimelineView::keyframeBounds(
    int trackIndex, int keyIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(_tracks.size()))
        return {};
    const auto& keys = _tracks[static_cast<size_t>(trackIndex)].keyTimesMs;
    if (keyIndex < 0 || keyIndex >= static_cast<int>(keys.size())) return {};
    const math::FRectangle bounds = getWorldBounds();
    const float cx = worldXForTime(keys[static_cast<size_t>(keyIndex)]);
    const float cy = bounds.minY + kHeaderHeight +
        (static_cast<float>(trackIndex) + 0.5f) * kTrackHeight;
    return math::FRectangle(cx - kKeySize * 0.5f, cy - kKeySize * 0.5f,
                            cx + kKeySize * 0.5f, cy + kKeySize * 0.5f);
}

bool LayoutAnimationTimelineView::hitKeyframe(
    const math::FVector2& point, int& trackIndex, int& keyIndex) const {
    for (int track = 0; track < static_cast<int>(_tracks.size()); ++track) {
        const float rowBottom = getWorldBounds().minY + kHeaderHeight +
            static_cast<float>(track + 1) * kTrackHeight;
        if (rowBottom > getWorldBounds().maxY) break;
        const auto& keys = _tracks[static_cast<size_t>(track)].keyTimesMs;
        for (int key = 0; key < static_cast<int>(keys.size()); ++key) {
            math::FRectangle hit = keyframeBounds(track, key);
            hit.minX -= 4.0f;
            hit.minY -= 4.0f;
            hit.maxX += 4.0f;
            hit.maxY += 4.0f;
            if (hit.contains(point)) {
                trackIndex = track;
                keyIndex = key;
                return true;
            }
        }
    }
    return false;
}

void LayoutAnimationTimelineView::emitSeek(float worldX) {
    const float time = timeAtWorldX(worldX);
    setCurrentTimeMs(time);
    if (_onSeek) _onSeek(time);
}

bool LayoutAnimationTimelineView::onMouseButtonDown(
    const UIMouseEvent& event) {
    if (!getWorldBounds().contains(event.mousePos)) return false;
    if (event.mouseButton == 2) {
        _panning = true;
        _lastPointerX = event.mousePos.x;
        return true;
    }
    if (event.mouseButton != 0 || !plotBounds().contains(event.mousePos))
        return false;

    int track = -1;
    int key = -1;
    if (hitKeyframe(event.mousePos, track, key)) {
        setSelection(track, key);
        if (_onSelectionChanged) _onSelectionChanged(track, key);
        _draggingKey = true;
        _dragTrack = track;
        _dragKey = key;
        if (_onKeyDragged) {
            const int updated = _onKeyDragged(
                track, key,
                _tracks[static_cast<size_t>(track)].keyTimesMs[
                    static_cast<size_t>(key)],
                LayoutAnimationKeyDragPhase::Begin);
            if (updated >= 0) _dragKey = updated;
        }
        return true;
    }

    _scrubbing = true;
    emitSeek(event.mousePos.x);
    return true;
}

bool LayoutAnimationTimelineView::onMouseMove(const UIMouseEvent& event) {
    if (_panning) {
        const math::FRectangle plot = plotBounds();
        const float width = std::max(1.0f, plot.maxX - plot.minX);
        const float deltaMs = (event.mousePos.x - _lastPointerX) /
            width * visibleDurationMs();
        _viewStartMs = clampViewStart(_viewStartMs - deltaMs);
        _lastPointerX = event.mousePos.x;
        markDirty();
        return true;
    }
    if (_draggingKey) {
        const float time = std::round(timeAtWorldX(event.mousePos.x));
        if (_onKeyDragged) {
            const int updated = _onKeyDragged(
                _dragTrack, _dragKey, time,
                LayoutAnimationKeyDragPhase::Update);
            if (updated >= 0) _dragKey = updated;
        }
        setCurrentTimeMs(time);
        return true;
    }
    if (_scrubbing) {
        emitSeek(event.mousePos.x);
        return true;
    }
    return getWorldBounds().contains(event.mousePos);
}

bool LayoutAnimationTimelineView::onMouseButtonUp(
    const UIMouseEvent& event) {
    if (event.mouseButton == 2 && _panning) {
        _panning = false;
        return true;
    }
    if (event.mouseButton != 0) return false;
    if (_draggingKey) {
        _draggingKey = false;
        if (_onKeyDragged) {
            const int updated = _onKeyDragged(
                _dragTrack, _dragKey, timeAtWorldX(event.mousePos.x),
                LayoutAnimationKeyDragPhase::End);
            if (updated >= 0) _dragKey = updated;
        }
        _dragTrack = -1;
        _dragKey = -1;
        return true;
    }
    if (_scrubbing) {
        _scrubbing = false;
        emitSeek(event.mousePos.x);
        return true;
    }
    return false;
}

bool LayoutAnimationTimelineView::onMouseWheel(
    const UIMouseWheelEvent& event) {
    if (!plotBounds().contains(event.mousePos)) return false;
    const float anchorTime = timeAtWorldX(event.mousePos.x);
    const float oldZoom = _zoom;
    _zoom = std::clamp(
        _zoom * std::pow(1.1f, -event.deltaY / 40.0f), 1.0f, 16.0f);
    if (std::fabs(_zoom - oldZoom) < 0.0001f) return true;
    const math::FRectangle plot = plotBounds();
    const float normalized = std::clamp(
        (event.mousePos.x - plot.minX) /
            std::max(1.0f, plot.maxX - plot.minX),
        0.0f, 1.0f);
    _viewStartMs = clampViewStart(
        anchorTime - normalized * visibleDurationMs());
    markDirty();
    return true;
}

void LayoutAnimationTimelineView::onMouseLeave() {
    // Captured drags continue to receive move/up through UIManager. Only a
    // non-drag hover can disappear here, so there is no state to cancel.
}

UiCursorHint LayoutAnimationTimelineView::getCursorHint() const {
    return _panning ? UiCursorHint::Move : UiCursorHint::SizeHorizontal;
}

void LayoutAnimationTimelineView::drawCurvePreview(
    IRenderBackend& renderer, const math::FRectangle& bounds) const {
    renderer.drawBorderRect(bounds, math::FVector4(0.25f, 0.30f, 0.38f, 1.0f),
                            1.0f, 2.0f);
    const float width = std::max(1.0f, bounds.maxX - bounds.minX - 6.0f);
    const float height = std::max(1.0f, bounds.maxY - bounds.minY - 6.0f);
    for (int i = 0; i <= 22; ++i) {
        const float x = static_cast<float>(i) / 22.0f;
        const float y = std::clamp(easeCurve(x, _selectedCurve), 0.0f, 1.0f);
        const float px = bounds.minX + 3.0f + x * width;
        const float py = bounds.maxY - 3.0f - y * height;
        renderer.drawRoundedRect(
            math::FRectangle(px - 1.0f, py - 1.0f, px + 1.0f, py + 1.0f),
            math::FVector4(0.31f, 0.68f, 1.0f, 1.0f), 1.0f);
    }
}

void LayoutAnimationTimelineView::onRender(IRenderBackend& renderer) {
    const math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    renderer.pushClip(bounds);
    renderer.drawRect(bounds, math::FVector4(0.055f, 0.065f, 0.085f, 1.0f));
    renderer.drawRect(
        math::FRectangle(bounds.minX, bounds.minY, bounds.maxX,
                         std::min(bounds.maxY, bounds.minY + kHeaderHeight)),
        math::FVector4(0.075f, 0.09f, 0.12f, 1.0f));
    renderer.drawText(
        math::FRectangle(bounds.minX + 10.0f, bounds.minY,
                         bounds.minX + 76.0f, bounds.minY + kHeaderHeight),
        L"TRACKS", 11, math::FVector4(0.62f, 0.69f, 0.78f, 1.0f));
    drawCurvePreview(renderer,
        math::FRectangle(bounds.minX + 82.0f, bounds.minY + 5.0f,
                         bounds.minX + kLabelWidth - 8.0f,
                         bounds.minY + kHeaderHeight - 5.0f));

    const math::FRectangle plot = plotBounds();
    renderer.drawRect(
        math::FRectangle(plot.minX - 1.0f, bounds.minY,
                         plot.minX, bounds.maxY),
        math::FVector4(0.20f, 0.24f, 0.31f, 1.0f));

    const float plotWidth = std::max(1.0f, plot.maxX - plot.minX);
    const float msPerPixel = visibleDurationMs() / plotWidth;
    const float targetTick = msPerPixel * 84.0f;
    const float candidates[] = {
        10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
        1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
    };
    float tickMs = candidates[sizeof(candidates) / sizeof(candidates[0]) - 1];
    for (float candidate : candidates) {
        if (candidate >= targetTick) {
            tickMs = candidate;
            break;
        }
    }
    const float firstTick = std::ceil(_viewStartMs / tickMs) * tickMs;
    const float viewEnd = _viewStartMs + visibleDurationMs();
    for (float tick = firstTick; tick <= viewEnd + 0.01f; tick += tickMs) {
        const float x = worldXForTime(tick);
        renderer.drawRect(
            math::FRectangle(x, bounds.minY + 18.0f, x + 1.0f, bounds.maxY),
            math::FVector4(0.14f, 0.17f, 0.22f, 1.0f));
        renderer.drawText(
            math::FRectangle(x + 4.0f, bounds.minY,
                             std::min(bounds.maxX, x + 64.0f),
                             bounds.minY + kHeaderHeight),
            timeLabel(tick), 10,
            math::FVector4(0.48f, 0.55f, 0.65f, 1.0f));
    }

    for (int track = 0; track < static_cast<int>(_tracks.size()); ++track) {
        const float y = bounds.minY + kHeaderHeight +
            static_cast<float>(track) * kTrackHeight;
        if (y >= bounds.maxY) break;
        const bool selected = track == _selectedTrack;
        if (selected) {
            renderer.drawRect(
                math::FRectangle(bounds.minX, y, bounds.maxX,
                                 std::min(bounds.maxY, y + kTrackHeight)),
                math::FVector4(0.08f, 0.18f, 0.29f, 0.86f));
        }
        renderer.drawRect(
            math::FRectangle(bounds.minX, y + kTrackHeight - 1.0f,
                             bounds.maxX, y + kTrackHeight),
            math::FVector4(0.11f, 0.14f, 0.19f, 1.0f));
        renderer.drawText(
            math::FRectangle(bounds.minX + 10.0f, y,
                             bounds.minX + kLabelWidth - 8.0f,
                             std::min(bounds.maxY, y + kTrackHeight)),
            _tracks[static_cast<size_t>(track)].label, 11,
            selected ? math::FVector4(0.82f, 0.91f, 1.0f, 1.0f)
                     : math::FVector4(0.64f, 0.69f, 0.77f, 1.0f));

        const auto& keys = _tracks[static_cast<size_t>(track)].keyTimesMs;
        for (int key = 0; key < static_cast<int>(keys.size()); ++key) {
            const float x = worldXForTime(keys[static_cast<size_t>(key)]);
            if (x < plot.minX - kKeySize || x > plot.maxX + kKeySize) continue;
            const math::FRectangle keyRect = keyframeBounds(track, key);
            const bool keySelected = selected && key == _selectedKey;
            renderer.drawRoundedRect(
                keyRect,
                keySelected ? math::FVector4(1.0f, 0.73f, 0.25f, 1.0f)
                            : math::FVector4(0.31f, 0.68f, 1.0f, 1.0f),
                2.0f);
            if (keySelected) {
                renderer.drawBorderRect(
                    math::FRectangle(keyRect.minX - 2.0f, keyRect.minY - 2.0f,
                                     keyRect.maxX + 2.0f, keyRect.maxY + 2.0f),
                    math::FVector4(1.0f, 0.88f, 0.62f, 1.0f), 1.0f, 3.0f);
            }
        }
    }

    const float playheadX = worldXForTime(_currentTimeMs);
    if (playheadX >= plot.minX && playheadX <= plot.maxX) {
        renderer.drawRect(
            math::FRectangle(playheadX - 1.0f, bounds.minY,
                             playheadX + 1.0f, bounds.maxY),
            math::FVector4(1.0f, 0.34f, 0.30f, 1.0f));
        renderer.drawRoundedRect(
            math::FRectangle(playheadX - 5.0f, bounds.minY + 1.0f,
                             playheadX + 5.0f, bounds.minY + 8.0f),
            math::FVector4(1.0f, 0.34f, 0.30f, 1.0f), 2.0f);
    }
    renderer.popClip();
}

} // namespace ayt::ui
