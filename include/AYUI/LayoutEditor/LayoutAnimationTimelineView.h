#pragma once

#include "AYUI/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

struct LayoutAnimationTimelineTrackView {
    std::wstring label;
    std::vector<float> keyTimesMs;
};

enum class LayoutAnimationKeyDragPhase {
    Begin,
    Update,
    End
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
    void setSelectedCurve(AnimationCurve curve);
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
    math::FRectangle plotBounds() const;
    math::FRectangle keyframeBounds(int trackIndex, int keyIndex) const;
    float timeAtWorldX(float worldX) const;

    bool onMouseButtonDown(const UIMouseEvent& event) override;
    bool onMouseMove(const UIMouseEvent& event) override;
    bool onMouseButtonUp(const UIMouseEvent& event) override;
    bool onMouseWheel(const UIMouseWheelEvent& event) override;
    void onMouseLeave() override;
    UiCursorHint getCursorHint() const override;

protected:
    void onRender(IRenderBackend& renderer) override;

private:
    float visibleDurationMs() const;
    float worldXForTime(float timeMs) const;
    float clampViewStart(float value) const;
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
    int _selectedTrack = -1;
    int _selectedKey = -1;
    AnimationCurve _selectedCurve = AnimationCurve::Linear;

    bool _scrubbing = false;
    bool _panning = false;
    bool _draggingKey = false;
    int _dragTrack = -1;
    int _dragKey = -1;
    float _lastPointerX = 0.0f;

    SeekCallback _onSeek;
    SelectionCallback _onSelectionChanged;
    KeyDragCallback _onKeyDragged;

    static constexpr float kLabelWidth = 150.0f;
    static constexpr float kHeaderHeight = 30.0f;
    static constexpr float kTrackHeight = 28.0f;
    static constexpr float kKeySize = 9.0f;
};

} // namespace ayt::ui
