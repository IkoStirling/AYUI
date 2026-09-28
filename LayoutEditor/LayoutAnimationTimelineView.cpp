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
    _verticalScrollOffset = verticalScrollOffset();
    markDirty();
}

bool LayoutAnimationTimelineView::setTrackKeyTimes(
    int trackIndex, std::vector<float> keyTimesMs) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(_tracks.size())) {
        return false;
    }
    auto& track = _tracks[static_cast<size_t>(trackIndex)];
    track.keyTimesMs = std::move(keyTimesMs);
    if (_selectedTrack == trackIndex &&
        _selectedKey >= static_cast<int>(track.keyTimesMs.size())) {
        _selectedKey = track.keyTimesMs.empty()
            ? -1 : static_cast<int>(track.keyTimesMs.size()) - 1;
    }
    markDirty();
    return true;
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
    const bool changed =
        _selectedTrack != trackIndex || _selectedKey != keyIndex;
    _selectedTrack = trackIndex;
    _selectedKey = keyIndex;
    ensureTrackVisible(trackIndex);
    if (changed) markDirty();
}

void LayoutAnimationTimelineView::setSelectedCurve(
    AnimationCurve curve, const CubicBezierParameters& bezier,
    const SpringParameters& spring, float segmentDurationMs) {
    const float duration = std::max(1.0f, segmentDurationMs);
    const bool changed = _selectedCurve != curve ||
        _selectedBezier.x1 != bezier.x1 || _selectedBezier.y1 != bezier.y1 ||
        _selectedBezier.x2 != bezier.x2 || _selectedBezier.y2 != bezier.y2 ||
        _selectedSpring.mass != spring.mass ||
        _selectedSpring.stiffness != spring.stiffness ||
        _selectedSpring.damping != spring.damping ||
        _selectedSpring.initialVelocity != spring.initialVelocity ||
        _selectedSpring.clampOvershoot != spring.clampOvershoot ||
        _selectedSegmentDurationMs != duration;
    if (!changed) return;
    _selectedCurve = curve;
    _selectedBezier = bezier;
    _selectedSpring = spring;
    _selectedSegmentDurationMs = duration;
    markDirty();
}

void LayoutAnimationTimelineView::clearCallbacks() {
    _onSeek = {};
    _onSelectionChanged = {};
    _onKeyDragged = {};
}

math::FRectangle LayoutAnimationTimelineView::plotBounds() const {
    const math::FRectangle bounds = getWorldBounds();
    const float right = hasVerticalOverflow()
        ? std::max(bounds.minX, bounds.maxX - kScrollBarWidth - 2.0f)
        : bounds.maxX;
    return math::FRectangle(
        std::min(right, bounds.minX + kLabelWidth),
        std::min(bounds.maxY, bounds.minY + kHeaderHeight),
        right, bounds.maxY);
}

float LayoutAnimationTimelineView::visibleDurationMs() const {
    return _durationMs / std::max(1.0f, _zoom);
}

float LayoutAnimationTimelineView::clampViewStart(float value) const {
    return std::clamp(finiteOr(value, 0.0f), 0.0f,
                      std::max(0.0f, _durationMs - visibleDurationMs()));
}

float LayoutAnimationTimelineView::maxVerticalScrollOffset() const {
    const math::FRectangle bounds = getWorldBounds();
    const float viewportHeight = std::max(
        0.0f, bounds.maxY - bounds.minY - kHeaderHeight);
    const float contentHeight =
        static_cast<float>(_tracks.size()) * kTrackHeight;
    return std::max(0.0f, contentHeight - viewportHeight);
}

float LayoutAnimationTimelineView::verticalScrollOffset() const {
    return std::clamp(finiteOr(_verticalScrollOffset, 0.0f), 0.0f,
                      maxVerticalScrollOffset());
}

void LayoutAnimationTimelineView::setVerticalScrollOffset(float value) {
    const float next = std::clamp(finiteOr(value, 0.0f), 0.0f,
                                  maxVerticalScrollOffset());
    if (std::fabs(next - _verticalScrollOffset) < 0.001f) return;
    _verticalScrollOffset = next;
    markDirty();
}

void LayoutAnimationTimelineView::ensureTrackVisible(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(_tracks.size())) {
        return;
    }
    const math::FRectangle bounds = getWorldBounds();
    const float viewportHeight = std::max(
        0.0f, bounds.maxY - bounds.minY - kHeaderHeight);
    if (viewportHeight <= 0.0f) return;
    const float top = static_cast<float>(trackIndex) * kTrackHeight;
    const float bottom = top + kTrackHeight;
    const float offset = verticalScrollOffset();
    if (top < offset) {
        setVerticalScrollOffset(top);
    } else if (bottom > offset + viewportHeight) {
        setVerticalScrollOffset(bottom - viewportHeight);
    }
}

bool LayoutAnimationTimelineView::hasVerticalOverflow() const {
    return maxVerticalScrollOffset() > 0.001f;
}

math::FRectangle LayoutAnimationTimelineView::verticalScrollBarBounds() const {
    const math::FRectangle bounds = getWorldBounds();
    return math::FRectangle(
        std::max(bounds.minX, bounds.maxX - kScrollBarWidth),
        std::min(bounds.maxY, bounds.minY + kHeaderHeight),
        bounds.maxX, bounds.maxY);
}

math::FRectangle LayoutAnimationTimelineView::verticalScrollThumbBounds() const {
    const math::FRectangle bar = verticalScrollBarBounds();
    const float barHeight = std::max(0.0f, bar.maxY - bar.minY);
    const float contentHeight =
        static_cast<float>(_tracks.size()) * kTrackHeight;
    if (barHeight <= 0.0f || contentHeight <= 0.0f) return bar;
    const float thumbHeight = std::min(
        barHeight, std::max(kMinScrollThumbHeight,
            barHeight * barHeight / contentHeight));
    const float travel = std::max(0.0f, barHeight - thumbHeight);
    const float maxOffset = maxVerticalScrollOffset();
    const float fraction = maxOffset > 0.0f
        ? verticalScrollOffset() / maxOffset : 0.0f;
    const float top = bar.minY + travel * fraction;
    return math::FRectangle(bar.minX, top, bar.maxX, top + thumbHeight);
}

void LayoutAnimationTimelineView::updateVerticalScrollFromPointer(
    float pointerY) {
    const math::FRectangle bar = verticalScrollBarBounds();
    const math::FRectangle thumb = verticalScrollThumbBounds();
    const float thumbHeight = std::max(0.0f, thumb.maxY - thumb.minY);
    const float travel = std::max(
        0.0f, bar.maxY - bar.minY - thumbHeight);
    if (travel <= 0.0f) {
        setVerticalScrollOffset(0.0f);
        return;
    }
    const float top = std::clamp(
        pointerY - _scrollThumbGrabOffset,
        bar.minY, bar.maxY - thumbHeight);
    setVerticalScrollOffset(
        (top - bar.minY) / travel * maxVerticalScrollOffset());
}

float LayoutAnimationTimelineView::worldXForTime(float timeMs) const {
    const math::FRectangle plot = plotBounds();
    const float width = std::max(1.0f, plot.maxX - plot.minX);
    const authoring::TimeViewport view{_viewStartMs * 0.001,
                                      visibleDurationMs() * 0.001};
    return plot.minX + static_cast<float>(view.normalizedAt(timeMs * 0.001)) * width;
}

float LayoutAnimationTimelineView::timeAtWorldX(float worldX) const {
    const math::FRectangle plot = plotBounds();
    const float width = std::max(1.0f, plot.maxX - plot.minX);
    const float normalized = std::clamp(
        (worldX - plot.minX) / width, 0.0f, 1.0f);
    const authoring::TimeViewport view{_viewStartMs * 0.001,
                                      visibleDurationMs() * 0.001};
    return std::clamp(static_cast<float>(view.timeAt(normalized) * 1000.0),
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
        (static_cast<float>(trackIndex) + 0.5f) * kTrackHeight -
        verticalScrollOffset();
    return math::FRectangle(cx - kKeySize * 0.5f, cy - kKeySize * 0.5f,
                            cx + kKeySize * 0.5f, cy + kKeySize * 0.5f);
}

bool LayoutAnimationTimelineView::hitKeyframe(
    const math::FVector2& point, int& trackIndex, int& keyIndex) const {
    for (int track = 0; track < static_cast<int>(_tracks.size()); ++track) {
        const math::FRectangle bounds = getWorldBounds();
        const float rowTop = bounds.minY + kHeaderHeight +
            static_cast<float>(track) * kTrackHeight -
            verticalScrollOffset();
        const float rowBottom = rowTop + kTrackHeight;
        if (rowBottom <= bounds.minY + kHeaderHeight) continue;
        if (rowTop >= bounds.maxY) break;
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
    if (event.mouseButton == 0 && hasVerticalOverflow() &&
        verticalScrollBarBounds().contains(event.mousePos)) {
        const math::FRectangle thumb = verticalScrollThumbBounds();
        _scrollingTracks = true;
        if (thumb.contains(event.mousePos)) {
            _scrollThumbGrabOffset = event.mousePos.y - thumb.minY;
        } else {
            _scrollThumbGrabOffset =
                std::max(0.0f, thumb.maxY - thumb.minY) * 0.5f;
            updateVerticalScrollFromPointer(event.mousePos.y);
        }
        return true;
    }
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
        _dragLastTimeMs = _tracks[static_cast<size_t>(track)].keyTimesMs[
            static_cast<size_t>(key)];
        if (_onKeyDragged) {
            const int updated = _onKeyDragged(
                track, key, _dragLastTimeMs,
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
    if (_scrollingTracks) {
        updateVerticalScrollFromPointer(event.mousePos.y);
        return true;
    }
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
        if (_onKeyDragged && std::fabs(time - _dragLastTimeMs) >= 0.001f) {
            const int updated = _onKeyDragged(
                _dragTrack, _dragKey, time,
                LayoutAnimationKeyDragPhase::Update);
            if (updated >= 0) _dragKey = updated;
            _dragLastTimeMs = time;
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
    if (_scrollingTracks) {
        _scrollingTracks = false;
        return true;
    }
    if (_draggingKey) {
        _draggingKey = false;
        const float finalTime = std::round(timeAtWorldX(event.mousePos.x));
        if (_onKeyDragged) {
            if (std::fabs(finalTime - _dragLastTimeMs) >= 0.001f) {
                const int updated = _onKeyDragged(
                    _dragTrack, _dragKey, finalTime,
                    LayoutAnimationKeyDragPhase::Update);
                if (updated >= 0) _dragKey = updated;
                _dragLastTimeMs = finalTime;
            }
            const int updated = _onKeyDragged(
                _dragTrack, _dragKey, finalTime,
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
    if (!getWorldBounds().contains(event.mousePos)) return false;
    const math::FRectangle plot = plotBounds();
    if (hasVerticalOverflow() && !plot.contains(event.mousePos)) {
        setVerticalScrollOffset(
            verticalScrollOffset() + event.deltaY);
        return true;
    }
    if (!plot.contains(event.mousePos)) return false;
    const float oldZoom = _zoom;
    const float normalized = std::clamp(
        (event.mousePos.x - plot.minX) /
            std::max(1.0f, plot.maxX - plot.minX),
        0.0f, 1.0f);
    authoring::TimeViewport view{_viewStartMs * 0.001, visibleDurationMs() * 0.001};
    view.zoomAt(normalized, std::pow(1.1, event.deltaY / 40.0f),
                _durationMs * 0.001 / 16.0, _durationMs * 0.001);
    _zoom = static_cast<float>(_durationMs * 0.001 / view.durationSeconds);
    if (std::fabs(_zoom - oldZoom) < 0.0001f) return true;
    _viewStartMs = clampViewStart(static_cast<float>(view.startSeconds * 1000.0));
    markDirty();
    return true;
}

void LayoutAnimationTimelineView::onCaptureCancelled() {
    const bool wasActive = _scrubbing || _panning || _draggingKey ||
        _scrollingTracks;
    const bool cancelKeyDrag = _draggingKey;
    const int track = _dragTrack;
    const int key = _dragKey;
    const float time = _dragLastTimeMs;

    _scrubbing = false;
    _panning = false;
    _draggingKey = false;
    _scrollingTracks = false;
    _dragTrack = -1;
    _dragKey = -1;

    if (cancelKeyDrag && _onKeyDragged) {
        (void)_onKeyDragged(
            track, key, time, LayoutAnimationKeyDragPhase::Cancel);
    }
    if (wasActive) markDirty();
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
        float sample = easeCurve(x, _selectedCurve);
        if (_selectedCurve == AnimationCurve::CubicBezier) {
            sample = evaluateCubicBezier(x, _selectedBezier);
        } else if (_selectedCurve == AnimationCurve::Spring) {
            sample = evaluateSpring(
                x, _selectedSegmentDurationMs, _selectedSpring);
        }
        const float y = std::clamp(sample, 0.0f, 1.0f);
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
                             std::min(plot.maxX, x + 64.0f),
                             bounds.minY + kHeaderHeight),
            timeLabel(tick), 10,
            math::FVector4(0.48f, 0.55f, 0.65f, 1.0f));
    }

    renderer.pushClip(math::FRectangle(
        bounds.minX, std::min(bounds.maxY, bounds.minY + kHeaderHeight),
        plot.maxX, bounds.maxY));
    const float verticalOffset = verticalScrollOffset();
    for (int track = 0; track < static_cast<int>(_tracks.size()); ++track) {
        const float y = bounds.minY + kHeaderHeight +
            static_cast<float>(track) * kTrackHeight - verticalOffset;
        if (y + kTrackHeight <= bounds.minY + kHeaderHeight) continue;
        if (y >= bounds.maxY) break;
        const bool selected = track == _selectedTrack;
        if (selected) {
            renderer.drawRect(
                math::FRectangle(bounds.minX, y, plot.maxX,
                                 std::min(bounds.maxY, y + kTrackHeight)),
                math::FVector4(0.08f, 0.18f, 0.29f, 0.86f));
        }
        renderer.drawRect(
            math::FRectangle(bounds.minX, y + kTrackHeight - 1.0f,
                             plot.maxX, y + kTrackHeight),
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
    renderer.popClip();

    if (hasVerticalOverflow()) {
        const math::FRectangle bar = verticalScrollBarBounds();
        const math::FRectangle thumb = verticalScrollThumbBounds();
        renderer.drawRoundedRect(
            bar, math::FVector4(0.075f, 0.09f, 0.12f, 1.0f), 4.0f);
        renderer.drawRoundedRect(
            thumb,
            _scrollingTracks
                ? math::FVector4(0.40f, 0.66f, 0.92f, 1.0f)
                : math::FVector4(0.28f, 0.38f, 0.52f, 1.0f),
            4.0f);
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
