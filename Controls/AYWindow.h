#pragma once

#include "AYWidget.h"
#include "AYScrollBar.h"

namespace ayt::ui {

// =============================================================================
// Phase D (D1) — Resize hit-zone enum (Q1).
// =============================================================================
//
// v1 ships ONLY the bottom-right (SE) corner. The enum is a deliberate hook so
// the future "all 4 corners + 4 edges" upgrade is a one-line branch addition
// inside hitTestResizeEdge without touching the public API.
// =============================================================================
enum class ResizeEdge {
    None,
    BottomRight,
};

class Window : public CompoundWidget {
public:
    Window();
    virtual ~Window();

    void setTitle(const std::wstring& title) { _title = title; }
    const std::wstring& getTitle() const { return _title; }

    void setMovable(bool movable) { _movable = movable; }
    bool isMovable() const { return _movable; }

    void setResizable(bool resizable) { _resizable = resizable; }
    bool isResizable() const { return _resizable; }

    void setClosable(bool closable) { _closable = closable; }
    bool isClosable() const { return _closable; }

    void setModal(bool modal) { _modal = modal; }
    bool isModal() const { return _modal; }

    void setOnClose(std::function<void()> callback) { _onClose = callback; }

    // Phase D (D1) — Resize callback (Q4). Fires only on button-up after a
    // real size change. Drag-internal setSize calls do NOT fire (avoids
    // notification noise). Signature: (oldSize, newSize). Pass by value so
    // hosts get stable snapshots regardless of timing.
    void setOnResize(std::function<void(const math::FVector2& /*oldSize*/,
                                         const math::FVector2& /*newSize*/)> cb) {
        _onResize = std::move(cb);
    }

    void setMinSize(float width, float height);
    void setMinSize(const math::FVector2& size);
    const math::FVector2& getMinSize() const { return _minSize; }

    void setSize(const math::FVector2& size);

    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;

    void setTitleBarHeight(float height) { _titleBarHeight = height; }
    float getTitleBarHeight() const { return _titleBarHeight; }

    bool isDragging() const { return _isDragging; }
    bool isResizing() const { return _isResizing; }
    ResizeEdge getActiveResizeEdge() const { return _resizeEdge; }

    void onMouseLeave() override;

    UiCursorHint getCursorHint() const override;

    void onRender(IRenderBackend& renderer) override;
    void renderChildren(IRenderBackend& renderer) override;

    void layoutChildren() override;

    // When body children exceed the client height, a vertical scrollbar
    // appears and layout offsets children by -_scrollY.
    void setBodyScrollEnabled(bool enabled) { _bodyScrollEnabled = enabled; }
    bool isBodyScrollEnabled() const { return _bodyScrollEnabled; }
    bool scrollBodyBy(float dy);

    // Test seam — exposes the SE hit-zone computation. Not virtual (single
    // owner; no widget subclass should override). Returns ResizeEdge::None
    // when _resizable=false or outside the SE band.
    ResizeEdge hitTestResizeEdge(const math::FVector2& worldPos) const;

protected:
    math::FVector2 localPositionFromMouse(const math::FVector2& mouseWorldPos) const;
    void clampPositionWithinParent();
    void renderResizeGrip(IRenderBackend& renderer) const;
    void ensureBodyScrollBar();
    void syncBodyScrollBar();

    std::wstring _title;
    bool _movable;
    bool _resizable;
    bool _closable;
    bool _modal;
    std::function<void()> _onClose;
    std::function<void(const math::FVector2&, const math::FVector2&)> _onResize;
    float _titleBarHeight;
    math::FVector2 _minSize;

    bool _isDragging;
    bool _titleBarHover = false;
    math::FVector2 _dragOffset;

    // Phase D (D1) — resize state.
    ResizeEdge _resizeEdge = ResizeEdge::None;
    bool _isResizing = false;
    math::FVector2 _resizeStartSize{};
    math::FVector2 _resizeStartMousePos{};

    // Overflow scroll for stacked body children (Render / Inspector).
    bool _bodyScrollEnabled = true;
    float _scrollY = 0.0f;
    float _contentExtentY = 0.0f;
    float _bodyViewportH = 0.0f;
    ScrollBar* _bodyVBar = nullptr;
};

} // namespace ayt::ui
