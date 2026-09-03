#include "AYUI/TreeNode.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/SvgIcon.h"
#include <algorithm>

namespace ayt::ui {

namespace {

const SvgDocument::Ptr& collapsedDisclosureIcon() {
    static const SvgDocument::Ptr icon = SvgDocument::parse(
        R"(<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M6 3.5L10.5 8L6 12.5"/></svg>)");
    return icon;
}

const SvgDocument::Ptr& expandedDisclosureIcon() {
    static const SvgDocument::Ptr icon = SvgDocument::parse(
        R"(<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3.5 6L8 10.5L12.5 6"/></svg>)");
    return icon;
}

} // namespace

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
    markDirty();
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

    // Disclosure icon. Do not use font glyphs here: the editor's compact
    // UI font does not guarantee U+25B6/U+25BC and rendered a tofu square on
    // Windows when the fallback font was unavailable.
    if (_hasChildren) {
        const math::FRectangle arrowBounds(
            arrowMinX + 3.0f, b.minY + 3.0f,
            arrowMinX + kArrowColPx - 3.0f, b.maxY - 3.0f);
        const math::FVector4 arrowColor = isEnabled()
            ? math::FVector4(0.85f, 0.85f, 0.90f, 1.0f)
            : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        const SvgDocument::Ptr& icon = _expanded
            ? expandedDisclosureIcon() : collapsedDisclosureIcon();
        if (icon != nullptr) icon->draw(renderer, arrowBounds, arrowColor);
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
