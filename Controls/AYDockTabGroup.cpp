#include "AYDockTabGroup.h"
#include "AYDockCard.h"
#include "AYUIManager.h"
#include "AYDockTrace.h"
#include "IAYRenderBackend.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

DockTabGroup::DockTabGroup() {
    setSize(math::FVector2(200.0f, 200.0f));
}

DockTabGroup::~DockTabGroup() {
    // Cards are children owned by the children tree (UI-OWN-1: the
    // owning DockArea tears the whole subtree down via
    // destroyWidgetTree). Only the bookkeeping index needs clearing.
    _tabs.clear();
}

void DockTabGroup::addTab(DockCard* card) {
    if (card == nullptr || containsCard(card)) {
        return;
    }
    // addChild auto-detaches from any previous parent.
    addChild(card);
    _tabs.push_back(card);
    _activeIndex = _tabs.size() - 1;
    // Only the active tab is visible. A fresh add activates the new
    // card (VS Code convention — the just-dropped panel is shown).
    card->setVisible(true);
    for (size_t i = 0; i < _tabs.size() - 1; ++i) {
        if (_tabs[i] != nullptr) {
            _tabs[i]->setVisible(false);
        }
    }
}

bool DockTabGroup::removeTab(DockCard* card) {
    if (card == nullptr) {
        return false;
    }
    for (size_t i = 0; i < _tabs.size(); ++i) {
        if (_tabs[i] != card) {
            continue;
        }
        _tabs.erase(_tabs.begin() + static_cast<std::ptrdiff_t>(i));
        // Detach-only — the caller (DockArea) decides whether the card
        // is destroyed, floated, or re-parented elsewhere.
        removeChild(card);
        if (_tabs.empty()) {
            _activeIndex = 0;
        } else {
            if (i < _activeIndex) {
                --_activeIndex;   // a card before the active one left
            } else if (i == _activeIndex) {
                _activeIndex = std::min(_activeIndex, _tabs.size() - 1);
            }
            _tabs[_activeIndex]->setVisible(true);
        }
        _hoveredTab = -1;
        _hoveredClose = -1;
        return true;
    }
    return false;
}

bool DockTabGroup::containsCard(const DockCard* card) const {
    if (card == nullptr) {
        return false;
    }
    for (const DockCard* c : _tabs) {
        if (c == card) {
            return true;
        }
    }
    return false;
}

DockCard* DockTabGroup::getTab(size_t index) const {
    if (index >= _tabs.size()) {
        return nullptr;
    }
    return _tabs[index];
}

void DockTabGroup::activateTab(size_t index) {
    if (index >= _tabs.size() || index == _activeIndex) {
        return;
    }
    if (_tabs[_activeIndex] != nullptr) {
        _tabs[_activeIndex]->setVisible(false);
    }
    _activeIndex = index;
    if (_tabs[_activeIndex] != nullptr) {
        _tabs[_activeIndex]->setVisible(true);
    }
}

void DockTabGroup::activateTabById(const std::string& id) {
    for (size_t i = 0; i < _tabs.size(); ++i) {
        if (_tabs[i] != nullptr && _tabs[i]->getId() == id) {
            activateTab(i);
            return;
        }
    }
}

DockCard* DockTabGroup::getActiveTab() const {
    if (_tabs.empty()) {
        return nullptr;
    }
    return _tabs[_activeIndex];
}

std::string DockTabGroup::getActiveTabId() const {
    const DockCard* card = getActiveTab();
    return card != nullptr ? card->getId() : std::string();
}

void DockTabGroup::performLayout() {
    const math::FVector2 sz = getSize();
    if (_tabs.empty()) {
        return;
    }
    DockCard* active = getActiveTab();
    if (active == nullptr) {
        return;
    }
    if (_tabs.size() == 1) {
        // Single card: full-bleed — its own title bar is the only
        // chrome (legacy 5-slot geometry; test_center_card_fills_
        // slot_height pins the exact world bounds).
        active->setPosition(math::FVector2(0.0f, 0.0f));
        active->setSize(sz);
    } else {
        // Tab strip covers the active card's own title bar: slide the
        // card up by (stripH - cardHeaderHeight) so the strip region
        // (0..stripH) exactly overlays the title bar and the card body
        // starts below the strip. Render order (strip paints last) and
        // hitTest (strip claims hits) keep the overlap invisible.
        const float headerH = std::max(0.0f, active->getHeaderHeight());
        const float offsetY = kTabStripHeight - headerH;
        active->setPosition(math::FVector2(0.0f, offsetY));
        active->setSize(math::FVector2(sz.x, std::max(0.0f, sz.y - offsetY)));
    }
    compoundDescendLayout(this);
}

math::FRectangle DockTabGroup::stripRectWorld() const {
    const math::FRectangle b = getWorldBounds();
    return math::FRectangle(b.minX, b.minY, b.maxX, b.minY + kTabStripHeight);
}

math::FRectangle DockTabGroup::tabRectWorld(size_t index) const {
    const math::FRectangle strip = stripRectWorld();
    const size_t n = std::max<size_t>(1, _tabs.size());
    const float w = (strip.maxX - strip.minX) / static_cast<float>(n);
    return math::FRectangle(strip.minX + w * static_cast<float>(index),
                            strip.minY,
                            strip.minX + w * static_cast<float>(index + 1),
                            strip.maxY);
}

math::FRectangle DockTabGroup::closeRectWorld(size_t index) const {
    const math::FRectangle tab = tabRectWorld(index);
    constexpr float kCloseHalfWidth = 8.0f;
    return math::FRectangle(tab.maxX - kCloseHalfWidth, tab.minY,
                            tab.maxX, tab.maxY);
}

int DockTabGroup::tabIndexAt(const math::FVector2& worldPos) const {
    if (_tabs.empty() || !stripRectWorld().contains(worldPos)) {
        return -1;
    }
    for (size_t i = 0; i < _tabs.size(); ++i) {
        if (tabRectWorld(i).contains(worldPos)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Widget* DockTabGroup::hitTest(const math::FVector2& worldPos) {
    if (!isVisible()) {
        return nullptr;
    }
    const math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) {
        return nullptr;
    }
    if (_tabs.size() >= 2 && stripRectWorld().contains(worldPos)) {
        // The tab strip claims hits before the active card's title bar
        // (which the strip covers visually and interactively).
        return this;
    }
    return compoundDescendHitTest(this, worldPos);
}

bool DockTabGroup::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }
    if (_tabs.size() < 2) {
        // No strip — the single card's own title-bar path runs.
        return false;
    }
    if (!stripRectWorld().contains(e.mousePos)) {
        return false;
    }
    const int idx = tabIndexAt(e.mousePos);
    if (idx < 0) {
        return false;
    }
    DockCard* tab = _tabs[static_cast<size_t>(idx)];
    if (tab == nullptr) {
        return false;
    }

    // Close x fires before activate/drag (mirrors DockCard's
    // close-first rule for its own title bar).
    if (closeRectWorld(static_cast<size_t>(idx)).contains(e.mousePos)) {
        dockTrace("[dock] tabGroup close x card=%s leaf=%s\n",
                  tab->getId().c_str(), _leafId.c_str());
        if (_onCloseTab) {
            _onCloseTab(tab);
        }
        return true;
    }

    if (static_cast<size_t>(idx) == _activeIndex) {
        // Pressing the ACTIVE tab tears the card off — same G12 path
        // as the card's own title bar (K-INV-D3-2 gate: non-floatable
        // cards never start a session).
        if (tab->isFloatable()) {
            if (UIManager* ui = UIManager::tryGet()) {
                if (ui->beginDrag(tab)) {
                    dockTrace("[dock] tabGroup beginDrag active card=%s leaf=%s\n",
                              tab->getId().c_str(), _leafId.c_str());
                }
            }
        }
        return true;
    }

    activateTab(static_cast<size_t>(idx));
    return true;
}

bool DockTabGroup::onMouseMove(const UIMouseEvent& e) {
    _hoveredTab = -1;
    _hoveredClose = -1;
    if (_tabs.size() >= 2 && stripRectWorld().contains(e.mousePos)) {
        const int idx = tabIndexAt(e.mousePos);
        if (idx >= 0) {
            _hoveredTab = idx;
            if (closeRectWorld(static_cast<size_t>(idx)).contains(e.mousePos)) {
                _hoveredClose = idx;
            }
        }
    }
    // No-op; return false so UIManager keeps tracking hover propagation.
    return false;
}

void DockTabGroup::onMouseLeave() {
    _hoveredTab = -1;
    _hoveredClose = -1;
    Widget::onMouseLeave();
}

UiCursorHint DockTabGroup::getCursorHint() const {
    if (_hoveredClose >= 0) {
        return UiCursorHint::Hand;
    }
    return UiCursorHint::Default;
}

void DockTabGroup::render(IRenderBackend& renderer) {
    if (!isVisible()) {
        return;
    }
    // Cards first; the strip paints AFTER so it covers the active
    // card's own title bar (slid up beneath it).
    renderChildren(renderer);
    if (_tabs.size() >= 2) {
        paintStrip(renderer);
    }
}

void DockTabGroup::paintStrip(IRenderBackend& renderer) {
    const math::FRectangle strip = stripRectWorld();
    if (strip.maxX <= strip.minX || strip.maxY <= strip.minY) {
        return;
    }
    renderer.drawRect(strip, math::FVector4(0.14f, 0.14f, 0.16f, 1.0f));
    renderer.drawBorderRect(strip, math::FVector4(0.08f, 0.08f, 0.10f, 1.0f),
                            1.0f, 0.0f);

    for (size_t i = 0; i < _tabs.size(); ++i) {
        DockCard* tab = _tabs[i];
        if (tab == nullptr) {
            continue;
        }
        const math::FRectangle r = tabRectWorld(i);
        const math::FRectangle close = closeRectWorld(i);
        const bool isActive = (i == _activeIndex);
        const bool hovered = (static_cast<int>(i) == _hoveredTab);

        if (isActive) {
            renderer.drawRect(r, math::FVector4(0.22f, 0.24f, 0.30f, 1.0f));
        } else if (hovered) {
            renderer.drawRect(r, math::FVector4(0.18f, 0.18f, 0.22f, 1.0f));
        }

        // Title text (leave room for the close x).
        const math::FRectangle textRect(
            r.minX + 6.0f, r.minY,
            close.maxX - 2.0f, r.maxY);
        if (!tab->getTitle().empty()) {
            renderer.drawText(textRect, tab->getTitle(), 12,
                              math::FVector4(0.90f, 0.90f, 0.92f, 1.0f));
        }

        // Close x (hover highlight + glyph).
        if (static_cast<int>(i) == _hoveredClose) {
            renderer.drawRect(close, math::FVector4(0.55f, 0.18f, 0.18f, 1.0f));
        }
        renderer.drawText(close, L"x", 12,
                          math::FVector4(0.90f, 0.90f, 0.92f, 1.0f));
    }
}

} // namespace ayt::ui
