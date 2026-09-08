#include "AYTest.h"

#include "AYUI/Button.h"
#include "AYUI/LayoutLoader.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Panel.h"
#include "AYUI/Widget.h"

#include <string>

using namespace ayt::math;
using namespace ayt::ui;

TEST_SUITE(AYUI_AnchorLayout)

TEST_CASE(fixed_bottom_right_anchor_tracks_parent_and_preserves_size) {
    Panel parent;
    Button child;
    parent.setSize(FVector2(200.0f, 100.0f));
    child.setPosition(FVector2(130.0f, 50.0f));
    child.setSize(FVector2(50.0f, 30.0f));
    child.setLayoutPositionManaged(false);
    child.setLayoutSizeManaged(false);
    parent.addChildExternal(&child);

    child.setAnchorLayoutPreservingRect(
        FVector2(1.0f, 1.0f), FVector2(1.0f, 1.0f),
        FVector2(1.0f, 1.0f));
    parent.setSize(FVector2(300.0f, 200.0f));

    CHECK_FLOAT_EQ(child.getPosition().x, 230.0f, 1e-5f);
    CHECK_FLOAT_EQ(child.getPosition().y, 150.0f, 1e-5f);
    CHECK_FLOAT_EQ(child.getSize().x, 50.0f, 1e-5f);
    CHECK_FLOAT_EQ(child.getSize().y, 30.0f, 1e-5f);
    parent.removeChild(&child);
}

TEST_CASE(stretch_anchor_resizes_between_parent_edges) {
    Panel parent;
    Button child;
    parent.setSize(FVector2(200.0f, 100.0f));
    child.setPosition(FVector2(10.0f, 20.0f));
    child.setSize(FVector2(180.0f, 30.0f));
    child.setLayoutPositionManaged(false);
    child.setLayoutSizeManaged(false);
    parent.addChildExternal(&child);

    child.setAnchorLayoutPreservingRect(
        FVector2(0.0f, 0.0f), FVector2(1.0f, 0.0f));
    parent.setSize(FVector2(320.0f, 160.0f));

    CHECK_FLOAT_EQ(child.getPosition().x, 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(child.getPosition().y, 20.0f, 1e-5f);
    CHECK_FLOAT_EQ(child.getSize().x, 300.0f, 1e-5f);
    CHECK_FLOAT_EQ(child.getSize().y, 30.0f, 1e-5f);
    parent.removeChild(&child);
}

TEST_CASE(anchor_layout_round_trips_through_ui_layout_json) {
    auto* root = new Panel();
    root->setId("anchor_root");
    root->setSize(FVector2(200.0f, 100.0f));
    root->setLayoutPositionManaged(false);
    root->setLayoutSizeManaged(false);

    auto* child = new Button();
    child->setId("anchored_button");
    child->setPosition(FVector2(130.0f, 50.0f));
    child->setSize(FVector2(50.0f, 30.0f));
    child->setLayoutPositionManaged(false);
    child->setLayoutSizeManaged(false);
    root->addChild(child);
    child->setAnchorLayoutPreservingRect(
        FVector2(1.0f, 1.0f), FVector2(1.0f, 1.0f),
        FVector2(1.0f, 1.0f));

    UILayoutLoader loader;
    std::string json;
    CHECK(loader.saveLayoutToString(root, json, false));
    CHECK(json.find("\"anchors\"") != std::string::npos);

    Widget* loadedRoot = loader.loadFromString(json);
    Widget* loadedChild = loader.findWidgetById("anchored_button");
    CHECK_NOT_NULL(loadedRoot);
    CHECK_NOT_NULL(loadedChild);
    if (loadedRoot != nullptr && loadedChild != nullptr) {
        CHECK(loadedChild->hasAnchorLayout());
        const AnchorLayout& anchor = loadedChild->getAnchorLayout();
        CHECK_FLOAT_EQ(anchor.anchorMin.x, 1.0f, 1e-5f);
        CHECK_FLOAT_EQ(anchor.anchorMin.y, 1.0f, 1e-5f);
        CHECK_FLOAT_EQ(anchor.pivot.x, 1.0f, 1e-5f);
        CHECK_FLOAT_EQ(anchor.pivot.y, 1.0f, 1e-5f);
        loadedRoot->setSize(FVector2(300.0f, 200.0f));
        CHECK_FLOAT_EQ(loadedChild->getPosition().x, 230.0f, 1e-5f);
        CHECK_FLOAT_EQ(loadedChild->getPosition().y, 150.0f, 1e-5f);
    }

    destroyWidgetTree(root);
    destroyWidgetTree(loadedRoot);
}

TEST_CASE(anchor_values_are_normalized_and_can_be_cleared) {
    Button child;
    AnchorLayout layout;
    layout.anchorMin = FVector2(-1.0f, 0.75f);
    layout.anchorMax = FVector2(0.25f, 2.0f);
    layout.pivot = FVector2(-2.0f, 4.0f);
    child.setAnchorLayout(layout);

    const AnchorLayout& normalized = child.getAnchorLayout();
    CHECK_FLOAT_EQ(normalized.anchorMin.x, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(normalized.anchorMin.y, 0.75f, 1e-5f);
    CHECK_FLOAT_EQ(normalized.anchorMax.x, 0.25f, 1e-5f);
    CHECK_FLOAT_EQ(normalized.anchorMax.y, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(normalized.pivot.x, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(normalized.pivot.y, 1.0f, 1e-5f);
    child.clearAnchorLayout();
    CHECK(!child.hasAnchorLayout());
}

TEST_CASE(responsive_visibility_switches_at_parent_width_without_losing_authored_state) {
    Panel parent;
    Button child;
    parent.setSize({800.0f, 400.0f});
    parent.addChildExternal(&child);

    ResponsiveLayoutRule compact;
    compact.name = "Compact";
    compact.maxParentWidth = 600.0f;
    compact.visibility = ResponsiveVisibility::Hidden;
    child.setResponsiveLayoutRules({compact});

    CHECK(child.isAuthoredVisible());
    CHECK(child.isVisible());
    parent.setSize({390.0f, 400.0f});
    CHECK_FALSE(child.isVisible());
    CHECK(child.isAuthoredVisible());
    CHECK(child.getActiveResponsiveRuleIndex() == 0);

    parent.setSize({900.0f, 400.0f});
    CHECK(child.isVisible());
    CHECK(child.getActiveResponsiveRuleIndex() == -1);

    child.setVisible(false);
    compact.visibility = ResponsiveVisibility::Visible;
    child.setResponsiveLayoutRules({compact});
    parent.setSize({390.0f, 400.0f});
    CHECK(child.isVisible());
    parent.setSize({900.0f, 400.0f});
    CHECK_FALSE(child.isVisible());
    parent.removeChild(&child);
}

TEST_CASE(responsive_anchor_override_reverts_to_authored_anchor_outside_band) {
    Panel parent;
    Button child;
    parent.setSize({800.0f, 400.0f});
    child.setPosition({20.0f, 20.0f});
    child.setSize({100.0f, 32.0f});
    child.setLayoutPositionManaged(false);
    child.setLayoutSizeManaged(false);
    parent.addChildExternal(&child);
    child.setAnchorLayoutPreservingRect({0.0f, 0.0f}, {0.0f, 0.0f});

    ResponsiveLayoutRule compact;
    compact.name = "Compact";
    compact.maxParentWidth = 600.0f;
    compact.overrideAnchors = true;
    compact.anchors.anchorMin = {1.0f, 0.0f};
    compact.anchors.anchorMax = {1.0f, 0.0f};
    compact.anchors.offsetMin = {-120.0f, 20.0f};
    compact.anchors.offsetMax = {-20.0f, 52.0f};
    child.setResponsiveLayoutRules({compact});

    parent.setSize({390.0f, 400.0f});
    CHECK_FLOAT_EQ(child.getPosition().x, 270.0f, 1e-5f);
    CHECK_FLOAT_EQ(child.getSize().x, 100.0f, 1e-5f);

    parent.setSize({800.0f, 400.0f});
    CHECK_FLOAT_EQ(child.getPosition().x, 20.0f, 1e-5f);
    CHECK_FLOAT_EQ(child.getSize().x, 100.0f, 1e-5f);
    parent.removeChild(&child);
}

TEST_CASE(responsive_visibility_gates_rendering_and_hit_testing) {
    Panel parent;
    Button child;
    parent.setSize({390.0f, 200.0f});
    child.setPosition({10.0f, 10.0f});
    child.setSize({100.0f, 32.0f});
    child.setLayoutPositionManaged(false);
    child.setLayoutSizeManaged(false);
    ResponsiveLayoutRule compact;
    compact.name = "Compact";
    compact.maxParentWidth = 600.0f;
    compact.visibility = ResponsiveVisibility::Hidden;
    child.setResponsiveLayoutRules({compact});
    parent.addChildExternal(&child);

    MockRenderer renderer;
    child.render(renderer);
    CHECK(renderer.getDrawCalls().empty());
    CHECK(parent.hitTest({20.0f, 20.0f}) == &parent);

    parent.setSize({800.0f, 200.0f});
    renderer.clear();
    child.render(renderer);
    CHECK_FALSE(renderer.getDrawCalls().empty());
    CHECK(parent.hitTest({20.0f, 20.0f}) == &child);
    parent.removeChild(&child);
}

TEST_CASE(responsive_rules_round_trip_through_layout_json) {
    auto* root = new Panel();
    root->setId("responsive_root");
    root->setSize({800.0f, 480.0f});
    auto* child = new Button();
    child->setId("responsive_button");
    child->setLayoutPositionManaged(false);
    child->setLayoutSizeManaged(false);
    child->setAnchorLayoutPreservingRect({0.0f, 0.0f}, {0.0f, 0.0f});
    ResponsiveLayoutRule rule;
    rule.name = "Compact";
    rule.maxParentWidth = 600.0f;
    rule.visibility = ResponsiveVisibility::Hidden;
    rule.overrideAnchors = true;
    rule.anchors = child->getAnchorLayout();
    child->setResponsiveLayoutRules({rule});
    root->addChild(child);

    UILayoutLoader loader;
    std::string encoded;
    CHECK(loader.saveLayoutToString(root, encoded, false));
    CHECK(encoded.find("\"responsive\"") != std::string::npos);
    Widget* loaded = loader.loadFromString(encoded);
    Widget* loadedChild = loader.findWidgetById("responsive_button");
    CHECK_NOT_NULL(loaded);
    CHECK_NOT_NULL(loadedChild);
    if (loaded != nullptr && loadedChild != nullptr) {
        CHECK(loadedChild->getResponsiveLayoutRules().size() == 1u);
        CHECK(loadedChild->getResponsiveLayoutRules()[0].name == "Compact");
        loaded->setSize({390.0f, 480.0f});
        CHECK_FALSE(loadedChild->isVisible());
    }
    destroyWidgetTree(root);
    destroyWidgetTree(loaded);
}

TEST_SUITE_END
