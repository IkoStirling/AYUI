#include "AYStatusBar.h"
#include "AYTextLabel.h"
#include "IAYRenderBackend.h"
#include <algorithm>

namespace ayt::ui {

StatusBar::StatusBar() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
}

StatusBar::~StatusBar() {
    _panels.clear();
}

class TextLabel* StatusBar::addPanel(const std::wstring& text) {
    auto* lbl = new TextLabel();
    lbl->setText(text);
    lbl->setLayoutPositionManaged(false);
    lbl->setLayoutSizeManaged(false);
    addChild(lbl);
    _panels.push_back(lbl);
    performLayout();
    return lbl;
}

class TextLabel* StatusBar::getPanel(size_t index) const {
    if (index >= _panels.size()) return nullptr;
    return _panels[index];
}

void StatusBar::setPanelText(size_t index, const std::wstring& text) {
    if (index >= _panels.size()) return;
    _panels[index]->setText(text);
}

void StatusBar::clearPanels() {
    for (auto* p : _panels) {
        if (p != nullptr) {
            removeChild(p);
            delete p;
        }
    }
    _panels.clear();
}

void StatusBar::performLayout() {
    CompoundWidget::performLayout();
    float x = kPadding;
    const float y = kPadding;
    const float h = kDefaultHeight - 2.0f * kPadding;
    for (size_t i = 0; i < _panels.size(); ++i) {
        TextLabel* lbl = _panels[i];
        if (lbl == nullptr) continue;
        // Panels are sized by content + a small fixed pad; the last
        // panel may be told the leftover width so it right-aligns.
        const int charCount = static_cast<int>(lbl->getText().size());
        float w = static_cast<float>(charCount) * 7.0f + 8.0f;
        if (i == _panels.size() - 1) {
            // Stretch to remaining width minus trailing pad.
            w = std::max(w, getSize().x - x - kPadding);
        }
        lbl->setPosition(math::FVector2(x, y));
        lbl->setSize(math::FVector2(w, h));
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
