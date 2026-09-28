#pragma once
#include "AYUI/WidgetSerializer.h"
#include <memory>
#include <string>

namespace ayt::ui::test {
struct WidgetTreeDeleter {
    void operator()(Widget* root) const { destroyWidgetTree(root); }
};
using WidgetTree = std::unique_ptr<Widget, WidgetTreeDeleter>;

enum class SerializationForm { Document, Widget };

// Common setup/cleanup only: every case keeps its own contract assertions.
// Owns the restored tree, never the borrowed source (which may be stack-owned).
struct WidgetRoundTrip {
    std::string serialized;
    WidgetTree restored;
    explicit WidgetRoundTrip(Widget* source,
        SerializationForm form = SerializationForm::Document)
        : serialized(form == SerializationForm::Widget
            ? WidgetSerializer::serializeWidget(source)
            : WidgetSerializer::serialize(source)),
          restored(WidgetSerializer::deserialize(serialized)) {}
};
} // namespace ayt::ui::test
