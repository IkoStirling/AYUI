#include "AYTest.h"
#include "AYUI/Image.h"
#include "AYUI/LeafWidget.h"
#include "AYUI/Widget.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Image)

TEST_CASE(image_initial_state) {
    Image image;
    // G10 — getTexture() now returns a typed ImageTextureHandle. The
    // default handle has handle=nullptr + name="" + width=0 + height=0.
    CHECK(image.getTexture().handle == nullptr);
    CHECK(image.getTextureName().empty());
    CHECK(!image.hasTexture());
    CHECK(image.getColor().x == 1.0f);  // default white
    CHECK(image.getColor().y == 1.0f);
    CHECK(image.getColor().z == 1.0f);
    CHECK(image.getColor().w == 1.0f);
}

TEST_CASE(image_set_texture) {
    Image image;
    void* tex = reinterpret_cast<void*>(0x12345678);
    image.setTexture(tex);   // legacy void* → anonymous typed handle
    CHECK(image.getTexture().handle == tex);
    CHECK(image.getTextureName().empty());
    CHECK(image.hasTexture());
}

TEST_CASE(image_set_color) {
    Image image;
    FVector4 color(0.5f, 0.3f, 0.8f, 1.0f);
    image.setColor(color);
    CHECK(image.getColor().x == 0.5f);
    CHECK(image.getColor().y == 0.3f);
    CHECK(image.getColor().z == 0.8f);
    CHECK(image.getColor().w == 1.0f);
}

TEST_CASE(image_set_uv) {
    Image image;
    FRectangle uv(0.0f, 0.0f, 1.0f, 1.0f);
    image.setUV(uv);

    const FRectangle& storedUV = image.getUV();
    CHECK(storedUV.minX == 0.0f);
    CHECK(storedUV.minY == 0.0f);
    CHECK(storedUV.maxX == 1.0f);
    CHECK(storedUV.maxY == 1.0f);
}

TEST_CASE(image_partial_uv) {
    Image image;
    FRectangle uv(0.25f, 0.25f, 0.75f, 0.75f);
    image.setUV(uv);

    const FRectangle& storedUV = image.getUV();
    CHECK(storedUV.minX == 0.25f);
    CHECK(storedUV.minY == 0.25f);
    CHECK(storedUV.maxX == 0.75f);
    CHECK(storedUV.maxY == 0.75f);
}

// Image used to extend CompoundWidget — a violation of the R-6 invariant
// ("leaf widgets MUST NOT host children"). Image is a pure leaf renderer:
// no children, no child layout, no hit-test descent. It now extends
// LeafWidget (which is a Widget subclass with a no-op performLayout).
TEST_CASE(image_extends_leafwidget_not_compoundwidget) {
    Image image;
    CHECK(dynamic_cast<LeafWidget*>(&image) != nullptr);
    CHECK(dynamic_cast<CompoundWidget*>(&image) == nullptr);
    // And transitively it's a Widget (LeafWidget extends Widget).
    CHECK(dynamic_cast<Widget*>(&image) != nullptr);
}

// Image's performLayout is the inherited LeafWidget no-op — explicit
// override was removed. Calling it must not crash and must not mutate
// the existing bounds.
TEST_CASE(image_perform_layout_inherited_noop) {
    Image image;
    image.setSize({64.0f, 48.0f});
    image.setPosition({10.0f, 20.0f});

    image.performLayout();

    CHECK(image.getWidth() == 64.0f);
    CHECK(image.getHeight() == 48.0f);
    CHECK(image.getPosition().x == 10.0f);
    CHECK(image.getPosition().y == 20.0f);
}

TEST_SUITE_END