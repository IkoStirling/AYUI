#include "AYUI/LayoutEditor/LayoutInteractionModel.h"

#include "AYUI/Widget.h"

#include <algorithm>
#include <utility>

namespace ayt::ui {
namespace {

std::wstring wide(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

} // namespace

void LayoutInteractionRegistry::replace(
    std::vector<LayoutControllerContract> controllers) {
    _controllers = std::move(controllers);
}

void LayoutInteractionRegistry::clear() {
    _controllers.clear();
}

const LayoutControllerContract* LayoutInteractionRegistry::findController(
    const std::string& id) const {
    const auto it = std::find_if(_controllers.begin(), _controllers.end(),
        [&id](const LayoutControllerContract& controller) {
            return controller.id == id;
        });
    return it != _controllers.end() ? &*it : nullptr;
}

const LayoutEventHandlerContract* LayoutInteractionRegistry::findHandler(
    const std::string& controllerId,
    const std::string& handlerName) const {
    const LayoutControllerContract* controller = findController(controllerId);
    if (controller == nullptr) return nullptr;
    const auto it = std::find_if(controller->handlers.begin(),
        controller->handlers.end(),
        [&handlerName](const LayoutEventHandlerContract& handler) {
            return handler.name == handlerName;
        });
    return it != controller->handlers.end() ? &*it : nullptr;
}

bool LayoutInteractionRegistry::handlerAccepts(
    const LayoutEventHandlerContract& handler,
    const std::string& eventName) const {
    return handler.acceptedEvents.empty() ||
        std::find(handler.acceptedEvents.begin(), handler.acceptedEvents.end(),
                  eventName) != handler.acceptedEvents.end();
}

void LayoutInteractionGraphModel::rebuild(
    const std::vector<Widget*>& authoredWidgets,
    const LayoutInteractionRegistry* registry) {
    _edges.clear();
    for (Widget* widget : authoredWidgets) {
        if (widget == nullptr) continue;
        for (const auto& [eventName, handlerName] :
             widget->getEventBindings()) {
            LayoutInteractionEdge edge;
            edge.widget = widget;
            edge.widgetId = widget->getId();
            edge.eventName = eventName;
            edge.controllerId = widget->getControllerId();
            edge.handlerName = handlerName;
            if (edge.controllerId.empty()) {
                edge.resolution =
                    LayoutInteractionResolution::MissingController;
            } else if (registry == nullptr) {
                edge.resolution = LayoutInteractionResolution::Unvalidated;
            } else if (registry->findController(edge.controllerId) == nullptr) {
                edge.resolution =
                    LayoutInteractionResolution::MissingController;
            } else if (const LayoutEventHandlerContract* handler =
                           registry->findHandler(edge.controllerId,
                                                 edge.handlerName)) {
                edge.resolution = registry->handlerAccepts(*handler, eventName)
                    ? LayoutInteractionResolution::Resolved
                    : LayoutInteractionResolution::EventTypeMismatch;
            } else {
                edge.resolution = LayoutInteractionResolution::MissingHandler;
            }
            _edges.push_back(std::move(edge));
        }
    }
    std::sort(_edges.begin(), _edges.end(),
        [](const LayoutInteractionEdge& left,
           const LayoutInteractionEdge& right) {
            if (left.widgetId != right.widgetId)
                return left.widgetId < right.widgetId;
            return left.eventName < right.eventName;
        });
}

std::size_t LayoutInteractionGraphModel::resolvedCount() const {
    return static_cast<std::size_t>(std::count_if(
        _edges.begin(), _edges.end(), [](const LayoutInteractionEdge& edge) {
            return edge.resolution == LayoutInteractionResolution::Resolved;
        }));
}

std::size_t LayoutInteractionGraphModel::unresolvedCount() const {
    return static_cast<std::size_t>(std::count_if(
        _edges.begin(), _edges.end(), [](const LayoutInteractionEdge& edge) {
            return edge.resolution != LayoutInteractionResolution::Resolved &&
                   edge.resolution != LayoutInteractionResolution::Unvalidated;
        }));
}

std::vector<std::wstring> LayoutInteractionGraphModel::displayLabels() const {
    std::vector<std::wstring> labels;
    labels.reserve(_edges.size());
    for (const LayoutInteractionEdge& edge : _edges) {
        const wchar_t* prefix = L"[?] ";
        switch (edge.resolution) {
        case LayoutInteractionResolution::Resolved: prefix = L"[OK] "; break;
        case LayoutInteractionResolution::MissingController:
            prefix = L"[C!] "; break;
        case LayoutInteractionResolution::MissingHandler:
            prefix = L"[H!] "; break;
        case LayoutInteractionResolution::EventTypeMismatch:
            prefix = L"[E!] "; break;
        case LayoutInteractionResolution::Unvalidated: break;
        }
        const std::wstring widgetName = edge.widgetId.empty()
            ? L"(anonymous)" : wide(edge.widgetId);
        const std::wstring controllerName = edge.controllerId.empty()
            ? L"(no controller)" : wide(edge.controllerId);
        labels.push_back(std::wstring(prefix) + widgetName + L"." +
            wide(edge.eventName) + L"  ->  " + controllerName + L"." +
            wide(edge.handlerName));
    }
    if (labels.empty()) labels.push_back(L"No event bindings");
    return labels;
}

} // namespace ayt::ui
