#include "AYSplitterHandle.h"
#include "AYBox.h"
#include "AYIRenderBackend.h"

namespace ayt::ui {

SplitterHandle::SplitterHandle() {
    setSize(math::FVector2(kDefaultWidth, 100.0f));
}

void SplitterHandle::bindPanels(HBox* owner, int leftPanelSlot, int rightPanelSlot) {
    _owner = owner;
    _leftPanelSlot = leftPanelSlot;
    _rightPanelSlot = rightPanelSlot;
}

Widget* SplitterHandle::hitTest(const math::FVector2& worldPos) {
    if (!_visible) {
        return nullptr;
    }
    return getWorldBounds().contains(worldPos) ? this : nullptr;
}

bool SplitterHandle::onMouseMove(const UIMouseEvent& e) {
    _hover = true;
    if (_dragging) {
        applyDrag(e.mousePos.x);
    }
    return true;
}

bool SplitterHandle::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }

    if (_owner == nullptr || _leftPanelSlot < 0 || _rightPanelSlot < 0) {
        if (HBox* box = dynamic_cast<HBox*>(getParent())) {
            box->rebindSplitters();
        }
    }

    if (_owner == nullptr || _leftPanelSlot < 0 || _rightPanelSlot < 0) {
        return false;
    }

    _dragging = true;
    _hover = true;
    _dragStartMouseX = e.mousePos.x;
    _adjustLeft = _owner->slotWidth(_leftPanelSlot) > 0.0f;
    const int targetSlot = _adjustLeft ? _leftPanelSlot : _rightPanelSlot;
    _dragStartPrimaryWidth = _owner->slotWidth(targetSlot);
    return true;
}

bool SplitterHandle::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }

    if (_dragging) {
        _dragging = false;
        return true;
    }

    return false;
}

void SplitterHandle::onMouseLeave() {
    if (!_dragging) {
        _hover = false;
    }
}

UiCursorHint SplitterHandle::getCursorHint() const {
    if (_dragging || _hover) {
        return UiCursorHint::SizeHorizontal;
    }
    return UiCursorHint::Default;
}

void SplitterHandle::applyDrag(float mouseWorldX) {
    if (_owner == nullptr) {
        return;
    }

    _owner->applySplitterDrag(_leftPanelSlot, _rightPanelSlot, mouseWorldX,
                              _dragStartMouseX, _dragStartPrimaryWidth, _adjustLeft);
}

void SplitterHandle::onRender(IRenderBackend& renderer) {
    const math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    const bool active = _hover || _dragging;
    const math::FVector4 color =
        active ? math::FVector4(0.45f, 0.55f, 0.75f, 1.0f)
               : math::FVector4(0.28f, 0.30f, 0.34f, 1.0f);
    renderer.drawRect(bounds, color);
}

} // namespace ayt::ui
