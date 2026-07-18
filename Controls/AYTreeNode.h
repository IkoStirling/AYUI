#pragma once

// =============================================================================
// C-12 TreeNode: a single row inside a TreeView.
// =============================================================================
//
// Architecture (v1):
//   TreeNode : SelectableWidget
//     - _depth: int                 // indent depth (0 = root level)
//     - _hasChildren: bool          // whether this node shows an expand arrow
//     - _expanded: bool             // arrow state (visible/hidden children)
//     - _icon: std::wstring         // optional glyph (📁 / 📄 / empty)
//     - _label: std::wstring        // display text
//     - _onExpandToggled: callback fired when the user clicks the arrow
//
// Click routing (mirrors MenuItem + ListView::Row pattern):
//   - Click in arrow column  → toggle _expanded, fire _onExpandToggled,
//                              DO NOT toggle _selected (consumed)
//   - Click in label column  → forward to ListView / TreeView via
//                              _onClickByNode (parent owns selection)
//   - Hover/press state machine inherited from InteractiveWidget —
//     no need to reimplement.
//
// v1 design notes:
//   - Row height fixed at kDefaultHeight (16 px) — matches ListView row.
//   - Indent per depth is kIndentPx (16 px). Arrow column is
//     kArrowColPx (16 px). Icon column is kIconColPx (18 px). Total
//     prefix width = depth * kIndentPx + kArrowColPx + kIconColPx.
//   - TreeView wires _onClickByNode to its handleNodeClick — the parent
//     toggles _selected on the rows itself (single-select model).
//   - We deliberately do NOT override SelectableWidget::handleClick —
//     TreeView gets a clean callback and doesn't need the base's
//     _onActivated path.

#include "AYSelectableWidget.h"
#include <functional>
#include <string>

namespace ayt::ui {

class TreeNode : public SelectableWidget {
public:
    static constexpr float kDefaultWidth  = 240.0f;
    static constexpr float kDefaultHeight = 16.0f;
    static constexpr float kIndentPx      = 16.0f;
    static constexpr float kArrowColPx    = 16.0f;
    static constexpr float kIconColPx     = 18.0f;

    TreeNode();
    ~TreeNode() override;

    // Hierarchy data
    void setDepth(int d) { _depth = d; markBoundsDirty(); }
    int  getDepth() const { return _depth; }

    void setHasChildren(bool h) { _hasChildren = h; markBoundsDirty(); }
    bool hasChildren() const { return _hasChildren; }

    void setExpanded(bool e);     // fires _onExpandToggled if changed
    bool isExpanded() const { return _expanded; }

    void setIcon(const std::wstring& glyph) { _icon = glyph; markBoundsDirty(); }
    const std::wstring& getIcon() const { return _icon; }

    void setLabel(const std::wstring& s) { _label = s; markBoundsDirty(); }
    const std::wstring& getLabel() const { return _label; }

    void setOnExpandToggled(std::function<void(bool)> cb) {
        _onExpandToggled = std::move(cb);
    }

    // Parent's routing hook (mirrors ListView::Row::_onClickByRow).
    // Fires with this node's flat index whenever the label region is
    // clicked (i.e. NOT the arrow region).
    void setOnClickByNode(std::function<void(int)> cb) {
        _onClickByNode = std::move(cb);
    }

    // Click routing: arrow vs label.
    bool onMouseButtonUp(const UIMouseEvent& e) override;

    void onRender(IRenderBackend& renderer) override;

private:
    int  _depth = 0;
    bool _hasChildren = false;
    bool _expanded = false;
    std::wstring _icon;
    std::wstring _label;
    std::function<void(bool)> _onExpandToggled;
    std::function<void(int)>  _onClickByNode;

    // Index assigned by TreeView — required by _onClickByNode callback.
    int _index = -1;

    friend class TreeView;
};

Widget* createTreeNodeWidget();

} // namespace ayt::ui