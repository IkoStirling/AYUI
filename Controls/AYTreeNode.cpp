#include "AYUI/TreeNode.h"
#include "AYUI/IRenderBackend.h"
#include <algorithm>

namespace ayt::ui {

TreeNode::TreeNode() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    setLayoutPositionManaged(false);   // TreeView positions nodes itself
    setLayoutSizeManaged(false);
}

TreeNode::~TreeNode() = default;

void TreeNode::setExpanded(bool e) {
    if (_expanded == e) return;
    _expanded = e;
    if (_onExpandToggled) _onExpandToggled(_expanded);
    markBoundsDirty();
}

bool TreeNode::onMouseButtonUp(const UIMouseEvent& e) {
    if (!isEnabled() || e.mouseButton != 0) return false;
    const math::FRectangle b = getWorldBounds();
    if (!b.contains(e.mousePos)) return false;

    // Arrow column = first (depth + 1) * kIndentPx pixels.
    // For depth 0 the arrow sits at x ∈ [0, kIndentPx). For depth N it
    // sits at x ∈ [N*kIndentPx, N*kIndentPx + kArrowColPx). Anything
    // inside that band AND hasChildren==true → toggle expand + return
    // true (consume — do NOT toggle selection).
    if (_hasChildren) {
        const float arrowMinX = b.minX + static_cast<float>(_depth) * kIndentPx;
        const float arrowMaxX = arrowMinX + kArrowColPx;
        if (e.mousePos.x >= arrowMinX && e.mousePos.x < arrowMaxX) {
            setExpanded(!_expanded);
            return true;
        }
    }

    // Otherwise fall through to label region — route to TreeView via
    // _onClickByNode (parent owns selection state for single-select).
    if (_onClickByNode) {
        _onClickByNode(_index);
    }
    return true;
}

void TreeNode::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;

    // Selection band — full row, accent color when selected.
    if (_selected) {
        renderer.drawRect(b, math::FVector4(0.18f, 0.45f, 0.78f, 0.55f));
    } else if (isMouseOver() && isEnabled()) {
        renderer.drawRect(b, math::FVector4(0.30f, 0.30f, 0.32f, 0.5f));
    }

    // Compute prefix columns.
    const float arrowMinX = b.minX + static_cast<float>(_depth) * kIndentPx;
    const float iconMinX  = arrowMinX + kArrowColPx;
    const float labelMinX = iconMinX + kIconColPx;

    // Arrow glyph — ▶ when collapsed, ▼ when expanded, blank if leaf.
    if (_hasChildren) {
        const math::FRectangle arrowBounds(
            arrowMinX, b.minY, arrowMinX + kArrowColPx, b.maxY);
        const wchar_t* arrow = _expanded ? L"▼" : L"▶";
        const math::FVector4 arrowColor = isEnabled()
            ? math::FVector4(0.85f, 0.85f, 0.90f, 1.0f)
            : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        renderer.drawText(arrowBounds, std::wstring(arrow), 12, arrowColor);
    }

    // Icon glyph (if any).
    if (!_icon.empty()) {
        const math::FRectangle iconBounds(
            iconMinX, b.minY, iconMinX + kIconColPx, b.maxY);
        const math::FVector4 iconColor = isEnabled()
            ? math::FVector4(0.85f, 0.78f, 0.55f, 1.0f)
            : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        renderer.drawText(iconBounds, _icon, 14, iconColor);
    }

    // Label text.
    if (!_label.empty()) {
        const math::FRectangle labelBounds(
            labelMinX, b.minY, b.maxX, b.maxY);
        const math::FVector4 textColor = isEnabled()
            ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
            : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        renderer.drawText(labelBounds, _label, 14, textColor);
    }
}

Widget* createTreeNodeWidget() { return new TreeNode(); }

} // namespace ayt::ui