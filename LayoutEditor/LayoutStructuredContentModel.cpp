#include "AYUI/LayoutEditor/LayoutStructuredContentModel.h"

#include "AYUI/ComboBox.h"
#include "AYUI/ListView.h"
#include "AYUI/TabControl.h"
#include "AYUI/TabStrip.h"
#include "AYUI/TileView.h"
#include "AYUI/TreeView.h"
#include "AYUI/Widget.h"

#include <algorithm>
#include <cwchar>
#include <unordered_set>

namespace ayt::ui {

void LayoutStructuredContentModel::bind(Widget* widget) {
    if (_widget != widget) _selectedIndex = -1;
    _widget = widget;
    if (dynamic_cast<ComboBox*>(widget) != nullptr ||
        dynamic_cast<ListView*>(widget) != nullptr ||
        dynamic_cast<TileView*>(widget) != nullptr) {
        _kind = LayoutStructuredKind::ItemList;
    } else if (dynamic_cast<TreeView*>(widget) != nullptr) {
        _kind = LayoutStructuredKind::Tree;
    } else if (dynamic_cast<TabStrip*>(widget) != nullptr ||
               dynamic_cast<TabControl*>(widget) != nullptr) {
        _kind = LayoutStructuredKind::Tabs;
    } else if (dynamic_cast<RichText*>(widget) != nullptr) {
        _kind = LayoutStructuredKind::RichText;
    } else {
        _kind = LayoutStructuredKind::None;
    }
    refresh();
}

std::vector<std::wstring> LayoutStructuredContentModel::displayLabels() const {
    std::vector<std::wstring> labels;
    labels.reserve(_entries.size());
    for (size_t i = 0; i < _entries.size(); ++i) {
        const LayoutStructuredEntry& entry = _entries[i];
        std::wstring label;
        if (_kind == LayoutStructuredKind::Tree) {
            label.assign(static_cast<size_t>(std::max(0, entry.depth)) * 2u,
                         L' ');
            label += entry.expanded ? L"\u25be " : L"\u25b8 ";
        } else if (_kind == LayoutStructuredKind::RichText) {
            label = L"Run " + std::to_wstring(i + 1u) + L"  ";
        } else {
            label = std::to_wstring(i + 1u) + L"  ";
        }
        label += entry.label.empty() ? L"(empty)" : entry.label;
        labels.push_back(std::move(label));
    }
    return labels;
}

void LayoutStructuredContentModel::setSelectedIndex(int index) {
    _selectedIndex = index >= 0 && index < static_cast<int>(_entries.size())
        ? index : -1;
}

bool LayoutStructuredContentModel::replaceItemList(
    const std::vector<std::wstring>& items) {
    if (auto* combo = dynamic_cast<ComboBox*>(_widget)) combo->setItems(items);
    else if (auto* list = dynamic_cast<ListView*>(_widget)) list->setItems(items);
    else if (auto* tiles = dynamic_cast<TileView*>(_widget)) tiles->setItems(items);
    else return false;
    return true;
}

bool LayoutStructuredContentModel::add(const std::wstring& label,
                                        bool asChild,
                                        PageFactory pageFactory) {
    const std::wstring value = label.empty() ? L"New item" : label;
    if (_kind == LayoutStructuredKind::ItemList) {
        std::vector<std::wstring> items;
        items.reserve(_entries.size() + 1u);
        for (const auto& entry : _entries) items.push_back(entry.label);
        const int insertAt = _selectedIndex >= 0 ? _selectedIndex + 1
                                                 : static_cast<int>(items.size());
        items.insert(items.begin() + insertAt, value);
        if (!replaceItemList(items)) return false;
        _selectedIndex = insertAt;
    } else if (_kind == LayoutStructuredKind::Tree) {
        auto* tree = dynamic_cast<TreeView*>(_widget);
        if (tree == nullptr) return false;
        std::vector<TreeNodeData> nodes = tree->getTreeDataRef();
        TreeNodeData node;
        node.label = value;
        if (asChild && _selectedIndex >= 0 &&
            _selectedIndex < static_cast<int>(nodes.size())) {
            node.parentIndex = _selectedIndex;
            nodes[static_cast<size_t>(_selectedIndex)].hasChildren = true;
            nodes[static_cast<size_t>(_selectedIndex)].expanded = true;
        } else if (_selectedIndex >= 0 &&
                   _selectedIndex < static_cast<int>(nodes.size())) {
            node.parentIndex = nodes[static_cast<size_t>(_selectedIndex)].parentIndex;
        }
        nodes.push_back(std::move(node));
        tree->setTree(nodes);
        _selectedIndex = static_cast<int>(nodes.size()) - 1;
    } else if (_kind == LayoutStructuredKind::Tabs) {
        if (auto* strip = dynamic_cast<TabStrip*>(_widget)) {
            strip->addTab(value);
            _selectedIndex = strip->getTabCount() - 1;
        } else if (auto* tabs = dynamic_cast<TabControl*>(_widget)) {
            Widget* page = pageFactory ? pageFactory() : nullptr;
            if (page == nullptr) return false;
            tabs->addTabOwned(value, page);
            _selectedIndex = static_cast<int>(tabs->getTabCount()) - 1;
            tabs->setSelectedIndex(_selectedIndex);
        } else return false;
    } else if (_kind == LayoutStructuredKind::RichText) {
        auto* rich = dynamic_cast<RichText*>(_widget);
        if (rich == nullptr) return false;
        RichRun run;
        run.text = value == L"New item" ? L"New run" : value;
        run.color = rich->getDefaultColor();
        run.fontSize = rich->getDefaultFontSize();
        const size_t insertAt = _selectedIndex >= 0
            ? static_cast<size_t>(_selectedIndex + 1) : rich->getRunCount();
        rich->insertRun(insertAt, run);
        _selectedIndex = static_cast<int>(insertAt);
    } else {
        return false;
    }
    refresh();
    return true;
}

bool LayoutStructuredContentModel::removeSelected() {
    const int index = _selectedIndex;
    if (index < 0 || index >= static_cast<int>(_entries.size())) return false;
    if (_kind == LayoutStructuredKind::ItemList) {
        std::vector<std::wstring> items;
        for (const auto& entry : _entries) items.push_back(entry.label);
        items.erase(items.begin() + index);
        if (!replaceItemList(items)) return false;
    } else if (_kind == LayoutStructuredKind::Tree) {
        auto* tree = dynamic_cast<TreeView*>(_widget);
        if (tree == nullptr) return false;
        const std::vector<TreeNodeData>& source = tree->getTreeDataRef();
        std::unordered_set<int> removed{index};
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < static_cast<int>(source.size()); ++i) {
                if (removed.count(source[static_cast<size_t>(i)].parentIndex) != 0u &&
                    removed.insert(i).second) changed = true;
            }
        }
        std::vector<int> remap(source.size(), -1);
        std::vector<TreeNodeData> nodes;
        for (int i = 0; i < static_cast<int>(source.size()); ++i) {
            if (removed.count(i) != 0u) continue;
            remap[static_cast<size_t>(i)] = static_cast<int>(nodes.size());
            nodes.push_back(source[static_cast<size_t>(i)]);
        }
        for (TreeNodeData& node : nodes) {
            if (node.parentIndex >= 0) {
                node.parentIndex = remap[static_cast<size_t>(node.parentIndex)];
            }
            node.hasChildren = false;
        }
        for (const TreeNodeData& node : nodes) {
            if (node.parentIndex >= 0) {
                nodes[static_cast<size_t>(node.parentIndex)].hasChildren = true;
            }
        }
        tree->setTree(nodes);
    } else if (_kind == LayoutStructuredKind::Tabs) {
        if (auto* strip = dynamic_cast<TabStrip*>(_widget)) strip->removeTab(index);
        else if (auto* tabs = dynamic_cast<TabControl*>(_widget)) tabs->removeTab(index);
        else return false;
    } else if (_kind == LayoutStructuredKind::RichText) {
        auto* rich = dynamic_cast<RichText*>(_widget);
        if (rich == nullptr || !rich->removeRun(static_cast<size_t>(index))) return false;
    } else return false;
    refresh();
    _selectedIndex = _entries.empty() ? -1
        : std::min(index, static_cast<int>(_entries.size()) - 1);
    return true;
}

int LayoutStructuredContentModel::adjacentTreeSibling(int index,
                                                       int delta) const {
    if (index < 0 || index >= static_cast<int>(_entries.size())) return -1;
    const int parent = _entries[static_cast<size_t>(index)].parentIndex;
    if (delta < 0) {
        for (int i = index - 1; i >= 0; --i) {
            if (_entries[static_cast<size_t>(i)].parentIndex == parent) return i;
        }
    } else {
        for (int i = index + 1; i < static_cast<int>(_entries.size()); ++i) {
            if (_entries[static_cast<size_t>(i)].parentIndex == parent) return i;
        }
    }
    return -1;
}

bool LayoutStructuredContentModel::moveSelected(int delta) {
    const int from = _selectedIndex;
    if (from < 0 || from >= static_cast<int>(_entries.size()) || delta == 0)
        return false;
    int to = std::clamp(from + (delta < 0 ? -1 : 1), 0,
                        static_cast<int>(_entries.size()) - 1);
    if (_kind == LayoutStructuredKind::Tree) {
        to = adjacentTreeSibling(from, delta);
        if (to < 0) return false;
        auto* tree = dynamic_cast<TreeView*>(_widget);
        std::vector<TreeNodeData> nodes = tree->getTreeDataRef();
        std::swap(nodes[static_cast<size_t>(from)], nodes[static_cast<size_t>(to)]);
        for (TreeNodeData& node : nodes) {
            if (node.parentIndex == from) node.parentIndex = to;
            else if (node.parentIndex == to) node.parentIndex = from;
        }
        tree->setTree(nodes);
    } else if (_kind == LayoutStructuredKind::ItemList) {
        if (to == from) return false;
        std::vector<std::wstring> items;
        for (const auto& entry : _entries) items.push_back(entry.label);
        std::swap(items[static_cast<size_t>(from)], items[static_cast<size_t>(to)]);
        if (!replaceItemList(items)) return false;
    } else if (_kind == LayoutStructuredKind::Tabs) {
        if (to == from) return false;
        if (auto* strip = dynamic_cast<TabStrip*>(_widget)) {
            std::vector<std::wstring> labels;
            for (const auto& entry : _entries) labels.push_back(entry.label);
            std::swap(labels[static_cast<size_t>(from)], labels[static_cast<size_t>(to)]);
            const int selected = strip->getSelectedIndex();
            strip->clearTabs();
            for (const auto& label : labels) strip->addTab(label);
            int mapped = selected;
            if (selected == from) mapped = to;
            else if (selected == to) mapped = from;
            strip->setSelectedIndex(mapped);
        } else if (auto* tabs = dynamic_cast<TabControl*>(_widget)) {
            if (!tabs->moveTab(from, to)) return false;
        } else return false;
    } else if (_kind == LayoutStructuredKind::RichText) {
        if (to == from) return false;
        auto* rich = dynamic_cast<RichText*>(_widget);
        if (rich == nullptr || !rich->moveRun(
                static_cast<size_t>(from), static_cast<size_t>(to))) return false;
    } else return false;
    _selectedIndex = to;
    refresh();
    return true;
}

bool LayoutStructuredContentModel::setSelectedLabel(
    const std::wstring& label) {
    const int index = _selectedIndex;
    if (index < 0 || index >= static_cast<int>(_entries.size())) return false;
    if (_kind == LayoutStructuredKind::ItemList) {
        std::vector<std::wstring> items;
        for (const auto& entry : _entries) items.push_back(entry.label);
        items[static_cast<size_t>(index)] = label;
        if (!replaceItemList(items)) return false;
    } else if (_kind == LayoutStructuredKind::Tree) {
        auto* tree = dynamic_cast<TreeView*>(_widget);
        std::vector<TreeNodeData> nodes = tree->getTreeDataRef();
        nodes[static_cast<size_t>(index)].label = label;
        tree->setTree(nodes);
    } else if (_kind == LayoutStructuredKind::Tabs) {
        if (auto* strip = dynamic_cast<TabStrip*>(_widget)) {
            if (!strip->setTabLabel(index, label)) return false;
        } else if (auto* tabs = dynamic_cast<TabControl*>(_widget)) {
            if (!tabs->setTabLabel(index, label)) return false;
        } else return false;
    } else if (_kind == LayoutStructuredKind::RichText) {
        RichRun run = _entries[static_cast<size_t>(index)].richRun;
        run.text = label;
        return setSelectedRichRun(run);
    } else return false;
    refresh();
    return true;
}

bool LayoutStructuredContentModel::setSelectedRichRun(const RichRun& run) {
    if (_kind != LayoutStructuredKind::RichText || _selectedIndex < 0) return false;
    auto* rich = dynamic_cast<RichText*>(_widget);
    if (rich == nullptr || !rich->setRun(static_cast<size_t>(_selectedIndex), run))
        return false;
    refresh();
    return true;
}

void LayoutStructuredContentModel::refresh() {
    _entries.clear();
    if (_kind == LayoutStructuredKind::ItemList) {
        const std::vector<std::wstring>* items = nullptr;
        if (auto* combo = dynamic_cast<ComboBox*>(_widget)) items = &combo->getItemsRef();
        else if (auto* list = dynamic_cast<ListView*>(_widget)) items = &list->getItemsRef();
        else if (auto* tiles = dynamic_cast<TileView*>(_widget)) items = &tiles->getItemsRef();
        if (items != nullptr) {
            for (const auto& item : *items) _entries.push_back({item});
        }
    } else if (_kind == LayoutStructuredKind::Tree) {
        if (auto* tree = dynamic_cast<TreeView*>(_widget)) {
            const auto& nodes = tree->getTreeDataRef();
            _entries.reserve(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i) {
                const TreeNodeData& node = nodes[i];
                int depth = 0;
                int parent = node.parentIndex;
                std::unordered_set<int> visited;
                while (parent >= 0 && parent < static_cast<int>(nodes.size()) &&
                       visited.insert(parent).second) {
                    ++depth;
                    parent = nodes[static_cast<size_t>(parent)].parentIndex;
                }
                LayoutStructuredEntry entry;
                entry.label = node.label;
                entry.depth = depth;
                entry.parentIndex = node.parentIndex;
                entry.expanded = node.expanded;
                _entries.push_back(std::move(entry));
            }
        }
    } else if (_kind == LayoutStructuredKind::Tabs) {
        if (auto* strip = dynamic_cast<TabStrip*>(_widget)) {
            for (int i = 0; i < strip->getTabCount(); ++i)
                _entries.push_back({strip->getTabLabel(i)});
        } else if (auto* tabs = dynamic_cast<TabControl*>(_widget)) {
            for (int i = 0; i < static_cast<int>(tabs->getTabCount()); ++i)
                _entries.push_back({tabs->getTabLabel(i)});
        }
    } else if (_kind == LayoutStructuredKind::RichText) {
        if (auto* rich = dynamic_cast<RichText*>(_widget)) {
            for (size_t i = 0; i < rich->getRunCount(); ++i) {
                LayoutStructuredEntry entry;
                entry.richRun = rich->getRun(i);
                entry.label = entry.richRun.text;
                _entries.push_back(std::move(entry));
            }
        }
    }
    if (_selectedIndex >= static_cast<int>(_entries.size()))
        _selectedIndex = _entries.empty() ? -1 : static_cast<int>(_entries.size()) - 1;
}

} // namespace ayt::ui
