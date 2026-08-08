#pragma once

// Gallery child-window host: DockCard promote → independent top-level
// window (IDE-style tear-off), hosted entirely inside the AYUI demo.
//
// MIRRORS AYEditor's EditorChildWindowManager but lives in the demo
// (AYUI's demo cannot depend on AYEditor) and is simplified — no JSON
// config path, no routeKey; the only open path is the LIVE DockCard
// migration (PR-Dock-TearOff).
//
// Each promoted card gets its own top-level OS window (via
// WindowManager::createTopLevelWindow), a per-HWND UIManager rendering
// through a GDI backend (bgfx is process-singleton-bound to the primary
// window and cannot switch HWNDs per frame), and the full typed input
// path forwarded from TopLevelWindowCallbacks.
//
// Lifecycle contract (K-INV-D5-6): destroy child windows BEFORE the
// primary UI goes down. The child UIManager's dtor calls shutdown()
// which can poke the active-manager slot; keeping the primary alive
// while the manager tears down avoids an UAF cleanup race.

#include "AYUIManager.h"
#include "AYWindowManager.h"
#include "AYWindowTypes.h"
#include "AYInputTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace ayt::ui {
class DockCard;
} // namespace ayt::ui

namespace ayt::gallery {

class GalleryChildWindows {
public:
    GalleryChildWindows(ayt::device::WindowManager& wm, ayt::ui::UIManager& primary);
    ~GalleryChildWindows();

    GalleryChildWindows(const GalleryChildWindows&) = delete;
    GalleryChildWindows& operator=(const GalleryChildWindows&) = delete;

    // Promote a live DockCard into a new top-level window. The frame
    // arrives in PRIMARY-window client coordinates (AYUI world space);
    // Win32 converts to screen coords here (ClientToScreen) — AYUI
    // stays cross-platform, AYDevice's TopLevelWindowDesc keeps OS
    // screen space. Non-Win32: coordinates pass through unchanged and
    // createTopLevelWindow's stub returns false.
    bool promoteCard(ayt::ui::DockCard* card, const std::wstring& title,
                     int x, int y, int w, int h);

    // Tick every open child: pushActive scope → update → GDI render
    // into the per-window backend (Win32; GetDC per frame).
    void tickAll(float dt);

    size_t count() const { return _entries.size(); }

    // Internal — exposed for tests to inspect entries.
    struct Entry {
        void*                              handle = nullptr;
        std::shared_ptr<ayt::ui::UIManager> ui;
        std::unique_ptr<ayt::ui::IRenderBackend> backend;
        ayt::ui::DockCard*                 card = nullptr;
    };
    const std::vector<Entry>& entries() const { return _entries; }

private:
    void closeChildWindow(void* h);

    ayt::device::WindowManager& _wm;
    ayt::ui::UIManager&         _primary;
    std::vector<Entry>          _entries;
};

} // namespace ayt::gallery
