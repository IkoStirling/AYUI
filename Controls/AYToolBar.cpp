#include "AYToolBar.h"
#include "AYToolBarSeparator.h"
#include "AYButton.h"
#include "AYScrollView.h"
#include "AYScrollBar.h"
#include "AYSeparator.h"
#include "IAYRenderBackend.h"
#include <algorithm>

namespace ayt::ui {

ToolBar::ToolBar() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    // G3 — wrap pattern (matches TabStrip Q12 plan). We don't eagerly
    // create the ScrollView here so a stack-allocated ToolBar that never
    // adds items pays no cost; first addButton / addSeparator triggers
    // ensureScrollWrap().
}

ToolBar::~ToolBar() {
    // G3 — ToolBar owns its ScrollView + _contentStrip. They are
    // attached via addChild (owning), so the base ~CompoundWidget path
    // would free them via destroyWidgetTree. But destroyWidgetTree uses
    // isExternallyOwned() to skip external children — for our internal
    // owned ScrollView/Strip we DO want them freed. Detach first and
    // delete deterministically so we don't rely on a base-class ordering
    // (mirrors ScrollView's own pattern of just nulling bar pointers —
    // we instead own a heap ScrollView that must not leak).
    //
    // Note: ~Widget() now calls detachFromParent() FIRST (commit 3844a95),
    // so removing the children here is safe even mid-unwind.
    clearOwnedItems();
    if (_contentStrip != nullptr) {
        if (_contentStrip->getParent() != nullptr) {
            _contentStrip->getParent()->removeChild(_contentStrip);
        }
        delete _contentStrip;
        _contentStrip = nullptr;
    }
    if (_scrollView != nullptr) {
        if (_scrollView->getParent() != nullptr) {
            _scrollView->getParent()->removeChild(_scrollView);
        }
        delete _scrollView;
        _scrollView = nullptr;
    }
    _items.clear();
}

ScrollBar* ToolBar::getHorizontalScrollBar() const {
    if (_scrollView == nullptr) return nullptr;
    return _scrollView->getHorizontalScrollBar();
}

void ToolBar::ensureScrollWrap() {
    if (_scrollView != nullptr) return;
    _scrollView = new ScrollView();
    _scrollView->setVerticalScrollBarEnabled(false);
    _scrollView->setHorizontalScrollBarEnabled(true);
    addChild(_scrollView);   // ToolBar owns delete via ~ToolBar above
    _contentStrip = new Widget();
    _contentStrip->setSize(math::FVector2(0.0f, getHeight()));
    _scrollView->setContent(_contentStrip);
}

class Button* ToolBar::addButton(const std::wstring& text,
                                  std::function<void()> onClick) {
    ensureScrollWrap();
    auto* btn = new Button();
    btn->setText(text);
    btn->setSize(math::FVector2(80.0f, kDefaultHeight - 2.0f * kPadding));
    btn->setLayoutPositionManaged(false);
    btn->setLayoutSizeManaged(false);
    // Add as owning child of _contentStrip — _contentStrip owns delete.
    // ToolBar::clearOwnedItems() walks _contentStrip's children and
    // deletes them; clearItems() also calls this.
    _contentStrip->addChild(btn);
    if (onClick) {
        btn->setOnClicked(onClick);
    }
    _items.push_back({ItemRecord::Kind::Button, btn});
    layoutItems();
    return btn;
}

void ToolBar::addSeparator() {
    ensureScrollWrap();
    // G7 — use the dedicated ToolBarSeparator so it has the toolbar
    // palette pre-applied (vs a raw Separator which would render with
    // the neutral gray). The factory-registered type also lets hosts
    // walk the toolbar tree by widget type.
    auto* sep = new ToolBarSeparator();
    _contentStrip->addChild(sep);
    _items.push_back({ItemRecord::Kind::Separator, sep});
    layoutItems();
}

Widget* ToolBar::getItem(size_t index) const {
    if (index >= _items.size()) return nullptr;
    return _items[index].widget;
}

void ToolBar::clearItems() {
    clearOwnedItems();
    _items.clear();
    layoutItems();
}

void ToolBar::clearOwnedItems() {
    if (_contentStrip == nullptr) return;
    // Snapshot children before deleting — CompoundWidget owns by default.
    // We delete each Button/Separator and remove them from _contentStrip
    // so re-adding more items doesn't double-free.
    std::vector<Widget*> kids = _contentStrip->getChildren();
    for (auto* kid : kids) {
        if (kid != nullptr) {
            // removeChild first so _contentStrip's _children vector is
            // consistent; then delete. ~Widget() calls detachFromParent
            // FIRST (commit 3844a95), so this is R3-safe.
            _contentStrip->removeChild(kid);
            delete kid;
        }
    }
}

void ToolBar::layoutItems() {
    if (_contentStrip == nullptr || _scrollView == nullptr) return;
    // Lay items out horizontally inside _contentStrip.
    const float h = kDefaultHeight - 2.0f * kPadding;
    float x = kPadding;
    const float y = kPadding;
    for (size_t i = 0; i < _items.size(); ++i) {
        Widget* w = _items[i].widget;
        if (w == nullptr) continue;
        const float ww = w->getSize().x;
        if (_items[i].kind == ItemRecord::Kind::Separator) {
            // Separators are slim + tall.
            w->setPosition(math::FVector2(x + 2.0f, y));
            w->setSize(math::FVector2(2.0f, h));
            x += 4.0f + kItemSpacing;
        } else {
            w->setPosition(math::FVector2(x, y));
            w->setSize(math::FVector2(ww, h));
            x += ww + kItemSpacing;
        }
    }
    const float totalW = x + kPadding;
    // Resize content strip to its preferred width — ScrollView's
    // contentSize trigger uses this to compute the horizontal scrollbar.
    _contentStrip->setSize(math::FVector2(totalW, getHeight()));
    _scrollView->setContentSize(math::FVector2(totalW, getHeight()));
    // NOTE: hbar visibility derivation is deferred to performLayout —
    // ScrollView::performLayout lazily creates the hbar via
    // ensureBarsCreated(), so it does not exist yet here. Setting
    // visibility on a non-existent pointer would be a NOP; doing it
    // after CompoundWidget::performLayout guarantees the hbar exists.
}

void ToolBar::performLayout() {
    if (_scrollView == nullptr) {
        // No items yet — just a base layout pass.
        CompoundWidget::performLayout();
        return;
    }
    // Size the ScrollView to fill the ToolBar's bounds. The host
    // controls ToolBar width (and can override height); we use the host
    // height here so the content strip has the same vertical headroom.
    _scrollView->setSize(math::FVector2(getWidth(), getHeight()));
    _scrollView->setPosition(math::FVector2(0.0f, 0.0f));
    // Re-layout items in case the host resized the ToolBar. This also
    // updates _contentStrip size + ScrollView contentSize.
    layoutItems();
    // Default CompoundWidget cascade (recurse into children). This
    // triggers ScrollView::performLayout which lazily creates the
    // horizontal ScrollBar via ensureBarsCreated().
    CompoundWidget::performLayout();
    // G3 — derive hbar visibility AFTER ScrollView::performLayout so
    // the hbar is guaranteed to exist. ScrollView itself never toggles
    // ScrollBar.isVisible when content ≤ viewport; we must do it here
    // for the "content fits → no chrome" polish (line 192/303 of
    // AYWidget.cpp honor setVisible for hitTest + render).
    if (ScrollBar* hbar = _scrollView->getHorizontalScrollBar()) {
        const float viewW = getWidth();
        const float contentW = _contentStrip->getSize().x;
        const bool needsHbar = contentW > viewW + 1e-3f;
        hbar->setVisible(needsHbar);
    }
}

void ToolBar::onRender(IRenderBackend& renderer) {
    // G3 — render strategy: ScrollView draws its own background + border
    // + content strip + scrollbar (via the Widget::render cascade —
    // _scrollView is our child, and Widget::render() runs onRender THEN
    // renderChildren, so the base cascade handles the ScrollView for
    // us). We paint a 1px bottom rule for the classic toolbar visual;
    // it sits on top of ScrollView's bottom border for a single line
    // of accent.
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;
    renderer.drawRect(
        math::FRectangle(b.minX, b.maxY - 1.0f, b.maxX, b.maxY),
        math::FVector4(0.36f, 0.36f, 0.40f, 1.0f));
}

Widget* createToolBarWidget() { return new ToolBar(); }

} // namespace ayt::ui