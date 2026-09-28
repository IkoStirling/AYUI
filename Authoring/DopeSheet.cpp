#include "AYUI/Authoring/DopeSheet.h"

#include <AYUI/IRenderBackend.h>
#include <AYUI/UnicodeText.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ayt::ui::authoring {
namespace {

constexpr float kLabelWidth = 168.0f;
constexpr float kHeaderHeight = 22.0f;
constexpr float kRowHeight = 24.0f;
constexpr float kKeyRadius = 5.0f;

} // namespace

DopeSheet::DopeSheet(
    std::shared_ptr<ICurveEditorSource> document)
    : _document(std::move(document)),
      _selection(_document ? _document->selectionState() : std::make_shared<TimelineSelection>()),
      _gesture(_document)
{
    setId("dope_sheet");
}

DopeSheet::~DopeSheet()
{
    _onEdited = {};
    _onSelectionChanged = {};
    finishDrag(true);
}

void DopeSheet::frameAll()
{
    _viewValid = false;
    markDirty();
}

void DopeSheet::setSelection(
    std::string trackId, std::string keyId)
{
    if (_selection->trackId == trackId && _selection->primaryKeyId == keyId) return;
    _selection->trackId = std::move(trackId);
    _selection->primaryKeyId = std::move(keyId);
    _selection->keyIds = _selection->primaryKeyId.empty()
        ? std::vector<std::string>{} : std::vector<std::string>{_selection->primaryKeyId};
    markDirty();
}

ayt::math::FRectangle DopeSheet::plotBounds() const noexcept
{
    const auto bounds = getWorldBounds();
    return {std::min(bounds.maxX, bounds.minX + kLabelWidth),
            std::min(bounds.maxY, bounds.minY + kHeaderHeight),
            bounds.maxX, bounds.maxY};
}

double DopeSheet::secondsAt(float x) const noexcept
{
    const auto plot = plotBounds();
    return TimeViewport{_viewStart, _viewDuration}.timeAt(
        static_cast<double>((x - plot.minX)
            / std::max(1.0f, plot.maxX - plot.minX)));
}

float DopeSheet::worldX(double seconds) const noexcept
{
    const auto plot = plotBounds();
    return plot.minX + static_cast<float>(
        TimeViewport{_viewStart, _viewDuration}.normalizedAt(seconds))
        * (plot.maxX - plot.minX);
}

DopeSheet::KeyHit DopeSheet::hitKey(
    ayt::math::FVector2 point) const
{
    if (_document == nullptr || !plotBounds().contains(point)) return {};
    const auto snapshot = _document->timelineSnapshot();
    if (!snapshot) return {};
    const auto& tracks = snapshot->tracks;
    const auto& keys = snapshot->keys;
    const TimelineRowLayout rows{plotBounds().minY, kRowHeight, 0.0};
    const auto visible = rows.visibleRows(tracks.size(), plotBounds().maxY - plotBounds().minY);
    for (std::size_t row = visible.first; row < visible.second; ++row) {
        const float y = static_cast<float>(rows.rowCenter(row));
        for (const auto& key : keys) {
            if (key.trackId != tracks[row].id) continue;
            const ayt::math::FVector2 center{worldX(key.timeSeconds), y};
            const float dx = point.x - center.x;
            const float dy = point.y - center.y;
            if (dx * dx + dy * dy <= 100.0f) {
                return {key.trackId, key.id};
            }
        }
    }
    return {};
}

bool DopeSheet::onMouseButtonDown(
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
    const KeyHit hit = hitKey(event.mousePos);
    if (hit.keyId.empty()) {
        if (_document != nullptr) {
            (void)_document->seek(secondsAt(event.mousePos.x));
        }
        markDirty();
        return true;
    }
    _selection->trackId = hit.trackId;
    _selection->primaryKeyId = hit.keyId;
    _selection->keyIds = {hit.keyId};
    _dragKeyId = hit.keyId;
    _gestureChanged = false;
    if (_onSelectionChanged) {
        _onSelectionChanged(_selection->trackId, _selection->primaryKeyId);
    }
    markDirty();
    if (_document != nullptr
        && _gesture.begin(
            "Move timeline key")) {
        _draggingKey = true;
        return true;
    }
    _dragKeyId.clear();
    return true; // Read-only owners still permit key selection.
}

bool DopeSheet::onMouseMove(
    const ayt::ui::UIMouseEvent& event)
{
    if (_panning) {
        const auto plot = plotBounds();
        _viewStart -= (event.mousePos.x - _lastPointer.x)
            / std::max(1.0f, plot.maxX - plot.minX) * _viewDuration;
        _lastPointer = event.mousePos;
        markDirty();
        return true;
    }
    if (!_draggingKey || _document == nullptr) {
        return getWorldBounds().contains(event.mousePos);
    }
    const double seconds = _document->snapTime(
        _selection->trackId, secondsAt(event.mousePos.x));
    std::string updatedKey = _dragKeyId;
    std::string updatedTrack = _selection->trackId;
    if (_document->moveTimelineKey(updatedTrack, updatedKey, seconds)) {
        _dragKeyId = updatedKey;
        _selection->primaryKeyId = updatedKey;
        _selection->keyIds = {updatedKey};
        _selection->trackId = updatedTrack;
        _gestureChanged = true;
        (void)_document->seek(seconds);
        if (_onSelectionChanged)
            _onSelectionChanged(_selection->trackId, _selection->primaryKeyId);
        if (_onEdited) _onEdited();
        markDirty();
    }
    return true;
}

bool DopeSheet::onMouseButtonUp(
    const ayt::ui::UIMouseEvent& event)
{
    if ((event.mouseButton == 1 || event.mouseButton == 2) && _panning) {
        _panning = false;
        return true;
    }
    if (event.mouseButton != 0 || !_draggingKey) return false;
    finishDrag(false);
    return true;
}

bool DopeSheet::onMouseWheel(
    const ayt::ui::UIMouseWheelEvent& event)
{
    if (!plotBounds().contains(event.mousePos)) return false;
    const auto plot = plotBounds();
    const double normalized = std::clamp(static_cast<double>(
        (event.mousePos.x - plot.minX)
        / std::max(1.0f, plot.maxX - plot.minX)), 0.0, 1.0);
    TimeViewport timeView{_viewStart, _viewDuration};
    timeView.zoomAt(normalized, std::pow(1.1, event.deltaY / 40.0f));
    _viewStart = timeView.startSeconds;
    _viewDuration = timeView.durationSeconds;
    _viewValid = true;
    markDirty();
    return true;
}

void DopeSheet::finishDrag(bool cancel)
{
    (void)_gesture.finish(cancel);
    const bool changed = _gestureChanged;
    _draggingKey = false;
    _dragKeyId.clear();
    _gestureChanged = false;
    if (changed && _onEdited) _onEdited();
    markDirty();
}

void DopeSheet::onCaptureCancelled()
{
    _panning = false;
    finishDrag(true);
}

ayt::ui::UiCursorHint DopeSheet::getCursorHint() const
{
    return _panning || _draggingKey ? ayt::ui::UiCursorHint::Move
                                    : ayt::ui::UiCursorHint::SizeHorizontal;
}

void DopeSheet::onRender(ayt::ui::IRenderBackend& renderer)
{
    const auto bounds = getWorldBounds();
    renderer.pushClip(bounds);
    renderer.drawRect(bounds, {0.043f, 0.052f, 0.069f, 1.0f});
    if (_document == nullptr) {
        renderer.popClip();
        return;
    }
    if (!_viewValid) {
        _viewStart = 0.0;
        _viewDuration = std::max(0.05, _document->durationSeconds());
        _viewValid = true;
    }
    const auto plot = plotBounds();
    renderer.drawRect({bounds.minX, bounds.minY, bounds.maxX,
                       bounds.minY + kHeaderHeight},
                      {0.065f, 0.079f, 0.105f, 1.0f});
    renderer.drawText({bounds.minX + 8.0f, bounds.minY,
                       bounds.minX + kLabelWidth, bounds.minY + kHeaderHeight},
        L"DOPE SHEET", 11, ayt::math::FVector4{0.66f, 0.72f, 0.82f, 1.0f});
    for (double seconds : timelineTicks({_viewStart, _viewDuration}, plot.maxX - plot.minX)) {
        const float x = worldX(seconds);
        renderer.drawRect({x, bounds.minY, x + 1.0f, bounds.maxY},
                          {0.11f, 0.13f, 0.17f, 1.0f});
        renderer.drawText({x + 3.0f, bounds.minY, x + 58.0f,
                           bounds.minY + kHeaderHeight}, formatTime(seconds, TimeDisplay::Seconds, 2), 9,
                          ayt::math::FVector4{0.47f, 0.53f, 0.62f, 1.0f});
    }
    const auto snapshot = _document->timelineSnapshot();
    if (!snapshot) {
        renderer.popClip();
        return;
    }
    const auto& tracks = snapshot->tracks;
    const auto& keys = snapshot->keys;
    const TimelineRowLayout rows{plot.minY, kRowHeight, 0.0};
    const auto visible = rows.visibleRows(tracks.size(), plot.maxY - plot.minY);
    for (std::size_t row = visible.first; row < visible.second; ++row) {
        const float y = static_cast<float>(rows.rowTop(row));
        const bool selected = tracks[row].id == _selection->trackId;
        if (selected) {
            renderer.drawRect({bounds.minX, y, bounds.maxX,
                               std::min(bounds.maxY, y + kRowHeight)},
                              {0.075f, 0.16f, 0.25f, 0.85f});
        }
        renderer.drawRect({bounds.minX, y + kRowHeight - 1.0f, bounds.maxX,
                           y + kRowHeight}, {0.10f, 0.12f, 0.16f, 1.0f});
        renderer.drawText({bounds.minX + 8.0f, y,
                           bounds.minX + kLabelWidth - 5.0f,
                           std::min(bounds.maxY, y + kRowHeight)},
            ayt::ui::decodeUtf8Text(tracks[row].name), 10,
            selected ? ayt::math::FVector4{0.83f, 0.91f, 1.0f, 1.0f}
                     : ayt::math::FVector4{0.59f, 0.65f, 0.74f, 1.0f});
        for (const auto& key : keys) {
            if (key.trackId != tracks[row].id) continue;
            const float x = worldX(key.timeSeconds);
            if (x < plot.minX - kKeyRadius || x > plot.maxX + kKeyRadius) continue;
            const float centerY = y + kRowHeight * 0.5f;
            const bool keySelected = key.id == _selection->primaryKeyId;
            renderer.drawRoundedRect(
                {x - kKeyRadius, centerY - kKeyRadius,
                 x + kKeyRadius, centerY + kKeyRadius},
                keySelected
                    ? ayt::math::FVector4{1.0f, 0.75f, 0.28f, 1.0f}
                    : tracks[row].kind == TimelineTrackKind::Event
                        ? ayt::math::FVector4{0.85f, 0.42f, 0.76f, 1.0f}
                        : ayt::math::FVector4{0.31f, 0.68f, 1.0f, 1.0f},
                2.0f);
        }
    }
    const float playhead = worldX(_document->positionSeconds());
    if (playhead >= plot.minX && playhead <= plot.maxX) {
        renderer.drawRect({playhead, bounds.minY, playhead + 1.0f, bounds.maxY},
                          {1.0f, 0.33f, 0.29f, 0.95f});
    }
    renderer.popClip();
}

} // namespace ayt::ui::authoring
