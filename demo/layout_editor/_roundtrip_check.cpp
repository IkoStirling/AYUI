// Headless round-trip check for LayoutEditorSession (no HWND).
#include "LayoutEditorSession.h"

#include "AYUI/LayoutLoader.h"
#include "AYUI/MenuBar.h"
#include "AYUI/Theme.h"
#include "AYUI/UIKeyCode.h"
#include "AYUI/UIManager.h"
#include "AYUI/Widget.h"
#include "AYUI/WidgetFactory.h"

#include <cstdio>
#include <string>

int main() {
    ayt::ui::ThemeManager::get().ensureDefaultThemes();
    ayt::ui::ThemeManager::get().setActiveTheme("dark");

    ayt::ui::UIManager ui;
    ui.initialize(nullptr);
    if (!ui.loadLayout("assets/layout_editor.ui.json")) {
        std::fprintf(stderr, "chrome load failed\n");
        return 1;
    }

    ayt::ui::LayoutEditorSession session;
    if (!session.attach(ui)) {
        std::fprintf(stderr, "attach failed\n");
        return 2;
    }
    auto* menuBar = dynamic_cast<ayt::ui::MenuBar*>(
        ui.findById("designer_menubar"));
    if (menuBar == nullptr || menuBar->getMenuCount() != 2u) {
        std::fprintf(stderr, "designer menu regression: count=%zu\n",
            menuBar != nullptr ? menuBar->getMenuCount() : 0u);
        return 9;
    }
    if (!session.open("assets/sample_blank.ui.json")) {
        std::fprintf(stderr, "open failed\n");
        return 3;
    }

    session.selectById("btn_hello");
    ayt::ui::Widget* button = session.selected();
    if (button == nullptr) {
        std::fprintf(stderr, "button selection failed\n");
        return 10;
    }
    const ayt::math::FVector2 buttonBeforeNudge = button->getPosition();
    if (!session.onKeyDown(ayt::ui::UIKey_Right) ||
        button->getPosition().x != buttonBeforeNudge.x + 1.0f ||
        button->getPosition().y != buttonBeforeNudge.y) {
        std::fprintf(stderr, "arrow nudge regression\n");
        return 11;
    }
    session.onKeyDown(ayt::ui::UIKey_Left);

    // Regression: child then Shift-select root must not nest ancestor+child.
    session.select(session.documentRoot(), true);
    if (session.selection().size() != 1 ||
        session.selected() != session.documentRoot()) {
        std::fprintf(stderr,
            "nested-select regression: expected exclusive root, got sel=%zu\n",
            session.selection().size());
        return 8;
    }

    const ayt::math::FVector2 rootBeforeInput =
        session.documentRoot()->getPosition();
    session.applyProperty("x", L"96");
    session.onKeyDown(ayt::ui::UIKey_Right);
    session.setViewZoom(1.1f, {420.0f, 320.0f});
    if (session.documentRoot()->getPosition().x != rootBeforeInput.x ||
        session.documentRoot()->getPosition().y != rootBeforeInput.y) {
        std::fprintf(stderr, "document root moved through editor input\n");
        return 12;
    }
    session.setViewZoom(1.0f, {420.0f, 320.0f});

    session.selectById("btn_hello");
    session.applyProperty("text", L"RoundTrip");
    session.applyProperty("w", L"150");

    const std::string outPath = "assets/sample_roundtrip_out.ui.json";
    if (!session.saveAs(outPath)) {
        std::fprintf(stderr, "save failed\n");
        return 4;
    }

    ayt::ui::UILayoutLoader loader;
    loader.setWidgetFactory(&ayt::ui::WidgetFactory::get());
    ayt::ui::Widget* reloaded = loader.loadFromFile(outPath);
    if (reloaded == nullptr) {
        std::fprintf(stderr, "reload failed\n");
        return 5;
    }

    std::string json;
    const bool okJson = loader.saveLayoutToString(reloaded, json, true);
    ayt::ui::destroyWidgetTree(reloaded);
    if (!okJson || json.find("RoundTrip") == std::string::npos) {
        std::fprintf(stderr, "verify text failed\n%s\n", json.c_str());
        return 6;
    }
    if (json.find("150") == std::string::npos) {
        std::fprintf(stderr, "verify size failed\n%s\n", json.c_str());
        return 7;
    }

    session.detach();
    ui.shutdown();
    std::printf("ROUNDTRIP_OK\n");
    return 0;
}
