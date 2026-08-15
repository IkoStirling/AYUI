#pragma once

#include "AYUI/Widget.h"

namespace ayt::ui {

// =============================================================================
// C-8 GridPanel: a fixed-grid form layout container.
// =============================================================================
//
// Architecture (v1):
//   GridPanel (CompoundWidget)
//     └─ cells: one Widget* per (row, col) cell — slot is owned by reference
//        (NOT freed); widgets that span >1 cell are stored under their top-
//        left cell (rowSpan / colSpan).
//
//   Row/Column definitions live in `_rowDefs` / `_colDefs`. Each definition
//   has a SizePolicy + a value:
//     - Fixed(h)   → exactly `h` pixels.
//     - Stretch(w) → proportional share of remaining space; `w` is weight
//                    (default 1.0). All Stretch rows / columns share the
//                    leftover after Fixed.
//
//   v1 deliberately does NOT auto-measure widget preferred size — every
//   child fills its allocated cell. Per-cell horizontal / vertical
//   alignment lets hosts center / right-align / etc. inside the cell.
//
// Algorithm (one layout pass, two phases):
//   Phase 1 (measure): sum the Fixed sizes, split the leftover across
//                       Stretch rows/columns by weight.
//   Phase 2 (arrange): walk row × col in order; for each cell with a
//                       widget, compute the cell rect (spanning rows/cols
//                       merged), then apply alignment + spacing + padding
//                       to position + size the child.
//
// -----------------------------------------------------------------------------
// v1 design decisions + known limitations + v1.1 upgrade paths
// -----------------------------------------------------------------------------
//
// DECISION 1: Fixed-size grid, NOT auto-row/auto-col.
//   Why: simple, predictable, and the canonical use case (form layout:
//   3 columns × N rows, "label | input | button") does NOT need auto-row.
//   v1.1 upgrade: when a host calls addCell(row = current_row_count),
//   auto-grow rows. Today that throws via the bounds-check; add an
//   `addCellAuto(widget, col)` that appends to the next free row.
//
// DECISION 2: Two SizePolicies (Fixed + Stretch). NO Auto (no measure pass
// over widget preferred size).
//   Why: Auto requires every child widget to expose a preferredSize API
//   that does not yet exist. Adding it now would touch every leaf widget.
//   v1.1 upgrade: add `Widget::getPreferredSize() const` (default returns
//   current size) + a third policy `Auto` that picks the column's max
//   preferred size.
//
// DECISION 3: Cell holds a Widget* (no row/col metadata outside).
//   Why: spans are stored on the cell (rowSpan / colSpan); alignment too.
//   One allocation per cell keeps the data structure flat — important for
//   grids with hundreds of cells (inventory views). v1.1: add a flat
//   `_cellIndex` cache so the inner loop doesn't pay an O(N) lookup.
//
// DECISION 4: NO GridSplitter (resizable row/column borders).
//   v2+. v1.1 path: introduce `GridSplitter` analog of `SplitterHandle`,
//   bind to (row|col) index. Mirrors HBox's splitter pattern verbatim.
//
// DECISION 5: NO z-order rules beyond child add order.
//   Widgets that overlap (span >1) just paint in tree order — same as
//   VBox / HBox today. v1.1: add GridPanel::setRowZOrder / setCellZOrder.
// =============================================================================

class GridPanel : public CompoundWidget {
public:
    enum class SizePolicy {
        Fixed,    // exact pixel size (`value` = px)
        Stretch,  // proportional share of leftover (`value` = weight; 0 → 1)
    };

    enum class HAlign { Left, Center, Right, Fill };   // Fill = stretch to cell
    enum class VAlign { Top,  Middle, Bottom, Fill };  // Fill = stretch to cell

    struct CellInfo {
        Widget* widget = nullptr;
        int rowSpan = 1;
        int colSpan = 1;
        HAlign hAlign = HAlign::Fill;
        VAlign vAlign = VAlign::Fill;
    };

    struct RowDef {
        SizePolicy policy = SizePolicy::Stretch;
        float value = 1.0f;
    };

    struct ColDef {
        SizePolicy policy = SizePolicy::Stretch;
        float value = 1.0f;
    };

    GridPanel();
    ~GridPanel() override;

    // Grid sizing. Rows/cols default to Stretch weight 1.0. Calling
    // setRowCount(N) pads with the default policy.
    void setRowCount(int n);
    void setColumnCount(int n);
    int  getRowCount() const { return static_cast<int>(_rowDefs.size()); }
    int  getColumnCount() const { return static_cast<int>(_colDefs.size()); }

    void setRowDef(int row, const RowDef& def);
    void setColumnDef(int col, const ColDef& def);
    const RowDef& getRowDef(int row) const;
    const ColDef& getColumnDef(int col) const;

    // Cell management.
    void setCell(int row, int col, Widget* widget,
                 int rowSpan = 1, int colSpan = 1,
                 HAlign hAlign = HAlign::Fill,
                 VAlign vAlign = VAlign::Fill);
    // Code-review 2026-08-02 #16: setCell silently resets rowSpan /
    // colSpan / hAlign / vAlign to the values passed (defaults 1, 1,
    // Fill, Fill) when re-attaching a widget that was previously at a
    // different cell. Moving a wide-span widget to a new cell via
    // setCell(N, M, widget) without re-supplying the span will collapse
    // it back to 1×1 Fill — by design. Callers that want spans to
    // carry across a move must pass them explicitly on every call.
    void clearCell(int row, int col);
    Widget* getCell(int row, int col) const;
    const CellInfo* findCell(int row, int col) const;

    // Padding + spacing (cell-to-cell gap). Padding shrinks the inner grid
    // area; spacing is the gap between adjacent cells.
    void setPadding(float left, float top, float right, float bottom);
    const math::FVector4& getPadding() const { return _padding; }
    void setSpacing(float h, float v);
    float getHorizontalSpacing() const { return _spacingH; }
    float getVerticalSpacing() const { return _spacingV; }

    // Layout pass. Walks rowDefs / colDefs and positions every cell.
    void performLayout() override;

protected:
    void layoutChildren() override;

private:
    // Resolve a column's pixel width given the available content width.
    float resolveColWidth(int col, float availableWidth,
                          const std::vector<float>& fixedTotals,
                          float stretchTotalWeight) const;
    // Resolve a row's pixel height given the available content height.
    float resolveRowHeight(int row, float availableHeight,
                           const std::vector<float>& fixedTotals,
                           float stretchTotalWeight) const;

    std::vector<RowDef> _rowDefs;
    std::vector<ColDef> _colDefs;

    // Cells stored flat; key is (row * colCount + col). Cells without a
    // widget are still allocated (CellInfo with widget == nullptr) so
    // `getCell` returns a stable pointer.
    std::vector<CellInfo> _cells;

    math::FVector4 _padding{4.0f, 4.0f, 4.0f, 4.0f};
    float _spacingH = 4.0f;
    float _spacingV = 4.0f;
};

} // namespace ayt::ui