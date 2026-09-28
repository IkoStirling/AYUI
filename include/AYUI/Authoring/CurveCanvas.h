#pragma once

#include "AYUI/Authoring/TimelineModel.h"
#include "AYUI/Authoring/AuthoringPrimitives.h"

#include <AYUI/Widget.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ayt::ui::authoring {

class CurveCanvas : public ayt::ui::Widget {
public:
    explicit CurveCanvas(
        std::shared_ptr<ICurveEditorSource> document);
    ~CurveCanvas() override;

    void setTrackId(std::string trackId);
    const std::string& trackId() const noexcept { return _trackId; }
    void setComponentVisible(std::size_t component, bool visible);
    bool componentVisible(std::size_t component) const noexcept;
    void frameAll();
    void selectAllKeys();
    void clearSelection();
    bool deleteSelectedKeys();
    std::size_t selectedKeyCount() const noexcept {
        return _selection->keyIds.size();
    }
    void setOnSelectionChanged(
        std::function<void(const std::string&, std::size_t)> callback) {
        _onSelectionChanged = std::move(callback);
    }
    void setOnEdited(std::function<void()> callback) {
        _onEdited = std::move(callback);
    }

    bool onMouseMove(const ayt::ui::UIMouseEvent& event) override;
    bool onMouseButtonDown(const ayt::ui::UIMouseEvent& event) override;
    bool onMouseButtonUp(const ayt::ui::UIMouseEvent& event) override;
    bool onMouseWheel(const ayt::ui::UIMouseWheelEvent& event) override;
    void onCaptureCancelled() override;
    ayt::ui::UiCursorHint getCursorHint() const override;

protected:
    void onRender(ayt::ui::IRenderBackend& renderer) override;

private:
    enum class HitKind { None, Key, InTangent, OutTangent };
    struct Hit {
        HitKind kind = HitKind::None;
        std::string keyId;
        std::size_t component = 0u;
    };

    ayt::math::FRectangle plotBounds() const noexcept;
    std::shared_ptr<const CurveTrack> loadTrack() const;
    void ensureView(const CurveTrack& track);
    float worldX(double seconds) const noexcept;
    float worldY(float value) const noexcept;
    double secondsAt(float x) const noexcept;
    float valueAt(float y) const noexcept;
    Hit hitTest(ayt::math::FVector2 point,
                const CurveTrack& track) const;
    void finishGesture(bool cancel);
    bool isSelected(const std::string& keyId) const;

    std::shared_ptr<ICurveEditorSource> _document;
    std::string _trackId;
    std::shared_ptr<TimelineSelection> _selection;
    std::array<bool, 4> _componentVisible{{true, true, true, true}};
    std::function<void(const std::string&, std::size_t)> _onSelectionChanged;
    std::function<void()> _onEdited;
    double _viewStart = 0.0;
    double _viewDuration = 1.0;
    float _valueCenter = 0.0f;
    float _valueSpan = 2.0f;
    bool _viewValid = false;
    bool _panning = false;
    bool _boxSelecting = false;
    bool _boxMoved = false;
    bool _gestureChanged = false;
    EditGestureSession _gesture;
    Hit _dragHit;
    std::shared_ptr<const CurveTrack> _sampleTrack;
    double _sampleStart = 0.0;
    double _sampleDuration = 0.0;
    std::array<std::vector<float>, 4> _samples;
    ayt::math::FVector2 _lastPointer{};
    ayt::math::FVector2 _boxStart{};
    ayt::math::FVector2 _boxEnd{};
};

} // namespace ayt::ui::authoring
