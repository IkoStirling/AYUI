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

    // C-3 focus + keyboard routing. setFocus replaces the currently
    // focused widget (if any) with the new one; pass nullptr to drop
    // focus. onKeyDown / onKeyUp route to the focused widget if it
    // exists and is visible / not destroyed. onTextInput routes typed
    // characters to the focused widget. Returns true if the event was
    // consumed.
    void  setFocus(Widget* widget);
    Widget* getFocusedWidget() const { return _focusedWidget; }
    bool  onKeyDown(int keyCode);
    bool  onKeyUp(int keyCode);
    bool  onTextInput(wchar_t ch);

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
    Widget* _focusedWidget = nullptr;
    bool _shutdown = false;
    // Phase UI-PERF-1: track the last client size we laid out against. If
    // layout() is invoked again with the same values and no explicit tree
    // mutation has occurred, skip performLayout entirely. Set to a sentinel
    // (-1, -1) after tree mutations so the next layout() always re-runs.
    float _lastLayoutWidth = -1.0f;
    float _lastLayoutHeight = -1.0f;

    // Last pointer position from onMouseMove / onMouseLeave. update()
    // re-hit-tests against this so a missed leave (hit stayed on a fat
    // splitter band, etc.) is corrected every frame before render.
    float _lastMouseX = 0.0f;
    float _lastMouseY = 0.0f;
    bool _hasLastMouse = false;
};

} // namespace ayt::ui
