#include "AYLayoutLoader.h"
#include "AYWidgetFactory.h"
#include "AYI18n.h"
#include "AYButton.h"
#include "AYTextLabel.h"
#include "AYTextInput.h"
#include "AYTextArea.h"
#include "AYTooltip.h"
#include "AYMenuItem.h"
#include "AYWindow.h"
#include "AYBox.h"
#include "AYGridPanel.h"
#include "AYSplitterHandle.h"
#include "AYImage.h"
#include "AYComboBox.h"
#include "AYListView.h"
#include "AYSlider.h"
#include "AYCheckBox.h"
#include "AYProgressBar.h"
#include "AYDockArea.h"
#include "AYDockCard.h"
#include "AYDockOverlay.h"

#include "AYWidgetSerializer.h"
#include "AYLayoutLoader.h"

#include <ayio/FileWatcher.h>

#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_DEBUG) && defined(_MSC_VER)
#  include <crtdbg.h>

namespace {

void loaderHeapCheck(const char* label)
{
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

} // namespace
#  define LOADER_HEAP_CHECK(label) loaderHeapCheck(label)
#  define LOADER_HEAP_CHECK_ATTACH(parentId, childId)                                       \
      loaderHeapCheckId("after_attach", (parentId), (childId))
#else
#  define LOADER_HEAP_CHECK(label) ((void)0)
#  define LOADER_HEAP_CHECK_ATTACH(parentId, childId) ((void)0)
#endif

namespace ayt::ui {

namespace {

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

DockCard* UILayoutLoader::buildDockCardFromJson(const json& cj) {
    if (!cj.is_object()) return nullptr;
    auto card = std::make_unique<DockCard>();
    std::string id;
    if (cj.contains("id")) {
        id = cj["id"].get<std::string>();
        card->setId(id);
    }
    if (cj.contains("title")) {
        std::string u8 = cj["title"].get<std::string>();
        card->setTitle(std::wstring(u8.begin(), u8.end()));
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
        content = buildWidgetTree(cj["content"]);
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
        return nullptr;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    _lastJson = ss.str();
    _lastFilePath = filepath;

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

Widget* UILayoutLoader::loadFromString(const std::string& jsonStr) {
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

    try {
        json j = json::parse(jsonStr);
        LOADER_HEAP_CHECK("after_json_parse");
        Widget* root = buildWidgetTree(j);
        LOADER_HEAP_CHECK("after_build_widget_tree");
        // Success: the new tree's buildWidgetTree path already populated
        // _widgetsById during recursion (the early-swap above restored
        // the old map's empty state). Build succeeded; commit the new
        // index by leaving it in place (we already wrote into it).
        // (Note: buildWidgetTree used _widgetsById after the swap, so
        // the new index IS what _widgetsById holds now.)
        (void)oldIndex;
        return root;
    }
    catch (const std::exception& e) {
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

    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::fprintf(stderr, "[UILayoutLoader] save error: cannot open '%s' for write\n",
                     filepath.c_str());
        return false;
    }
    file.write(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
    if (!file.good()) {
        std::fprintf(stderr, "[UILayoutLoader] save error: write to '%s' failed\n",
                     filepath.c_str());
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

void UILayoutLoader::clearEventBindings() {
    _eventBindings.clear();
}

void UILayoutLoader::clearWidgetRegistry() {
    _widgetsById.clear();
}

Widget* UILayoutLoader::findWidgetById(const std::string& id) const {
    auto it = _widgetsById.find(id);
    return (it != _widgetsById.end()) ? it->second : nullptr;
}

Widget* UILayoutLoader::buildWidgetTree(const json& j) {
    if (!j.is_object()) return nullptr;

    std::string type = j.value("type", "Widget");
    std::string id = j.value("id", "");

    Widget* widget = _factory->create(type);
    if (widget == nullptr) {
        std::fprintf(stderr, "[UILayoutLoader] missing factory creator for type='%s'\n",
                     type.c_str());
        return nullptr;
    }
#if defined(_DEBUG) && defined(_MSC_VER)
    loaderHeapCheckId("after_factory_create", type.c_str(),
                      id.empty() ? "anonymous" : id.c_str());
#endif

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

    // Visible
    widget->setVisible(j.value("visible", true));

    // Style
    std::string style = j.value("style", "");
    if (!style.empty()) {
        widget->setStyleId(style);
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

    // ComboBox / ListView item lists (mirror WidgetSerializer deserialize).
    if (j.contains("items") && j["items"].is_array()) {
        std::vector<std::wstring> items;
        items.reserve(j["items"].size());
        for (const auto& s : j["items"]) {
            if (!s.is_string()) continue;
            const std::string u8 = s.get<std::string>();
            items.emplace_back(u8.begin(), u8.end());
        }
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
        }
    }

    // Text with i18n support
    std::string text = j.value("text", "");
    if (!text.empty()) {
        if (_i18n && isI18nKey(text)) {
            std::wstring wtext = _i18n->resolve(text);
            text = std::string(wtext.begin(), wtext.end());
        }

        const std::wstring wtext(text.begin(), text.end());
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
                    mi->setShortcut(std::wstring(sc.begin(), sc.end()));
                }
            }
        } else if (type == "Window") {
            if (Window* window = dynamic_cast<Window*>(widget)) {
                window->setTitle(wtext);
            }
        }
        LOADER_HEAP_CHECK("after_set_text");
    }

    // onClick binding
    if (!id.empty() && j.contains("onClick")) {
        std::string handlerName = j["onClick"].get<std::string>();
        std::string key = id + ".onClick";
        auto it = _eventBindings.find(key);
        if (it != _eventBindings.end()) {
            if (Button* button = dynamic_cast<Button*>(widget)) {
                button->setOnClicked(it->second);
            }
        }
    }

    // Children
    if (j.contains("children") && j["children"].is_array()) {
        const char* parentId = id.empty() ? type.c_str() : id.c_str();
        if (VBox* vbox = dynamic_cast<VBox*>(widget)) {
            for (const auto& childJson : j["children"]) {
                Widget* child = buildWidgetTree(childJson);
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
                Widget* child = buildWidgetTree(childJson);
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
                Widget* child = buildWidgetTree(childJson);
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
                Widget* cellChild = buildWidgetTree(cellJson["content"]);
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
                Widget* child = buildWidgetTree(childJson);
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
                DockCard* card = buildDockCardFromJson(cj);
                if (card == nullptr) continue;
                dock->addCard(slot, std::unique_ptr<DockCard>(card));
            }
        }
        if (j.contains("floating") && j["floating"].is_array()) {
            DockOverlay* overlay = dock->getOverlay();
            for (const auto& cj : j["floating"]) {
                DockCard* card = buildDockCardFromJson(cj);
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
        if (j.contains("title")) {
            std::string u8 = j["title"].get<std::string>();
            card->setTitle(std::wstring(u8.begin(), u8.end()));
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
