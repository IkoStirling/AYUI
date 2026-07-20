#include "AYStatusBar.h"
#include "AYTextLabel.h"
#include "IAYRenderBackend.h"
#include <algorithm>

namespace ayt::ui {

StatusBar::StatusBar() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
}

StatusBar::~StatusBar() {
    // G8 — StatusBar does NOT delete panel widgets in its dtor. Cleanup
    // is the host's responsibility, EITHER via:
    //   1. clearPanels() — explicit teardown before the StatusBar goes
    //      out of scope.
    //   2. destroyWidgetTree(this) — recursive delete of the whole
    //      tree (panels are children of the StatusBar via addChild).
    // Mixing both paths causes double-free. v1 used this same pattern
    // (~StatusBar did nothing); we keep it for backwards compatibility.
    // Panels added via addPanel(Widget*) are conceptually "owned" by
    // StatusBar in that nobody else should delete them, but the dtor
    // itself does not free them — that's the caller's job.
    _panels.clear();
}

class Widget* StatusBar::addPanel(class Widget* w) {
    if (w == nullptr) return nullptr;
    // G8 — own the panel. Caller must not delete after this call.
    // Set the panel's layout-management OFF so performLayout controls
    // position/size (mirrors v1 TextLabel setup).
    w->setLayoutPositionManaged(false);
    w->setLayoutSizeManaged(false);
    addChild(w);
    _panels.push_back(w);
    performLayout();
    return w;
}

class TextLabel* StatusBar::addPanel(const std::wstring& text) {
    // Legacy convenience: wrap text in a TextLabel and own it.
    auto* lbl = new TextLabel();
    lbl->setText(text);
    // Height: stay at single-row default. Hosts wanting multi-row
    // panels call addPanel(custom Widget) with a pre-sized widget.
    lbl->setSize(math::FVector2(0.0f, kRowHeight));
    return static_cast<TextLabel*>(addPanel(static_cast<class Widget*>(lbl)));
}

class Widget* StatusBar::getPanelWidget(size_t index) const {
    if (index >= _panels.size()) return nullptr;
    return _panels[index];
}

class TextLabel* StatusBar::getPanel(size_t index) const {
    // Legacy text accessor: dynamic_cast. Returns nullptr if the panel
    // is not a TextLabel (e.g. a custom Widget added via addPanel(W*)).
    Widget* p = getPanelWidget(index);
    return dynamic_cast<TextLabel*>(p);
}

void StatusBar::setPanelText(size_t index, const std::wstring& text) {
    TextLabel* lbl = getPanel(index);
    if (lbl != nullptr) {
        lbl->setText(text);
    }
}

void StatusBar::clearPanels() {
    for (auto* p : _panels) {
        if (p == nullptr) continue;
        if (p->getParent() != nullptr) {
            p->getParent()->removeChild(p);
        }
        delete p;
    }
    _panels.clear();
    performLayout();
}

float StatusBar::computeDesiredHeight() const {
    // G8 — bar height tracks the tallest panel's preferred height.
    // Default fallback: kDefaultHeight so an empty bar stays at its
    // declared size (matches v1 behavior).
    float maxH = kDefaultHeight;
    for (const auto* p : _panels) {
        if (p == nullptr) continue;
        const float ph = p->getSize().y;
        if (ph > 0.0f && ph + 2.0f * kPadding > maxH) {
            maxH = ph + 2.0f * kPadding;
        }
    }
    return maxH;
}

void StatusBar::performLayout() {
    // G8 — adapt bar height to tallest panel. We mutate our own size
    // so render bounds grow with content. CompoundWidget::performLayout
    // runs the children cascade (still needed for child rendering).
    const float desiredH = computeDesiredHeight();
    if (getSize().y < desiredH) {
        setSize(math::FVector2(getSize().x, desiredH));
    }

    CompoundWidget::performLayout();

    // Lay out panels left-to-right. Each panel's height comes from its
    // own size (set by caller); width defaults to content size for
    // TextLabels or its declared size otherwise.
    float x = kPadding;
    const float y = kPadding;
    const float rowH = std::max(kRowHeight, desiredH - 2.0f * kPadding);
    const float barW = getSize().x;
    for (size_t i = 0; i < _panels.size(); ++i) {
        Widget* p = _panels[i];
        if (p == nullptr) continue;
        const auto ps = p->getSize();
        float w = ps.x;
        if (w <= 0.0f) {
            // Default width: 8px pad + 7px per char estimate.
            if (auto* lbl = dynamic_cast<TextLabel*>(p)) {
                w = static_cast<float>(lbl->getText().size()) * 7.0f + 8.0f;
            } else {
                w = 60.0f; // generic panel fallback
            }
        }
        const float h = (ps.y > 0.0f) ? ps.y : rowH;
        if (i == _panels.size() - 1) {
            // Stretch last panel to remaining width minus trailing pad
            // so multi-line right-grip reserves space consistently.
            w = std::max(w, barW - x - kPadding);
        }
        p->setPosition(math::FVector2(x, y));
        p->setSize(math::FVector2(w, h));
        x += w + kPanelSpacing;
    }
}

void StatusBar::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;
    // Light bar background.
    renderer.drawRect(b, math::FVector4(0.20f, 0.21f, 0.25f, 1.0f));
    // Top border.
    renderer.drawRect(
        math::FRectangle(b.minX, b.minY, b.maxX, b.minY + 1.0f),
        math::FVector4(0.36f, 0.36f, 0.40f, 1.0f));
}

Widget* createStatusBarWidget() { return new StatusBar(); }

} // namespace ayt::ui