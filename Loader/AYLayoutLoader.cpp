#include "AYUI/LayoutLoader.h"
#include "../Controls/DockJsonImpl.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/I18n.h"
#include "AYUI/Button.h"
#include "AYUI/TextLabel.h"
#include "AYUI/TextInput.h"
#include "AYUI/TextArea.h"
#include "AYUI/Tooltip.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuBar.h"
#include "AYUI/MenuItem.h"
#include "AYUI/Window.h"
#include "AYUI/Box.h"
#include "AYUI/GridPanel.h"
#include "AYUI/SplitterHandle.h"

using nlohmann::json;
#include "AYUI/Image.h"
#include "AYUI/ComboBox.h"
#include "AYUI/ListView.h"
#include "AYUI/TileView.h"
#include "AYUI/ScrollView.h"
#include "AYUI/Slider.h"
#include "AYUI/CheckBox.h"
#include "AYUI/RadioButton.h"
#include "AYUI/InteractiveWidget.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/Panel.h"
#include "AYUI/RichText.h"
#include "AYUI/Separator.h"
#include "AYUI/TabControl.h"
#include "AYUI/TabStrip.h"
#include "AYUI/TreeView.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/Dimmer.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"
#include "AYUI/UIAnimation.h"

#include "AYUI/WidgetSerializer.h"
#include "AYUI/LayoutLoader.h"

#include <AYIO/FileWatcher.h>

#include <fstream>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <codecvt>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

#if defined(_DEBUG) && defined(_MSC_VER)
#  include <crtdbg.h>
#endif

namespace {

std::atomic<std::uint64_t> gLoaderHeapValidationCount{0};

std::uint64_t loaderHeapValidationCount() noexcept
{
    return gLoaderHeapValidationCount.load(std::memory_order_relaxed);
}

#if defined(_DEBUG) && defined(_MSC_VER)

bool loaderHeapCheckEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("AY_UI_LOADER_HEAP_CHECK");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

void loaderHeapCheck(const char* label)
{
    gLoaderHeapValidationCount.fetch_add(1, std::memory_order_relaxed);
    if (!_CrtCheckMemory()) {
        // Headless UnitTests hang on _CrtDbgBreak's dialog — log only.
        std::fprintf(stderr, "[LoaderHeapCheck] FAIL at %s\n", label);
    }
}

void loaderHeapCheckId(const char* prefix, const char* parentId, const char* childId)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s_%s_%s", prefix, parentId, childId);
    loaderHeapCheck(buf);
}

#  define LOADER_HEAP_CHECK(label)                                      \
      do {                                                               \
          if (loaderHeapCheckEnabled()) loaderHeapCheck(label);          \
      } while (false)
#  define LOADER_HEAP_CHECK_ID(prefix, parentId, childId)                \
      do {                                                               \
          if (loaderHeapCheckEnabled()) {                                \
              loaderHeapCheckId((prefix), (parentId), (childId));        \
          }                                                              \
      } while (false)
#  define LOADER_HEAP_CHECK_ATTACH(parentId, childId)                    \
      LOADER_HEAP_CHECK_ID("after_attach", (parentId), (childId))
#else
#  define LOADER_HEAP_CHECK(label) ((void)0)
#  define LOADER_HEAP_CHECK_ID(prefix, parentId, childId) ((void)0)
#  define LOADER_HEAP_CHECK_ATTACH(parentId, childId) ((void)0)
#endif

} // namespace

namespace ayt::ui {

namespace {

std::wstring utf8ToWide(const std::string& text)
{
    if constexpr (sizeof(wchar_t) == 2) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(text);
    } else {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(text);
    }
}

bool readColor4(const json& value, math::FVector4& out)
{
    if (!value.is_array() || value.size() != 4u) return false;
    for (const auto& channel : value) {
        if (!channel.is_number()) return false;
    }
    out = math::FVector4(
        value[0].get<float>(), value[1].get<float>(),
        value[2].get<float>(), value[3].get<float>());
    return true;
}

BoxSlotLimits parseHBoxSlotLimits(const json& childJson, float& outWidth)
{
    BoxSlotLimits limits;
    outWidth = 0.0f;
    if (childJson.contains("size") && childJson["size"].is_object()) {
        outWidth = childJson["size"].value("w", 0.0f);
    }
    if (!childJson.contains("slot") || !childJson["slot"].is_object()) {
        return limits;
    }

    const json& slot = childJson["slot"];
    if (slot.contains("width")) {
        outWidth = slot["width"].get<float>();
    }
    limits.minWidth = slot.value("minWidth", 0.0f);
    limits.maxWidth = slot.value("maxWidth", 0.0f);
    limits.minWidthPercent = slot.value("minWidthPercent", 0.0f);
    limits.maxWidthPercent = slot.value("maxWidthPercent", 0.0f);
    return limits;
}

// L4 — parseBoxGravity. JSON shape: "TopLeft" / "TopCenter" / "TopRight" /
// "CenterLeft" / "Center" / "CenterRight" / "BottomLeft" / "BottomCenter" /
// "BottomRight". Unknown / missing = TopLeft (default).
BoxBase::Gravity parseBoxGravity(const std::string& s)
{
    if (s == "TopCenter")    return BoxBase::Gravity::TopCenter;
    if (s == "TopRight")     return BoxBase::Gravity::TopRight;
    if (s == "CenterLeft")   return BoxBase::Gravity::CenterLeft;
    if (s == "Center")       return BoxBase::Gravity::Center;
    if (s == "CenterRight")  return BoxBase::Gravity::CenterRight;
    if (s == "BottomLeft")   return BoxBase::Gravity::BottomLeft;
    if (s == "BottomCenter") return BoxBase::Gravity::BottomCenter;
    if (s == "BottomRight")  return BoxBase::Gravity::BottomRight;
    return BoxBase::Gravity::TopLeft;
}

ResponsiveVisibility parseResponsiveVisibility(const std::string& value)
{
    if (value == "visible" || value == "show")
        return ResponsiveVisibility::Visible;
    if (value == "hidden" || value == "hide")
        return ResponsiveVisibility::Hidden;
    return ResponsiveVisibility::Inherit;
}

// L3 — parseGridHAlign / parseGridVAlign. JSON shape: "Left" / "Center" /
// "Right" / "Fill" for HAlign; "Top" / "Middle" / "Bottom" / "Fill" for
// VAlign. Unknown / missing = Fill (default = stretch to cell).
GridPanel::HAlign parseGridHAlign(const std::string& s)
{
    if (s == "Left")   return GridPanel::HAlign::Left;
    if (s == "Center") return GridPanel::HAlign::Center;
    if (s == "Right")  return GridPanel::HAlign::Right;
    return GridPanel::HAlign::Fill;
}

GridPanel::VAlign parseGridVAlign(const std::string& s)
{
    if (s == "Top")    return GridPanel::VAlign::Top;
    if (s == "Middle") return GridPanel::VAlign::Middle;
    if (s == "Bottom") return GridPanel::VAlign::Bottom;
    return GridPanel::VAlign::Fill;
}

// D2 — DockArea JSON bridge. Editor shells persist their layout via
// DockArea, so the loader needs to (1) apply slot weights/min sizes,
// (2) add cards with optional content subtrees (recursive deserialize
// via WidgetSerializer), and (3) round-trip floating cards with their
// frame coords. The deserializer-side wire format is documented in
// `ay-ui.md` §D2.
namespace {

// Apply a sparse slot-weights / slot-min-sizes map. Missing slots
// keep the header-defined defaults (so a JSON file that only sets
// `slotWeights.Left` doesn't reset the rest).
void applySlotWeightMap(DockArea& area, const json& wj) {
    if (!wj.is_object()) return;
    for (auto it = wj.begin(); it != wj.end(); ++it) {
        DockArea::Slot slot;
        if (!DockArea::parseSlot(it.key(), slot)) continue;
        if (!it.value().is_number()) continue;
        area.setSlotWeight(slot, it.value().get<float>());
    }
}

void applySlotMinSizeMap(DockArea& area, const json& mj) {
    if (!mj.is_object()) return;
    for (auto it = mj.begin(); it != mj.end(); ++it) {
        DockArea::Slot slot;
        if (!DockArea::parseSlot(it.key(), slot)) continue;
        if (!it.value().is_number()) continue;
        area.setSlotMinSize(slot, it.value().get<float>());
    }
}

} // namespace
} // namespace

UILayoutLoader::UILayoutLoader()
    : _factory(&WidgetFactory::get())
    , _i18n(&I18n::get())
    , _watcher(std::make_unique<ayt::io::FileWatcher>())
{
}

UILayoutLoader::~UILayoutLoader() {
}

void UILayoutLoader::retranslate(Widget* root) const
{
    if (root == nullptr || !_textResolver) return;

    const auto resolve = [this, root](const char* property,
                                      const std::wstring& fallback) {
        const std::string& key = root->getLocalizationKey(property);
        return key.empty() ? fallback : _textResolver(key, fallback);
    };

    if (!root->getLocalizationKey("accessibilityLabel").empty()) {
        root->setAccessibilityLabel(resolve(
            "accessibilityLabel", root->getAccessibilityLabel()));
    }
    if (!root->getLocalizationKey("accessibilityDescription").empty()) {
        root->setAccessibilityDescription(resolve(
            "accessibilityDescription", root->getAccessibilityDescription()));
    }
    if (!root->getLocalizationKey("accessibilityValue").empty()) {
        root->setAccessibilityValue(resolve(
            "accessibilityValue", root->getAccessibilityValue()));
    }

    if (!root->getLocalizationKey("text").empty()) {
        if (auto* value = dynamic_cast<Button*>(root)) {
            value->setText(resolve("text", value->getText()));
        } else if (auto* value = dynamic_cast<TextLabel*>(root)) {
            value->setText(resolve("text", value->getText()));
        } else if (auto* value = dynamic_cast<CheckBox*>(root)) {
            value->setText(resolve("text", value->getText()));
        } else if (auto* value = dynamic_cast<RadioButton*>(root)) {
            value->setText(resolve("text", value->getText()));
        } else if (auto* value = dynamic_cast<TextInput*>(root)) {
            value->setText(resolve("text", value->getText()));
        } else if (auto* value = dynamic_cast<TextArea*>(root)) {
            value->setText(resolve("text", value->getText()));
        } else if (auto* value = dynamic_cast<Tooltip*>(root)) {
            value->setText(resolve("text", value->getText()));
        } else if (auto* value = dynamic_cast<MenuItem*>(root)) {
            value->setText(resolve("text", value->getText()));
        }
    }

    if (!root->getLocalizationKey("placeholder").empty()) {
        if (auto* value = dynamic_cast<TextInput*>(root)) {
            value->setPlaceholder(resolve(
                "placeholder", value->getPlaceholder()));
        }
    }

    if (!root->getLocalizationKey("title").empty()) {
        if (auto* value = dynamic_cast<DockCard*>(root)) {
            value->setTitle(resolve("title", value->getTitle()));
        } else if (auto* value = dynamic_cast<Window*>(root)) {
            value->setTitle(resolve("title", value->getTitle()));
        }
    }

    if (auto* dialog = dynamic_cast<ModalDialog*>(root)) {
        if (!root->getLocalizationKey("acceptText").empty()) {
            dialog->setAcceptText(resolve(
                "acceptText", dialog->getAcceptText()));
        }
        if (!root->getLocalizationKey("rejectText").empty()) {
            dialog->setRejectText(resolve(
                "rejectText", dialog->getRejectText()));
        }
    }

    const auto& itemKeys = root->getLocalizationKeys("items");
    if (!itemKeys.empty()) {
        const auto translateItems = [this, &itemKeys](
            const std::vector<std::wstring>& fallbacks) {
            std::vector<std::wstring> translated;
            translated.reserve(std::max(itemKeys.size(), fallbacks.size()));
            const std::size_t count = std::max(
                itemKeys.size(), fallbacks.size());
            for (std::size_t i = 0; i < count; ++i) {
                const std::wstring fallback = i < fallbacks.size()
                    ? fallbacks[i] : std::wstring{};
                translated.push_back(i < itemKeys.size()
                    && !itemKeys[i].empty()
                    ? _textResolver(itemKeys[i], fallback)
                    : fallback);
            }
            return translated;
        };

        if (auto* value = dynamic_cast<ComboBox*>(root)) {
            const int selected = value->getSelectedIndex();
            value->setItems(translateItems(value->getItemsRef()));
            value->setSelectedIndex(selected);
        } else if (auto* value = dynamic_cast<ListView*>(root)) {
            const std::vector<int> selected = value->getSelectedIndices();
            const int anchor = value->getAnchorIndex();
            const math::FVector2 scroll = value->getScrollOffset();
            value->setItems(translateItems(value->getItemsRef()));
            value->setSelectedIndices(selected);
            value->setAnchorIndex(anchor);
            value->setScrollOffset(scroll);
        } else if (auto* value = dynamic_cast<TileView*>(root)) {
            const std::vector<int> selected = value->getSelectedIndices();
            const int focused = value->getFocusedIndex();
            const math::FVector2 scroll = value->getScrollOffset();
            value->setItems(translateItems(value->getItemsRef()));
            value->setSelectedIndices(selected);
            value->setFocusedIndex(focused, false);
            value->setScrollOffset(scroll);
        }
    }

    for (Widget* child : root->getChildren()) retranslate(child);
}

std::wstring UILayoutLoader::resolveLocalizedString(
    JsonHandle h, std::string_view valueProperty,
    std::string_view keyProperty) const {
    const json& source = jsonRefConst(h);
    const std::string valueName(valueProperty);
    const std::string keyName(keyProperty);
    const std::string fallback = source.contains(valueName)
        && source[valueName].is_string()
        ? source[valueName].get<std::string>() : std::string{};
    const std::string key = source.contains(keyName)
        && source[keyName].is_string()
        ? source[keyName].get<std::string>() : std::string{};

    if (!key.empty()) {
        if (_textResolver) {
            return _textResolver(key, utf8ToWide(fallback));
        }
        if (_i18n != nullptr) {
            return utf8ToWide(_i18n->resolve(key, fallback));
        }
        return utf8ToWide(fallback);
    }
    // Audit H-S-4 (i18n auto-detect phishing): pre-fix the loader
    // auto-detected any `text` starting with "ui." as a translation
    // key. That allowed a hostile layout to silently route user-supplied
    // strings through i18n. Removed: `text` is now always a literal
    // fallback. Translations must be opted into explicitly via
    // `keyProperty` (e.g. textKey, titleKey, itemsKey) at the JSON
    // level, or via the TR() macro at C++ call sites.
    return utf8ToWide(fallback);
}

std::vector<std::wstring> UILayoutLoader::resolveLocalizedList(
    JsonHandle h, std::string_view valueProperty,
    std::string_view keyProperty) const {
    const json& source = jsonRefConst(h);
    const std::string valuesName(valueProperty);
    const std::string keysName(keyProperty);
    const json* values = source.contains(valuesName)
        && source[valuesName].is_array() ? &source[valuesName] : nullptr;
    const json* keys = source.contains(keysName)
        && source[keysName].is_array() ? &source[keysName] : nullptr;
    const std::size_t count = std::max(
        values != nullptr ? values->size() : 0u,
        keys != nullptr ? keys->size() : 0u);
    std::vector<std::wstring> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::string fallback = values != nullptr && i < values->size()
            && (*values)[i].is_string()
            ? (*values)[i].get<std::string>() : std::string{};
        const std::string key = keys != nullptr && i < keys->size()
            && (*keys)[i].is_string()
            ? (*keys)[i].get<std::string>() : std::string{};
        if (!key.empty() && _textResolver) {
            result.push_back(_textResolver(key, utf8ToWide(fallback)));
        } else if (!key.empty() && _i18n != nullptr) {
            result.push_back(utf8ToWide(_i18n->resolve(key, fallback)));
        } else {
            result.push_back(utf8ToWide(fallback));
        }
    }
    return result;
}

DockCard* UILayoutLoader::buildDockCardFromJson(JsonHandle h) {
    const json& cj = jsonRefConst(h);
    if (!cj.is_object()) return nullptr;
    auto card = std::make_unique<DockCard>();
    std::string id;
    if (cj.contains("id")) {
        id = cj["id"].get<std::string>();
        card->setId(id);
    }
    if (cj.contains("title") || cj.contains("titleKey")) {
        card->setTitle(resolveLocalizedString(h, "title", "titleKey"));
        if (cj.contains("titleKey") && cj["titleKey"].is_string()) {
            card->setLocalizationKey(
                "title", cj["titleKey"].get<std::string>());
        }
    }
    if (cj.contains("icon")) {
        card->setIcon(cj["icon"].get<std::string>());
    }
    if (cj.contains("closable"))  card->setClosable(cj["closable"].get<bool>());
    if (cj.contains("floatable")) card->setFloatable(cj["floatable"].get<bool>());
    if (cj.contains("collapsed")) card->setCollapsed(cj["collapsed"].get<bool>());
    if (cj.contains("headerHeight")) {
        card->setHeaderHeight(cj["headerHeight"].get<float>());
    }
    Widget* content = nullptr;
    if (cj.contains("content") && cj["content"].is_object()) {
        content = buildWidgetTree(JsonHandle(const_cast<json*>(&cj["content"])));
    }
    if (content != nullptr) {
        card->setContent(content);
    }
    DockCard* raw = card.release();
    if (!id.empty()) {
        _widgetsById[id] = raw;
    }
    return raw;
}

Widget* UILayoutLoader::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        _lastLoadStats = {};
        return nullptr;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    _lastJson = ss.str();
    _lastFilePath = filepath;

    // AYUI-Audit-2026-08-26 DoS: enforce a 64 MiB upper bound on the
    // raw JSON payload before delegating to loadFromString. nlohmann's
    // json::parse will happily attempt to materialize a multi-GiB
    // document, exhausting memory and stalling the UI thread. We
    // refuse early here (before the file watcher is also re-armed
    // below) and return nullptr so callers see the same shape as a
    // parse failure. 64 MiB is comfortably above the largest known
    // layout file (Gallery's editor shell ~200 KiB) and below the
    // single-allocation threshold for typical desktop heap budgets.
    constexpr size_t kMaxJsonBytes = 64ULL * 1024 * 1024;
    if (_lastJson.size() > kMaxJsonBytes) {
        _lastLoadStats = {};
        _lastLoadStats.inputBytes = _lastJson.size();
        std::fprintf(stderr,
            "[UILayoutLoader] rejecting oversized layout file '%s' "
            "(%zu bytes > 64 MiB cap)\n",
            filepath.c_str(), _lastJson.size());
        _lastJson.clear();
        _lastFilePath.clear();
        return nullptr;
    }

    // R-4: register (or refresh) the watch on this file. The watcher is the
    // canonical cross-platform filesystem notification (ReadDirectoryChangesW
    // on Windows, inotify on POSIX). Watching is single-file: events are
    // reported only when this exact path changes, so we don't have to filter
    // directory events for unrelated siblings.
    //
    // The watcher thread is started lazily on first watch — multiple loaders
    // each own a FileWatcher so they're independent. isReloadNeeded() on the
    // main thread drains the queue via pollPending.
    if (_watcher) {
        _watcher->unwatch(_lastFilePath);   // safe even if not previously watched
        _watcher->watch(_lastFilePath, nullptr);
        if (!_watcherStarted) {
            _watcher->start();
            _watcherStarted = true;
        }
        // A freshly reloaded file has no pending events for itself (the load
        // itself does not enqueue anything), but clear any stale dirty flag
        // left by a prior isReloadNeeded() call.
        _dirty = false;
    }

    return loadFromString(_lastJson);
}

void UILayoutLoader::stopHotReload() {
    if (_watcher && !_lastFilePath.empty()) {
        _watcher->unwatch(_lastFilePath);
    }
    _dirty = false;
}

Widget* UILayoutLoader::loadFromString(const std::string& jsonStr) {
    _lastLoadStats = {};
    _lastLoadStats.inputBytes = jsonStr.size();
    _buildDepth = 0;
    const std::uint64_t heapChecksBefore = loaderHeapValidationCount();
    const auto refreshHeapValidationCount = [&]() {
        _lastLoadStats.heapValidationCount =
            loaderHeapValidationCount() - heapChecksBefore;
    };

    // AYUI-Audit-2026-08-26 DoS: cap the raw payload at 64 MiB before
    // touching nlohmann. Mirrors the loadFromFile check so a malicious
    // or runaway layout JSON cannot exhaust memory regardless of which
    // entrypoint the caller used. Returning nullptr + logged error
    // matches the existing parse-failure contract; callers destroy
    // their previous tree (if any) on nullptr the same way.
    constexpr size_t kMaxJsonBytes = 64ULL * 1024 * 1024;
    if (jsonStr.size() > kMaxJsonBytes) {
        std::fprintf(stderr,
            "[UILayoutLoader] rejecting oversized layout JSON "
            "(%zu bytes > 64 MiB cap)\n", jsonStr.size());
        refreshHeapValidationCount();
        return nullptr;
    }

    // Code-review 2026-08-02 #15: do NOT clear _widgetsById before
    // buildWidgetTree. A throw mid-recursion (deeply nested + malformed
    // JSON) would otherwise wipe the previous load's id index AND leak
    // the partially-built widget tree (nlohmann's recursive build has
    // already allocated children but the throw skips cleanup).
    //
    // Strategy: keep the old index intact until we know the new tree
    // was built successfully. On success, swap in the new tree's index
    // (the root widget owns the new tree's lifetime). On failure, the
    // old index still resolves to the old widgets (caller is expected
    // to release them via destroyWidgetTree as before; we log + return
    // nullptr). The leaked partial tree is unavoidable from here
    // without a try/catch inside buildWidgetTree itself; deferring the
    // wipe is the highest-value half of the fix.
    std::unordered_map<std::string, Widget*> oldIndex;
    oldIndex.swap(_widgetsById);
    std::unordered_set<std::string> oldDeclarativeEventHandlers;
    oldDeclarativeEventHandlers.swap(_declarativeEventHandlers);
    UIAnimationLibrary oldAnimations = std::move(_animationLibrary);

    const auto parseStart = std::chrono::steady_clock::now();
    bool parseCompleted = false;
    auto buildStart = parseStart;
    bool buildStarted = false;
    try {
        json j = json::parse(jsonStr);
        const auto parseEnd = std::chrono::steady_clock::now();
        _lastLoadStats.parseMicroseconds =
            static_cast<std::uint64_t>(std::chrono::duration_cast<
                std::chrono::microseconds>(parseEnd - parseStart).count());
        parseCompleted = true;
        LOADER_HEAP_CHECK("after_json_parse");
        // Reusable authoring documents wrap the runtime root with a local
        // block library. Instances are expanded when inserted, so runtime
        // loading only needs the root and remains independent of the editor.
        json* rootJson = &j;
        UIAnimationLibrary decodedAnimations;
        if (j.is_object() && !j.contains("type") && j.contains("root") &&
            j["root"].is_object()) {
            rootJson = &j["root"];
            if (j.contains("animations")) {
                std::string animationError;
                if (!decodedAnimations.deserialize(
                        j["animations"].dump(), &animationError)) {
                    throw std::runtime_error(animationError);
                }
            }
        }
        buildStart = std::chrono::steady_clock::now();
        buildStarted = true;
        Widget* root = buildWidgetTree(JsonHandle(rootJson));
        _lastLoadStats.buildMicroseconds =
            static_cast<std::uint64_t>(std::chrono::duration_cast<
                std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - buildStart).count());
        _lastLoadStats.registeredIdCount = _widgetsById.size();
        if (root == nullptr) {
            refreshHeapValidationCount();
            _widgetsById = std::move(oldIndex);
            _declarativeEventHandlers =
                std::move(oldDeclarativeEventHandlers);
            _animationLibrary = std::move(oldAnimations);
            return nullptr;
        }
        LOADER_HEAP_CHECK("after_build_widget_tree");
        refreshHeapValidationCount();
        // Success: the new tree's buildWidgetTree path already populated
        // _widgetsById during recursion (the early-swap above restored
        // the old map's empty state). Build succeeded; commit the new
        // index by leaving it in place (we already wrote into it).
        // (Note: buildWidgetTree used _widgetsById after the swap, so
        // the new index IS what _widgetsById holds now.)
        (void)oldIndex;
        _animationLibrary = std::move(decodedAnimations);
        _lastLoadStats.succeeded = true;
        return root;
    }
    catch (const std::exception& e) {
        const auto failedAt = std::chrono::steady_clock::now();
        if (!parseCompleted) {
            _lastLoadStats.parseMicroseconds =
                static_cast<std::uint64_t>(std::chrono::duration_cast<
                    std::chrono::microseconds>(failedAt - parseStart).count());
        } else if (buildStarted) {
            _lastLoadStats.buildMicroseconds =
                static_cast<std::uint64_t>(std::chrono::duration_cast<
                    std::chrono::microseconds>(failedAt - buildStart).count());
        }
        _lastLoadStats.registeredIdCount = _widgetsById.size();
        refreshHeapValidationCount();
        std::fprintf(stderr, "[UILayoutLoader] parse error: %s\n", e.what());
        // Also dump to a fixed file so we can debug when stderr is
        // detached (Gallery AllocConsole + GUI apps lose stderr under
        // bash redirect).
        std::FILE* f = std::fopen("ayui_loader_error.txt", "w");
        if (f) {
            std::fprintf(f, "[UILayoutLoader] parse error: %s\n", e.what());
            std::fclose(f);
        }
        // Restore the old index so callers that hold pointers to the
        // previous tree can still find them. Wipe the (incomplete)
        // index that buildWidgetTree may have partially populated.
        _widgetsById = std::move(oldIndex);
        _declarativeEventHandlers = std::move(oldDeclarativeEventHandlers);
        _animationLibrary = std::move(oldAnimations);
        return nullptr;
    }
}

// D4 (2026-07-26): Layout persistence. We delegate the actual
// serialization to WidgetSerializer so the wire format stays single-sourced
// (the file format = the format loadFromString already understands). The
// loader class owns save because it already owns the loadFrom{File,String}
// pair + the FileWatcher — saving the layout to disk completes the "I
// edited my layout in a JSON file, hot-reloaded it, edited it more, want to
// commit those edits back" workflow without leaking the serializer type to
// callers.
bool UILayoutLoader::saveLayout(const std::string& filepath, Widget* root, bool pretty) {
    if (!root) return false;

    std::string jsonStr;
    if (!saveLayoutToString(root, jsonStr, pretty)) return false;

    return saveJsonDocument(filepath, jsonStr);
}

bool UILayoutLoader::saveJsonDocument(const std::string& filepath,
                                      const std::string& jsonStr) {
    if (filepath.empty() || jsonStr.empty()) return false;

    namespace fs = std::filesystem;
    const fs::path target = fs::u8path(filepath);
    static std::atomic<uint64_t> sequence{0};
    const uint64_t stamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path temporary = target;
    temporary += ".tmp." + std::to_string(stamp) + "." +
                 std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));

    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::fprintf(stderr, "[UILayoutLoader] save error: cannot open '%s' for write\n",
                     filepath.c_str());
        return false;
    }
    file.write(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
    file.flush();
    const bool writeOk = file.good();
    file.close();
    if (!writeOk || file.fail()) {
        std::fprintf(stderr, "[UILayoutLoader] save error: write to '%s' failed\n",
                     filepath.c_str());
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }

    bool replaced = false;
#if defined(_WIN32)
    replaced = ::MoveFileExW(
        temporary.c_str(), target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code renameError;
    fs::rename(temporary, target, renameError);
    replaced = !renameError;
#endif
    if (!replaced) {
        std::fprintf(stderr,
                     "[UILayoutLoader] save error: cannot atomically replace '%s'\n",
                     filepath.c_str());
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool UILayoutLoader::saveLayoutToString(Widget* root, std::string& outJson, bool pretty) {
    if (!root) {
        outJson.clear();
        return false;
    }
    outJson = WidgetSerializer::serialize(root, pretty);
    return !outJson.empty();
}

Widget* UILayoutLoader::reload(const std::string& id) {
    AYUNREFERENCED_PARAM(id);
    if (_lastFilePath.empty()) return nullptr;
    return loadFromFile(_lastFilePath);
}

bool UILayoutLoader::pollWatcherAndCheckDirty() {
    if (!_watcher) return false;

    std::vector<ayt::io::FileWatchEvent> events;
    _watcher->pollPending(events);
    if (events.empty()) {
        return _dirty;
    }

    // Any event whose path matches the loaded file (or the file's parent
    // directory, for OSes that report child paths) flips _dirty. The watcher
    // is registered for a single file so the path equality is exact, but we
    // also accept events for the parent dir to be safe across backends.
    for (const auto& ev : events) {
        if (ev.path == _lastFilePath) {
            _dirty = true;
            break;
        }
    }
    return _dirty;
}

bool UILayoutLoader::isReloadNeeded() {
    if (_lastFilePath.empty()) return false;
    return pollWatcherAndCheckDirty();
}

Widget* UILayoutLoader::tryReload() {
    if (!isReloadNeeded()) return nullptr;
    if (_lastFilePath.empty()) return nullptr;

    // Consume the dirty flag — caller will see _dirty cleared on next
    // loadFromFile() invocation (which re-arms the watcher).
    Widget* reloaded = loadFromFile(_lastFilePath);
    return reloaded;
}

void UILayoutLoader::bindEvent(const std::string& widgetId, const std::string& eventType,
                                std::function<void()> handler) {
    std::string key = widgetId + "." + eventType;
    _eventBindings[key] = handler;
}

void UILayoutLoader::bindControllerEvent(const std::string& controllerId,
                                         const std::string& handlerName,
                                         std::function<void()> handler) {
    _eventBindings["@controller." + controllerId + "." + handlerName] =
        std::move(handler);
}

void UILayoutLoader::bindHandler(const std::string& handlerName,
                                 std::function<void()> handler) {
    _eventBindings["@handler." + handlerName] = std::move(handler);
}

void UILayoutLoader::clearEventBindings() {
    _eventBindings.clear();
}

void UILayoutLoader::clearWidgetRegistry() {
    _widgetsById.clear();
    _declarativeEventHandlers.clear();
}

Widget* UILayoutLoader::findWidgetById(const std::string& id) const {
    auto it = _widgetsById.find(id);
    return (it != _widgetsById.end()) ? it->second : nullptr;
}

AnimationTimeline UILayoutLoader::createAnimationTimeline(
    const std::string& clipName,
    std::size_t* unresolvedTrackCount) const {
    return _animationLibrary.createTimeline(
        _animationLibrary.findClipIndex(clipName),
        [this](const std::string& id) { return findWidgetById(id); },
        unresolvedTrackCount);
}

Widget* UILayoutLoader::buildWidgetTree(JsonHandle h) {
    const json& j = jsonRefConst(h);
    if (!j.is_object()) return nullptr;

    struct BuildDepthScope {
        explicit BuildDepthScope(std::size_t& value) : depth(value) {
            ++depth;
        }
        ~BuildDepthScope() { --depth; }
        std::size_t& depth;
    } depthScope(_buildDepth);

    std::string type = j.value("type", "Widget");
    std::string id = j.value("id", "");

    Widget* widget = _factory->create(type);
    if (widget == nullptr) {
        std::fprintf(stderr, "[UILayoutLoader] missing factory creator for type='%s'\n",
                     type.c_str());
        return nullptr;
    }
    ++_lastLoadStats.widgetCount;
    _lastLoadStats.maxDepth = std::max(
        _lastLoadStats.maxDepth, _buildDepth);
    LOADER_HEAP_CHECK_ID("after_factory_create", type.c_str(),
                         id.empty() ? "anonymous" : id.c_str());

    // ID
    if (!id.empty()) {
        widget->setId(id);
        // Code-review 2026-08-02 #14: warn on duplicate registration so
        // hand-edited layout mistakes (parent + child sharing an id)
        // surface in the editor's stderr instead of silently making
        // findWidgetById return the wrong node. Last-wins is preserved.
        if (_widgetsById.find(id) != _widgetsById.end()) {
            std::fprintf(stderr,
                "[UILayoutLoader] duplicate id '%s' in type='%s' "
                "(last write wins; previous widget will no longer be "
                "findable by id)\n",
                id.c_str(), type.c_str());
        }
        _widgetsById[id] = widget;
    }

    // Position — only axes present in JSON are applied. Pinning requires both
    // x and y so a single-axis value (e.g. toolbar offset) does not block
    // parent layout from managing the other axis.
    if (j.contains("position") && j["position"].is_object()) {
        const json& posJson = j["position"];
        const bool hasX = posJson.contains("x");
        const bool hasY = posJson.contains("y");
        math::FVector2 pos = widget->getPosition();
        if (hasX) {
            pos.x = posJson["x"].get<float>();
        }
        if (hasY) {
            pos.y = posJson["y"].get<float>();
        }
        if (hasX || hasY) {
            widget->setPosition(pos);
        }
        if (hasX && hasY) {
            widget->setLayoutPositionManaged(false);
        }
    }

    // Size — same per-axis rule: `"size": { "h": 44 }` sets height only and
    // leaves width layout-managed so VBox/HBox can stretch the child.
    if (j.contains("size") && j["size"].is_object()) {
        const json& sizeJson = j["size"];
        const bool hasW = sizeJson.contains("w");
        const bool hasH = sizeJson.contains("h");
        math::FVector2 size = widget->getSize();
        if (hasW) {
            size.x = sizeJson["w"].get<float>();
        }
        if (hasH) {
            size.y = sizeJson["h"].get<float>();
        }
        if (hasW || hasH) {
            widget->setSize(size);
        }
        if (hasW && hasH) {
            widget->setLayoutSizeManaged(false);
        }
        LOADER_HEAP_CHECK("after_set_size");
    }

    // Responsive free-layout anchors. Offsets are signed deltas from the
    // normalized parent anchor points to the child's near/far edges. Their
    // presence makes both axes anchor-managed rather than Box/Grid-managed.
    if (j.contains("anchors") && j["anchors"].is_object()) {
        const json& anchors = j["anchors"];
        AnchorLayout layout;
        auto readVec2 = [&anchors](const char* key,
                                   const math::FVector2& fallback) {
            math::FVector2 value = fallback;
            if (anchors.contains(key) && anchors[key].is_object()) {
                value.x = anchors[key].value("x", value.x);
                value.y = anchors[key].value("y", value.y);
            }
            return value;
        };
        layout.anchorMin = readVec2("min", layout.anchorMin);
        layout.anchorMax = readVec2("max", layout.anchorMax);
        layout.offsetMin = readVec2("offsetMin", layout.offsetMin);
        layout.offsetMax = readVec2("offsetMax", layout.offsetMax);
        layout.pivot = readVec2("pivot", layout.pivot);
        widget->setAnchorLayout(layout);
        widget->setLayoutPositionManaged(false);
        widget->setLayoutSizeManaged(false);
    }

    if (j.contains("responsive") && j["responsive"].is_array()) {
        std::vector<ResponsiveLayoutRule> rules;
        for (const json& value : j["responsive"]) {
            if (!value.is_object()) continue;
            ResponsiveLayoutRule rule;
            rule.name = value.value("name", std::string{});
            rule.minParentWidth = value.value("minWidth", 0.0f);
            rule.maxParentWidth = value.value("maxWidth", 0.0f);
            rule.visibility = parseResponsiveVisibility(
                value.value("visibility", std::string{"inherit"}));
            if (value.contains("anchors") && value["anchors"].is_object()) {
                const json& anchors = value["anchors"];
                auto readVec2 = [&anchors](const char* key,
                                           const math::FVector2& fallback) {
                    math::FVector2 result = fallback;
                    if (anchors.contains(key) && anchors[key].is_object()) {
                        result.x = anchors[key].value("x", result.x);
                        result.y = anchors[key].value("y", result.y);
                    }
                    return result;
                };
                rule.anchors.anchorMin = readVec2(
                    "min", rule.anchors.anchorMin);
                rule.anchors.anchorMax = readVec2(
                    "max", rule.anchors.anchorMax);
                rule.anchors.offsetMin = readVec2(
                    "offsetMin", rule.anchors.offsetMin);
                rule.anchors.offsetMax = readVec2(
                    "offsetMax", rule.anchors.offsetMax);
                rule.anchors.pivot = readVec2(
                    "pivot", rule.anchors.pivot);
                rule.overrideAnchors = true;
            }
            rules.push_back(std::move(rule));
        }
        widget->setResponsiveLayoutRules(std::move(rules));
    }

    // Visible
    widget->setVisible(j.value("visible", true));

    const auto preserveKey = [&j, widget](const char* property) {
        const std::string keyProperty = std::string(property) + "Key";
        if (j.contains(keyProperty) && j[keyProperty].is_string()) {
            widget->setLocalizationKey(
                property, j[keyProperty].get<std::string>());
        }
    };
    preserveKey("text");
    preserveKey("title");
    preserveKey("acceptText");
    preserveKey("rejectText");
    preserveKey("accessibilityLabel");
    preserveKey("accessibilityDescription");
    preserveKey("accessibilityValue");
    if (j.contains("itemsKey") && j["itemsKey"].is_array()) {
        std::vector<std::string> keys;
        keys.reserve(j["itemsKey"].size());
        for (const auto& value : j["itemsKey"]) {
            keys.push_back(value.is_string()
                ? value.get<std::string>() : std::string{});
        }
        widget->setLocalizationKeys("items", std::move(keys));
    }

    widget->setAccessibilityHidden(j.value("accessibilityHidden", false));
    const std::string accessibilityLive = j.value("accessibilityLive", "off");
    widget->setAccessibilityLiveSetting(accessibilityLive == "assertive"
        ? AccessibilityLiveSetting::Assertive
        : (accessibilityLive == "polite" ? AccessibilityLiveSetting::Polite
                                          : AccessibilityLiveSetting::Off));
    if (j.contains("accessibilityLabel")
        || j.contains("accessibilityLabelKey")) {
        widget->setAccessibilityLabel(resolveLocalizedString(
            h, "accessibilityLabel", "accessibilityLabelKey"));
    }
    if (j.contains("accessibilityDescription")
        || j.contains("accessibilityDescriptionKey")) {
        widget->setAccessibilityDescription(resolveLocalizedString(
            h, "accessibilityDescription", "accessibilityDescriptionKey"));
    }
    if (j.contains("accessibilityValue")
        || j.contains("accessibilityValueKey")) {
        widget->setAccessibilityValue(resolveLocalizedString(
            h, "accessibilityValue", "accessibilityValueKey"));
    }

    // Style
    std::string style = j.value("style", "");
    if (!style.empty()) {
        widget->setStyleId(style);
    }

    // Declarative interaction metadata is retained even when the current
    // host has not registered a controller. This is essential for editor
    // round-trips and lets tools inspect a layout without game code loaded.
    widget->setControllerId(j.value("controller", ""));
    if (j.contains("events") && j["events"].is_object()) {
        for (auto it = j["events"].begin(); it != j["events"].end(); ++it) {
            if (it.value().is_string()) {
                const std::string handler = it.value().get<std::string>();
                widget->setEventBinding(it.key(), handler);
                if (!handler.empty()) {
                    _declarativeEventHandlers.insert(handler);
                }
            }
        }
    }
    if (j.contains("onClick") && j["onClick"].is_string() &&
        widget->getEventBinding("onClick").empty()) {
        const std::string handler = j["onClick"].get<std::string>();
        widget->setEventBinding("onClick", handler);
        if (!handler.empty()) _declarativeEventHandlers.insert(handler);
    }

    if (VBox* vbox = dynamic_cast<VBox*>(widget)) {
        if (j.contains("spacing")) {
            vbox->setSpacing(j["spacing"].get<float>());
        }
        if (j.contains("padding") && j["padding"].is_object()) {
            vbox->setPadding(
                j["padding"].value("left", 4.0f),
                j["padding"].value("top", 4.0f),
                j["padding"].value("right", 4.0f),
                j["padding"].value("bottom", 4.0f));
        }
        // L4 — parity with WidgetSerializer. BoxBase::setGravity
        // exists since v1.0; the loader only read spacing/padding
        // before. JSON shape: "gravity": "Center" (8 alignments).
        if (j.contains("gravity") && j["gravity"].is_string()) {
            vbox->setGravity(parseBoxGravity(j["gravity"].get<std::string>()));
        }
    } else if (HBox* hbox = dynamic_cast<HBox*>(widget)) {
        if (j.contains("spacing")) {
            hbox->setSpacing(j["spacing"].get<float>());
        }
        if (j.contains("padding") && j["padding"].is_object()) {
            hbox->setPadding(
                j["padding"].value("left", 4.0f),
                j["padding"].value("top", 4.0f),
                j["padding"].value("right", 4.0f),
                j["padding"].value("bottom", 4.0f));
        }
        // L4 — partner branch to VBox above.
        if (j.contains("gravity") && j["gravity"].is_string()) {
            hbox->setGravity(parseBoxGravity(j["gravity"].get<std::string>()));
        }
    } else if (GridPanel* grid = dynamic_cast<GridPanel*>(widget)) {
        // L3 — parity with WidgetSerializer. GridPanel::setCell has
        // existed since C-8; the loader only consumed rowCount /
        // columnCount + flattened children (auto-linear). The cells
        // shape lets hosts spell out exact (row, col, rowSpan, colSpan,
        // hAlign, vAlign) without the implicit left-to-right walk.
        const int rows = j.value("rowCount", 0);
        const int cols = j.value("columnCount", 0);
        if (rows > 0) grid->setRowCount(rows);
        if (cols > 0) grid->setColumnCount(cols);

        // Optional row / column definitions. JSON shape:
        //   "rowDefs":    [{ "policy": "Fixed", "value": 32 }, ...]
        //   "columnDefs": [{ "policy": "Stretch", "value": 1.0 }, ...]
        // Parsed before cells so setCell's bounds-check sees the
        // final row/col count.
        if (j.contains("rowDefs") && j["rowDefs"].is_array()) {
            int idx = 0;
            for (const auto& rd : j["rowDefs"]) {
                if (idx >= grid->getRowCount()) break;
                GridPanel::RowDef def;
                if (rd.contains("policy") && rd["policy"].is_string()) {
                    const std::string p = rd["policy"].get<std::string>();
                    def.policy = (p == "Fixed")
                        ? GridPanel::SizePolicy::Fixed
                        : GridPanel::SizePolicy::Stretch;
                }
                if (rd.contains("value")) {
                    def.value = rd["value"].get<float>();
                }
                grid->setRowDef(idx, def);
                ++idx;
            }
        }
        if (j.contains("columnDefs") && j["columnDefs"].is_array()) {
            int idx = 0;
            for (const auto& cd : j["columnDefs"]) {
                if (idx >= grid->getColumnCount()) break;
                GridPanel::ColDef def;
                if (cd.contains("policy") && cd["policy"].is_string()) {
                    const std::string p = cd["policy"].get<std::string>();
                    def.policy = (p == "Fixed")
                        ? GridPanel::SizePolicy::Fixed
                        : GridPanel::SizePolicy::Stretch;
                }
                if (cd.contains("value")) {
                    def.value = cd["value"].get<float>();
                }
                grid->setColumnDef(idx, def);
                ++idx;
            }
        }
        if (j.contains("padding") && j["padding"].is_object()) {
            const auto& padding = j["padding"];
            grid->setPadding(
                padding.value("left", 4.0f), padding.value("top", 4.0f),
                padding.value("right", 4.0f), padding.value("bottom", 4.0f));
        }
        if (j.contains("spacing") && j["spacing"].is_object()) {
            const auto& spacing = j["spacing"];
            grid->setSpacing(spacing.value("horizontal", 4.0f),
                             spacing.value("vertical", 4.0f));
        }
    }

    if (Image* image = dynamic_cast<Image*>(widget)) {
        if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4) {
            const auto& c = j["color"];
            image->setColor(math::FVector4(
                c[0].get<float>(),
                c[1].get<float>(),
                c[2].get<float>(),
                c[3].get<float>()));
        }
        if (j.contains("uv") && j["uv"].is_object()) {
            const auto& uv = j["uv"];
            image->setUV(math::FRectangle(
                uv.value("minX", 0.0f), uv.value("minY", 0.0f),
                uv.value("maxX", 1.0f), uv.value("maxY", 1.0f)));
        }
        // L2 — parity with WidgetSerializer. TextureRegistry + the
        // typed ImageTextureHandle are landed (G10), but the loader
        // previously only read `color`; a named texture in JSON was
        // silently dropped. Calling setTexture(name) resolves the
        // handle through the registry; if the texture is not yet
        // registered, the Image keeps an empty handle and the host
        // can re-bind via Image::registerExternal() later.
        if (j.contains("textureName") && j["textureName"].is_string()) {
            const std::string name = j["textureName"].get<std::string>();
            if (!name.empty()) {
                image->setTexture(name);
            }
        }
    }

    if (Window* window = dynamic_cast<Window*>(widget)) {
        if (j.contains("title") || j.contains("titleKey")) {
            window->setTitle(resolveLocalizedString(
                h, "title", "titleKey"));
        }
        if (j.contains("movable")) {
            window->setMovable(j["movable"].get<bool>());
        }
        if (j.contains("resizable")) {
            window->setResizable(j["resizable"].get<bool>());
        }
        if (j.contains("titleBarHeight")) {
            window->setTitleBarHeight(j["titleBarHeight"].get<float>());
        }
        if (j.contains("minSize") && j["minSize"].is_object()) {
            window->setMinSize(j["minSize"].value("w", 120.0f),
                               j["minSize"].value("h", 80.0f));
        }
    }

    // Slider / ProgressBar / CheckBox — mirror WidgetSerializer. Editor
    // layouts go through LayoutLoader; without this, Slider stays on the
    // default [0,1] / 0.5 and panel knobs cannot leave that range.
    if (Slider* sl = dynamic_cast<Slider*>(widget)) {
        const bool hasMin = j.contains("min");
        const bool hasMax = j.contains("max");
        if (hasMin && hasMax) {
            sl->setValueRange(j["min"].get<float>(), j["max"].get<float>());
        } else {
            // Expand max before min so setMin is not clamped to old max.
            if (hasMax) {
                sl->setMax(j["max"].get<float>());
            }
            if (hasMin) {
                sl->setMin(j["min"].get<float>());
            }
        }
        if (j.contains("value")) {
            sl->setValue(j["value"].get<float>());
        }
    }

    if (ProgressBar* pb = dynamic_cast<ProgressBar*>(widget)) {
        const bool hasMin = j.contains("min");
        const bool hasMax = j.contains("max");
        if (hasMin && hasMax) {
            pb->setValueRange(j["min"].get<float>(), j["max"].get<float>());
        } else {
            if (hasMax) {
                pb->setMax(j["max"].get<float>());
            }
            if (hasMin) {
                pb->setMin(j["min"].get<float>());
            }
        }
        if (j.contains("value")) {
            pb->setValue(j["value"].get<float>());
        }
    }

    if (CheckBox* cb = dynamic_cast<CheckBox*>(widget)) {
        if (j.contains("checked")) {
            cb->setChecked(j["checked"].get<bool>());
        }
    }

    // Keep the production loader aligned with WidgetSerializer for the
    // authorable leaf controls. The layout editor saves through the
    // serializer but re-opens through this loader, so dropping any of these
    // fields makes a successful save appear to work until the next open.
    if (RadioButton* radio = dynamic_cast<RadioButton*>(widget)) {
        if (j.contains("checked")) {
            radio->setChecked(j["checked"].get<bool>());
        }
        if (j.contains("groupId")) {
            radio->setGroupId(j["groupId"].get<int>());
        }
    }
    if (TextInput* input = dynamic_cast<TextInput*>(widget)) {
        if (j.contains("placeholder") || j.contains("placeholderKey")) {
            input->setPlaceholder(resolveLocalizedString(
                h, "placeholder", "placeholderKey"));
            if (j.contains("placeholderKey")
                && j["placeholderKey"].is_string()) {
                input->setLocalizationKey(
                    "placeholder", j["placeholderKey"].get<std::string>());
            }
        }
        if (j.contains("password")) {
            input->setPasswordMode(j["password"].get<bool>());
        }
        if (j.contains("readOnly")) {
            input->setReadOnly(j["readOnly"].get<bool>());
        }
        if (j.contains("maxLength")) {
            input->setMaxLength(static_cast<size_t>(
                std::max(0, j["maxLength"].get<int>())));
        }
        if (j.contains("hAlign")) {
            const int align = j["hAlign"].get<int>();
            input->setHAlign(align == 1 ? TextInput::HAlign::Center
                             : align == 2 ? TextInput::HAlign::Right
                                          : TextInput::HAlign::Left);
        }
    }
    if (TextArea* area = dynamic_cast<TextArea*>(widget)) {
        if (j.contains("readOnly")) {
            area->setReadOnly(j["readOnly"].get<bool>());
        }
        if (j.contains("maxLength")) {
            area->setMaxLength(static_cast<size_t>(
                std::max(0, j["maxLength"].get<int>())));
        }
        if (j.contains("lineHeight")) {
            area->setLineHeight(j["lineHeight"].get<float>());
        }
    }
    if (TextLabel* label = dynamic_cast<TextLabel*>(widget)) {
        if (j.contains("fontSize")) {
            label->setFontSize(j["fontSize"].get<int>());
        }
        math::FVector4 color;
        if (j.contains("textColor") && readColor4(j["textColor"], color)) {
            label->setTextColor(color);
        }
        if (j.contains("backgroundColor")
            && readColor4(j["backgroundColor"], color)) {
            label->setBackgroundColor(color);
        }
        if (j.contains("hAlign")) {
            if (j["hAlign"].is_string()) {
                const std::string align = j["hAlign"].get<std::string>();
                label->setHorizontalAlignment(
                    align == "Center" ? TextLabel::HAlignment::Center
                    : align == "Right" ? TextLabel::HAlignment::Right
                                       : TextLabel::HAlignment::Left);
            } else if (j["hAlign"].is_number_integer()) {
                const int align = j["hAlign"].get<int>();
                label->setHorizontalAlignment(
                    align == 1 ? TextLabel::HAlignment::Center
                    : align == 2 ? TextLabel::HAlignment::Right
                                 : TextLabel::HAlignment::Left);
            }
        }
        if (j.contains("vAlign")) {
            if (j["vAlign"].is_string()) {
                const std::string align = j["vAlign"].get<std::string>();
                label->setVerticalAlignment(
                    (align == "Center" || align == "Middle")
                        ? TextLabel::VAlignment::Center
                    : align == "Bottom" ? TextLabel::VAlignment::Bottom
                                        : TextLabel::VAlignment::Top);
            } else if (j["vAlign"].is_number_integer()) {
                const int align = j["vAlign"].get<int>();
                label->setVerticalAlignment(
                    align == 1 ? TextLabel::VAlignment::Center
                    : align == 2 ? TextLabel::VAlignment::Bottom
                                 : TextLabel::VAlignment::Top);
            }
        }
    }
    if (Panel* panel = dynamic_cast<Panel*>(widget)) {
        if (j.contains("borderEnabled")) {
            panel->setBorderEnabled(j["borderEnabled"].get<bool>());
        }
        if (j.contains("backgroundEnabled")) {
            panel->setBackgroundEnabled(j["backgroundEnabled"].get<bool>());
        }
        if (j.contains("padding") && j["padding"].is_object()) {
            panel->setPadding(math::FVector4(
                j["padding"].value("left", 0.0f),
                j["padding"].value("top", 0.0f),
                j["padding"].value("right", 0.0f),
                j["padding"].value("bottom", 0.0f)));
        }
    }
    if (Tooltip* tooltip = dynamic_cast<Tooltip*>(widget)) {
        if (j.contains("hoverDelay")) {
            tooltip->setHoverDelay(j["hoverDelay"].get<float>());
        }
    }
    if (Separator* separator = dynamic_cast<Separator*>(widget)) {
        if (j.contains("orientation") && j["orientation"].is_string()) {
            separator->setOrientation(j["orientation"].get<std::string>() == "vertical"
                ? Separator::Orientation::Vertical
                : Separator::Orientation::Horizontal);
        }
        if (j.contains("color") && j["color"].is_object()) {
            const auto& color = j["color"];
            separator->setColor(math::FVector4(
                color.value("r", 1.0f), color.value("g", 1.0f),
                color.value("b", 1.0f), color.value("a", 1.0f)));
        }
        if (j.contains("thickness")) {
            separator->setThickness(j["thickness"].get<float>());
        }
        if (j.contains("inset")) {
            separator->setInset(j["inset"].get<float>());
        }
    }

    // LayoutLoader is the path used by Editor shell JSON. Keep ScrollView's
    // single-content wire format symmetric with WidgetSerializer; treating
    // `content` as an ordinary child leaves ScrollView::_content null, so the
    // subtree is neither laid out nor painted and its ids cannot be bound.
    if (ScrollView* scroll = dynamic_cast<ScrollView*>(widget)) {
        if (j.contains("verticalScrollBar")) {
            scroll->setVerticalScrollBarEnabled(
                j["verticalScrollBar"].get<bool>());
        }
        if (j.contains("horizontalScrollBar")) {
            scroll->setHorizontalScrollBarEnabled(
                j["horizontalScrollBar"].get<bool>());
        }
        auto parseVisibility = [](const json& value,
                                  ScrollView::ScrollBarVisibility fallback) {
            if (!value.is_string()) return fallback;
            const std::string mode = value.get<std::string>();
            if (mode == "auto") return ScrollView::ScrollBarVisibility::Auto;
            if (mode == "always") return ScrollView::ScrollBarVisibility::Always;
            if (mode == "hidden") return ScrollView::ScrollBarVisibility::Hidden;
            return fallback;
        };
        if (j.contains("verticalScrollBarVisibility")) {
            scroll->setVerticalScrollBarVisibility(parseVisibility(
                j["verticalScrollBarVisibility"],
                scroll->getVerticalScrollBarVisibility()));
        }
        if (j.contains("horizontalScrollBarVisibility")) {
            scroll->setHorizontalScrollBarVisibility(parseVisibility(
                j["horizontalScrollBarVisibility"],
                scroll->getHorizontalScrollBarVisibility()));
        }
        if (j.contains("contentSize") && j["contentSize"].is_object()) {
            scroll->setContentSize(math::FVector2(
                j["contentSize"].value("w", 0.0f),
                j["contentSize"].value("h", 0.0f)));
        }
        if (j.contains("content") && j["content"].is_object()) {
            if (Widget* content = buildWidgetTree(JsonHandle(const_cast<json*>(&j["content"])))) {
                scroll->setContentOwned(content);
            }
        }
        if (j.contains("scrollOffset") && j["scrollOffset"].is_object()) {
            scroll->setScrollOffset(math::FVector2(
                j["scrollOffset"].value("x", 0.0f),
                j["scrollOffset"].value("y", 0.0f)));
        }
    }

    // Tab and modal controls own structured payloads that are deliberately
    // not emitted as ordinary children[]. Build them through buildWidgetTree
    // (rather than WidgetSerializer::deserialize) so every nested authored ID
    // is registered in this loader and receives declarative event bindings.
    if (TabControl* tabs = dynamic_cast<TabControl*>(widget)) {
        if (j.contains("tabs") && j["tabs"].is_array()) {
            for (const auto& tabJson : j["tabs"]) {
                if (!tabJson.is_object()) continue;
                Widget* content = nullptr;
                if (tabJson.contains("content")
                    && tabJson["content"].is_object()) {
                    content = buildWidgetTree(JsonHandle(
                        const_cast<json*>(&tabJson["content"])));
                }
                tabs->addTabOwned(utf8ToWide(
                    tabJson.value("label", std::string())), content);
            }
        }
        if (j.contains("selectedIndex")) {
            tabs->setSelectedIndex(j["selectedIndex"].get<int>());
        }
        if (j.contains("headerHeight")) {
            tabs->setHeaderHeight(j["headerHeight"].get<float>());
        }
    }
    if (TabStrip* strip = dynamic_cast<TabStrip*>(widget)) {
        if (j.contains("tabs") && j["tabs"].is_array()) {
            for (const auto& tabJson : j["tabs"]) {
                if (tabJson.is_string()) {
                    strip->addTab(utf8ToWide(tabJson.get<std::string>()));
                }
            }
        }
        if (j.contains("selectedIndex")) {
            strip->setSelectedIndex(j["selectedIndex"].get<int>());
        }
        if (j.contains("tabHeight")) {
            strip->setTabHeight(j["tabHeight"].get<float>());
        }
        if (j.contains("spacing")) {
            strip->setSpacing(j["spacing"].get<float>());
        }
        if (j.contains("indicatorTweenMs")) {
            strip->setIndicatorTweenMs(j["indicatorTweenMs"].get<float>());
        }
        if (j.contains("overflowMode")) {
            const int mode = j["overflowMode"].get<int>();
            if (mode >= 0
                && mode <= static_cast<int>(TabStrip::OverflowMode::Clip)) {
                strip->setOverflowMode(
                    static_cast<TabStrip::OverflowMode>(mode));
            }
        }
        if (j.contains("minTabWidth")) {
            strip->setMinTabWidth(j["minTabWidth"].get<float>());
        }
    }

    const auto restoreModalBase = [&j](Modal* modal) {
        if (j.contains("dismissOnDimmerClick")) {
            modal->setDismissOnDimmerClick(
                j["dismissOnDimmerClick"].get<bool>());
        }
        if (j.contains("dimmer") && j["dimmer"].is_object()) {
            const auto& dimmerJson = j["dimmer"];
            auto* dimmer = new Dimmer();
            if (dimmerJson.contains("scrimColor")
                && dimmerJson["scrimColor"].is_object()) {
                const auto& color = dimmerJson["scrimColor"];
                dimmer->setScrimColor(math::FVector4(
                    color.value("r", 0.0f), color.value("g", 0.0f),
                    color.value("b", 0.0f), color.value("a", 0.5f)));
            }
            modal->setDimmerOwned(dimmer);
        }
    };
    if (ModalDialog* dialog = dynamic_cast<ModalDialog*>(widget)) {
        restoreModalBase(dialog);
        if (j.contains("acceptText") || j.contains("acceptTextKey")) {
            dialog->setAcceptText(resolveLocalizedString(
                h, "acceptText", "acceptTextKey"));
        }
        if (j.contains("rejectText") || j.contains("rejectTextKey")) {
            dialog->setRejectText(resolveLocalizedString(
                h, "rejectText", "rejectTextKey"));
        }
        if (j.contains("bodyContent") && j["bodyContent"].is_object()) {
            if (Widget* body = buildWidgetTree(JsonHandle(
                    const_cast<json*>(&j["bodyContent"])))) {
                dialog->setBodyContentOwned(body);
            }
        }
    } else if (Modal* modal = dynamic_cast<Modal*>(widget)) {
        restoreModalBase(modal);
        if (j.contains("content") && j["content"].is_object()) {
            if (Widget* content = buildWidgetTree(JsonHandle(
                    const_cast<json*>(&j["content"])))) {
                modal->setContentOwned(content);
            }
        }
    }

    if (TreeView* tree = dynamic_cast<TreeView*>(widget)) {
        if (j.contains("tree") && j["tree"].is_array()) {
            std::vector<TreeNodeData> nodes;
            nodes.reserve(j["tree"].size());
            for (const auto& nodeJson : j["tree"]) {
                if (!nodeJson.is_object()) continue;
                TreeNodeData node;
                node.label = utf8ToWide(
                    nodeJson.value("label", std::string()));
                node.icon = utf8ToWide(
                    nodeJson.value("icon", std::string()));
                node.hasChildren = nodeJson.value("hasChildren", false);
                node.expanded = nodeJson.value("expanded", false);
                node.parentIndex = nodeJson.value("parentIndex", -1);
                nodes.push_back(std::move(node));
            }
            tree->setTree(nodes);
        }
        if (j.contains("selectedIndex")) {
            tree->setSelectedIndex(j["selectedIndex"].get<int>());
        }
        if (j.contains("itemHeight")) {
            tree->setItemHeight(j["itemHeight"].get<float>());
        }
    }

    if (RichText* rich = dynamic_cast<RichText*>(widget)) {
        rich->clearRuns();
        if (j.contains("defaultColor") && j["defaultColor"].is_object()) {
            const auto& color = j["defaultColor"];
            rich->setDefaultColor(math::FVector4(
                color.value("r", 1.0f), color.value("g", 1.0f),
                color.value("b", 1.0f), color.value("a", 1.0f)));
        }
        if (j.contains("defaultFontSize")) {
            rich->setDefaultFontSize(j["defaultFontSize"].get<int>());
        }
        if (j.contains("wrapWidth")) {
            rich->setWrapWidth(j["wrapWidth"].get<float>());
        }
        if (j.contains("wrapMode")) {
            const int value = j["wrapMode"].get<int>();
            if (value >= 0
                && value <= static_cast<int>(RichTextWrapMode::Character)) {
                rich->setWrapMode(static_cast<RichTextWrapMode>(value));
            }
        }
        if (j.contains("alignment")) {
            const int value = j["alignment"].get<int>();
            if (value >= 0
                && value <= static_cast<int>(RichTextAlignment::Justify)) {
                rich->setAlignment(static_cast<RichTextAlignment>(value));
            }
        }
        if (j.contains("verticalAlignment")) {
            const int value = j["verticalAlignment"].get<int>();
            if (value >= 0 && value <= static_cast<int>(
                    RichTextVerticalAlignment::Bottom)) {
                rich->setVerticalAlignment(
                    static_cast<RichTextVerticalAlignment>(value));
            }
        }
        if (j.contains("overflow")) {
            const int value = j["overflow"].get<int>();
            if (value >= 0
                && value <= static_cast<int>(RichTextOverflow::Ellipsis)) {
                rich->setOverflow(static_cast<RichTextOverflow>(value));
            }
        }
        if (j.contains("lineHeight")) {
            rich->setLineHeight(j["lineHeight"].get<float>());
        }
        if (j.contains("lineSpacing")) {
            rich->setLineSpacing(j["lineSpacing"].get<float>());
        }
        if (j.contains("maxLines")) {
            rich->setMaxLines(j["maxLines"].get<size_t>());
        }
        if (j.contains("textDirection")) {
            const int value = j["textDirection"].get<int>();
            if (value >= 0
                && value <= static_cast<int>(TextDirection::RightToLeft)) {
                rich->setTextDirection(static_cast<TextDirection>(value));
            }
        }
        rich->setSelectable(j.value("selectable", true));
        rich->setEditable(j.value("editable", false));
        if (j.contains("runs") && j["runs"].is_array()) {
            for (const auto& runJson : j["runs"]) {
                if (!runJson.is_object() || !runJson.contains("text")) continue;
                RichRun run;
                run.text = utf8ToWide(runJson["text"].get<std::string>());
                run.color = rich->getDefaultColor();
                run.fontSize = rich->getDefaultFontSize();
                if (runJson.contains("color")
                    && runJson["color"].is_object()) {
                    const auto& color = runJson["color"];
                    run.color = math::FVector4(
                        color.value("r", 1.0f), color.value("g", 1.0f),
                        color.value("b", 1.0f), color.value("a", 1.0f));
                }
                if (runJson.contains("fontSize")) {
                    run.fontSize = runJson["fontSize"].get<int>();
                }
                if (runJson.contains("fontFamily")) {
                    run.fontFamily = utf8ToWide(
                        runJson["fontFamily"].get<std::string>());
                }
                run.fontWeight = std::clamp(
                    runJson.value("fontWeight", 400), 100, 900);
                run.language = runJson.value("language", std::string());
                run.bold = runJson.value("bold", false);
                run.italic = runJson.value("italic", false);
                run.underline = runJson.value("underline", false);
                run.strikethrough = runJson.value("strikethrough", false);
                run.letterSpacing = runJson.value("letterSpacing", 0.0f);
                run.baselineShift = runJson.value("baselineShift", 0.0f);
                const int inlineKind = runJson.value("inlineKind", 0);
                if (inlineKind >= 0
                    && inlineKind <= static_cast<int>(RichInlineKind::Widget)) {
                    run.inlineKind = static_cast<RichInlineKind>(inlineKind);
                }
                if (runJson.contains("inlineSize")
                    && runJson["inlineSize"].is_object()) {
                    run.inlineSize = math::FVector2(
                        runJson["inlineSize"].value("w", 16.0f),
                        runJson["inlineSize"].value("h", 16.0f));
                }
                run.inlineBaseline = runJson.value("inlineBaseline", 0.0f);
                if (runJson.contains("inlineAltText")) {
                    run.inlineAltText = utf8ToWide(
                        runJson["inlineAltText"].get<std::string>());
                }
                const int semanticKind = runJson.value("semanticKind", 0);
                if (semanticKind >= 0
                    && semanticKind <= static_cast<int>(RichSemanticKind::Link)) {
                    run.semanticKind = static_cast<RichSemanticKind>(semanticKind);
                }
                if (runJson.contains("semanticLabel")) {
                    run.semanticLabel = utf8ToWide(
                        runJson["semanticLabel"].get<std::string>());
                }
                if (runJson.contains("linkTarget")) {
                    run.linkTarget = utf8ToWide(
                        runJson["linkTarget"].get<std::string>());
                }
                rich->addRun(run);
            }
        }
    }

    // MenuBar/Menu structured content. WidgetSerializer already supported
    // this wire format, but production layouts go through UILayoutLoader;
    // keeping the two loaders in parity avoids a visible-but-empty menubar.
    if (MenuBar* menuBar = dynamic_cast<MenuBar*>(widget)) {
        if (j.contains("anchorSpacing")) {
            menuBar->setAnchorSpacing(j["anchorSpacing"].get<float>());
        }
        if (j.contains("anchorWidth")) {
            menuBar->setAnchorWidth(j["anchorWidth"].get<float>());
        }
        if (j.contains("anchorAutoWidth")) {
            menuBar->setAnchorAutoWidth(j["anchorAutoWidth"].get<bool>());
        }
        if (j.contains("menus") && j["menus"].is_array()) {
            for (const auto& menuJson : j["menus"]) {
                JsonHandle menuHandle(const_cast<json*>(&menuJson));
                Menu* menu = menuBar->addMenu(resolveLocalizedString(
                    menuHandle, "title", "titleKey"));
                if (menuJson.contains("titleKey")
                    && menuJson["titleKey"].is_string()) {
                    menu->setLocalizationKey(
                        "title", menuJson["titleKey"].get<std::string>());
                }
                const json* payload = &menuJson;
                if (menuJson.contains("menu")
                    && menuJson["menu"].is_object()) {
                    payload = &menuJson["menu"];
                }
                if (!payload->contains("items")
                    || !(*payload)["items"].is_array()) {
                    continue;
                }
                for (const auto& itemJson : (*payload)["items"]) {
                    JsonHandle itemHandle(const_cast<json*>(&itemJson));
                    MenuItem* item = menu->addItem(resolveLocalizedString(
                        itemHandle, "text", "textKey"));
                    if (itemJson.contains("textKey")
                        && itemJson["textKey"].is_string()) {
                        item->setLocalizationKey(
                            "text", itemJson["textKey"].get<std::string>());
                    }
                    if (itemJson.contains("shortcut")) {
                        item->setShortcut(utf8ToWide(
                            itemJson["shortcut"].get<std::string>()));
                    }
                    if (itemJson.contains("submenu")
                        && itemJson["submenu"].is_object()) {
                        Widget* submenuWidget = buildWidgetTree(
                            JsonHandle(const_cast<json*>(&itemJson["submenu"])));
                        Menu* submenu = dynamic_cast<Menu*>(submenuWidget);
                        if (submenu != nullptr) {
                            menu->attachSubmenu(item, submenu);
                        } else {
                            destroyWidgetTree(submenuWidget);
                        }
                    }
                }
            }
        }
    } else if (Menu* menu = dynamic_cast<Menu*>(widget)) {
        if (j.contains("items") && j["items"].is_array()) {
            for (const auto& itemJson : j["items"]) {
                JsonHandle itemHandle(const_cast<json*>(&itemJson));
                MenuItem* item = menu->addItem(resolveLocalizedString(
                    itemHandle, "text", "textKey"));
                if (itemJson.contains("textKey")
                    && itemJson["textKey"].is_string()) {
                    item->setLocalizationKey(
                        "text", itemJson["textKey"].get<std::string>());
                }
                if (itemJson.contains("shortcut")) {
                    item->setShortcut(utf8ToWide(
                        itemJson["shortcut"].get<std::string>()));
                }
            }
        }
    }

    // ComboBox / ListView item lists (mirror WidgetSerializer deserialize).
    if ((j.contains("items") && j["items"].is_array())
        || (j.contains("itemsKey") && j["itemsKey"].is_array())) {
        const std::vector<std::wstring> items = resolveLocalizedList(
            h, "items", "itemsKey");
        if (ComboBox* cb = dynamic_cast<ComboBox*>(widget)) {
            cb->setItems(items);
            if (j.contains("selectedIndex")) {
                cb->setSelectedIndex(j["selectedIndex"].get<int>());
            }
            if (j.contains("maxPopupItems")) {
                cb->setMaxPopupItems(j["maxPopupItems"].get<int>());
            }
        } else if (ListView* lv = dynamic_cast<ListView*>(widget)) {
            lv->setItems(items);
            if (j.contains("selectedIndex")) {
                lv->setSelectedIndex(j["selectedIndex"].get<int>());
            }
            // G1 — multi-select round-trip. Mirror of AYWidgetSerializer:
            // selectionMode is an int (0 = Single, 1 = Extended);
            // selectedIndices is an int array.
            if (j.contains("selectionMode")) {
                const int mode = j["selectionMode"].get<int>();
                lv->setSelectionMode(static_cast<ListView::SelectionMode>(
                    mode == 1 ? 1 : 0));
            }
            if (j.contains("selectedIndices") && j["selectedIndices"].is_array()) {
                std::vector<int> v;
                for (const auto& s : j["selectedIndices"]) {
                    v.push_back(s.get<int>());
                }
                lv->setSelectedIndices(v);
            }
            if (j.contains("itemHeight")) {
                lv->setItemHeight(j["itemHeight"].get<float>());
            }
        } else if (TileView* tv = dynamic_cast<TileView*>(widget)) {
            if (j.contains("tileSize") && j["tileSize"].is_object()) {
                tv->setTileSize(math::FVector2(
                    j["tileSize"].value("w", tv->getTileSize().x),
                    j["tileSize"].value("h", tv->getTileSize().y)));
            }
            if (j.contains("tileSpacing")) {
                tv->setTileSpacing(j["tileSpacing"].get<float>());
            }
            if (j.contains("contentPadding")) {
                tv->setContentPadding(j["contentPadding"].get<float>());
            }
            if (j.contains("labelHeight")) {
                tv->setLabelHeight(j["labelHeight"].get<float>());
            }
            if (j.contains("infoStripHeight")) {
                tv->setInfoStripHeight(j["infoStripHeight"].get<float>());
            }
            if (j.contains("cornerMarkerSize")) {
                tv->setCornerMarkerSize(j["cornerMarkerSize"].get<float>());
            }
            if (j.contains("thumbnailAspectRatio")) {
                tv->setThumbnailAspectRatio(
                    j["thumbnailAspectRatio"].get<float>());
            }
            if (j.contains("overscanRows")) {
                tv->setOverscanRows(j["overscanRows"].get<int>());
            }
            if (j.contains("dragEnabled")) {
                tv->setDragEnabled(j["dragEnabled"].get<bool>());
            }
            tv->setItems(items);
            if (j.contains("selectionMode")) {
                tv->setSelectionMode(j["selectionMode"].get<int>() == 1
                    ? TileView::SelectionMode::Extended
                    : TileView::SelectionMode::Single);
            }
            if (j.contains("selectedIndices")
                && j["selectedIndices"].is_array()) {
                std::vector<int> selected;
                for (const auto& value : j["selectedIndices"]) {
                    selected.push_back(value.get<int>());
                }
                tv->setSelectedIndices(selected);
            } else if (j.contains("selectedIndex")) {
                tv->setSelectedIndex(j["selectedIndex"].get<int>());
            }
            if (j.contains("focusedIndex")) {
                tv->setFocusedIndex(j["focusedIndex"].get<int>(), false);
            }
            if (j.contains("scrollOffset")
                && j["scrollOffset"].is_object()) {
                tv->setScrollOffset(math::FVector2(
                    0.0f, j["scrollOffset"].value("y", 0.0f)));
            }
        }
    }

    // Text with i18n support
    const std::string text = j.value("text", "");
    if (!text.empty() || j.contains("textKey")) {
        const std::wstring wtext = resolveLocalizedString(
            h, "text", "textKey");

        if (type == "Button") {
            if (Button* button = dynamic_cast<Button*>(widget)) {
                button->setText(wtext);
            }
        } else if (type == "TextLabel") {
            if (TextLabel* label = dynamic_cast<TextLabel*>(widget)) {
                label->setText(wtext);
            }
        } else if (type == "CheckBox") {
            if (CheckBox* checkBox = dynamic_cast<CheckBox*>(widget)) {
                checkBox->setText(wtext);
            }
        } else if (type == "RadioButton") {
            if (RadioButton* radio = dynamic_cast<RadioButton*>(widget)) {
                radio->setText(wtext);
            }
        } else if (type == "TextInput") {
            // L1 — parity with WidgetSerializer. Inspector inputs ship
            // initial values like "0.00" in layout JSON; previously the
            // loader dropped this field and the value reburned on the
            // first refresh tick, causing visible flicker.
            if (TextInput* textInput = dynamic_cast<TextInput*>(widget)) {
                textInput->setText(wtext);
            }
        } else if (type == "TextArea") {
            // L1 — parity with WidgetSerializer.
            if (TextArea* textArea = dynamic_cast<TextArea*>(widget)) {
                textArea->setText(wtext);
            }
        } else if (type == "Tooltip") {
            // L1 — parity with WidgetSerializer (Tooltip's text is its
            // body label, not a title — distinct from Window's title).
            if (Tooltip* tip = dynamic_cast<Tooltip*>(widget)) {
                tip->setText(wtext);
            }
        } else if (type == "MenuItem") {
            // L1 — parity with WidgetSerializer. MenuItem's setText ===
            // menu label; shortcut is a separate field.
            //
            // Code-review 2026-08-02 #11: previously the loader only
            // round-tripped the visible label and dropped the `shortcut`
            // field. The serializer emits it (line ~777), so save -> load
            // lost the accelerator binding too (setShortcut re-parses).
            // Read shortcut here using the same UTF-8 -> wstring path.
            if (MenuItem* mi = dynamic_cast<MenuItem*>(widget)) {
                mi->setText(wtext);
                if (j.contains("shortcut") && j["shortcut"].is_string()) {
                    const std::string sc = j["shortcut"].get<std::string>();
                    mi->setShortcut(utf8ToWide(sc));
                }
            }
        } else if (type == "Window" && !j.contains("title")
                   && !j.contains("titleKey")) {
            if (Window* window = dynamic_cast<Window*>(widget)) {
                window->setTitle(wtext);
            }
        }
        LOADER_HEAP_CHECK("after_set_text");
    }

    // Resolve a declarative event using the old id/event binding first, then
    // the controller-scoped name, then a globally registered handler name.
    auto resolveEvent = [this, widget, &id](const char* eventName)
            -> std::function<void()> {
        auto find = [this](const std::string& key) -> std::function<void()> {
            const auto it = _eventBindings.find(key);
            return it != _eventBindings.end() ? it->second : std::function<void()>{};
        };
        if (!id.empty()) {
            if (auto callback = find(id + "." + eventName)) return callback;
        }
        const std::string& handlerName = widget->getEventBinding(eventName);
        if (handlerName.empty()) return {};
        if (!widget->getControllerId().empty()) {
            if (auto callback = find("@controller." + widget->getControllerId()
                                     + "." + handlerName)) return callback;
        }
        if (auto callback = find("@handler." + handlerName)) return callback;
        return _declarativeEventResolver
            ? _declarativeEventResolver(*widget, eventName, handlerName)
            : std::function<void()>{};
    };

    if (auto callback = resolveEvent("onClick")) {
        if (auto* interactive = dynamic_cast<InteractiveWidget*>(widget)) {
            interactive->setOnClicked(std::move(callback));
        }
    }
    if (auto callback = resolveEvent("onToggled")) {
        if (auto* check = dynamic_cast<CheckBox*>(widget)) {
            check->setOnToggled([callback](bool) { callback(); });
        } else if (auto* radio = dynamic_cast<RadioButton*>(widget)) {
            radio->setOnToggled([callback](bool) { callback(); });
        }
    }
    if (auto callback = resolveEvent("onValueChanged")) {
        if (auto* slider = dynamic_cast<Slider*>(widget)) {
            slider->setOnValueChanged([callback](float) { callback(); });
        }
    }
    if (auto callback = resolveEvent("onTextChanged")) {
        if (auto* input = dynamic_cast<TextInput*>(widget)) {
            input->setOnTextChanged([callback](const std::wstring&) { callback(); });
        } else if (auto* area = dynamic_cast<TextArea*>(widget)) {
            area->setOnTextChanged([callback](const std::wstring&) { callback(); });
        } else if (auto* rich = dynamic_cast<RichText*>(widget)) {
            rich->setOnTextChanged([callback](const std::wstring&) { callback(); });
        }
    }
    if (auto callback = resolveEvent("onSubmit")) {
        if (auto* input = dynamic_cast<TextInput*>(widget)) {
            input->setOnSubmit([callback](const std::wstring&) { callback(); });
        }
    }
    if (auto callback = resolveEvent("onSelectionChanged")) {
        auto relay = [callback](int) { callback(); };
        if (auto* combo = dynamic_cast<ComboBox*>(widget)) combo->setOnSelectionChanged(relay);
        else if (auto* list = dynamic_cast<ListView*>(widget)) list->setOnSelectionChanged(relay);
        else if (auto* tiles = dynamic_cast<TileView*>(widget)) tiles->setOnSelectionChanged(relay);
        else if (auto* tree = dynamic_cast<TreeView*>(widget)) tree->setOnSelectionChanged(relay);
        else if (auto* tabs = dynamic_cast<TabControl*>(widget)) tabs->setOnSelectionChanged(relay);
        else if (auto* strip = dynamic_cast<TabStrip*>(widget)) strip->setOnSelectionChanged(relay);
    }
    if (auto callback = resolveEvent("onItemActivated")) {
        auto relay = [callback](int) { callback(); };
        if (auto* list = dynamic_cast<ListView*>(widget)) list->setOnItemActivated(relay);
        else if (auto* tiles = dynamic_cast<TileView*>(widget)) tiles->setOnItemActivated(relay);
    }
    if (auto callback = resolveEvent("onClose")) {
        if (auto* modal = dynamic_cast<Modal*>(widget)) {
            modal->setOnClose(std::move(callback));
        } else if (auto* window = dynamic_cast<Window*>(widget)) {
            window->setOnClose(std::move(callback));
        }
    }

    // Children
    if (j.contains("children") && j["children"].is_array()) {
        const char* parentId = id.empty() ? type.c_str() : id.c_str();
        if (VBox* vbox = dynamic_cast<VBox*>(widget)) {
            for (const auto& childJson : j["children"]) {
                Widget* child = buildWidgetTree(JsonHandle(const_cast<json*>(&childJson)));
                if (!child) continue;
                if (child->isSplitterHandle()) {
                    // Mirror HBox: pin thin-axis size so a missed
                    // size.h=0 cannot stretch the hover band. Vertical
                    // drag axis matches VBox stacking.
                    if (SplitterHandle* sh = dynamic_cast<SplitterHandle*>(child)) {
                        sh->setOrientation(SplitterHandle::Orientation::Vertical);
                    }
                    vbox->addWidget(child, SplitterHandle::kDefaultWidth);
                } else {
                    float slotHeight = 0.0f;
                    if (childJson.contains("size") && childJson["size"].is_object()) {
                        slotHeight = childJson["size"].value("h", 0.0f);
                    }
                    vbox->addWidget(child, slotHeight);
                }
                LOADER_HEAP_CHECK_ATTACH(parentId, child->getId().empty() ? "child" : child->getId().c_str());
            }
            vbox->rebindSplitters();
        } else if (HBox* hbox = dynamic_cast<HBox*>(widget)) {
            for (const auto& childJson : j["children"]) {
                Widget* child = buildWidgetTree(JsonHandle(const_cast<json*>(&childJson)));
                if (!child) continue;
                if (child->isSplitterHandle()) {
                    hbox->addWidget(child, SplitterHandle::kDefaultWidth);
                } else {
                    float slotWidth = 0.0f;
                    const BoxSlotLimits limits = parseHBoxSlotLimits(childJson, slotWidth);
                    hbox->addWidget(child, slotWidth, limits);
                }
                LOADER_HEAP_CHECK_ATTACH(parentId, child->getId().empty() ? "child" : child->getId().c_str());
            }
            hbox->rebindSplitters();
        } else if (GridPanel* grid = dynamic_cast<GridPanel*>(widget)) {
            // L3 — GridPanel cells + children are handled in a dedicated
            // block below. Place nothing here; the dedicated block is
            // sibling to VBox/HBox so it runs without a `children` array.
        } else {
            for (const auto& childJson : j["children"]) {
                Widget* child = buildWidgetTree(JsonHandle(const_cast<json*>(&childJson)));
                if (child) {
                    widget->addChild(child);
                    LOADER_HEAP_CHECK_ATTACH(parentId, child->getId().empty() ? "child" : child->getId().c_str());
                }
            }
        }
    }

    // L3 — GridPanel cells[] + children[] are sibling to the layout
    // containers (VBox/HBox). The cells[] shape is the structured form
    // (row, col, rowSpan, colSpan, hAlign, vAlign) and runs whether or
    // not the JSON also has a children[] array. children[] is the
    // auto-linear fallback that fills the next free (row, col) cell.
    if (GridPanel* grid = dynamic_cast<GridPanel*>(widget)) {
        int nextRow = 0;
        int nextCol = 0;
        auto findNextCell = [&]() {
            while (nextRow < grid->getRowCount()
                   && nextCol < grid->getColumnCount()
                   && grid->getCell(nextRow, nextCol) != nullptr) {
                ++nextCol;
                if (nextCol >= grid->getColumnCount()) {
                    nextCol = 0;
                    ++nextRow;
                }
            }
        };
        if (j.contains("cells") && j["cells"].is_array()) {
            for (const auto& cellJson : j["cells"]) {
                if (!cellJson.is_object()) continue;
                const int row = cellJson.value("row", nextRow);
                const int col = cellJson.value("col", nextCol);
                if (row < 0 || col < 0
                    || row >= grid->getRowCount()
                    || col >= grid->getColumnCount()) {
                    continue;
                }
                if (!cellJson.contains("content")
                    || !cellJson["content"].is_object()) {
                    continue;
                }
                Widget* cellChild = buildWidgetTree(JsonHandle(const_cast<json*>(&cellJson["content"])));
                if (cellChild == nullptr) continue;
                const int rowSpan = cellJson.value("rowSpan", 1);
                const int colSpan = cellJson.value("colSpan", 1);
                GridPanel::HAlign hAlign = GridPanel::HAlign::Fill;
                if (cellJson.contains("hAlign") && cellJson["hAlign"].is_string()) {
                    hAlign = parseGridHAlign(
                        cellJson["hAlign"].get<std::string>());
                }
                GridPanel::VAlign vAlign = GridPanel::VAlign::Fill;
                if (cellJson.contains("vAlign") && cellJson["vAlign"].is_string()) {
                    vAlign = parseGridVAlign(
                        cellJson["vAlign"].get<std::string>());
                }
                grid->setCell(row, col, cellChild,
                              rowSpan, colSpan, hAlign, vAlign);
                nextRow = row;
                nextCol = col + 1;
                if (nextCol >= grid->getColumnCount()) {
                    nextCol = 0;
                    ++nextRow;
                }
                findNextCell();
            }
        }
        if (j.contains("children") && j["children"].is_array()) {
            const char* parentId = id.empty() ? type.c_str() : id.c_str();
            for (const auto& childJson : j["children"]) {
                Widget* child = buildWidgetTree(JsonHandle(const_cast<json*>(&childJson)));
                if (!child) continue;
                findNextCell();
                if (nextRow >= grid->getRowCount()) break;
                grid->setCell(nextRow, nextCol, child);
                ++nextCol;
                if (nextCol >= grid->getColumnCount()) {
                    nextCol = 0;
                    ++nextRow;
                }
                LOADER_HEAP_CHECK_ATTACH(parentId, child->getId().empty() ? "child" : child->getId().c_str());
            }
        }
    }

    if (!id.empty()) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "after_build_%s", id.c_str());
        LOADER_HEAP_CHECK(buf);
    }

    // =================================================================
    // D2 — DockArea JSON bridge. Editor shells persist their layout as
    // a single DockArea node containing all docked + floating cards.
    // Wire format: see `ay-ui.md` §D2. We process the bridge AFTER the
    // base children[] walk above so an edge-case JSON containing both
    // `children` (legacy form) and `cards` (new form) ends with the
    // `cards[]` representation winning — matching the "last write wins"
    // convention used by DockArea::addCard itself.
    // =================================================================
    if (DockArea* dock = dynamic_cast<DockArea*>(widget)) {
        if (j.contains("slotWeights") && j["slotWeights"].is_object()) {
            applySlotWeightMap(*dock, j["slotWeights"]);
        }
        if (j.contains("slotMinSizes") && j["slotMinSizes"].is_object()) {
            applySlotMinSizeMap(*dock, j["slotMinSizes"]);
        }
        if (j.contains("cards") && j["cards"].is_array()) {
            for (const auto& cj : j["cards"]) {
                if (!cj.is_object()) continue;
                if (!cj.contains("slot") || !cj["slot"].is_string()) continue;
                DockArea::Slot slot;
                if (!DockArea::parseSlot(cj["slot"].get<std::string>(), slot)) {
                    continue;
                }
                DockCard* card = buildDockCardFromJson(JsonHandle(const_cast<json*>(&cj)));
                if (card == nullptr) continue;
                dock->addCard(slot, std::unique_ptr<DockCard>(card));
            }
        }
        if (j.contains("floating") && j["floating"].is_array()) {
            DockOverlay* overlay = dock->getOverlay();
            for (const auto& cj : j["floating"]) {
                DockCard* card = buildDockCardFromJson(JsonHandle(const_cast<json*>(&cj)));
                if (card == nullptr) continue;
                // Apply the floating frame (if present). Cards without
                // an explicit frame are positioned at (0, 0) — keep
                // behavior aligned with v0 default for unfloated cards.
                if (cj.contains("x") && cj.contains("y")) {
                    card->setPosition(math::FVector2(
                        cj["x"].get<float>(),
                        cj["y"].get<float>()));
                }
                if (cj.contains("w") && cj.contains("h")) {
                    card->setSize(math::FVector2(
                        cj["w"].get<float>(),
                        cj["h"].get<float>()));
                }
                if (overlay != nullptr) {
                    overlay->addFloatingCard(card);
                }
            }
        }
    }
    // D2 — A bare DockCard or DockOverlay may appear directly (e.g.
    // when an editor wraps a single floating window in a small file).
    // Apply the same field set so the round-trip is symmetric.
    if (DockCard* card = dynamic_cast<DockCard*>(widget)) {
        if (j.contains("title") || j.contains("titleKey")) {
            card->setTitle(resolveLocalizedString(
                h, "title", "titleKey"));
        }
        if (j.contains("icon")) {
            card->setIcon(j["icon"].get<std::string>());
        }
        if (j.contains("closable"))  card->setClosable(j["closable"].get<bool>());
        if (j.contains("floatable")) card->setFloatable(j["floatable"].get<bool>());
        if (j.contains("collapsed")) card->setCollapsed(j["collapsed"].get<bool>());
        if (j.contains("headerHeight")) card->setHeaderHeight(j["headerHeight"].get<float>());
        if (j.contains("content") && j["content"].is_object()) {
            Widget* content = WidgetSerializer::deserialize(j["content"].dump());
            if (content != nullptr) {
                card->setContent(content);
            }
        }
    }

    return widget;
}

} // namespace ayt::ui
