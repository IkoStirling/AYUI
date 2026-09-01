#pragma once

// Gallery child-window host: DockCard promote → independent top-level
// window (IDE-style tear-off), hosted entirely inside the AYUI demo.
//
// Redock rules (must ALL hold):
//   * G12 drag active on the child card
//   * cursor moved past a small threshold (not a title click)
//   * cursor lies in the PRIMARY client rect (screen→client)
//   * topmost HWND is primary, OR the child currently being dragged
//     (window-follows-cursor covers the dock; other floaters still reject)
//   * hitTestSlot returns a real slot inside the dock bounds
//   * slot rect is large enough (rejects disabled Top/Bottom at 1e-6)

#include "AYUI/UIManager.h"
#include "AYDevice/WindowManager.h"
#include "AYDevice/WindowTypes.h"
#include "AYDevice/InputTypes.h"
#include "AYMath/MathTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace ayt::ui {
class DockCard;
class DockArea;
} // namespace ayt::ui

namespace ayt::gallery {

class GalleryChildWindows {
public:
    GalleryChildWindows(ayt::device::WindowManager& wm, ayt::ui::UIManager& primary);
    ~GalleryChildWindows();

    GalleryChildWindows(const GalleryChildWindows&) = delete;
    GalleryChildWindows& operator=(const GalleryChildWindows&) = delete;

    bool promoteCard(ayt::ui::DockCard* card, const std::wstring& title,
                     int x, int y, int w, int h);

    void tickAll(float dt);

    void setRedockTarget(ayt::ui::DockArea* dock) { _dock = dock; }

    bool hasActiveDrag() const;

    // Close the child HWND that currently hosts `card` (destroys the
    // card with the child UIManager). Returns false if not hosted here.
    bool closeCardHost(ayt::ui::DockCard* card);

    size_t count() const { return _entries.size(); }

    struct Entry {
        void*                              handle = nullptr;
        std::shared_ptr<ayt::ui::UIManager> ui;
        std::unique_ptr<ayt::ui::IRenderBackend> backend;
        ayt::ui::DockCard*                 card = nullptr;
        bool dragMoveActive = false;
        int  dragGrabX = 0;
        int  dragGrabY = 0;
        int  dragStartScreenX = 0;
        int  dragStartScreenY = 0;
        int  dragLastScreenX = 0;
        int  dragLastScreenY = 0;
        int  dragTravel = 0;
    };
    const std::vector<Entry>& entries() const { return _entries; }

private:
    void closeChildWindow(void* h);

    bool tryRedock(const std::shared_ptr<ayt::ui::UIManager>& ui);

    bool screenToPrimaryWorld(int screenX, int screenY,
                              ayt::math::FVector2& out) const;
    // True when the tracked mouse-message screen point is over the primary
    // client area and the topmost HWND is either the primary or the child
    // currently being G12-dragged (so follow-cursor tear-offs can redock).
    bool screenPointOverPrimaryWindow(int screenX, int screenY) const;

    void beginDragMove(void* handle, float clientX, float clientY);
    void updateDragMove(void* handle, float clientX, float clientY);
    void endDragMove(void* handle);
    void updateRedockHover();
    void configurePromotedCardChrome(ayt::ui::DockCard* card);
    void resetPromotedCardChrome(ayt::ui::DockCard* card);

    Entry* findEntryByHandle(void* handle);
    Entry* findEntryByUi(const ayt::ui::UIManager* ui);
    Entry* findEntryByCard(const ayt::ui::DockCard* card);

    ayt::device::WindowManager& _wm;
    ayt::ui::UIManager&         _primary;
    ayt::ui::DockArea*          _dock = nullptr;
    std::vector<Entry>          _entries;
};

} // namespace ayt::gallery
