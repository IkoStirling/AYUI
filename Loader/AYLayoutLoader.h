#pragma once

#include "AYWidget.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>

namespace ayt::io {
class FileWatcher;
struct FileWatchEvent;
}

namespace ayt::ui {
using json = nlohmann::json;

class Widget;
class WidgetFactory;
class I18n;

class UILayoutLoader {
public:
    UILayoutLoader();
    ~UILayoutLoader();

    void setWidgetFactory(WidgetFactory* factory) { _factory = factory; }
    void setI18n(I18n* i18n) { _i18n = i18n; }

    Widget* loadFromFile(const std::string& filepath);
    Widget* loadFromString(const std::string& json);

    Widget* reload(const std::string& id);
    bool isReloadNeeded();
    Widget* tryReload();

    void bindEvent(const std::string& widgetId, const std::string& eventType,
                   std::function<void()> handler);
    void clearEventBindings();
    void clearWidgetRegistry();

    Widget* findWidgetById(const std::string& id) const;

private:
    Widget* buildWidgetTree(const json& j);
    // Drain the watcher queue and update _dirty. Returns _dirty after the poll.
    // R-4: const-ness relaxed vs the old mtime-based design because FileWatcher
    // pollPending mutates internal queues. Callers that only check the flag
    // can still treat isReloadNeeded as "logically const".
    bool pollWatcherAndCheckDirty();

    WidgetFactory* _factory;
    I18n* _i18n;

    std::unordered_map<std::string, std::function<void()>> _eventBindings;
    std::unordered_map<std::string, Widget*> _widgetsById;

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