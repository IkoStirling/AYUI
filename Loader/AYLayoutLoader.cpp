#include "AYLayoutLoader.h"
#include "AYWidgetFactory.h"
#include "AYI18n.h"
#include "AYButton.h"
#include "AYTextLabel.h"
#include "AYWindow.h"
#include "AYBox.h"
#include "AYSplitterHandle.h"
#include "AYImage.h"
#include "AYComboBox.h"
#include "AYListView.h"

#include <ayio/FileWatcher.h>

#include <fstream>
#include <sstream>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_DEBUG) && defined(_MSC_VER)
#  include <crtdbg.h>

namespace {

void loaderHeapCheck(const char* label)
{
    if (!_CrtCheckMemory()) {
        std::fprintf(stderr, "[LoaderHeapCheck] FAIL at %s\n", label);
        _CrtDbgBreak();
    } else {
        std::fprintf(stderr, "[LoaderHeapCheck] OK at %s\n", label);
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

} // namespace

UILayoutLoader::UILayoutLoader()
    : _factory(&WidgetFactory::get())
    , _i18n(&I18n::get())
    , _watcher(std::make_unique<ayt::io::FileWatcher>())
{
}

UILayoutLoader::~UILayoutLoader() {
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
    _widgetsById.clear();

    try {
        json j = json::parse(jsonStr);
        LOADER_HEAP_CHECK("after_json_parse");
        Widget* root = buildWidgetTree(j);
        LOADER_HEAP_CHECK("after_build_widget_tree");
        return root;
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "[UILayoutLoader] parse error: %s\n", e.what());
        return nullptr;
    }
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
    loaderHeapCheckId("after_factory_create", type.c_str(),
                      id.empty() ? "anonymous" : id.c_str());

    // ID
    if (!id.empty()) {
        widget->setId(id);
        _widgetsById[id] = widget;
        std::fprintf(stderr, "[UILayoutLoader] built widget type='%s' id='%s'\n", type.c_str(),
                     id.c_str());
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
                float slotHeight = 0.0f;
                if (childJson.contains("size") && childJson["size"].is_object()) {
                    slotHeight = childJson["size"].value("h", 0.0f);
                }
                vbox->addWidget(child, slotHeight);
                LOADER_HEAP_CHECK_ATTACH(parentId, child->getId().empty() ? "child" : child->getId().c_str());
            }
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

    if (!id.empty()) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "after_build_%s", id.c_str());
        LOADER_HEAP_CHECK(buf);
    }

    return widget;
}

} // namespace ayt::ui