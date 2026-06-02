#include "AYTest.h"
#include "AYImage.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Image)

TEST_CASE(image_initial_state) {
    Image image;
    CHECK(image.getTexture() == nullptr);
    CHECK(image.getColor().x == 1.0f);  // default white
    CHECK(image.getColor().y == 1.0f);
    CHECK(image.getColor().z == 1.0f);
    CHECK(image.getColor().w == 1.0f);
}

TEST_CASE(image_set_texture) {
    Image image;
    void* tex = reinterpret_cast<void*>(0x12345678);
    image.setTexture(tex);
    CHECK(image.getTexture() == tex);
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

TEST_SUITE_END