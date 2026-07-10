#pragma once

#include "AYLayoutLoader.h"
#include "AYIRenderBackend.h"

#include <memory>
#include <string>

namespace ayt::ui {

class UIManager {
public:
    UIManager() = default;
    // RAII: destructor delegates to shutdown() so factory-allocated widget
    // trees are released via destroyWidgetTree even if the caller forgets
    // to invoke shutdown() explicitly. Idempotent — shutdown() guards
    // itself with _shutdown.
    ~UIManager() { shutdown(); }

    static UIManager& get();

    void initialize(IRenderBackend* backend);
    void shutdown();

    bool loadLayout(const std::string& path);
    bool loadFromString(const std::string& json);
    void bindEvent(const std::string& widgetId, const std::string& eventType,
                   std::function<void()> handler);

    void setClientSize(float width, float height);
    void update(float dt);
    void layout();
    void render();

    Widget* root() const { return _root; }
    Widget* findById(const std::string& id) const;

    bool onMouseMove(float x, float y);
    bool onMouseButtonDown(float x, float y, int button);
    bool onMouseButtonUp(float x, float y, int button);
    void onMouseLeave();
    void clearHover();

    bool isHoverInteractive() const;
    UiCursorHint getCursorHint() const;
    bool isCapturing() const { return _capturedWidget != nullptr; }
    void cancelCapture();

    UILayoutLoader& loader() { return _loader; }
    IRenderBackend* backend() const { return _backend; }

private:
    IRenderBackend* _backend = nullptr;
    Widget* _root = nullptr;
    UILayoutLoader _loader;
    float _clientWidth = 1280.0f;
    float _clientHeight = 720.0f;
    Widget* _capturedWidget = nullptr;
    Widget* _hoverWidget = nullptr;
    bool _shutdown = false;
    // Phase UI-PERF-1: track the last client size we laid out against. If
    // layout() is invoked again with the same values and no explicit tree
    // mutation has occurred, skip performLayout entirely. Set to a sentinel
    // (-1, -1) after tree mutations so the next layout() always re-runs.
    float _lastLayoutWidth = -1.0f;
    float _lastLayoutHeight = -1.0f;
};

} // namespace ayt::ui
