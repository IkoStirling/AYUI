#include "GalleryChildWindows.h"

#include "AYDockCard.h"

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

#include <cstdio>

namespace ayt::gallery {

namespace {

// PR-Dock-TearOff: convert a promote frame from primary-window CLIENT
// coordinates to SCREEN coordinates (the card's world position lives in
// primary-client space; createTopLevelWindow positions in OS screen
// space). Win32: ClientToScreen against the primary HWND. Non-Win32:
// identity pass-through (createTopLevelWindow is a stub there anyway).
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
    // K-INV-D5-6: tear down child windows BEFORE the primary UI.
    // ~UIManager calls shutdown() which can poke g_activeUIManager
    // (only if it was active); the primary's active flag wins over
    // a potentially-null child, so destroying the manager here
    // (with primary still alive) avoids an UAF cleanup race.
    for (auto& e : _entries) {
        if (e.handle != nullptr) {
            _wm.destroyTopLevelWindow(e.handle);
            e.handle = nullptr;
        }
    }
    _entries.clear();
}

bool GalleryChildWindows::promoteCard(ayt::ui::DockCard* card,
                                      const std::wstring& title,
                                      int x, int y, int w, int h) {
    if (card == nullptr) {
        return false;
    }
    // The promote frame is the card's WORLD position = primary client
    // coords. Convert to screen coords before handing to
    // createTopLevelWindow (which positions in OS screen space).
    clientToScreenCoords(_wm, x, y);

    ayt::device::TopLevelWindowDesc d;
    d.title  = std::string(title.begin(), title.end());
    d.x      = x;
    d.y      = y;
    d.width  = w;
    d.height = h;

    void* handle = nullptr;
    if (!_wm.createTopLevelWindow(d, handle)) {
        std::fprintf(stderr,
            "[GalleryChildWindows] createTopLevelWindow failed for '%ls'\n",
            title.c_str());
        return false;
    }

    // Build the entry first so the callbacks can capture a stable
    // shared_ptr (the vector may reallocate on push_back; std::shared_ptr
    // keeps the UIManager alive across the lifetime of the callback
    // even if closeChildWindow removes the entry under it).
    Entry e;
    e.handle = handle;
    e.ui     = std::make_shared<ayt::ui::UIManager>();
    e.card   = card;
#if defined(_WIN32)
    // Per-HWND GDI backend — the promoted card renders into THIS
    // window's DC (bgfx is process-singleton-bound to the primary
    // window and cannot switch HWNDs per frame).
    e.backend = std::make_unique<GalleryChildBackend>(static_cast<HWND>(handle));
    e.ui->initialize(e.backend.get());
#else
    e.ui->initialize(nullptr);  // K-INV-D5-4 null backend = no render
#endif
    e.ui->setClientSize(static_cast<float>(w), static_cast<float>(h));

    // PR-Dock-TearOff live-card migration: reparent the LIVE card into
    // the child root. addChild auto-detaches from the old parent (the
    // source DockOverlay); the card keeps its whole widget subtree and
    // callback state (K-INV-D5.5-2 — detachToOwnWindow already snapped
    // the overlay bookkeeping BEFORE our callback ran).
    card->setPosition(ayt::math::FVector2(0.0f, 0.0f));
    card->setSize(ayt::math::FVector2(static_cast<float>(w),
                                      static_cast<float>(h)));
    e.ui->root()->addChild(card);
    e.ui->root()->performLayout();

    ayt::device::TopLevelWindowCallbacks cbs;
    // Capture by value. The UIManager lives in `_entries` by
    // shared_ptr; the lambdas run on the Win32 message thread, NOT
    // concurrent with our tick (single-threaded Gallery v1).
    cbs.onCloseRequested = [this, handle]() {
        this->closeChildWindow(handle);
    };

    // Input forwarding. Every callback grabs its own ActiveScope —
    // these fire during the Win32 message pump (pollEvents), NOT inside
    // tickAll, so each must push/pop the active UIManager independently
    // without polluting the primary's slot. Coordinates arrive
    // client-relative (AYDevice translated them); buttons map down/up
    // to the UIManager pair (bool return = request capture). Capture
    // the shared_ptr + card (NOT the Entry — it holds a non-copyable
    // unique_ptr backend).
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
        ui->root()->performLayout();
    };
    cbs.onMouseMove = [ui](float x, float y) {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        ui->onMouseMove(x, y);
    };
    cbs.onMouseLeave = [ui]() {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        ui->onMouseLeave();
    };
    cbs.onMouseButton = [ui](float x, float y, int button, bool pressed) {
        ayt::ui::UIManager::ActiveScope guard(ui.get());
        return pressed ? ui->onMouseButtonDown(x, y, button)
                       : ui->onMouseButtonUp(x, y, button);
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

    _entries.push_back(std::move(e));
    return true;
}

void GalleryChildWindows::closeChildWindow(void* handle) {
    for (auto it = _entries.begin(); it != _entries.end(); ++it) {
        if (it->handle == handle) {
            if (it->handle != nullptr) {
                _wm.destroyTopLevelWindow(it->handle);
            }
            // shared_ptr<UIManager> drops here — ~UIManager calls
            // shutdown() which resets the active slot IF this was the
            // active one. The primary is re-established as active by
            // its own update path on the next main-loop frame.
            _entries.erase(it);
            return;
        }
    }
}

void GalleryChildWindows::tickAll(float dt) {
    for (auto& e : _entries) {
        if (!e.ui) continue;
        // pushActive swaps the active UIManager for the duration of
        // this iteration; on scope exit the previous active (typically
        // the Gallery's primary) is restored.
        ayt::ui::UIManager::ActiveScope guard(e.ui.get());
        e.ui->update(dt);
#if defined(_WIN32)
        // Per-window GDI draw. Grab the window DC for this frame, point
        // the backend at it, render. GetDC/ReleaseDC round-trip per
        // frame keeps the DC lifetime tight (no stale handle across
        // resize/destroy).
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
        e.ui->render();  // nullptr backend → populateFrame/flushFrame guard
#endif
    }
}

} // namespace ayt::gallery
