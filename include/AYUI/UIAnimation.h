#pragma once

#include "AYUI/Animation.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class Widget;

enum class UIAnimationProperty {
    Opacity,
    Position,
    Size
};

struct UIAnimationKeyframe {
    float timeMs = 0.0f;
    math::FVector4 value{0.0f, 0.0f, 0.0f, 0.0f};
    AnimationCurve curve = AnimationCurve::Linear;
    SpringParameters spring;
    bool hasSpringParameters = false;
};

struct UIAnimationTrack {
    std::string targetId;
    UIAnimationProperty property = UIAnimationProperty::Opacity;
    std::vector<UIAnimationKeyframe> keyframes;
};

struct UIAnimationClip {
    std::string name;
    int repeatCount = 0;
    bool yoyo = false;
    AnimationImportance importance = AnimationImportance::Decorative;
    std::vector<UIAnimationTrack> tracks;
};

// Declarative, document-level UI animation data. The library stores stable
// widget IDs and ordinary values; runtime callbacks are created only when a
// timeline is instantiated, so no Widget pointer or backend handle enters
// JSON. A produced timeline must not outlive the resolved Widget tree.
class UIAnimationLibrary {
public:
    using WidgetResolver = std::function<Widget*(const std::string& id)>;

    const std::vector<UIAnimationClip>& clips() const { return _clips; }
    std::size_t size() const { return _clips.size(); }
    bool empty() const { return _clips.empty(); }
    void clear() { _clips.clear(); }
    int findClipIndex(const std::string& name) const;
    const UIAnimationClip* findClip(const std::string& name) const;

    int addClip(const std::string& name, std::string* error = nullptr);
    bool removeClip(int clipIndex);
    bool setPlayback(int clipIndex, int repeatCount, bool yoyo,
                     AnimationImportance importance);
    int addTrack(int clipIndex, const std::string& targetId,
                 UIAnimationProperty property,
                 std::string* error = nullptr);
    bool removeTrack(int clipIndex, int trackIndex);
    // Keeps document references valid when the author renames a Widget ID.
    // Fails without mutation if the new ID would duplicate a property track.
    bool retargetWidget(const std::string& oldId, const std::string& newId,
                        std::string* error = nullptr);
    int upsertKeyframe(int clipIndex, int trackIndex,
                       UIAnimationKeyframe keyframe);
    bool removeKeyframe(int clipIndex, int trackIndex, int keyframeIndex);

    std::string serialize(bool pretty = false) const;
    bool deserialize(const std::string& json,
                     std::string* error = nullptr);

    AnimationTimeline createTimeline(
        int clipIndex, const WidgetResolver& resolver,
        std::size_t* unresolvedTrackCount = nullptr) const;

    static const char* propertyName(UIAnimationProperty property);
    static bool propertyFromName(const std::string& name,
                                 UIAnimationProperty& property);
    static const char* curveName(AnimationCurve curve);
    static bool curveFromName(const std::string& name,
                              AnimationCurve& curve);

private:
    static bool validClipName(const std::string& name,
                              std::string* error);
    static void normalizeKeyframe(UIAnimationProperty property,
                                  UIAnimationKeyframe& keyframe);

    std::vector<UIAnimationClip> _clips;
};

} // namespace ayt::ui
