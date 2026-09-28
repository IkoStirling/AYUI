#pragma once

#include "AYUI/Widget.h"
#include "AYUI/Authoring/TimelineModel.h"

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

struct LayoutAnimationTimelineTrackView {
    std::wstring label;
    std::vector<float> keyTimesMs;
};

// Keep the existing callback type at the layout-page boundary while sharing
// phase ordering with the neutral authoring model (no callback ABI change).
enum class LayoutAnimationKeyDragPhase {
    Begin = static_cast<int>(authoring::EditGesturePhase::Begin),
    Update = static_cast<int>(authoring::EditGesturePhase::Update),
    End = static_cast<int>(authoring::EditGesturePhase::End),
    Cancel = static_cast<int>(authoring::EditGesturePhase::Cancel)
};

// Authoring-only timeline surface. It owns no document data and never creates
// a second animation evaluator: the LayoutEditorSession supplies a lightweight
// view snapshot and routes edits back into UIAnimationLibrary.
class LayoutAnimationTimelineView final : public Widget {
public:
    using SeekCallback = std::function<void(float)>;
    using SelectionCallback = std::function<void(int, int)>;
    using KeyDragCallback = std::function<int(
        int, int, float, LayoutAnimationKeyDragPhase)>;

    LayoutAnimationTimelineView();

    void setTracks(std::vector<LayoutAnimationTimelineTrackView> tracks);
    bool setTrackKeyTimes(int trackIndex, std::vector<float> keyTimesMs);
    const std::vector<LayoutAnimationTimelineTrackView>& tracks() const {
        return _tracks;
    }
    void setDurationMs(float durationMs);
    float durationMs() const { return _durationMs; }
    void setCurrentTimeMs(float timeMs);
    float currentTimeMs() const { return _currentTimeMs; }
    void setSelection(int trackIndex, int keyIndex);
    int selectedTrackIndex() const { return _selectedTrack; }
    int selectedKeyIndex() const { return _selectedKey; }
    void setSelectedCurve(AnimationCurve curve,
                          const CubicBezierParameters& bezier = {},
                          const SpringParameters& spring = {},
                          float segmentDurationMs = 1000.0f);
    AnimationCurve selectedCurve() const { return _selectedCurve; }

    void setOnSeek(SeekCallback callback) { _onSeek = std::move(callback); }
    void setOnSelectionChanged(SelectionCallback callback) {
        _onSelectionChanged = std::move(callback);
    }
    void setOnKeyDragged(KeyDragCallback callback) {
        _onKeyDragged = std::move(callback);
    }
    void clearCallbacks();

    float zoom() const { return _zoom; }
    float viewStartMs() const { return _viewStartMs; }
    float verticalScrollOffset() const;
    float maxVerticalScrollOffset() const;
    math::FRectangle plotBounds() const;
    math::FRectangle keyframeBounds(int trackIndex, int keyIndex) const;
    float timeAtWorldX(float worldX) const;

    bool onMouseButtonDown(const UIMouseEvent& event) override;
    bool onMouseMove(const UIMouseEvent& event) override;
    bool onMouseButtonUp(const UIMouseEvent& event) override;
    bool onMouseWheel(const UIMouseWheelEvent& event) override;
    void onCaptureCancelled() override;
    void onMouseLeave() override;
    UiCursorHint getCursorHint() const override;

protected:
    void onRender(IRenderBackend& renderer) override;

private:
    float visibleDurationMs() const;
    float worldXForTime(float timeMs) const;
    float clampViewStart(float value) const;
    void setVerticalScrollOffset(float value);
    void ensureTrackVisible(int trackIndex);
    bool hasVerticalOverflow() const;
    math::FRectangle verticalScrollBarBounds() const;
    math::FRectangle verticalScrollThumbBounds() const;
    void updateVerticalScrollFromPointer(float pointerY);
    bool hitKeyframe(const math::FVector2& point,
                     int& trackIndex, int& keyIndex) const;
    void emitSeek(float worldX);
    void drawCurvePreview(IRenderBackend& renderer,
                          const math::FRectangle& bounds) const;

    std::vector<LayoutAnimationTimelineTrackView> _tracks;
    float _durationMs = 1000.0f;
    float _currentTimeMs = 0.0f;
    float _zoom = 1.0f;
    float _viewStartMs = 0.0f;
    float _verticalScrollOffset = 0.0f;
    int _selectedTrack = -1;
    int _selectedKey = -1;
    AnimationCurve _selectedCurve = AnimationCurve::Linear;
    CubicBezierParameters _selectedBezier;
    SpringParameters _selectedSpring;
    float _selectedSegmentDurationMs = 1000.0f;

    bool _scrubbing = false;
    bool _panning = false;
    bool _draggingKey = false;
    bool _scrollingTracks = false;
    int _dragTrack = -1;
    int _dragKey = -1;
    float _dragLastTimeMs = 0.0f;
    float _lastPointerX = 0.0f;
    float _scrollThumbGrabOffset = 0.0f;

    SeekCallback _onSeek;
    SelectionCallback _onSelectionChanged;
    KeyDragCallback _onKeyDragged;

    static constexpr float kLabelWidth = 150.0f;
    static constexpr float kHeaderHeight = 30.0f;
    static constexpr float kTrackHeight = 28.0f;
    static constexpr float kKeySize = 9.0f;
    static constexpr float kScrollBarWidth = 9.0f;
    static constexpr float kMinScrollThumbHeight = 18.0f;
};

} // namespace ayt::ui
