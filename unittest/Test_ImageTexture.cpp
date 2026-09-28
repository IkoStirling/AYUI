#include "AYTest.h"
#include "AYUI/ImageTexture.h"
#include "AYUI/TextureRegistry.h"
#include "AYUI/Image.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/WidgetSerializer.h"

#include <cstring>
#include <vector>

using namespace ayt::ui;
using namespace ayt::math;

// G10 test suite. These tests intentionally clear the registry / global
// callback at the start of each case so order-independence holds —
// without that, a prior case's named entry would survive across the
// suite and inflate refcounts.

namespace {

// Per-test bookkeeping for release callbacks. Tests push entries into
// these vectors and the global Image::setReleaseCallback and the
// TextureRegistry::setReleaseCallback both append into them.
struct ReleasedEntry {
    std::string name;
    void* handle = nullptr;
    int width = 0;
    int height = 0;
};
std::vector<ReleasedEntry>& releasedByImage() {
    static std::vector<ReleasedEntry> v;
    return v;
}
std::vector<ReleasedEntry>& releasedByRegistry() {
    static std::vector<ReleasedEntry> v;
    return v;
}

void resetG10State() {
    Image::clearReleaseCallback();
    Image::setReleaseCallback([](const ImageTextureHandle& h) {
        ReleasedEntry e;
        e.name = h.name;
        e.handle = h.handle;
        e.width = h.width;
        e.height = h.height;
        releasedByImage().push_back(e);
    });
    TextureRegistry::get().setReleaseCallback(
        [](const ImageTextureHandle& h) {
            ReleasedEntry e;
            e.name = h.name;
            e.handle = h.handle;
            e.width = h.width;
            e.height = h.height;
            releasedByRegistry().push_back(e);
        });
    releasedByImage().clear();
    releasedByRegistry().clear();
    TextureRegistry::get().clearAll();
}

} // anon

TEST_SUITE(AYUI_ImageTexture_G10)

TEST_CASE(imagetexture_handle_pod_defaults_to_empty) {
    ImageTextureHandle h;
    CHECK(h.handle == nullptr);
    CHECK(h.width  == 0);
    CHECK(h.height == 0);
    CHECK(h.format == TextureFormat::RGBA8);
    CHECK(h.name.empty());
    CHECK(h.generation == 0u);
    CHECK(!h.isValid());
    CHECK(!h.isNamed());
}

TEST_CASE(imagetexture_registry_acquire_creates_entry_with_refcount_one) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    ImageTextureHandle h = r.acquire("foo");
    CHECK(r.has("foo"));
    CHECK(r.refcount("foo") == 1);
    CHECK(h.name == "foo");
    CHECK(h.handle == nullptr);   // not registered yet
}

TEST_CASE(imagetexture_registry_acquire_same_name_increments_refcount) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    r.acquire("foo");
    r.acquire("foo");
    r.acquire("foo");
    CHECK(r.refcount("foo") == 3);
}

TEST_CASE(imagetexture_registry_release_decrements_refcount) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    r.acquire("foo");
    r.acquire("foo");
    r.release("foo");
    CHECK(r.refcount("foo") == 1);
    CHECK(r.has("foo"));
}

TEST_CASE(imagetexture_registry_release_last_erases_entry) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    r.acquire("foo");
    r.release("foo");
    CHECK(!r.has("foo"));
    CHECK(r.refcount("foo") == 0);
    // Release callback only fires when there's a live handle; here the
    // entry had handle=null so nothing should have been logged.
    CHECK(releasedByRegistry().empty());
}

TEST_CASE(imagetexture_registry_release_unknown_is_noop) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    // Should not crash, should not throw, should not log anything.
    r.release("nonexistent");
    CHECK(!r.has("nonexistent"));
    CHECK(releasedByRegistry().empty());
}

TEST_CASE(imagetexture_registry_register_external_attaches_handle) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    void* fakePtr = reinterpret_cast<void*>(0xCAFEBABEull);
    r.registerExternal("icon", fakePtr, 64, 48, TextureFormat::RGBA8);
    r.acquire("icon");
    ImageTextureHandle h = r.acquire("icon");
    CHECK(h.handle == fakePtr);
    CHECK(h.width  == 64);
    CHECK(h.height == 48);
    CHECK(h.format == TextureFormat::RGBA8);
    CHECK(h.name   == "icon");
    CHECK(r.refcount("icon") == 2);
}

TEST_CASE(image_set_named_texture_uses_registry) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    void* fakePtr = reinterpret_cast<void*>(0xFEEDFACEull);
    r.registerExternal("ui/icon_save", fakePtr, 32, 32, TextureFormat::RGBA8);
    Image img;
    img.setTexture(std::string("ui/icon_save"));
    CHECK(img.hasTexture());
    CHECK(img.getTexture().handle == fakePtr);
    CHECK(img.getTexture().name   == "ui/icon_save");
    CHECK(img.getTexture().width  == 32);
    CHECK(img.getTexture().height == 32);
    CHECK(r.refcount("ui/icon_save") == 1);
}

TEST_CASE(image_setting_same_named_texture_is_idempotent) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    void* fakePtr = reinterpret_cast<void*>(0xFEED1234ull);
    r.registerExternal("ui/idempotent", fakePtr, 32, 32, TextureFormat::RGBA8);
    Image img;
    img.setTexture(std::string("ui/idempotent"));
    const uint64_t generation = img.getTexture().generation;
    img.setTexture(std::string("ui/idempotent"));
    CHECK(r.refcount("ui/idempotent") == 1u);
    CHECK(img.getTexture().handle == fakePtr);
    CHECK(img.getTexture().generation == generation);
    CHECK(releasedByRegistry().empty());
}

TEST_CASE(image_dtor_releases_named_texture_via_callback) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    void* fakePtr = reinterpret_cast<void*>(0xDEADBEEFull);
    r.registerExternal("icon_dtor", fakePtr, 16, 16, TextureFormat::R8);
    {
        Image img;
        img.setTexture(std::string("icon_dtor"));
        CHECK(r.refcount("icon_dtor") == 1);
    }
    // Image went out of scope → registry release fired (refcount 1→0)
    // → entry erased → registry callback fires once with the handle.
    CHECK(!r.has("icon_dtor"));
    CHECK(releasedByRegistry().size() == 1);
    CHECK(releasedByRegistry()[0].name == "icon_dtor");
    CHECK(releasedByRegistry()[0].handle == fakePtr);
}

TEST_CASE(image_dtor_releases_anonymous_texture_via_callback) {
    resetG10State();
    void* fakePtr = reinterpret_cast<void*>(0x12345678ull);
    {
        Image img;
        img.setTexture(fakePtr);   // legacy void* → anonymous handle
        CHECK(img.hasTexture());
        CHECK(img.getTextureName().empty());
        CHECK(img.getTexture().handle == fakePtr);
    }
    // Image out of scope → ~Image fires global callback for anon handle.
    CHECK(releasedByImage().size() == 1);
    CHECK(releasedByImage()[0].handle == fakePtr);
    CHECK(releasedByImage()[0].name.empty());
}

TEST_CASE(image_setting_same_anonymous_texture_does_not_release_live_handle) {
    resetG10State();
    void* fakePtr = reinterpret_cast<void*>(0x12344321ull);
    {
        Image img;
        img.setTexture(fakePtr);
        img.setTexture(fakePtr);
        CHECK(releasedByImage().empty());
        CHECK(img.getTexture().handle == fakePtr);
    }
    CHECK(releasedByImage().size() == 1u);
    CHECK(releasedByImage()[0].handle == fakePtr);
}

TEST_CASE(image_factory_and_serializer_round_trip_texture_name) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    // Pre-register so the serializer-side setTexture(name) finds a match.
    void* fakePtr = reinterpret_cast<void*>(0xABCDEF01ull);
    r.registerExternal("ui/icon_round", fakePtr, 64, 48, TextureFormat::RGBA8);

    // Build an Image with a named texture, serialize, deserialize, check.
    Image original;
    original.setTexture(std::string("ui/icon_round"));
    original.setSize(FVector2(64.0f, 48.0f));
    original.setPosition(FVector2(10.0f, 20.0f));

    std::string json = WidgetSerializer::serializeWidget(&original);
    CHECK(!json.empty());
    // The JSON should mention the texture name + dimensions.
    CHECK(json.find("ui/icon_round") != std::string::npos);
    CHECK(json.find("textureWidth") != std::string::npos);
    CHECK(json.find("64") != std::string::npos);
    CHECK(json.find("48") != std::string::npos);

    Widget* deserialized = WidgetSerializer::deserialize(json);
    CHECK(deserialized != nullptr);
    Image* roundTripped = dynamic_cast<Image*>(deserialized);
    CHECK(roundTripped != nullptr);
    CHECK(roundTripped->getTextureName() == "ui/icon_round");
    // Width/height travel with the JSON; the registry's entry has them
    // but setTexture(name) picks them up via the ImageTextureHandle copy.
    CHECK(roundTripped->getTexture().width  == 64);
    CHECK(roundTripped->getTexture().height == 48);
    CHECK(roundTripped->getTexture().handle == fakePtr);
    delete deserialized;
}

TEST_CASE(image_render_emits_texture_handle_to_backend) {
    resetG10State();
    TextureRegistry& r = TextureRegistry::get();
    void* fakePtr = reinterpret_cast<void*>(0xFACEFEEDull);
    r.registerExternal("icon_render", fakePtr, 32, 32, TextureFormat::RGBA8);

    Image img;
    img.setTexture(std::string("icon_render"));
    img.setSize(FVector2(32.0f, 32.0f));
    img.setPosition(FVector2(0.0f, 0.0f));
    img.performLayout();

    MockRenderer renderer;
    img.render(renderer);

    // MockRenderer records one DrawCall per drawRect(textured). Find it.
    const MockRenderer::DrawCall* found = nullptr;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Image ||
            (dc.type == MockRenderer::DrawCall::Rect && dc.texture != nullptr)) {
            if (dc.texture == fakePtr) {
                found = &dc;
                break;
            }
        }
    }
    CHECK(found != nullptr);
    if (found) {
        CHECK(found->bounds.maxX - found->bounds.minX == 32.0f);
        CHECK(found->bounds.maxY - found->bounds.minY == 32.0f);
    }
}

TEST_CASE(image_late_registration_rebuilds_retained_texture_command) {
    resetG10State();
    TextureRegistry& registry = TextureRegistry::get();
    Image image;
    image.setTexture(std::string("late_icon"));
    image.setSize(FVector2(32.0f, 32.0f));
    MockRenderer renderer;
    image.render(renderer);
    CHECK_FALSE(image.hasTexture());

    void* late = reinterpret_cast<void*>(0x11112222ull);
    registry.registerExternal("late_icon", late, 32, 32, TextureFormat::RGBA8);
    renderer.beginFrame();
    image.render(renderer);

    bool sawLate = false;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.texture == late) sawLate = true;
    }
    CHECK(sawLate);
    CHECK(image.getTexture().handle == late);
}

TEST_CASE(image_hot_reload_releases_old_and_submits_new_generation) {
    resetG10State();
    TextureRegistry& registry = TextureRegistry::get();
    void* first = reinterpret_cast<void*>(0x33334444ull);
    void* second = reinterpret_cast<void*>(0x55556666ull);
    registry.registerExternal("hot_icon", first, 16, 16, TextureFormat::RGBA8);
    Image image;
    image.setTexture(std::string("hot_icon"));
    image.setSize(FVector2(16.0f, 16.0f));
    MockRenderer renderer;
    image.render(renderer);
    const uint64_t firstGeneration = image.getTexture().generation;

    registry.registerExternal("hot_icon", second, 24, 24, TextureFormat::RGBA8);
    CHECK(releasedByRegistry().size() == 1u);
    CHECK(releasedByRegistry()[0].handle == first);
    renderer.beginFrame();
    image.render(renderer);

    CHECK(image.getTexture().handle == second);
    CHECK(image.getTexture().generation > firstGeneration);
    bool sawSecond = false;
    bool sawFirst = false;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.texture == second) sawSecond = true;
        if (dc.texture == first) sawFirst = true;
    }
    CHECK(sawSecond);
    CHECK_FALSE(sawFirst);
}

TEST_SUITE_END
