#pragma once

#include "AYUI/Version.h"

#include "AYUI/Widget.h"
#include "AYUI/DockJsonHandle.h"
#include "AYUI/UIAnimation.h"
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cstdint>
#include <utility>
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

// Lightweight telemetry for the most recent load attempt. Durations are
// diagnostic only; callers should use the structural counters for regression
// checks because wall-clock timings vary across machines and build modes.
struct UILayoutLoadStats {
    bool succeeded = false;
    std::size_t inputBytes = 0;
    std::size_t widgetCount = 0;
    std::size_t maxDepth = 0;
    std::size_t registeredIdCount = 0;
    std::uint64_t parseMicroseconds = 0;
    std::uint64_t buildMicroseconds = 0;
    std::uint64_t heapValidationCount = 0;
};

class UILayoutLoader {
public:
    using TextResolver = std::function<std::wstring(
        std::string_view key, std::wstring_view fallback)>;
    using DeclarativeEventResolver = std::function<std::function<void()>(
        const Widget& widget,
        std::string_view eventName,
        std::string_view handlerName)>;

    UILayoutLoader();
    ~UILayoutLoader();

    void setWidgetFactory(WidgetFactory* factory) { _factory = factory; }
    void setI18n(I18n* i18n) { _i18n = i18n; }
    void setTextResolver(TextResolver resolver) {
        _textResolver = std::move(resolver);
    }
    const TextResolver& textResolver() const { return _textResolver; }

    // Re-resolve every localized property in an existing widget tree without
    // rebuilding it. Runtime state such as selection and scroll position is
    // preserved for localized item collections.
    void retranslate(Widget* root) const;

    Widget* loadFromFile(const std::string& filepath);
    Widget* loadFromString(const std::string& json);
    const UILayoutLoadStats& getLastLoadStats() const noexcept {
        return _lastLoadStats;
    }

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
    // Final, dynamic fallback for reusable layouts whose semantic handlers
    // are supplied by a runtime host after authoring. Explicit widget,
    // controller and named bindings retain precedence.
    void setDeclarativeEventResolver(DeclarativeEventResolver resolver) {
        _declarativeEventResolver = std::move(resolver);
    }
    void clearEventBindings();
    void clearWidgetRegistry();

    Widget* findWidgetById(const std::string& id) const;
    bool hasDeclarativeEventHandler(std::string_view handlerName) const {
        return _declarativeEventHandlers.contains(std::string(handlerName));
    }
    const std::unordered_set<std::string>& getDeclarativeEventHandlers()
        const noexcept {
        return _declarativeEventHandlers;
    }
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
    std::wstring resolveLocalizedString(JsonHandle j,
                                        std::string_view valueProperty,
                                        std::string_view keyProperty) const;
    std::vector<std::wstring> resolveLocalizedList(
        JsonHandle j, std::string_view valueProperty,
        std::string_view keyProperty) const;
    // Drain the watcher queue and update _dirty. Returns _dirty after the poll.
    // R-4: const-ness relaxed vs the old mtime-based design because FileWatcher
    // pollPending mutates internal queues. Callers that only check the flag
    // can still treat isReloadNeeded as "logically const".
    bool pollWatcherAndCheckDirty();

    WidgetFactory* _factory;
    I18n* _i18n;
    TextResolver _textResolver;

    std::unordered_map<std::string, std::function<void()>> _eventBindings;
    DeclarativeEventResolver _declarativeEventResolver;
    std::unordered_map<std::string, Widget*> _widgetsById;
    std::unordered_set<std::string> _declarativeEventHandlers;
    UIAnimationLibrary _animationLibrary;
    UILayoutLoadStats _lastLoadStats;
    std::size_t _buildDepth = 0;

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
