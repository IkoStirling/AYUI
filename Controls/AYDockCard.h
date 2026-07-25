#pragma once

#include "AYPanel.h"
#include <memory>
#include <string>

namespace ayt::ui {

// D1: DockCard - a leaf panel with a title + optional icon that hosts a
// single widget tree. Built on top of Panel so existing style + border +
// padding plumbing keeps working. The "thing" the user moves between slots
// in the editor shell.
//
// D1 ships the data model only; tear-off UX (drag-to-float) is D3 and the
// persistence layer is D4. D1 cards already render with their title text
// in the header strip and host arbitrary content (any Widget subclass).
//
// Lifecycle: DockCard owns its content Widget* via setContent. When the
// card is destroyed (or detached from a slot), the content is destroyed
// through destroyWidgetTree to mirror how CompoundWidget handles children.
class DockCard : public Panel {
public:
    DockCard();
    ~DockCard() override;

    // The card's stable identifier inside the DockArea registry. Used by
    // DockArea::findCard / saveLayout / loadLayout to round-trip the card.
    const std::string& getId() const { return _id; }
    void setId(const std::string& id) { _id = id; }

    // Title shown in the header strip. Stored as wide string to match
    // the rest of the AYUI text API (Button, TextLabel, etc.).
    const std::wstring& getTitle() const { return _title; }
    void setTitle(const std::wstring& title);

    // Optional glyph (asset name) for the header strip. v1.5 just stores
    // the string; the render path that draws the icon lives in D1's
    // header draw and is a no-op when empty.
    const std::string& getIcon() const { return _icon; }
    void setIcon(const std::string& icon) { _icon = icon; }

    // Replaces the currently-hosted content. The previous content (if any)
    // is destroyed. Pass nullptr to clear.
    void setContent(Widget* w);
    Widget* getContent() const { return _content; }

    // Whether the card shows a close ("x") affordance in its header.
    // D1 only stores the flag; the click handler that fires onCloseRequested
    // lands in D3 alongside tear-off UX.
    void setClosable(bool c) { _closable = c; }
    bool isClosable() const { return _closable; }

    // Whether the card can be torn off into a floating card. D1 stores
    // the flag; D3 wires the actual drag path.
    void setFloatable(bool f) { _floatable = f; }
    bool isFloatable() const { return _floatable; }

    // Whether the header is collapsed (only the title strip is visible).
    // Default: false. Stored as a flag for D3 / D4 round-trip.
    void setCollapsed(bool c);
    bool isCollapsed() const { return _collapsed; }

    // Header strip height (logical pixels). Default 22px.
    void setHeaderHeight(float h) { _headerHeight = h; }
    float getHeaderHeight() const { return _headerHeight; }

    void onRender(IRenderBackend& renderer) override;
    void performLayout() override;

private:
    std::string _id;
    std::wstring _title;
    std::string _icon;
    Widget* _content = nullptr;       // owned (destroyed via destroyWidgetTree)
    bool _closable = true;
    bool _floatable = true;
    bool _collapsed = false;
    float _headerHeight = 22.0f;
};

} // namespace ayt::ui