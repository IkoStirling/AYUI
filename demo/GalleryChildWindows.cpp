#include "GalleryChildWindows.h"

#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockTrace.h"
#include "AYUI/DeviceInputBridge.h"

#if defined(_WIN32)
#  include "GalleryChildBackend.h"
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

#include <cmath>
#include <codecvt>
#include <cstdio>
#include <cstdlib>
#include <locale>

namespace ayt::gallery {

namespace {

std::string wideToUtf8(const std::wstring& text) {
    if constexpr (sizeof(wchar_t) == 2) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.to_bytes(text);
    } else {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(text);
    }
}

void clientToScreenCoords(ayt::device::WindowManager& wm, int& x, int& y) {
#if defined(_WIN32)
    if (HWND primaryHwnd = static_cast<HWND>(wm.getWindowHandle())) {
        POINT pt{static_cast<LONG>(x), static_cast<LONG>(y)};
        ::ClientToScreen(primaryHwnd, &pt);
        x = static_cast<int>(pt.x);
        y = static_cast<int>(pt.y);
    }
#else
    (void)wm;
    (void)x;
    (void)y;
#endif
}

} // namespace

GalleryChildWindows::GalleryChildWindows(ayt::device::WindowManager& wm,
                                         ayt::ui::UIManager& primary)
    : _wm(wm)
    , _primary(primary) {
}

GalleryChildWindows::~GalleryChildWindows() {
    for (auto& e : _entries) {
        if (e.handle != nullptr) {
            _wm.destroyTopLevelWindow(e.handle);
            e.handle = nullptr;
        }
    }
    _entries.clear();
}

GalleryChildWindows::Entry* GalleryChildWindows::findEntryByHandle(void* handle) {
    for (auto& e : _entries) {
        if (e.handle == handle) {
            return &e;
        }
    }
    return nullptr;
}

GalleryChildWindows::Entry* GalleryChildWindows::findEntryByUi(
    const ayt::ui::UIManager* ui) {
    for (auto& e : _entries) {
        if (e.ui.get() == ui) {
            return &e;
        }
    }
    return nullptr;
}

GalleryChildWindows::Entry* GalleryChildWindows::findEntryByCard(
    const ayt::ui::DockCard* card) {
    for (auto& e : _entries) {
        if (e.card == card) {
            return &e;
        }
    }
    return nullptr;
}

bool GalleryChildWindows::hasActiveDrag() const {
    for (const auto& e : _entries) {
        if (e.ui && e.ui->isDragging()) {
            return true;
        }
    }
    return false;
}

bool GalleryChildWindows::closeCardHost(ayt::ui::DockCard* card) {
    Entry* e = findEntryByCard(card);
    if (e == nullptr || e->handle == nullptr) {
        return false;
    }
    ayt::ui::dockTrace("[child] closeCardHost card=%s\n",
                       card ? card->getId().c_str() : "?");
    closeChildWindow(e->handle);
    return true;
}

bool GalleryChildWindows::cursorToPrimaryWorld(ayt::math::FVector2& out) const {
#if defined(_WIN32)
    HWND primaryHwnd = static_cast<HWND>(_wm.getWindowHandle());
    if (primaryHwnd == nullptr) {
        return false;
    }
    int screenX = 0;
    int screenY = 0;
    if (!_wm.getCursorScreenPosition(screenX, screenY)) {
        return false;
    }
    POINT pt{static_cast<LONG>(screenX), static_cast<LONG>(screenY)};
    if (!::ScreenToClient(primaryHwnd, &pt)) {
        return false;
    }
    out = ayt::math::FVector2(static_cast<float>(pt.x),
                              static_cast<float>(pt.y));
    return true;
#else
    (void)out;
    return false;
#endif
}

bool GalleryChildWindows::cursorOverPrimaryWindow() const {
#if defined(_WIN32)
    HWND primaryHwnd = static_cast<HWND>(_wm.getWindowHandle());
    if (primaryHwnd == nullptr) {
        return false;
    }
    int screenX = 0;
    int screenY = 0;
    if (!_wm.getCursorScreenPosition(screenX, screenY)) {
        return false;
    }
    POINT pt{static_cast<LONG>(screenX), static_cast<LONG>(screenY)};
    // Geometry gate: cursor must lie in the primary client rect. This is
    // independent of which HWND is topmost — the tear-off follows the
    // cursor with SetWindowPos, so WindowFromPoint alone always sees the
    // child and would permanently block redock + drop guides.
    POINT clientPt = pt;
    if (!::ScreenToClient(primaryHwnd, &clientPt)) {
        return false;
    }
    RECT clientRect{};
    if (!::GetClientRect(primaryHwnd, &clientRect)
        || !::PtInRect(&clientRect, clientPt)) {
        return false;
    }

    HWND under = ::WindowFromPoint(pt);
    if (under == nullptr) {
        return false;
    }
    if (under == primaryHwnd || ::IsChild(primaryHwnd, under)) {
        return true;
    }
    // Allow the child that is actively G12-dragging (covers the dock
    // while following the cursor). Reject other promoted floaters so a
    // parked window sitting over the dock cannot steal the drop.
    for (const auto& e : _entries) {
        if (e.handle == nullptr) {
            continue;
        }
        HWND child = static_cast<HWND>(e.handle);
        if (under == child || ::IsChild(child, under)) {
            return e.ui != nullptr && e.ui->isDragging();
        }
    }
    return false;
#else
    return false;
#endif
}

void GalleryChildWindows::beginDragMove(void* handle) {
#if defined(_WIN32)
    Entry* e = findEntryByHandle(handle);
    if (e == nullptr || e->handle == nullptr) {
        return;
    }
    HWND hwnd = static_cast<HWND>(e->handle);
    int screenX = 0;
    int screenY = 0;
    RECT wr{};
    if (!_wm.getCursorScreenPosition(screenX, screenY)
        || !::GetWindowRect(hwnd, &wr)) {
        return;
    }
    e->dragGrabX = screenX - static_cast<int>(wr.left);
    e->dragGrabY = screenY - static_cast<int>(wr.top);
    e->dragStartScreenX = screenX;
    e->dragStartScreenY = screenY;
    e->dragMoveActive = true;
#else
    (void)handle;
#endif
}

void GalleryChildWindows::updateDragMove(void* handle) {
#if defined(_WIN32)
    Entry* e = findEntryByHandle(handle);
    if (e == nullptr || !e->dragMoveActive || e->handle == nullptr) {
        return;
    }
    if (!e->ui || !e->ui->isDragging()) {
        e->dragMoveActive = false;
        return;
    }
    int screenX = 0;
    int screenY = 0;
    if (!_wm.getCursorScreenPosition(screenX, screenY)) {
        return;
    }
    ::SetWindowPos(static_cast<HWND>(e->handle), nullptr,
                   screenX - e->dragGrabX, screenY - e->dragGrabY,
                   0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#else
    (void)handle;
#endif
}

void GalleryChildWindows::endDragMove(void* handle) {
    if (Entry* e = findEntryByHandle(handle)) {
        e->dragMoveActive = false;
    }
}

bool GalleryChildWindows::promoteCard(ayt::ui::DockCard* card,
                                      const std::wstring& title,
                                      int x, int y, int w, int h) {
    if (card == nullptr) {
        return false;
    }
    clientToScreenCoords(_wm, x, y);

    ayt::device::TopLevelWindowDesc d;
    d.title  = wideToUtf8(title);
    d.x      = x;
    d.y      = y;
    d.width  = w;
    d.height = h;
    // No OS caption — DockCard title is the chrome. Thick-frame still
    // enables edge/corner resize (口 grip painted on the card).
    d.borderless = true;
    d.resizable  = true;
    // Create hidden; paint first GDI frame; then show (no white flash).
    d.visible = false;

    void* handle = nullptr;
    if (!_wm.createTopLevelWindow(d, handle)) {
        std::fprintf(stderr,
            "[GalleryChildWindows] createTopLevelWindow failed for '%ls'\n",
            title.c_str());
        return false;
    }

    Entry e;
    e.handle = handle;
    e.ui     = std::make_shared<ayt::ui::UIManager>();
    e.card   = card;
#if defined(_WIN32)
    e.backend = std::make_unique<GalleryChildBackend>(static_cast<HWND>(handle));
    e.ui->initialize(e.backend.get());
#else
    e.ui->initialize(nullptr);
#endif
    ayt::ui::UIManager::makeActive(&_primary);

    e.ui->setClientSize(static_cast<float>(w), static_cast<float>(h));

    ayt::ui::dockTrace("[child] promoteCard OK card=%s screen=(%d,%d) %dx%d\n",
                       card->getId().c_str(), x, y, w, h);

    card->setPosition(ayt::math::FVector2(0.0f, 0.0f));
    card->setSize(ayt::math::FVector2(static_cast<float>(w),
                                      static_cast<float>(h)));
    card->setShowResizeGrip(true);
    card->setShowMaximizeButton(true);
    card->setMaximizeHandler(
        [](void* user, ayt::ui::DockCard* c) {
            auto* self = static_cast<GalleryChildWindows*>(user);
            if (self == nullptr || c == nullptr) {
                return;
            }
            for (const auto& ent : self->entries()) {
                if (ent.card != c || ent.handle == nullptr) {
                    continue;
                }
                self->_wm.toggleTopLevelMaximized(ent.handle);
                c->setMaximizedVisual(
                    self->_wm.isTopLevelMaximized(ent.handle));
                break;
            }
        },
        this);
    e.ui->root()->addChild(card);
    e.ui->layout();

    ayt::device::TopLevelWindowCallbacks cbs;
    cbs.onCloseRequested = [this, handle]() {
        this->closeChildWindow(handle);
    };

    const std::shared_ptr<ayt::ui::UIManager> ui = e.ui;
    ayt::ui::DockCard* promotedCard = e.card;
    cbs.onResize = [ui, promotedCard](int width, int height) {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        ui->setClientSize(static_cast<float>(width),
                          static_cast<float>(height));
        if (promotedCard != nullptr) {
            promotedCard->setSize(ayt::math::FVector2(
                static_cast<float>(width), static_cast<float>(height)));
        }
        ui->layout();
    };
    cbs.onMouseMove = [this, ui, handle](float x, float y) {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        ui->onMouseMove(x, y);
        // G12 title-drag moves the OS window with the cursor so the
        // user can park it back over the primary DockArea.
        this->updateDragMove(handle);
    };
    cbs.onMouseLeave = [ui]() {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        ui->onMouseLeave();
    };
    cbs.cursorShape = [ui]() {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        return ayt::ui::systemCursorFromUi(ui->getCursorHint());
    };
    cbs.onMouseButton = [this, ui, handle](float x, float y, int button,
                                           bool pressed) {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        if (pressed) {
            const bool handled = ui->onMouseButtonDown(x, y, button);
            if (button == 0 && ui->isDragging()) {
                this->beginDragMove(handle);
            }
            return handled;
        }
        // Redock BEFORE endDrag �?uses OS cursor �?primary dock hit-test.
        if (button == 0 && this->tryRedock(ui)) {
            this->endDragMove(handle);
            return true;
        }
        const bool handled = ui->onMouseButtonUp(x, y, button);
        this->endDragMove(handle);
        return handled;
    };
    cbs.onMouseWheel = [ui](float x, float y, float deltaY) {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        ui->onMouseWheel(x, y, deltaY);
    };
    cbs.onKey = [ui](::ayt::device::KeyCode kc, bool pressed) {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        if (pressed) {
            ui->onDeviceKeyDown(kc);
        } else {
            ui->onDeviceKeyUp(kc);
        }
    };
    cbs.onChar = [ui](const char* utf8, int byteCount) {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        ui->onDeviceChar(utf8, byteCount);
    };
    _wm.setTopLevelCallbacks(handle, cbs);

#if defined(_WIN32)
    // First present while HWND is still hidden, then reveal.
    if (e.backend && e.handle != nullptr) {
        if (HDC hdc = ::GetDC(static_cast<HWND>(e.handle))) {
            ayt::ui::UIManager::ActiveScope guard(e.ui.get());
            auto* gdi = static_cast<GalleryChildBackend*>(e.backend.get());
            gdi->setDrawTarget(hdc, w, h);
            e.ui->render();
            ::ReleaseDC(static_cast<HWND>(e.handle), hdc);
        }
    }
#endif
    _wm.setTopLevelVisible(handle, true);

    _entries.push_back(std::move(e));
    return true;
}

void GalleryChildWindows::closeChildWindow(void* handle) {
    for (auto it = _entries.begin(); it != _entries.end(); ++it) {
        if (it->handle == handle) {
            if (it->handle != nullptr) {
                _wm.destroyTopLevelWindow(it->handle);
            }
            _entries.erase(it);
            ayt::ui::UIManager::makeActive(&_primary);
            return;
        }
    }
}

void GalleryChildWindows::updateRedockHover() {
    if (_dock == nullptr) {
        return;
    }
    for (const auto& e : _entries) {
        if (e.ui && e.ui->isDragging()) {
            ayt::math::FVector2 world;
            if (cursorOverPrimaryWindow() && cursorToPrimaryWorld(world)) {
                _dock->setExternalDropPos(world);
            } else {
                _dock->clearExternalDropPos();
            }
            return;
        }
    }
    _dock->clearExternalDropPos();
}

bool GalleryChildWindows::tryRedock(
    const std::shared_ptr<ayt::ui::UIManager>& ui) {
    if (_dock == nullptr || ui == nullptr || !ui->isDragging()) {
        return false;
    }

    Entry* entry = findEntryByUi(ui.get());
    if (entry == nullptr || entry->card == nullptr) {
        return false;
    }

#if defined(_WIN32)
    // Title-click / tiny nudge must not redock.
    int screenX = 0;
    int screenY = 0;
    if (_wm.getCursorScreenPosition(screenX, screenY)) {
        const int moved = std::abs(screenX - entry->dragStartScreenX)
                        + std::abs(screenY - entry->dragStartScreenY);
        if (moved < 12) {
            ayt::ui::dockTrace("[child] tryRedock skip moved=%d\n", moved);
            return false;
        }
    }
#endif

    // Cursor must be over the primary client. The dragging child HWND
    // is allowed to sit on top (follow-cursor); hit-testing uses the
    // cursor's primary-client coordinates, not WindowFromPoint ownership.
    if (!cursorOverPrimaryWindow()) {
        ayt::ui::dockTrace("[child] tryRedock skip (cursor outside primary client)\n");
        return false;
    }

    ayt::math::FVector2 world;
    if (!cursorToPrimaryWorld(world)) {
        return false;
    }

    // Prefer live tree leaf under the cursor; fall back to legacy slot
    // bands so collapsed sides can still revive.
    const ayt::ui::DockArea::Slot slot = _dock->hitTestSlot(world);
    const ayt::ui::DockTabGroup* treeLeaf = _dock->hitTestTree(world);
    ayt::ui::dockTrace("[child] tryRedock cursor primary=(%.1f,%.1f) slot=%d "
                       "tree=%s\n",
                       world.x, world.y, static_cast<int>(slot),
                       treeLeaf != nullptr ? treeLeaf->getLeafId().c_str()
                                           : "-");
    if (treeLeaf == nullptr && slot == ayt::ui::DockArea::Slot::Count) {
        return false;
    }

    // Reject disabled / near-zero bands when only the slot path applies
    // (Gallery Top/Bottom at 1e-6).
    if (treeLeaf == nullptr) {
        const ayt::math::FRectangle sr = _dock->getSlotRect(slot);
        if ((sr.maxX - sr.minX) < 8.0f || (sr.maxY - sr.minY) < 8.0f) {
            ayt::ui::dockTrace("[child] tryRedock skip tiny slot=%d\n",
                               static_cast<int>(slot));
            return false;
        }
    }

    ayt::ui::DockCard* card = entry->card;
    const std::string cardId = card->getId();
    void* handle = entry->handle;

    ui->cancelDrag();

    ui->root()->removeChild(card);
    // redockAt / adoptCard / requestRelayout must see the primary
    // UIManager via tryGet() — not the child that still owns
    // ActiveScope from the mouse-up handler.
    {
        ayt::ui::UIManager::ActiveScope primaryGuard(&_primary);
        if (!_dock->redockAt(card, world)) {
            ayt::ui::dockTrace("[child] tryRedock redockAt FAILED card=%s\n",
                               cardId.c_str());
            ui->root()->addChild(card);
            return false;
        }
        _dock->clearExternalDropPos();
        _primary.layout();
    }

    ayt::ui::dockTrace("[child] tryRedock OK card=%s pos=(%.1f,%.1f)\n",
                       cardId.c_str(), world.x, world.y);

    this->closeChildWindow(handle);
    return true;
}

void GalleryChildWindows::tickAll(float dt) {
    updateRedockHover();
    for (auto& e : _entries) {
        if (!e.ui) continue;
        ayt::ui::UIManager::ActiveScope guard(e.ui.get());
        e.ui->update(dt);
#if defined(_WIN32)
        if (e.backend && e.handle != nullptr) {
            if (HWND childHwnd = static_cast<HWND>(e.handle)) {
                if (HDC hdc = ::GetDC(childHwnd)) {
                    const int w = static_cast<int>(e.ui->getClientSize().x);
                    const int h = static_cast<int>(e.ui->getClientSize().y);
                    auto* gdi = static_cast<GalleryChildBackend*>(e.backend.get());
                    gdi->setDrawTarget(hdc, w, h);
                    e.ui->render();
                    ::ReleaseDC(childHwnd, hdc);
                }
            }
        }
#else
        e.ui->render();
#endif
    }
}

} // namespace ayt::gallery
