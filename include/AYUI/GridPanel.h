#pragma once

#include "AYUI/Widget.h"

namespace ayt::ui {

// Fixed/Stretch grid layout. Cells keep non-owning Widget pointers and store
// row/column span plus per-cell alignment; a spanning Widget is indexed by its
// top-left cell. Fixed definitions consume exact DIP sizes and Stretch
// definitions divide the remainder by weight.
//
// Algorithm (one layout pass, two phases):
//   Phase 1 (measure): sum the Fixed sizes, split the leftover across
//                       Stretch rows/columns by weight.
//   Phase 2 (arrange): walk row × col in order; for each cell with a
//                       widget, compute the cell rect (spanning rows/cols
//                       merged), then apply alignment + spacing + padding
//                       to position + size the child.
//
// Current limits are explicit: no Auto/preferred-size policy, automatic row
// growth, grid splitters or cell-specific z-order. Overlap follows normal
// child painter order.

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
