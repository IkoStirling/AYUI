#include "AYUI/AccessibilityAdapter.h"
#include "AYUI/UIManager.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <ole2.h>
#  include <UIAutomation.h>
#endif

namespace ayt::ui {
namespace {

struct FlatAccessibilityNode {
    AccessibilityNode node;
    uint64_t parentId = 0;
    size_t siblingIndex = 0;
    std::vector<uint64_t> childIds;
};

using FlatAccessibilityTree = std::unordered_map<uint64_t, FlatAccessibilityNode>;

void flattenNode(const AccessibilityNode& node, uint64_t parentId,
                 size_t siblingIndex, FlatAccessibilityTree& out) {
    FlatAccessibilityNode flat;
    flat.node = node;
    flat.node.children.clear();
    flat.parentId = parentId;
    flat.siblingIndex = siblingIndex;
    flat.childIds.reserve(node.children.size());
    for (const AccessibilityNode& child : node.children) {
        flat.childIds.push_back(child.id);
    }
    out[node.id] = std::move(flat);
    for (size_t i = 0; i < node.children.size(); ++i) {
        flattenNode(node.children[i], node.id, i, out);
    }
}

FlatAccessibilityTree flattenTree(const AccessibilityNode& root) {
    FlatAccessibilityTree out;
    flattenNode(root, 0, 0, out);
    return out;
}

bool sameBounds(const math::FRectangle& left, const math::FRectangle& right) {
    return left.minX == right.minX && left.minY == right.minY
        && left.maxX == right.maxX && left.maxY == right.maxY;
}

bool sameNumericValue(const AccessibilityNode& left,
                      const AccessibilityNode& right) {
    return left.hasNumericRange == right.hasNumericRange
        && left.numericReadOnly == right.numericReadOnly
        && left.numericValue == right.numericValue
        && left.numericMinimum == right.numericMinimum
        && left.numericMaximum == right.numericMaximum
        && left.numericSmallChange == right.numericSmallChange
        && left.numericLargeChange == right.numericLargeChange;
}

void appendChanges(const FlatAccessibilityTree& oldTree,
                   const FlatAccessibilityTree& newTree,
                   std::vector<AccessibilityChange>& out) {
    bool treeChanged = oldTree.size() != newTree.size();
    for (const auto& entry : newTree) {
        const uint64_t id = entry.first;
        const FlatAccessibilityNode& current = entry.second;
        const auto oldIt = oldTree.find(id);
        if (oldIt == oldTree.end()) {
            treeChanged = true;
            continue;
        }
        const FlatAccessibilityNode& previous = oldIt->second;
        if (previous.parentId != current.parentId
            || previous.childIds != current.childIds
            || previous.node.role != current.node.role) {
            treeChanged = true;
        }
        if (previous.node.label != current.node.label) {
            out.push_back({AccessibilityChangeKind::Name, id});
        }
        if (previous.node.description != current.node.description) {
            out.push_back({AccessibilityChangeKind::Description, id});
        }
        if (previous.node.value != current.node.value
            || !sameNumericValue(previous.node, current.node)) {
            out.push_back({AccessibilityChangeKind::Value, id});
        }
        if (!sameBounds(previous.node.bounds, current.node.bounds)) {
            out.push_back({AccessibilityChangeKind::Bounds, id});
        }
        if (previous.node.states != current.node.states) {
            const uint32_t focusMask = AccessibilityState_Focused;
            if ((previous.node.states & focusMask) != (current.node.states & focusMask)) {
                out.push_back({AccessibilityChangeKind::Focus, id});
            }
            if ((previous.node.states & ~focusMask) != (current.node.states & ~focusMask)) {
                out.push_back({AccessibilityChangeKind::State, id});
            }
        }
    }
    for (const auto& entry : oldTree) {
        if (newTree.find(entry.first) == newTree.end()) {
            treeChanged = true;
            break;
        }
    }
    if (treeChanged) out.insert(out.begin(), {AccessibilityChangeKind::Tree, 0});
}

#if defined(_WIN32)

constexpr UINT kAccessibilityActionMessage = WM_APP + 0x35A;

enum class NativeActionKind : uint8_t { Semantic, Numeric };

struct NativeActionRequest {
    NativeActionKind kind = NativeActionKind::Semantic;
    uint64_t nodeId = 0;
    AccessibilityAction action = AccessibilityAction::Focus;
    double value = 0.0;
    bool result = false;
};

struct WindowsAccessibilityState {
    mutable std::mutex mutex;
    UIManager* manager = nullptr;
    HWND window = nullptr;
    DWORD ownerThread = 0;
    float scale = 1.0f;
    FlatAccessibilityTree nodes;
    bool alive = true;

    bool getNode(uint64_t id, FlatAccessibilityNode& out) const {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = nodes.find(id);
        if (!alive || it == nodes.end()) return false;
        out = it->second;
        return true;
    }

    bool perform(uint64_t id, AccessibilityAction action) {
        UIManager* target = nullptr;
        HWND targetWindow = nullptr;
        DWORD targetThread = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!alive) return false;
            target = manager;
            targetWindow = window;
            targetThread = ownerThread;
        }
        if (target == nullptr) return false;
        if (::GetCurrentThreadId() == targetThread) {
            return target->performAccessibilityAction(id, action);
        }
        if (targetWindow == nullptr || !::IsWindow(targetWindow)) return false;
        NativeActionRequest request;
        request.nodeId = id;
        request.action = action;
        ::SendMessageW(targetWindow, kAccessibilityActionMessage,
                       reinterpret_cast<WPARAM>(&request), 0);
        return request.result;
    }

    bool setNumericValue(uint64_t id, double value) {
        UIManager* target = nullptr;
        HWND targetWindow = nullptr;
        DWORD targetThread = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!alive) return false;
            target = manager;
            targetWindow = window;
            targetThread = ownerThread;
        }
        if (target == nullptr) return false;
        if (::GetCurrentThreadId() == targetThread) {
            return target->setAccessibilityNumericValue(id, value);
        }
        if (targetWindow == nullptr || !::IsWindow(targetWindow)) return false;
        NativeActionRequest request;
        request.kind = NativeActionKind::Numeric;
        request.nodeId = id;
        request.value = value;
        ::SendMessageW(targetWindow, kAccessibilityActionMessage,
                       reinterpret_cast<WPARAM>(&request), 0);
        return request.result;
    }

    UiaRect screenBounds(const math::FRectangle& bounds) const {
        HWND targetWindow = nullptr;
        float targetScale = 1.0f;
        {
            std::lock_guard<std::mutex> lock(mutex);
            targetWindow = window;
            targetScale = scale;
        }
        POINT origin{0, 0};
        if (targetWindow != nullptr) ::ClientToScreen(targetWindow, &origin);
        UiaRect out{};
        out.left = static_cast<double>(origin.x) + bounds.minX * targetScale;
        out.top = static_cast<double>(origin.y) + bounds.minY * targetScale;
        out.width = std::max(0.0, static_cast<double>(bounds.maxX - bounds.minX)
                                 * targetScale);
        out.height = std::max(0.0, static_cast<double>(bounds.maxY - bounds.minY)
                                  * targetScale);
        return out;
    }

    uint64_t hitTest(double x, double y) const {
        FlatAccessibilityTree snapshot;
        HWND targetWindow = nullptr;
        float targetScale = 1.0f;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!alive) return 0;
            snapshot = nodes;
            targetWindow = window;
            targetScale = scale;
        }
        POINT origin{0, 0};
        if (targetWindow != nullptr) ::ClientToScreen(targetWindow, &origin);
        const auto visit = [&](const auto& self, uint64_t id) -> uint64_t {
            const auto it = snapshot.find(id);
            if (it == snapshot.end()) return UINT64_MAX;
            const math::FRectangle& b = it->second.node.bounds;
            const double left = origin.x + b.minX * targetScale;
            const double top = origin.y + b.minY * targetScale;
            const double right = origin.x + b.maxX * targetScale;
            const double bottom = origin.y + b.maxY * targetScale;
            if (x < left || x > right || y < top || y > bottom) return UINT64_MAX;
            for (auto child = it->second.childIds.rbegin();
                 child != it->second.childIds.rend(); ++child) {
                const uint64_t found = self(self, *child);
                if (found != UINT64_MAX) return found;
            }
            return id;
        };
        const uint64_t found = visit(visit, 0);
        return found == UINT64_MAX ? 0 : found;
    }
};

long controlTypeForRole(AccessibilityRole role) {
    switch (role) {
    case AccessibilityRole::Window:
    case AccessibilityRole::Dialog: return UIA_WindowControlTypeId;
    case AccessibilityRole::Group: return UIA_GroupControlTypeId;
    case AccessibilityRole::Button: return UIA_ButtonControlTypeId;
    case AccessibilityRole::CheckBox: return UIA_CheckBoxControlTypeId;
    case AccessibilityRole::RadioButton: return UIA_RadioButtonControlTypeId;
    case AccessibilityRole::StaticText: return UIA_TextControlTypeId;
    case AccessibilityRole::TextField:
    case AccessibilityRole::TextArea: return UIA_EditControlTypeId;
    case AccessibilityRole::Image: return UIA_ImageControlTypeId;
    case AccessibilityRole::List: return UIA_ListControlTypeId;
    case AccessibilityRole::ListItem: return UIA_ListItemControlTypeId;
    case AccessibilityRole::Tree: return UIA_TreeControlTypeId;
    case AccessibilityRole::TreeItem: return UIA_TreeItemControlTypeId;
    case AccessibilityRole::TabList: return UIA_TabControlTypeId;
    case AccessibilityRole::Tab: return UIA_TabItemControlTypeId;
    case AccessibilityRole::Slider: return UIA_SliderControlTypeId;
    case AccessibilityRole::ProgressBar: return UIA_ProgressBarControlTypeId;
    case AccessibilityRole::ComboBox: return UIA_ComboBoxControlTypeId;
    case AccessibilityRole::MenuBar: return UIA_MenuBarControlTypeId;
    case AccessibilityRole::Menu: return UIA_MenuControlTypeId;
    case AccessibilityRole::MenuItem: return UIA_MenuItemControlTypeId;
    case AccessibilityRole::ToolBar: return UIA_ToolBarControlTypeId;
    case AccessibilityRole::Separator: return UIA_SeparatorControlTypeId;
    case AccessibilityRole::ScrollBar: return UIA_ScrollBarControlTypeId;
    case AccessibilityRole::RichText: return UIA_DocumentControlTypeId;
    case AccessibilityRole::Generic:
    default: return UIA_CustomControlTypeId;
    }
}

std::wstring classNameForRole(AccessibilityRole role) {
    std::wstring result = L"AYUI.";
    const char* name = accessibilityRoleName(role);
    while (name != nullptr && *name != '\0') {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*name)));
        ++name;
    }
    return result;
}

bool hasAction(const AccessibilityNode& node, AccessibilityAction action) {
    return (node.actions & accessibilityActionMask(action)) != 0;
}

HRESULT setVariantString(VARIANT* value, const std::wstring& text) {
    value->vt = VT_BSTR;
    value->bstrVal = ::SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    return value->bstrVal != nullptr || text.empty() ? S_OK : E_OUTOFMEMORY;
}

void setVariantBool(VARIANT* value, bool state) {
    value->vt = VT_BOOL;
    value->boolVal = state ? VARIANT_TRUE : VARIANT_FALSE;
}

void setVariantInt(VARIANT* value, long number) {
    value->vt = VT_I4;
    value->lVal = number;
}

void setVariantDouble(VARIANT* value, double number) {
    value->vt = VT_R8;
    value->dblVal = number;
}

class WindowsUiaProvider final : public IRawElementProviderSimple,
                                 public IRawElementProviderFragment,
                                 public IRawElementProviderFragmentRoot,
                                 public IInvokeProvider,
                                 public IToggleProvider,
                                 public IRangeValueProvider,
                                 public IExpandCollapseProvider,
                                 public ISelectionItemProvider {
public:
    WindowsUiaProvider(std::shared_ptr<WindowsAccessibilityState> state,
                       uint64_t nodeId)
        : _state(std::move(state)), _nodeId(nodeId) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        FlatAccessibilityNode record;
        const bool available = _state && _state->getNode(_nodeId, record);
        if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple) {
            *object = static_cast<IRawElementProviderSimple*>(this);
        } else if (iid == IID_IRawElementProviderFragment) {
            *object = static_cast<IRawElementProviderFragment*>(this);
        } else if (iid == IID_IRawElementProviderFragmentRoot && _nodeId == 0) {
            *object = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else if (available && iid == IID_IInvokeProvider
                   && hasAction(record.node, AccessibilityAction::Press)) {
            *object = static_cast<IInvokeProvider*>(this);
        } else if (available && iid == IID_IToggleProvider
                   && hasAction(record.node, AccessibilityAction::Toggle)) {
            *object = static_cast<IToggleProvider*>(this);
        } else if (available && iid == IID_IRangeValueProvider
                   && record.node.hasNumericRange) {
            *object = static_cast<IRangeValueProvider*>(this);
        } else if (available && iid == IID_IExpandCollapseProvider
                   && (hasAction(record.node, AccessibilityAction::Expand)
                       || hasAction(record.node, AccessibilityAction::Collapse))) {
            *object = static_cast<IExpandCollapseProvider*>(this);
        } else if (available && iid == IID_ISelectionItemProvider
                   && hasAction(record.node, AccessibilityAction::Select)) {
            *object = static_cast<ISelectionItemProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++_references; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --_references;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (value == nullptr) return E_POINTER;
        *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider
            | ProviderOptions_ProviderOwnsSetFocus | ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,
                                                 IUnknown** value) override {
        if (value == nullptr) return E_POINTER;
        *value = nullptr;
        const auto queryPattern = [&](REFIID iid) {
            const HRESULT result = QueryInterface(iid, reinterpret_cast<void**>(value));
            return result == E_NOINTERFACE ? S_OK : result;
        };
        if (pattern == UIA_InvokePatternId) return queryPattern(IID_IInvokeProvider);
        if (pattern == UIA_TogglePatternId) return queryPattern(IID_IToggleProvider);
        if (pattern == UIA_RangeValuePatternId) return queryPattern(IID_IRangeValueProvider);
        if (pattern == UIA_ExpandCollapsePatternId) {
            return queryPattern(IID_IExpandCollapseProvider);
        }
        if (pattern == UIA_SelectionItemPatternId) {
            return queryPattern(IID_ISelectionItemProvider);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,
                                               VARIANT* value) override {
        if (value == nullptr) return E_POINTER;
        ::VariantInit(value);
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const AccessibilityNode& node = record.node;
        switch (property) {
        case UIA_ControlTypePropertyId: setVariantInt(value, controlTypeForRole(node.role)); break;
        case UIA_NamePropertyId: return setVariantString(value, node.label);
        case UIA_HelpTextPropertyId: return setVariantString(value, node.description);
        case UIA_AutomationIdPropertyId:
            return setVariantString(value, L"ayui-" + std::to_wstring(node.id));
        case UIA_ClassNamePropertyId:
            return setVariantString(value, classNameForRole(node.role));
        case UIA_FrameworkIdPropertyId: return setVariantString(value, L"AYUI");
        case UIA_ProcessIdPropertyId: setVariantInt(value, static_cast<long>(::GetCurrentProcessId())); break;
        case UIA_NativeWindowHandlePropertyId:
            if (_nodeId == 0) setVariantInt(value, static_cast<long>(reinterpret_cast<intptr_t>(_state->window)));
            break;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId: setVariantBool(value, true); break;
        case UIA_HasKeyboardFocusPropertyId:
            setVariantBool(value, (node.states & AccessibilityState_Focused) != 0); break;
        case UIA_IsKeyboardFocusablePropertyId:
            setVariantBool(value, (node.states & AccessibilityState_Focusable) != 0); break;
        case UIA_IsEnabledPropertyId:
            setVariantBool(value, (node.states & AccessibilityState_Enabled) != 0); break;
        case UIA_IsPasswordPropertyId:
            setVariantBool(value, (node.states & AccessibilityState_Password) != 0); break;
        case UIA_IsOffscreenPropertyId:
            setVariantBool(value, (node.states & AccessibilityState_Offscreen) != 0); break;
        case UIA_ValueValuePropertyId: return setVariantString(value, node.value);
        case UIA_IsInvokePatternAvailablePropertyId:
            setVariantBool(value, hasAction(node, AccessibilityAction::Press)); break;
        case UIA_IsTogglePatternAvailablePropertyId:
            setVariantBool(value, hasAction(node, AccessibilityAction::Toggle)); break;
        case UIA_IsRangeValuePatternAvailablePropertyId:
            setVariantBool(value, node.hasNumericRange); break;
        case UIA_IsExpandCollapsePatternAvailablePropertyId:
            setVariantBool(value, hasAction(node, AccessibilityAction::Expand)
                                  || hasAction(node, AccessibilityAction::Collapse)); break;
        case UIA_IsSelectionItemPatternAvailablePropertyId:
            setVariantBool(value, hasAction(node, AccessibilityAction::Select)); break;
        case UIA_ToggleToggleStatePropertyId:
            setVariantInt(value, (node.states & AccessibilityState_Indeterminate)
                ? ToggleState_Indeterminate
                : ((node.states & AccessibilityState_Checked) ? ToggleState_On : ToggleState_Off));
            break;
        case UIA_RangeValueValuePropertyId: setVariantDouble(value, node.numericValue); break;
        case UIA_RangeValueMinimumPropertyId: setVariantDouble(value, node.numericMinimum); break;
        case UIA_RangeValueMaximumPropertyId: setVariantDouble(value, node.numericMaximum); break;
        case UIA_RangeValueSmallChangePropertyId: setVariantDouble(value, node.numericSmallChange); break;
        case UIA_RangeValueLargeChangePropertyId: setVariantDouble(value, node.numericLargeChange); break;
        case UIA_RangeValueIsReadOnlyPropertyId: setVariantBool(value, node.numericReadOnly); break;
        case UIA_ExpandCollapseExpandCollapseStatePropertyId:
            setVariantInt(value, (node.states & AccessibilityState_Expanded)
                ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed);
            break;
        case UIA_SelectionItemIsSelectedPropertyId:
            setVariantBool(value, (node.states & AccessibilityState_Selected) != 0); break;
        default: break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** value) override {
        if (value == nullptr) return E_POINTER;
        *value = nullptr;
        if (_nodeId != 0 || !_state || _state->window == nullptr) return S_OK;
        return ::UiaHostProviderFromHwnd(_state->window, value);
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** value) override {
        if (value == nullptr) return E_POINTER;
        *value = nullptr;
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        uint64_t target = UINT64_MAX;
        if (direction == NavigateDirection_Parent && _nodeId != 0) {
            target = record.parentId;
        } else if (direction == NavigateDirection_FirstChild && !record.childIds.empty()) {
            target = record.childIds.front();
        } else if (direction == NavigateDirection_LastChild && !record.childIds.empty()) {
            target = record.childIds.back();
        } else if ((direction == NavigateDirection_NextSibling
                    || direction == NavigateDirection_PreviousSibling) && _nodeId != 0) {
            FlatAccessibilityNode parent;
            if (_state->getNode(record.parentId, parent)) {
                if (direction == NavigateDirection_NextSibling
                    && record.siblingIndex + 1u < parent.childIds.size()) {
                    target = parent.childIds[record.siblingIndex + 1u];
                } else if (direction == NavigateDirection_PreviousSibling
                           && record.siblingIndex > 0u) {
                    target = parent.childIds[record.siblingIndex - 1u];
                }
            }
        }
        if (target == UINT64_MAX) return S_OK;
        *value = static_cast<IRawElementProviderFragment*>(
            new WindowsUiaProvider(_state, target));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (value == nullptr) return E_POINTER;
        *value = ::SafeArrayCreateVector(VT_I4, 0, 4);
        if (*value == nullptr) return E_OUTOFMEMORY;
        const intptr_t windowId = _state ? reinterpret_cast<intptr_t>(_state->window) : 0;
        const LONG values[4] = {
            UiaAppendRuntimeId,
            static_cast<LONG>(windowId & 0xffffffffu),
            static_cast<LONG>(_nodeId & 0xffffffffu),
            static_cast<LONG>((_nodeId >> 32u) & 0xffffffffu),
        };
        for (LONG index = 0; index < 4; ++index) {
            LONG element = values[index];
            const HRESULT result = ::SafeArrayPutElement(*value, &index, &element);
            if (FAILED(result)) {
                ::SafeArrayDestroy(*value);
                *value = nullptr;
                return result;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (value == nullptr) return E_POINTER;
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = _state->screenBounds(record.node.bounds);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (value == nullptr) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return invokeAction(AccessibilityAction::Focus);
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (value == nullptr) return E_POINTER;
        *value = static_cast<IRawElementProviderFragmentRoot*>(
            new WindowsUiaProvider(_state, 0));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(
        double x, double y, IRawElementProviderFragment** value) override {
        if (value == nullptr) return E_POINTER;
        *value = nullptr;
        if (!_state) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = static_cast<IRawElementProviderFragment*>(
            new WindowsUiaProvider(_state, _state->hitTest(x, y)));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (value == nullptr) return E_POINTER;
        *value = nullptr;
        if (!_state) return UIA_E_ELEMENTNOTAVAILABLE;
        FlatAccessibilityTree nodes;
        {
            std::lock_guard<std::mutex> lock(_state->mutex);
            nodes = _state->nodes;
        }
        for (const auto& entry : nodes) {
            if ((entry.second.node.states & AccessibilityState_Focused) != 0) {
                *value = static_cast<IRawElementProviderFragment*>(
                    new WindowsUiaProvider(_state, entry.first));
                break;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke() override { return invokeAction(AccessibilityAction::Press); }
    HRESULT STDMETHODCALLTYPE Toggle() override { return invokeAction(AccessibilityAction::Toggle); }

    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* value) override {
        if (value == nullptr) return E_POINTER;
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = (record.node.states & AccessibilityState_Indeterminate)
            ? ToggleState_Indeterminate
            : ((record.node.states & AccessibilityState_Checked) ? ToggleState_On : ToggleState_Off);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(double value) override {
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (record.node.numericReadOnly) return UIA_E_NOTSUPPORTED;
        if ((record.node.states & AccessibilityState_Enabled) == 0) return UIA_E_ELEMENTNOTENABLED;
        return _state->setNumericValue(_nodeId, value) ? S_OK : UIA_E_NOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE get_Value(double* value) override { return numericValue(value, 0); }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* value) override {
        if (value == nullptr) return E_POINTER;
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = record.node.numericReadOnly ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* value) override { return numericValue(value, 1); }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* value) override { return numericValue(value, 2); }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* value) override { return numericValue(value, 3); }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* value) override { return numericValue(value, 4); }

    HRESULT STDMETHODCALLTYPE Expand() override { return invokeAction(AccessibilityAction::Expand); }
    HRESULT STDMETHODCALLTYPE Collapse() override { return invokeAction(AccessibilityAction::Collapse); }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* value) override {
        if (value == nullptr) return E_POINTER;
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = (record.node.states & AccessibilityState_Expanded)
            ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Select() override { return invokeAction(AccessibilityAction::Select); }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_NOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* value) override {
        if (value == nullptr) return E_POINTER;
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = (record.node.states & AccessibilityState_Selected) ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** value) override {
        if (value == nullptr) return E_POINTER;
        *value = nullptr;
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (_nodeId == 0) return S_OK;
        *value = static_cast<IRawElementProviderSimple*>(
            new WindowsUiaProvider(_state, record.parentId));
        return S_OK;
    }

private:
    HRESULT invokeAction(AccessibilityAction action) {
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        if ((record.node.states & AccessibilityState_Enabled) == 0) return UIA_E_ELEMENTNOTENABLED;
        return _state->perform(_nodeId, action) ? S_OK : UIA_E_NOTSUPPORTED;
    }

    HRESULT numericValue(double* value, int field) {
        if (value == nullptr) return E_POINTER;
        FlatAccessibilityNode record;
        if (!_state || !_state->getNode(_nodeId, record)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!record.node.hasNumericRange) return UIA_E_NOTSUPPORTED;
        switch (field) {
        case 1: *value = record.node.numericMaximum; break;
        case 2: *value = record.node.numericMinimum; break;
        case 3: *value = record.node.numericLargeChange; break;
        case 4: *value = record.node.numericSmallChange; break;
        case 0:
        default: *value = record.node.numericValue; break;
        }
        return S_OK;
    }

    std::atomic<ULONG> _references{1};
    std::shared_ptr<WindowsAccessibilityState> _state;
    uint64_t _nodeId = 0;
};

void raisePropertyString(const std::shared_ptr<WindowsAccessibilityState>& state,
                         uint64_t id, PROPERTYID property,
                         const std::wstring& oldValue,
                         const std::wstring& newValue) {
    auto* provider = new WindowsUiaProvider(state, id);
    VARIANT oldVariant;
    VARIANT newVariant;
    ::VariantInit(&oldVariant);
    ::VariantInit(&newVariant);
    setVariantString(&oldVariant, oldValue);
    setVariantString(&newVariant, newValue);
    ::UiaRaiseAutomationPropertyChangedEvent(provider, property,
                                             oldVariant, newVariant);
    ::VariantClear(&oldVariant);
    ::VariantClear(&newVariant);
    provider->Release();
}

void raisePropertyBool(const std::shared_ptr<WindowsAccessibilityState>& state,
                       uint64_t id, PROPERTYID property,
                       bool oldValue, bool newValue) {
    auto* provider = new WindowsUiaProvider(state, id);
    VARIANT oldVariant;
    VARIANT newVariant;
    ::VariantInit(&oldVariant);
    ::VariantInit(&newVariant);
    setVariantBool(&oldVariant, oldValue);
    setVariantBool(&newVariant, newValue);
    ::UiaRaiseAutomationPropertyChangedEvent(provider, property,
                                             oldVariant, newVariant);
    provider->Release();
}

void raisePropertyInt(const std::shared_ptr<WindowsAccessibilityState>& state,
                      uint64_t id, PROPERTYID property,
                      long oldValue, long newValue) {
    auto* provider = new WindowsUiaProvider(state, id);
    VARIANT oldVariant;
    VARIANT newVariant;
    ::VariantInit(&oldVariant);
    ::VariantInit(&newVariant);
    setVariantInt(&oldVariant, oldValue);
    setVariantInt(&newVariant, newValue);
    ::UiaRaiseAutomationPropertyChangedEvent(provider, property,
                                             oldVariant, newVariant);
    provider->Release();
}

void raisePropertyDouble(const std::shared_ptr<WindowsAccessibilityState>& state,
                         uint64_t id, PROPERTYID property,
                         double oldValue, double newValue) {
    auto* provider = new WindowsUiaProvider(state, id);
    VARIANT oldVariant;
    VARIANT newVariant;
    ::VariantInit(&oldVariant);
    ::VariantInit(&newVariant);
    setVariantDouble(&oldVariant, oldValue);
    setVariantDouble(&newVariant, newValue);
    ::UiaRaiseAutomationPropertyChangedEvent(provider, property,
                                             oldVariant, newVariant);
    provider->Release();
}

#endif // _WIN32

class NativeAccessibilityAdapter final : public AccessibilityAdapter {
public:
    NativeAccessibilityAdapter(UIManager& manager, void* nativeWindow)
        : _manager(manager) {
#if defined(_WIN32)
        _windows = std::make_shared<WindowsAccessibilityState>();
        _windows->manager = &manager;
        _windows->window = static_cast<HWND>(nativeWindow);
        _windows->ownerThread = ::GetCurrentThreadId();
#else
        (void)nativeWindow;
#endif
        refresh(false);
    }

    ~NativeAccessibilityAdapter() override {
#if defined(_WIN32)
        if (_windows) {
            std::lock_guard<std::mutex> lock(_windows->mutex);
            _windows->alive = false;
            _windows->manager = nullptr;
            _windows->nodes.clear();
        }
#endif
    }

    void update() override { refresh(true); }
    const AccessibilityNode& snapshot() const override { return _snapshot; }
    const std::vector<AccessibilityChange>& changes() const override { return _changes; }

    bool handleNativeMessage(uint32_t message, uintptr_t wParam,
                             intptr_t lParam, intptr_t& result) override {
#if defined(_WIN32)
        if (message == kAccessibilityActionMessage) {
            auto* request = reinterpret_cast<NativeActionRequest*>(wParam);
            if (request != nullptr) {
                request->result = request->kind == NativeActionKind::Numeric
                    ? _manager.setAccessibilityNumericValue(request->nodeId, request->value)
                    : _manager.performAccessibilityAction(request->nodeId, request->action);
            }
            result = 0;
            return true;
        }
        if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId
            && _windows && _windows->window != nullptr) {
            refresh(false);
            auto* provider = new WindowsUiaProvider(_windows, 0);
            result = static_cast<intptr_t>(::UiaReturnRawElementProvider(
                _windows->window, static_cast<WPARAM>(wParam),
                static_cast<LPARAM>(lParam), provider));
            provider->Release();
            return true;
        }
#else
        (void)message;
        (void)wParam;
        (void)lParam;
        (void)result;
#endif
        return false;
    }

private:
    void refresh(bool emitEvents) {
        AccessibilityNode nextSnapshot = _manager.buildAccessibilityTree();
        FlatAccessibilityTree nextFlat = flattenTree(nextSnapshot);
        _changes.clear();
        appendChanges(_flat, nextFlat, _changes);

#if defined(_WIN32)
        if (_windows) {
            {
                std::lock_guard<std::mutex> lock(_windows->mutex);
                _windows->nodes = nextFlat;
                _windows->scale = _manager.getEffectiveScale();
            }
            if (emitEvents && _windows->window != nullptr && ::UiaClientsAreListening()) {
                bool structureRaised = false;
                for (const AccessibilityChange& change : _changes) {
                    if (change.kind == AccessibilityChangeKind::Tree) {
                        if (!structureRaised) {
                            auto* provider = new WindowsUiaProvider(_windows, 0);
                            ::UiaRaiseStructureChangedEvent(provider,
                                StructureChangeType_ChildrenInvalidated, nullptr, 0);
                            provider->Release();
                            structureRaised = true;
                        }
                        continue;
                    }
                    const auto oldIt = _flat.find(change.nodeId);
                    const auto newIt = nextFlat.find(change.nodeId);
                    if (oldIt == _flat.end() || newIt == nextFlat.end()) continue;
                    const AccessibilityNode& oldNode = oldIt->second.node;
                    const AccessibilityNode& newNode = newIt->second.node;
                    if (change.kind == AccessibilityChangeKind::Name) {
                        raisePropertyString(_windows, change.nodeId, UIA_NamePropertyId,
                                            oldNode.label, newNode.label);
                    } else if (change.kind == AccessibilityChangeKind::Description) {
                        raisePropertyString(_windows, change.nodeId, UIA_HelpTextPropertyId,
                                            oldNode.description, newNode.description);
                    } else if (change.kind == AccessibilityChangeKind::Value) {
                        raisePropertyString(_windows, change.nodeId, UIA_ValueValuePropertyId,
                                            oldNode.value, newNode.value);
                        if (oldNode.hasNumericRange && newNode.hasNumericRange
                            && oldNode.numericValue != newNode.numericValue) {
                            raisePropertyDouble(_windows, change.nodeId,
                                                UIA_RangeValueValuePropertyId,
                                                oldNode.numericValue, newNode.numericValue);
                        }
                    } else if (change.kind == AccessibilityChangeKind::State) {
                        const bool oldEnabled = (oldNode.states & AccessibilityState_Enabled) != 0;
                        const bool newEnabled = (newNode.states & AccessibilityState_Enabled) != 0;
                        if (oldEnabled != newEnabled) {
                            raisePropertyBool(_windows, change.nodeId, UIA_IsEnabledPropertyId,
                                              oldEnabled, newEnabled);
                        }
                        const bool oldSelected = (oldNode.states & AccessibilityState_Selected) != 0;
                        const bool newSelected = (newNode.states & AccessibilityState_Selected) != 0;
                        if (oldSelected != newSelected) {
                            raisePropertyBool(_windows, change.nodeId,
                                              UIA_SelectionItemIsSelectedPropertyId,
                                              oldSelected, newSelected);
                        }
                        const bool oldExpanded = (oldNode.states & AccessibilityState_Expanded) != 0;
                        const bool newExpanded = (newNode.states & AccessibilityState_Expanded) != 0;
                        if (oldExpanded != newExpanded) {
                            raisePropertyInt(_windows, change.nodeId,
                                             UIA_ExpandCollapseExpandCollapseStatePropertyId,
                                             oldExpanded ? ExpandCollapseState_Expanded
                                                         : ExpandCollapseState_Collapsed,
                                             newExpanded ? ExpandCollapseState_Expanded
                                                         : ExpandCollapseState_Collapsed);
                        }
                        const auto toggleState = [](uint32_t states) {
                            return (states & AccessibilityState_Indeterminate)
                                ? ToggleState_Indeterminate
                                : ((states & AccessibilityState_Checked)
                                    ? ToggleState_On : ToggleState_Off);
                        };
                        const long oldToggle = toggleState(oldNode.states);
                        const long newToggle = toggleState(newNode.states);
                        if (oldToggle != newToggle) {
                            raisePropertyInt(_windows, change.nodeId,
                                             UIA_ToggleToggleStatePropertyId,
                                             oldToggle, newToggle);
                        }
                    } else if (change.kind == AccessibilityChangeKind::Focus
                               && (newNode.states & AccessibilityState_Focused) != 0) {
                        auto* provider = new WindowsUiaProvider(_windows, change.nodeId);
                        ::UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
                        provider->Release();
                    }
                }
            }
        }
#endif
        _snapshot = std::move(nextSnapshot);
        _flat = std::move(nextFlat);
    }

    UIManager& _manager;
    AccessibilityNode _snapshot;
    FlatAccessibilityTree _flat;
    std::vector<AccessibilityChange> _changes;
#if defined(_WIN32)
    std::shared_ptr<WindowsAccessibilityState> _windows;
#endif
};

} // namespace

std::unique_ptr<AccessibilityAdapter> createNativeAccessibilityAdapter(
    UIManager& manager, void* nativeWindow) {
    return std::make_unique<NativeAccessibilityAdapter>(manager, nativeWindow);
}

} // namespace ayt::ui
