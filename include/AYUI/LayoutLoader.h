#pragma once

#include "AYUI/Version.h"

#include "AYUI/Widget.h"
#include "AYUI/DockJsonHandle.h"
#include "AYUI/UIAnimation.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <vector>

namespace ayt::io {
class FileWatcher;
struct FileWatchEvent;
}

namespace ayt::ui {

class Widget;
class WidgetFactory;
class I18n;
class DockCard;

class UILayoutLoader {
public:
    UILayoutLoader();
    ~UILayoutLoader();

    void setWidgetFactory(WidgetFactory* factory) { _factory = factory; }
    void setI18n(I18n* i18n) { _i18n = i18n; }

    Widget* loadFromFile(const std::string& filepath);
    Widget* loadFromString(const std::string& json);

    // Stop filesystem watching for the last loaded path (no-op if idle).
    // Layout editors call this after chrome load so Save of a sibling
    // *.ui.json cannot hot-reload/destroy the chrome mid-button-callback.
    void stopHotReload();

    // D4 (2026-07-26): Layout persistence.
    // saveLayout serializes the Widget tree via WidgetSerializer and writes
    // it to disk; saveLayoutToString does the same into an out-param. Both
    // round-trip cleanly through loadFromFile / loadFromString because the
    // wire format is the same as what loadFromString already accepts. The
    // `root` is non-owning — caller retains its lifetime. Returns false on
    // nullptr root or filesystem failure (the file write reports via stderr
    // for parity with loadFromString's parse-error path).
    bool saveLayout(const std::string& filepath, Widget* root, bool pretty = true);
    bool saveLayoutToString(Widget* root, std::string& outJson, bool pretty = true);
    // Atomically persist an already-encoded layout document. Authoring tools
    // use this for the optional { reusable, root } envelope while retaining
    // the same temporary-file and replace guarantees as saveLayout().
    bool saveJsonDocument(const std::string& filepath,
                          const std::string& jsonDocument);

    Widget* reload(const std::string& id);
    bool isReloadNeeded();
    Widget* tryReload();

    void bindEvent(const std::string& widgetId, const std::string& eventType,
                   std::function<void()> handler);
    // Name-based bindings used by declarative controller metadata. Resolution
    // order is widget-id/event (legacy), controller/handler, then handler.
    void bindControllerEvent(const std::string& controllerId,
                             const std::string& handlerName,
                             std::function<void()> handler);
    void bindHandler(const std::string& handlerName,
                     std::function<void()> handler);
    void clearEventBindings();
    void clearWidgetRegistry();

    Widget* findWidgetById(const std::string& id) const;
    const UIAnimationLibrary& getAnimationLibrary() const {
        return _animationLibrary;
    }
    // Returned callbacks reference widgets owned by the most recently loaded
    // root. Destroy the timeline before destroying or replacing that tree.
    AnimationTimeline createAnimationTimeline(
        const std::string& clipName,
        std::size_t* unresolvedTrackCount = nullptr) const;

private:
    Widget* buildWidgetTree(JsonHandle j);
    DockCard* buildDockCardFromJson(JsonHandle cj);
    // Drain the watcher queue and update _dirty. Returns _dirty after the poll.
    // R-4: const-ness relaxed vs the old mtime-based design because FileWatcher
    // pollPending mutates internal queues. Callers that only check the flag
    // can still treat isReloadNeeded as "logically const".
    bool pollWatcherAndCheckDirty();

    WidgetFactory* _factory;
    I18n* _i18n;

    std::unordered_map<std::string, std::function<void()>> _eventBindings;
    std::unordered_map<std::string, Widget*> _widgetsById;
    UIAnimationLibrary _animationLibrary;

    std::string _lastJson;
    std::string _lastFilePath;

    // R-4: each loader owns its own ayio::FileWatcher so multiple loaders can
    // watch independent layout files. The watcher is started lazily by the
    // first loadFromFile() and lives for the loader's lifetime.
    std::unique_ptr<ayt::io::FileWatcher> _watcher;
    bool _dirty = false;          // set when pollPending saw events for our file
    bool _watcherStarted = false; // track first-start so start() is idempotent
};

} // namespace ayt::ui
