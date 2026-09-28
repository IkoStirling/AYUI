#include "AYUI/Authoring/CurveCanvas.h"

#include <AYUI/IRenderBackend.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace ayt::ui::authoring {
namespace {

constexpr float kLeftMargin = 48.0f;
constexpr float kRightMargin = 12.0f;
constexpr float kTopMargin = 18.0f;
constexpr float kBottomMargin = 20.0f;
constexpr float kHitRadius = 8.0f;
const std::array<ayt::math::FVector4, 4> kColors{{
    {0.95f, 0.32f, 0.28f, 1.0f}, {0.35f, 0.82f, 0.38f, 1.0f},
    {0.31f, 0.62f, 1.0f, 1.0f}, {0.88f, 0.66f, 0.22f, 1.0f}}};

double handleDelta(const CurveTrack& track,
                   std::size_t key, bool incoming)
{
    if (incoming && key > 0u) {
        return std::max(0.02,
            (track.keys[key].timeSeconds - track.keys[key - 1u].timeSeconds) / 3.0);
    }
    if (!incoming && key + 1u < track.keys.size()) {
        return std::max(0.02,
            (track.keys[key + 1u].timeSeconds - track.keys[key].timeSeconds) / 3.0);
    }
    return std::max(0.02, std::min(0.25, track.snapIntervalSeconds > 0.0
        ? 4.0 * track.snapIntervalSeconds : 0.1));
}

float distanceSquared(ayt::math::FVector2 a, ayt::math::FVector2 b) noexcept
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y;
}

} // namespace

CurveCanvas::CurveCanvas(
    std::shared_ptr<ICurveEditorSource> document)
    : _document(std::move(document)),
      _selection(_document ? _document->selectionState() : std::make_shared<TimelineSelection>()),
      _gesture(_document)
{
    setId("curve_canvas");
}

CurveCanvas::~CurveCanvas()
{
    // Only this widget's own gesture may be cancelled; another view can use
    // the same source. Do not invoke view callbacks while destroying it.
    _onEdited = {};
    _onSelectionChanged = {};
    finishGesture(true);
}

void CurveCanvas::setTrackId(std::string trackId)
{
    if (_trackId == trackId) return;
    finishGesture(true);
    _trackId = std::move(trackId);
    if (_selection->trackId != _trackId) {
        _selection->trackId = _trackId;
        _selection->primaryKeyId.clear();
        _selection->keyIds.clear();
    }
    _viewValid = false;
    markDirty();
}

void CurveCanvas::setComponentVisible(
    std::size_t component, bool visible)
{
    if (component >= _componentVisible.size()
        || _componentVisible[component] == visible) return;
    _componentVisible[component] = visible;
    _viewValid = false;
    markDirty();
}

bool CurveCanvas::componentVisible(std::size_t component) const noexcept
{
    return component < _componentVisible.size() && _componentVisible[component];
}

void CurveCanvas::frameAll()
{
    _viewValid = false;
    markDirty();
}

void CurveCanvas::selectAllKeys()
{
    const auto snapshot = loadTrack();
    if (!snapshot) return;
    const auto& track = *snapshot;
    _selection->trackId = _trackId;
    _selection->component = 0u;
    _selection->keyIds.clear();
    for (const auto& key : track.keys) _selection->keyIds.push_back(key.id);
    _selection->primaryKeyId = _selection->keyIds.empty() ? std::string{}
                                             : _selection->keyIds.front();
    if (_onSelectionChanged && !_selection->primaryKeyId.empty()) {
        _onSelectionChanged(_selection->primaryKeyId, 0u);
    }
    markDirty();
}

void CurveCanvas::clearSelection()
{
    if (_selection->keyIds.empty()) return;
    _selection->keyIds.clear();
    _selection->primaryKeyId.clear();
    markDirty();
}

bool CurveCanvas::deleteSelectedKeys()
{
    if (_document == nullptr || _selection->keyIds.empty()
        || !_document->removeKeys(_selection->keyIds)) return false;
    clearSelection();
    if (_onEdited) _onEdited();
    return true;
}

bool CurveCanvas::isSelected(const std::string& keyId) const
{
    return std::find(_selection->keyIds.begin(), _selection->keyIds.end(), keyId)
        != _selection->keyIds.end();
}

ayt::math::FRectangle CurveCanvas::plotBounds() const noexcept
{
    const auto bounds = getWorldBounds();
    return {bounds.minX + kLeftMargin, bounds.minY + kTopMargin,
            std::max(bounds.minX + kLeftMargin + 1.0f,
                     bounds.maxX - kRightMargin),
            std::max(bounds.minY + kTopMargin + 1.0f,
                     bounds.maxY - kBottomMargin)};
}

std::shared_ptr<const CurveTrack> CurveCanvas::loadTrack() const
{
    return _document != nullptr && !_trackId.empty()
        ? _document->curveTrack(_trackId) : nullptr;
}

void CurveCanvas::ensureView(
    const CurveTrack& track)
{
    if (_viewValid) return;
    const double duration = _document != nullptr
        ? _document->durationSeconds() : 1.0;
    _viewStart = 0.0;
    _viewDuration = std::max(0.05, duration);
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (const auto& key : track.keys) {
        for (std::size_t component = 0u; component < key.values.size()
             && component < _componentVisible.size(); ++component) {
            if (!_componentVisible[component]) continue;
            minimum = std::min(minimum, key.values[component]);
            maximum = std::max(maximum, key.values[component]);
        }
    }
    if (minimum > maximum) { minimum = -1.0f; maximum = 1.0f; }
    _valueCenter = (minimum + maximum) * 0.5f;
    _valueSpan = std::max(0.1f, (maximum - minimum) * 1.3f);
    _viewValid = true;
}

float CurveCanvas::worldX(double seconds) const noexcept
{
    const auto plot = plotBounds();
    const double normalized = TimeViewport{_viewStart, _viewDuration}
        .normalizedAt(seconds);
    return plot.minX + static_cast<float>(normalized)
        * (plot.maxX - plot.minX);
}

float CurveCanvas::worldY(float value) const noexcept
{
    const auto plot = plotBounds();
    const float normalized = (_valueCenter + _valueSpan * 0.5f - value)
        / std::max(1.0e-6f, _valueSpan);
    return plot.minY + normalized * (plot.maxY - plot.minY);
}

double CurveCanvas::secondsAt(float x) const noexcept
{
    const auto plot = plotBounds();
    return TimeViewport{_viewStart, _viewDuration}.timeAt(
        static_cast<double>((x - plot.minX)
            / std::max(1.0f, plot.maxX - plot.minX)));
}

float CurveCanvas::valueAt(float y) const noexcept
{
    const auto plot = plotBounds();
    const float normalized = std::clamp(
        (y - plot.minY) / std::max(1.0f, plot.maxY - plot.minY), 0.0f, 1.0f);
    return _valueCenter + _valueSpan * 0.5f - normalized * _valueSpan;
}

CurveCanvas::Hit CurveCanvas::hitTest(
    ayt::math::FVector2 point, const CurveTrack& track) const
{
    Hit result;
    float best = kHitRadius * kHitRadius;
    for (std::size_t keyIndex = 0u; keyIndex < track.keys.size(); ++keyIndex) {
        const auto& key = track.keys[keyIndex];
        for (std::size_t component = 0u; component < key.values.size()
             && component < _componentVisible.size(); ++component) {
            if (!_componentVisible[component]) continue;
            const ayt::math::FVector2 keyPoint{
                worldX(key.timeSeconds), worldY(key.values[component])};
            const float keyDistance = distanceSquared(point, keyPoint);
            if (keyDistance < best) {
                best = keyDistance;
                result = {HitKind::Key, key.id, component};
            }
            if (!track.editableTangents
                || key.id != _selection->primaryKeyId) continue;
            const double inDelta = handleDelta(track, keyIndex, true);
            const double outDelta = handleDelta(track, keyIndex, false);
            const float inSlope = component < key.inTangents.size()
                ? key.inTangents[component] : 0.0f;
            const float outSlope = component < key.outTangents.size()
                ? key.outTangents[component] : 0.0f;
            const ayt::math::FVector2 inPoint{
                worldX(key.timeSeconds - inDelta),
                worldY(key.values[component] - inSlope * static_cast<float>(inDelta))};
            const ayt::math::FVector2 outPoint{
                worldX(key.timeSeconds + outDelta),
                worldY(key.values[component] + outSlope * static_cast<float>(outDelta))};
            const float inDistance = distanceSquared(point, inPoint);
            if (inDistance < best) {
                best = inDistance;
                result = {HitKind::InTangent, key.id, component};
            }
            const float outDistance = distanceSquared(point, outPoint);
            if (outDistance < best) {
                best = outDistance;
                result = {HitKind::OutTangent, key.id, component};
            }
        }
    }
    return result;
}

bool CurveCanvas::onMouseButtonDown(
    const ayt::ui::UIMouseEvent& event)
{
    if (!getWorldBounds().contains(event.mousePos)) return false;
    _lastPointer = event.mousePos;
    if (event.mouseButton == 1 || event.mouseButton == 2) {
        _panning = true;
        return true;
    }
    if (event.mouseButton != 0 || !plotBounds().contains(event.mousePos)) {
        return false;
    }
    const auto snapshot = loadTrack();
    if (!snapshot) return false;
    const auto& track = *snapshot;
    ensureView(track);
    const Hit hit = hitTest(event.mousePos, track);
    if (hit.kind == HitKind::None) {
        _boxSelecting = true;
        _boxMoved = false;
        _boxStart = _boxEnd = event.mousePos;
        _selection->keyIds.clear();
        _selection->primaryKeyId.clear();
        markDirty();
        return true;
    }
    if (!isSelected(hit.keyId)) {
        _selection->keyIds = {hit.keyId};
    }
    _selection->primaryKeyId = hit.keyId;
    _selection->trackId = _trackId;
    _selection->component = hit.component;
    _dragHit = hit;
    _gestureChanged = false;
    if (_onSelectionChanged) {
        _onSelectionChanged(_selection->primaryKeyId, _dragHit.component);
    }
    markDirty();
    if (_gesture.begin(
            hit.kind == HitKind::Key ? "Move curve key"
                                     : "Edit curve tangent")) {
        return true;
    }
    _dragHit = {};
    return true; // Read-only owners still permit key selection.
}

bool CurveCanvas::onMouseMove(
    const ayt::ui::UIMouseEvent& event)
{
    if (_panning) {
        const auto plot = plotBounds();
        const float width = std::max(1.0f, plot.maxX - plot.minX);
        const float height = std::max(1.0f, plot.maxY - plot.minY);
        _viewStart -= (event.mousePos.x - _lastPointer.x) / width
            * _viewDuration;
        _valueCenter += (event.mousePos.y - _lastPointer.y) / height
            * _valueSpan;
        _lastPointer = event.mousePos;
        markDirty();
        return true;
    }
    if (_boxSelecting) {
        _boxEnd = event.mousePos;
        _boxMoved = _boxMoved
            || distanceSquared(_boxStart, _boxEnd) > 9.0f;
        const ayt::math::FRectangle selection{
            std::min(_boxStart.x, _boxEnd.x),
            std::min(_boxStart.y, _boxEnd.y),
            std::max(_boxStart.x, _boxEnd.x),
            std::max(_boxStart.y, _boxEnd.y)};
        const auto snapshot = loadTrack();
        if (snapshot) {
            const auto& track = *snapshot;
            _selection->keyIds.clear();
            for (const auto& key : track.keys) {
                bool selected = false;
                for (std::size_t component = 0u;
                     component < key.values.size()
                     && component < _componentVisible.size(); ++component) {
                    if (_componentVisible[component]
                        && selection.contains({worldX(key.timeSeconds),
                                               worldY(key.values[component])})) {
                        selected = true;
                        break;
                    }
                }
                if (selected) _selection->keyIds.push_back(key.id);
            }
            _selection->primaryKeyId = _selection->keyIds.empty() ? std::string{}
                                                     : _selection->keyIds.front();
        }
        markDirty();
        return true;
    }
    if (_dragHit.kind == HitKind::None) {
        return getWorldBounds().contains(event.mousePos);
    }
    const auto snapshot = loadTrack();
    if (!snapshot) return true;
    const auto& track = *snapshot;
    const auto key = std::find_if(track.keys.begin(), track.keys.end(),
        [this](const auto& candidate) {
            return candidate.id == _dragHit.keyId;
        });
    if (key == track.keys.end() || _dragHit.component >= key->values.size()) {
        return true;
    }
    if (_dragHit.kind == HitKind::Key) {
        if (_selection->keyIds.size() > 1u) {
            const auto plot = plotBounds();
            const double frame = track.snapIntervalSeconds > 0.0
                ? track.snapIntervalSeconds : 0.0;
            double deltaTime = (event.mousePos.x - _lastPointer.x)
                / std::max(1.0f, plot.maxX - plot.minX) * _viewDuration;
            if (frame > 0.0) {
                deltaTime = snapTimeToInterval(deltaTime, frame);
            }
            const float deltaValue = std::round(
                (-(event.mousePos.y - _lastPointer.y)
                    / std::max(1.0f, plot.maxY - plot.minY) * _valueSpan)
                    * 1000.0f) / 1000.0f;
            const auto primary = std::find(_selection->keyIds.begin(),
                _selection->keyIds.end(), _dragHit.keyId);
            const std::size_t primaryIndex = primary == _selection->keyIds.end()
                ? 0u : static_cast<std::size_t>(primary - _selection->keyIds.begin());
            if (_document->transformKeys(_selection->keyIds,
                    deltaTime, _dragHit.component, deltaValue)) {
                _dragHit.keyId = _selection->keyIds[std::min(
                    primaryIndex, _selection->keyIds.size() - 1u)];
                _selection->primaryKeyId = _dragHit.keyId;
                _gestureChanged = true;
                _lastPointer = event.mousePos;
                if (_onSelectionChanged) {
                    _onSelectionChanged(_selection->primaryKeyId, _dragHit.component);
                }
                if (_onEdited) _onEdited();
                markDirty();
            }
            return true;
        }
        double seconds = secondsAt(event.mousePos.x);
        seconds = _document->snapTime(_trackId, seconds);
        std::vector<float> values = key->values;
        values[_dragHit.component] = std::round(valueAt(event.mousePos.y) * 1000.0f)
            / 1000.0f;
        std::string updatedId = _dragHit.keyId;
        if (_document->updateKey(updatedId, seconds, values)) {
            _dragHit.keyId = updatedId;
            _selection->primaryKeyId = updatedId;
            _selection->keyIds = {updatedId};
            _gestureChanged = true;
            (void)_document->seek(seconds);
            if (_onSelectionChanged) {
                _onSelectionChanged(_selection->primaryKeyId, _dragHit.component);
            }
            if (_onEdited) _onEdited();
            markDirty();
        }
        return true;
    }

    std::vector<float> incoming = key->inTangents;
    std::vector<float> outgoing = key->outTangents;
    incoming.resize(key->values.size(), 0.0f);
    outgoing.resize(key->values.size(), 0.0f);
    const std::size_t keyIndex = static_cast<std::size_t>(key - track.keys.begin());
    const bool isIncoming = _dragHit.kind == HitKind::InTangent;
    const double delta = handleDelta(track, keyIndex, isIncoming);
    const float slope = isIncoming
        ? (key->values[_dragHit.component] - valueAt(event.mousePos.y))
            / static_cast<float>(delta)
        : (valueAt(event.mousePos.y) - key->values[_dragHit.component])
            / static_cast<float>(delta);
    (isIncoming ? incoming : outgoing)[_dragHit.component] =
        std::round(slope * 1000.0f) / 1000.0f;
    if (_document->setTangents(
            _dragHit.keyId, incoming, outgoing)) {
        _gestureChanged = true;
        if (_onEdited) _onEdited();
        markDirty();
    }
    return true;
}

bool CurveCanvas::onMouseButtonUp(
    const ayt::ui::UIMouseEvent& event)
{
    if ((event.mouseButton == 1 || event.mouseButton == 2) && _panning) {
        _panning = false;
        return true;
    }
    if (event.mouseButton == 0 && _boxSelecting) {
        _boxSelecting = false;
        _boxEnd = event.mousePos;
        if (!_boxMoved && _document != nullptr) {
            (void)_document->seek(secondsAt(event.mousePos.x));
        }
        if (_onSelectionChanged && !_selection->primaryKeyId.empty()) {
            _onSelectionChanged(_selection->primaryKeyId, 0u);
        }
        markDirty();
        return true;
    }
    if (event.mouseButton != 0 || _dragHit.kind == HitKind::None) return false;
    finishGesture(false);
    return true;
}

bool CurveCanvas::onMouseWheel(
    const ayt::ui::UIMouseWheelEvent& event)
{
    if (!plotBounds().contains(event.mousePos)) return false;
    const float anchorValue = valueAt(event.mousePos.y);
    const double factor = std::pow(1.1, event.deltaY / 40.0f);
    const auto plot = plotBounds();
    const double nx = std::clamp(static_cast<double>((event.mousePos.x - plot.minX)
        / std::max(1.0f, plot.maxX - plot.minX)), 0.0, 1.0);
    const float ny = std::clamp((plot.maxY - event.mousePos.y)
        / std::max(1.0f, plot.maxY - plot.minY), 0.0f, 1.0f);
    TimeViewport timeView{_viewStart, _viewDuration};
    timeView.zoomAt(nx, factor);
    _viewStart = timeView.startSeconds;
    _viewDuration = timeView.durationSeconds;
    _valueSpan = std::clamp(_valueSpan * static_cast<float>(factor),
                            0.0001f, 1.0e8f);
    _valueCenter = anchorValue - (ny - 0.5f) * _valueSpan;
    _viewValid = true;
    markDirty();
    return true;
}

void CurveCanvas::finishGesture(bool cancel)
{
    (void)_gesture.finish(cancel);
    const bool changed = _gestureChanged;
    _dragHit = {};
    _gestureChanged = false;
    if (changed && _onEdited) _onEdited();
    markDirty();
}

void CurveCanvas::onCaptureCancelled()
{
    _panning = false;
    _boxSelecting = false;
    _boxMoved = false;
    finishGesture(true);
}

ayt::ui::UiCursorHint CurveCanvas::getCursorHint() const
{
    if (_panning) return ayt::ui::UiCursorHint::Move;
    if (_dragHit.kind == HitKind::Key) return ayt::ui::UiCursorHint::Move;
    return ayt::ui::UiCursorHint::Default;
}

void CurveCanvas::onRender(ayt::ui::IRenderBackend& renderer)
{
    const auto bounds = getWorldBounds();
    renderer.pushClip(bounds);
    renderer.drawRect(bounds, {0.038f, 0.045f, 0.061f, 1.0f});
    const auto snapshot = loadTrack();
    if (!snapshot) {
        renderer.drawText(bounds, L"Select a track to edit curves", 11,
            ayt::math::FVector4{0.48f, 0.53f, 0.62f, 1.0f});
        renderer.popClip();
        return;
    }
    const auto& track = *snapshot;
    ensureView(track);
    const auto plot = plotBounds();
    renderer.drawRect(plot, {0.055f, 0.065f, 0.085f, 1.0f});
    for (double tick : timelineTicks({_viewStart, _viewDuration}, plot.maxX - plot.minX)) {
        const float x = worldX(tick);
        renderer.drawRect({x, plot.minY, x + 1.0f, plot.maxY},
                          {0.105f, 0.12f, 0.15f, 1.0f});
    }
    for (int line = 0; line <= 10; ++line) {
        const float y = plot.minY + (plot.maxY - plot.minY) * line / 10.0f;
        renderer.drawRect({plot.minX, y, plot.maxX, y + 1.0f},
                          {0.105f, 0.12f, 0.15f, 1.0f});
    }
    const float zeroY = worldY(0.0f);
    if (zeroY >= plot.minY && zeroY <= plot.maxY) {
        renderer.drawRect({plot.minX, zeroY, plot.maxX, zeroY + 1.0f},
                          {0.23f, 0.27f, 0.34f, 1.0f});
    }

    const std::size_t width = track.keys.empty() ? 0u : track.keys.front().values.size();
    constexpr int samples = 160;
    if (_sampleTrack != snapshot || _sampleStart != _viewStart
        || _sampleDuration != _viewDuration) {
        _sampleTrack = snapshot;
        _sampleStart = _viewStart;
        _sampleDuration = _viewDuration;
        for (std::size_t component = 0; component < _samples.size(); ++component) {
            auto& values = _samples[component];
            values.clear();
            if (component >= width) continue;
            values.reserve(samples + 1);
            for (int sample = 0; sample <= samples; ++sample) {
                const double seconds = _viewStart + _viewDuration * sample / samples;
                values.push_back(track.sample ? track.sample(component, seconds) : 0.0f);
            }
        }
    }
    for (std::size_t component = 0u; component < width
         && component < _componentVisible.size(); ++component) {
        if (!_componentVisible[component]) continue;
        const auto path = renderer.createPath();
        if (path.id >= 0) {
            std::array<ayt::math::FVector2, samples + 1> points{};
            for (int sample = 0; sample <= samples; ++sample) {
                const double seconds = _viewStart + _viewDuration
                    * static_cast<double>(sample) / samples;
                points[static_cast<std::size_t>(sample)] = {
                    worldX(seconds), worldY(_samples[component][static_cast<std::size_t>(sample)])};
            }
            renderer.addPathContour(path, points.data(), points.size(), false);
            renderer.setPathStrokeColor(path, kColors[component]);
            renderer.setPathStrokeWidth(path, 1.5f);
            renderer.drawPath(path, ayt::ui::PathFillMode::Stroke);
            renderer.releasePath(path);
        }
        for (std::size_t keyIndex = 0u; keyIndex < track.keys.size(); ++keyIndex) {
            const auto& key = track.keys[keyIndex];
            const float x = worldX(key.timeSeconds);
            const float y = worldY(key.values[component]);
            const bool selected = isSelected(key.id);
            renderer.drawRoundedRect({x - 4.0f, y - 4.0f, x + 4.0f, y + 4.0f},
                selected ? ayt::math::FVector4{1.0f, 0.86f, 0.48f, 1.0f}
                         : kColors[component], 2.0f);
            if (!selected || key.id != _selection->primaryKeyId
                || !track.editableTangents) continue;
            const double inDelta = handleDelta(track, keyIndex, true);
            const double outDelta = handleDelta(track, keyIndex, false);
            const float inSlope = component < key.inTangents.size()
                ? key.inTangents[component] : 0.0f;
            const float outSlope = component < key.outTangents.size()
                ? key.outTangents[component] : 0.0f;
            const ayt::math::FVector2 handles[] = {
                {worldX(key.timeSeconds - inDelta),
                 worldY(key.values[component] - inSlope * static_cast<float>(inDelta))},
                {worldX(key.timeSeconds + outDelta),
                 worldY(key.values[component] + outSlope * static_cast<float>(outDelta))}};
            const auto tangentPath = renderer.createPath();
            if (tangentPath.id >= 0) {
                const ayt::math::FVector2 left[] = {{x, y}, handles[0]};
                const ayt::math::FVector2 right[] = {{x, y}, handles[1]};
                renderer.addPathContour(tangentPath, left, 2u, false);
                renderer.addPathContour(tangentPath, right, 2u, false);
                renderer.setPathStrokeColor(tangentPath,
                    {0.72f, 0.76f, 0.82f, 0.85f});
                renderer.setPathStrokeWidth(tangentPath, 1.0f);
                renderer.drawPath(tangentPath, ayt::ui::PathFillMode::Stroke);
                renderer.releasePath(tangentPath);
            }
            for (const auto& handle : handles) {
                renderer.drawRoundedRect({handle.x - 3.0f, handle.y - 3.0f,
                                          handle.x + 3.0f, handle.y + 3.0f},
                    {0.78f, 0.81f, 0.88f, 1.0f}, 1.5f);
            }
        }
    }

    if (_document != nullptr) {
        const float playhead = worldX(_document->positionSeconds());
        if (playhead >= plot.minX && playhead <= plot.maxX) {
            renderer.drawRect({playhead, plot.minY, playhead + 1.0f, plot.maxY},
                              {1.0f, 0.33f, 0.29f, 0.9f});
        }
    }
    if (_boxSelecting && _boxMoved) {
        const ayt::math::FRectangle selection{
            std::min(_boxStart.x, _boxEnd.x),
            std::min(_boxStart.y, _boxEnd.y),
            std::max(_boxStart.x, _boxEnd.x),
            std::max(_boxStart.y, _boxEnd.y)};
        renderer.drawRect(selection, {0.20f, 0.46f, 0.78f, 0.18f});
        renderer.drawBorderRect(selection,
            {0.40f, 0.68f, 1.0f, 0.9f}, 1.0f, 0.0f);
    }
    std::wostringstream range;
    range << std::fixed << std::setprecision(2)
          << _viewStart << L" - " << (_viewStart + _viewDuration) << L" s";
    renderer.drawText(
        ayt::math::FRectangle{plot.minX, bounds.minY, plot.maxX, plot.minY},
        range.str(), 10,
        ayt::math::FVector4{0.5f, 0.56f, 0.65f, 1.0f});
    renderer.popClip();
}

} // namespace ayt::ui::authoring
