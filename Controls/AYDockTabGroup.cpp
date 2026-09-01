#include "AYUI/DockTabGroup.h"
#include "AYUI/DockCard.h"
#include "AYUI/UIManager.h"
#include "AYUI/DockTrace.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/SvgIcon.h"
#include "AYUI/TextMeasure.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

namespace {

constexpr int kDockTabFontSize = 12;

const SvgDocument::Ptr& closeIconDocument() {
    static const SvgDocument::Ptr icon = SvgDocument::parse(
        R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M18 6L6 18M6 6L18 18"/></svg>)");
    return icon;
}

std::wstring ellipsizeTabTitle(const std::wstring& title,
                               float availableWidth,
                               IRenderBackend& renderer) {
    if (title.empty() || availableWidth <= 0.0f) {
        return {};
    }
    if (measurePrefixWidth(title, title.size(), &renderer,
                           kDockTabFontSize) <= availableWidth) {
        return title;
    }

    const std::wstring ellipsis = L"\x2026";
    const float ellipsisWidth = measurePrefixWidth(
        ellipsis, ellipsis.size(), &renderer, kDockTabFontSize);
    if (ellipsisWidth > availableWidth) {
        return {};
    }

    size_t low = 0;
    size_t high = title.size();
    while (low < high) {
        const size_t mid = low + (high - low + 1) / 2;
        const float prefixWidth = measurePrefixWidth(
            title, mid, &renderer, kDockTabFontSize);
        if (prefixWidth + ellipsisWidth <= availableWidth) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return title.substr(0, low) + ellipsis;
}

} // namespace

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
        _armedClose = -1;
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
    // Visibility-only was not enough: inactive cards kept stale sizes from
    // before a join/split, so switching tabs showed a "hole" until the next
    // parent layout pass. Size the newly shown card immediately.
    markBoundsDirty();
    performLayout();
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
    math::FVector2 cardPos(0.0f, 0.0f);
    math::FVector2 cardSz = sz;
    // Every leaf has a real tab strip, including a leaf with one card. This
    // keeps the title at the editor tab width instead of stretching the
    // DockCard's legacy full-width header across the entire panel.
    //
    // The strip covers the active card's own title bar: slide the card up by
    // (stripH - cardHeaderHeight) so its body still begins directly below the
    // strip. Render order and hit testing keep the covered header invisible.
    const float headerH = std::max(0.0f, active->getHeaderHeight());
    const float offsetY = kTabStripHeight - headerH;
    cardPos = math::FVector2(0.0f, offsetY);
    cardSz = math::FVector2(sz.x, std::max(0.0f, sz.y - offsetY));
    // Size every tab (including hidden ones) so a later activateTab does
    // not reveal a card still holding pre-join geometry.
    for (DockCard* card : _tabs) {
        if (card == nullptr) {
            continue;
        }
        card->setPosition(cardPos);
        card->setSize(cardSz);
    }
    compoundDescendLayout(this);
}

math::FRectangle DockTabGroup::stripRectWorld() const {
    const math::FRectangle b = getWorldBounds();
    return math::FRectangle(b.minX, b.minY, b.maxX, b.minY + kTabStripHeight);
}

math::FRectangle DockTabGroup::getTabRectWorld(size_t index) const {
    const math::FRectangle strip = stripRectWorld();
    if (index >= _tabs.size() || _tabs.empty()) {
        return math::FRectangle();
    }
    const float stripWidth = std::max(0.0f, strip.maxX - strip.minX);
    const float preferredTotal =
        kPreferredTabWidth * static_cast<float>(_tabs.size());
    const float w = preferredTotal <= stripWidth
        ? kPreferredTabWidth
        : stripWidth / static_cast<float>(_tabs.size());
    return math::FRectangle(strip.minX + w * static_cast<float>(index),
                            strip.minY,
                            std::min(strip.maxX,
                                     strip.minX + w * static_cast<float>(index + 1)),
                            strip.maxY);
}

math::FRectangle DockTabGroup::closeRectWorld(size_t index) const {
    const math::FRectangle tab = getTabRectWorld(index);
    constexpr float kCloseButtonWidth = 22.0f;
    return math::FRectangle(tab.maxX - kCloseButtonWidth, tab.minY,
                            tab.maxX, tab.maxY);
}

int DockTabGroup::tabIndexAt(const math::FVector2& worldPos) const {
    if (_tabs.empty() || !stripRectWorld().contains(worldPos)) {
        return -1;
    }
    for (size_t i = 0; i < _tabs.size(); ++i) {
        if (getTabRectWorld(i).contains(worldPos)) {
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
    if (!_tabs.empty() && stripRectWorld().contains(worldPos)) {
        // The tab strip claims hits before the active card's title bar
        // (which the strip covers visually and interactively).
        return this;
    }
    // Gate card descent by leaf bounds (default getClientRect) so a
    // card's overflowing grandchildren cannot steal hits outside the leaf.
    return compoundDescendHitTestClipped(this, worldPos);
}

bool DockTabGroup::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
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

    // Close is armed before activate/drag, then committed on mouse-up so
    // UIManager can clear capture before the host destroys or parks a card.
    if (static_cast<size_t>(idx) == _activeIndex
        && tab->isClosable()
        && closeRectWorld(static_cast<size_t>(idx)).contains(e.mousePos)) {
        dockTrace("[dock] tabGroup close arm card=%s leaf=%s\n",
                  tab->getId().c_str(), _leafId.c_str());
        _armedClose = idx;
        return true;
    }

    _armedClose = -1;

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

bool DockTabGroup::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        _armedClose = -1;
        return false;
    }
    const int armed = _armedClose;
    _armedClose = -1;
    if (armed < 0 || static_cast<size_t>(armed) >= _tabs.size()) {
        return false;
    }
    DockCard* tab = _tabs[static_cast<size_t>(armed)];
    if (tab == nullptr || static_cast<size_t>(armed) != _activeIndex
        || !tab->isClosable()
        || !closeRectWorld(static_cast<size_t>(armed)).contains(e.mousePos)) {
        return true;
    }
    dockTrace("[dock] tabGroup close commit card=%s leaf=%s\n",
              tab->getId().c_str(), _leafId.c_str());
    if (_onCloseTab) _onCloseTab(tab);
    return true;
}

bool DockTabGroup::onMouseMove(const UIMouseEvent& e) {
    _hoveredTab = -1;
    _hoveredClose = -1;
    if (!_tabs.empty() && stripRectWorld().contains(e.mousePos)) {
        const int idx = tabIndexAt(e.mousePos);
        if (idx >= 0) {
            _hoveredTab = idx;
            DockCard* tab = _tabs[static_cast<size_t>(idx)];
            if (static_cast<size_t>(idx) == _activeIndex
                && tab != nullptr && tab->isClosable()
                && closeRectWorld(static_cast<size_t>(idx)).contains(e.mousePos)) {
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
    if (recordNestedRenderIfNeeded(renderer)) {
        return;
    }
    if (!isVisible()) {
        return;
    }
    // Empty leaf should be pruned/hidden by DockArea; if one still
    // reaches paint, draw opaque chrome so it can never read as a
    // black "hole" against the dock background.
    if (_tabs.empty()) {
        const math::FRectangle b = getWorldBounds();
        if (b.maxX > b.minX && b.maxY > b.minY) {
            renderer.drawRect(b, math::FVector4(0.16f, 0.16f, 0.18f, 1.0f));
            renderer.drawBorderRect(
                b, math::FVector4(0.08f, 0.08f, 0.10f, 1.0f), 1.0f, 0.0f);
        }
        return;
    }
    // Cards first (clipped to the leaf so card-body overflow cannot
    // paint into a neighboring panel). The strip paints AFTER popClip
    // so it still covers the active card's own title bar.
    compoundDescendClippedRender(this, renderer);
    paintStrip(renderer);
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
        const math::FRectangle r = getTabRectWorld(i);
        const math::FRectangle close = closeRectWorld(i);
        const bool isActive = (i == _activeIndex);
        const bool showClose = isActive && tab->isClosable();
        const bool hovered = (static_cast<int>(i) == _hoveredTab);

        if (isActive) {
            renderer.drawRect(r, math::FVector4(0.22f, 0.24f, 0.30f, 1.0f));
        } else if (hovered) {
            renderer.drawRect(r, math::FVector4(0.18f, 0.18f, 0.22f, 1.0f));
        }

        // Inactive tabs use the full title area. Only the selected tab
        // reserves space for its close affordance.
        const math::FRectangle textRect(
            r.minX + 6.0f, r.minY,
            showClose ? close.minX - 2.0f : r.maxX - 6.0f, r.maxY);
        if (!tab->getTitle().empty()) {
            const std::wstring displayTitle = ellipsizeTabTitle(
                tab->getTitle(),
                std::max(0.0f, textRect.maxX - textRect.minX), renderer);
            renderer.drawText(textRect, displayTitle, kDockTabFontSize,
                              math::FVector4(0.90f, 0.90f, 0.92f, 1.0f));
        }

        // Only the selected, closable tab exposes the close x.
        if (showClose) {
            if (static_cast<int>(i) == _hoveredClose) {
                renderer.drawRect(close, math::FVector4(0.55f, 0.18f, 0.18f, 1.0f));
            }
            const math::FRectangle iconBounds(
                close.minX + 5.0f, close.minY + 7.0f,
                close.maxX - 5.0f, close.maxY - 7.0f);
            const SvgDocument::Ptr& icon = closeIconDocument();
            if (icon != nullptr) {
                icon->draw(renderer, iconBounds,
                           math::FVector4(0.90f, 0.90f, 0.92f, 1.0f));
            }
        }
    }
}

} // namespace ayt::ui
