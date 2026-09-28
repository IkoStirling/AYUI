#pragma once

#include "AYUI/Authoring/TimelineModel.h"
#include "AYUI/Authoring/AuthoringPrimitives.h"

#include <AYUI/Widget.h>

#include <functional>
#include <memory>
#include <string>

namespace ayt::ui::authoring {

class DopeSheet : public ayt::ui::Widget {
public:
    explicit DopeSheet(
        std::shared_ptr<ICurveEditorSource> document);
    ~DopeSheet() override;

    void frameAll();
    /// Diagnostic reconstruction count; playhead-only redraws reuse row indices.
    std::uint64_t rowIndexBuildCount() const noexcept { return _rowIndexBuilds; }
    void setSelection(std::string trackId, std::string keyId);
    void setOnSelectionChanged(
        std::function<void(const std::string&, const std::string&)> callback) {
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
    struct KeyHit {
        std::string trackId;
        std::string keyId;
    };

    ayt::math::FRectangle plotBounds() const noexcept;
    double secondsAt(float x) const noexcept;
    float worldX(double seconds) const noexcept;
    KeyHit hitKey(ayt::math::FVector2 point) const;
    void finishDrag(bool cancel);
    void indexRows(std::shared_ptr<const TimelineSnapshot> snapshot) const;

    std::shared_ptr<ICurveEditorSource> _document;
    std::shared_ptr<TimelineSelection> _selection;
    mutable std::shared_ptr<const TimelineSnapshot> _rowSnapshot;
    mutable std::vector<std::vector<std::size_t>> _rowKeys;
    mutable std::uint64_t _rowIndexBuilds = 0;
    std::string _dragKeyId;
    double _viewStart = 0.0;
    double _viewDuration = 1.0;
    bool _viewValid = false;
    bool _panning = false;
    bool _draggingKey = false;
    bool _boxSelecting = false;
    bool _boxMoved = false;
    ayt::math::FVector2 _boxStart{}, _boxEnd{};
    TimelineSelection _boxBefore;
    double _dragPointerTime = 0, _dragKeyTime = 0;
    bool _gestureChanged = false;
    EditGestureSession _gesture;
    ayt::math::FVector2 _lastPointer{};
    std::function<void(const std::string&, const std::string&)>
        _onSelectionChanged;
    std::function<void()> _onEdited;
};

} // namespace ayt::ui::authoring
