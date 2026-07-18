#include "AYSplitterHandle.h"
#include "AYBox.h"
#include "AYIRenderBackend.h"

#include <cstdio>
#include <cstdlib>

namespace ayt::ui {

namespace {

bool splitterDebugEnabled()
{
    static int cached = -1;
    if (cached < 0) {
        const char* env = std::getenv("AY_UI_SPLITTER_DEBUG");
        cached = (env != nullptr && env[0] != '\0' && env[0] != '0') ? 1 : 0;
        if (cached) {
            std::fprintf(stderr,
                "[SplitterDebug] enabled (set AY_UI_SPLITTER_DEBUG=0 to disable)\n");
        }
    }
    return cached != 0;
}

const char* widgetLabel(const Widget* w)
{
    if (w == nullptr) {
        return "null";
    }
    if (!w->getId().empty()) {
        return w->getId().c_str();
    }
    if (w->isSplitterHandle()) {
        return "<SplitterHandle>";
    }
    return "<Widget>";
}

} // namespace

SplitterHandle::SplitterHandle() {
    setSize(math::FVector2(kDefaultWidth, 100.0f));
}

void SplitterHandle::bindPanels(HBox* owner, int leftPanelSlot, int rightPanelSlot) {
    _owner = owner;
    _leftPanelSlot = leftPanelSlot;
    _rightPanelSlot = rightPanelSlot;
}

math::FRectangle SplitterHandle::interactionBand() const {
    math::FRectangle band = getWorldBounds();
    // If layout accidentally stretched us to a fill slot, only the leading
    // kDefaultWidth strip is the real handle — otherwise "move away" still
    // lands inside the fat band and leave never fires.
    if (band.maxX - band.minX > kDefaultWidth + 0.5f) {
        band.maxX = band.minX + kDefaultWidth;
    }
    return band;
}

Widget* SplitterHandle::hitTest(const math::FVector2& worldPos) {
    if (!_visible) {
        return nullptr;
    }
    return interactionBand().contains(worldPos) ? this : nullptr;
}

bool SplitterHandle::isRevealed() const {
    if (_dragging) {
        return true;
    }
    // Both flags required — a stale _hoverElapsed after leave must never
    // keep the accent lit on its own.
    if (_hover && _hoverElapsed >= kHoverRevealDelay) {
        return true;
    }
    return false;
}

bool SplitterHandle::onMouseMove(const UIMouseEvent& e) {
    // Capture / drag delivers moves even when the cursor is outside the
    // band. Only arm hover when the pointer is actually inside.
    const math::FRectangle band = interactionBand();
    const math::FRectangle world = getWorldBounds();
    const bool inside = band.contains(e.mousePos);
    const bool wasHover = _hover;

    if (!inside) {
        clearHoverReveal();
        if (splitterDebugEnabled() && wasHover) {
            std::fprintf(stderr,
                "[SplitterDebug] %s onMouseMove OUTSIDE -> clearHover "
                "mouse=(%.1f,%.1f) band=[%.1f,%.1f)x[%.1f,%.1f) "
                "worldW=%.1f (clamped=%d)\n",
                widgetLabel(this), e.mousePos.x, e.mousePos.y,
                band.minX, band.maxX, band.minY, band.maxY,
                world.maxX - world.minX,
                (world.maxX - world.minX > kDefaultWidth + 0.5f) ? 1 : 0);
        }
        if (_dragging) {
            applyDrag(e.mousePos.x);
            return true;
        }
        return false;
    }

    if (!_hover) {
        // Re-entering: restart the delay so quick in/out doesn't accumulate.
        _hoverElapsed = 0.0f;
        if (splitterDebugEnabled()) {
            std::fprintf(stderr,
                "[SplitterDebug] %s HOVER ENTER mouse=(%.1f,%.1f) "
                "bandW=%.1f worldW=%.1f\n",
                widgetLabel(this), e.mousePos.x, e.mousePos.y,
                band.maxX - band.minX, world.maxX - world.minX);
        }
    }
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
    if (splitterDebugEnabled()) {
        std::fprintf(stderr,
            "[SplitterDebug] %s BUTTON DOWN -> dragging=1 mouse=(%.1f,%.1f)\n",
            widgetLabel(this), e.mousePos.x, e.mousePos.y);
    }
    return true;
}

bool SplitterHandle::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }

    if (_dragging) {
        if (splitterDebugEnabled()) {
            std::fprintf(stderr,
                "[SplitterDebug] %s BUTTON UP -> endDrag mouse=(%.1f,%.1f)\n",
                widgetLabel(this), e.mousePos.x, e.mousePos.y);
        }
        endDrag();
        return true;
    }

    return false;
}

void SplitterHandle::clearHoverReveal() {
    const bool had = _hover || _hoverElapsed >= 0.0f;
    _hover = false;
    _hoverElapsed = -1.0f;
    if (splitterDebugEnabled() && had) {
        // If revealed stays 1 here, `_dragging` is still true — that is
        // NOT a hover bug; isRevealed() treats drag as always-on.
        std::fprintf(stderr,
            "[SplitterDebug] %s clearHoverReveal hover=0 elapsed=-1 "
            "dragging=%d revealed=%d\n",
            widgetLabel(this), _dragging ? 1 : 0, isRevealed() ? 1 : 0);
    }
}

void SplitterHandle::endDrag() {
    if (!_dragging && !_hover && _hoverElapsed < 0.0f) {
        return;
    }
    if (splitterDebugEnabled()) {
        std::fprintf(stderr,
            "[SplitterDebug] %s endDrag (was dragging=%d hover=%d)\n",
            widgetLabel(this), _dragging ? 1 : 0, _hover ? 1 : 0);
    }
    _dragging = false;
    clearHoverReveal();
}

void SplitterHandle::onMouseLeave() {
    if (splitterDebugEnabled()) {
        std::fprintf(stderr,
            "[SplitterDebug] %s onMouseLeave (was hover=%d elapsed=%.3f "
            "dragging=%d revealed=%d)\n",
            widgetLabel(this), _hover ? 1 : 0, _hoverElapsed,
            _dragging ? 1 : 0, isRevealed() ? 1 : 0);
    }
    clearHoverReveal();
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

void SplitterHandle::tick(float dt) {
    // Self-heal: if hover was cleared, never keep a positive elapsed that
    // could be misread by a stale isRevealed caller.
    if (!_hover) {
        _hoverElapsed = -1.0f;
        return;
    }
    if (!_dragging && _hoverElapsed >= 0.0f) {
        const bool wasRevealed = isRevealed();
        _hoverElapsed += dt;
        if (splitterDebugEnabled() && !wasRevealed && isRevealed()) {
            std::fprintf(stderr,
                "[SplitterDebug] %s REVEAL threshold crossed elapsed=%.3f\n",
                widgetLabel(this), _hoverElapsed);
        }
    }
}

void SplitterHandle::onRender(IRenderBackend& renderer) {
    const math::FRectangle bounds = interactionBand();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    if (!isRevealed()) {
        return;
    }

    renderer.drawRect(bounds, math::FVector4(0.40f, 0.48f, 0.62f, 1.0f));

    const float cx = (bounds.minX + bounds.maxX) * 0.5f;
    const float grabHalfWidth = 1.0f;
    const float grabInsetTop = 4.0f;
    const float grabInsetBottom = 4.0f;
    if (bounds.maxY - bounds.minY > grabInsetTop + grabInsetBottom) {
        renderer.drawRect(
            math::FRectangle(cx - grabHalfWidth,
                             bounds.minY + grabInsetTop,
                             cx + grabHalfWidth,
                             bounds.maxY - grabInsetBottom),
            math::FVector4(0.85f, 0.88f, 0.92f, 0.9f));
    }
}

} // namespace ayt::ui
