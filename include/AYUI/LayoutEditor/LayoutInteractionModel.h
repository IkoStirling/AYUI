#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ayt::ui {

class Widget;

struct LayoutEventHandlerContract {
    std::string name;
    // Empty means the handler accepts every event exposed by the Widget.
    std::vector<std::string> acceptedEvents;
};

struct LayoutControllerContract {
    std::string id;
    std::vector<LayoutEventHandlerContract> handlers;
};

class LayoutInteractionRegistry {
public:
    void replace(std::vector<LayoutControllerContract> controllers);
    void clear();

    const std::vector<LayoutControllerContract>& controllers() const {
        return _controllers;
    }
    const LayoutControllerContract* findController(
        const std::string& id) const;
    const LayoutEventHandlerContract* findHandler(
        const std::string& controllerId,
        const std::string& handlerName) const;
    bool handlerAccepts(const LayoutEventHandlerContract& handler,
                        const std::string& eventName) const;

private:
    std::vector<LayoutControllerContract> _controllers;
};

enum class LayoutInteractionResolution {
    Resolved,
    Unvalidated,
    MissingController,
    MissingHandler,
    EventTypeMismatch,
};

struct LayoutInteractionEdge {
    Widget* widget = nullptr;
    std::string widgetId;
    std::string eventName;
    std::string controllerId;
    std::string handlerName;
    LayoutInteractionResolution resolution =
        LayoutInteractionResolution::Unvalidated;
};

// Read-only authoring graph. Runtime dispatch remains owned by UILayoutLoader;
// this model only projects serialized Widget metadata against a host contract.
class LayoutInteractionGraphModel {
public:
    void rebuild(const std::vector<Widget*>& authoredWidgets,
                 const LayoutInteractionRegistry* registry = nullptr);

    const std::vector<LayoutInteractionEdge>& edges() const { return _edges; }
    std::size_t resolvedCount() const;
    std::size_t unresolvedCount() const;
    std::vector<std::wstring> displayLabels() const;

private:
    std::vector<LayoutInteractionEdge> _edges;
};

} // namespace ayt::ui
