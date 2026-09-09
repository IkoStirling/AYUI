#include "AYUI/UIAnimation.h"

#include "AYUI/Widget.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace ayt::ui {

using nlohmann::json;

namespace {

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

float finiteOr(float value, float fallback = 0.0f) {
    return std::isfinite(value) ? value : fallback;
}

json encodeValue(UIAnimationProperty property,
                 const math::FVector4& value) {
    if (property == UIAnimationProperty::Opacity) return value.x;
    return json{{"x", value.x}, {"y", value.y}};
}

bool decodeValue(const json& encoded, UIAnimationProperty property,
                 math::FVector4& value) {
    if (property == UIAnimationProperty::Opacity) {
        if (!encoded.is_number()) return false;
        value.x = encoded.get<float>();
        return true;
    }
    if (!encoded.is_object()) return false;
    value.x = encoded.value("x", 0.0f);
    value.y = encoded.value("y", 0.0f);
    return true;
}

} // namespace

const char* UIAnimationLibrary::propertyName(UIAnimationProperty property) {
    switch (property) {
    case UIAnimationProperty::Position: return "position";
    case UIAnimationProperty::Size: return "size";
    default: return "opacity";
    }
}

bool UIAnimationLibrary::propertyFromName(
    const std::string& name, UIAnimationProperty& property) {
    if (name == "opacity") property = UIAnimationProperty::Opacity;
    else if (name == "position") property = UIAnimationProperty::Position;
    else if (name == "size") property = UIAnimationProperty::Size;
    else return false;
    return true;
}

const char* UIAnimationLibrary::curveName(AnimationCurve curve) {
    switch (curve) {
    case AnimationCurve::EaseIn: return "easeIn";
    case AnimationCurve::EaseOut: return "easeOut";
    case AnimationCurve::EaseInOut: return "easeInOut";
    case AnimationCurve::Spring: return "spring";
    default: return "linear";
    }
}

bool UIAnimationLibrary::curveFromName(
    const std::string& name, AnimationCurve& curve) {
    if (name == "linear") curve = AnimationCurve::Linear;
    else if (name == "easeIn") curve = AnimationCurve::EaseIn;
    else if (name == "easeOut") curve = AnimationCurve::EaseOut;
    else if (name == "easeInOut") curve = AnimationCurve::EaseInOut;
    else if (name == "spring") curve = AnimationCurve::Spring;
    else return false;
    return true;
}

bool UIAnimationLibrary::validClipName(
    const std::string& name, std::string* error) {
    if (name.empty()) return fail(error, "Animation clip name cannot be empty");
    if (name.size() > 96u)
        return fail(error, "Animation clip name is limited to 96 bytes");
    for (const unsigned char ch : name) {
        if (ch < 0x20u || ch == 0x7fu)
            return fail(error, "Animation clip name contains a control character");
    }
    return true;
}

void UIAnimationLibrary::normalizeKeyframe(
    UIAnimationProperty property, UIAnimationKeyframe& keyframe) {
    keyframe.timeMs = std::max(0.0f, finiteOr(keyframe.timeMs));
    keyframe.value.x = finiteOr(keyframe.value.x);
    keyframe.value.y = finiteOr(keyframe.value.y);
    keyframe.value.z = finiteOr(keyframe.value.z);
    keyframe.value.w = finiteOr(keyframe.value.w);
    if (property == UIAnimationProperty::Opacity)
        keyframe.value.x = std::clamp(keyframe.value.x, 0.0f, 1.0f);
    if (property == UIAnimationProperty::Size) {
        keyframe.value.x = std::max(0.0f, keyframe.value.x);
        keyframe.value.y = std::max(0.0f, keyframe.value.y);
    }
    keyframe.spring.mass = std::max(
        0.001f, finiteOr(keyframe.spring.mass, 1.0f));
    keyframe.spring.stiffness = std::max(
        0.001f, finiteOr(keyframe.spring.stiffness, 170.0f));
    keyframe.spring.damping = std::max(
        0.0f, finiteOr(keyframe.spring.damping, 18.0f));
    keyframe.spring.initialVelocity = finiteOr(
        keyframe.spring.initialVelocity);
    if (keyframe.curve != AnimationCurve::Spring)
        keyframe.hasSpringParameters = false;
}

int UIAnimationLibrary::addClip(
    const std::string& name, std::string* error) {
    if (!validClipName(name, error)) return -1;
    if (_clips.size() >= 128u) {
        fail(error, "Animation library is limited to 128 clips");
        return -1;
    }
    const auto duplicate = std::find_if(
        _clips.begin(), _clips.end(), [&name](const UIAnimationClip& clip) {
            return clip.name == name;
        });
    if (duplicate != _clips.end()) {
        fail(error, "Animation clip name must be unique");
        return -1;
    }
    _clips.push_back(UIAnimationClip{});
    _clips.back().name = name;
    return static_cast<int>(_clips.size() - 1u);
}

int UIAnimationLibrary::findClipIndex(const std::string& name) const {
    const auto found = std::find_if(
        _clips.begin(), _clips.end(), [&name](const UIAnimationClip& clip) {
            return clip.name == name;
        });
    return found == _clips.end()
        ? -1 : static_cast<int>(found - _clips.begin());
}

const UIAnimationClip* UIAnimationLibrary::findClip(
    const std::string& name) const {
    const int index = findClipIndex(name);
    return index >= 0 ? &_clips[static_cast<size_t>(index)] : nullptr;
}

bool UIAnimationLibrary::removeClip(int clipIndex) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_clips.size()))
        return false;
    _clips.erase(_clips.begin() + clipIndex);
    return true;
}

bool UIAnimationLibrary::setPlayback(
    int clipIndex, int repeatCount, bool yoyo,
    AnimationImportance importance) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_clips.size()))
        return false;
    UIAnimationClip& clip = _clips[static_cast<size_t>(clipIndex)];
    clip.repeatCount = std::max(AnimationTimeline::RepeatForever, repeatCount);
    clip.yoyo = yoyo;
    clip.importance = importance;
    return true;
}

int UIAnimationLibrary::addTrack(
    int clipIndex, const std::string& targetId,
    UIAnimationProperty property, std::string* error) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_clips.size())) {
        fail(error, "Select an animation clip first");
        return -1;
    }
    if (targetId.empty()) {
        fail(error, "Animation track target requires a Widget ID");
        return -1;
    }
    UIAnimationClip& clip = _clips[static_cast<size_t>(clipIndex)];
    if (clip.tracks.size() >= 128u) {
        fail(error, "Animation clip is limited to 128 tracks");
        return -1;
    }
    const auto duplicate = std::find_if(
        clip.tracks.begin(), clip.tracks.end(),
        [&targetId, property](const UIAnimationTrack& track) {
            return track.targetId == targetId && track.property == property;
        });
    if (duplicate != clip.tracks.end()) {
        fail(error, "The selected Widget property already has a track");
        return -1;
    }
    clip.tracks.push_back(UIAnimationTrack{});
    clip.tracks.back().targetId = targetId;
    clip.tracks.back().property = property;
    return static_cast<int>(clip.tracks.size() - 1u);
}

bool UIAnimationLibrary::removeTrack(int clipIndex, int trackIndex) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_clips.size()))
        return false;
    auto& tracks = _clips[static_cast<size_t>(clipIndex)].tracks;
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size()))
        return false;
    tracks.erase(tracks.begin() + trackIndex);
    return true;
}

bool UIAnimationLibrary::retargetWidget(
    const std::string& oldId, const std::string& newId,
    std::string* error) {
    if (oldId.empty() || newId.empty())
        return fail(error, "Animation target IDs cannot be empty");
    if (oldId == newId) return true;
    for (const UIAnimationClip& clip : _clips) {
        for (const UIAnimationTrack& track : clip.tracks) {
            if (track.targetId != oldId) continue;
            const auto duplicate = std::find_if(
                clip.tracks.begin(), clip.tracks.end(),
                [&track, &newId](const UIAnimationTrack& candidate) {
                    return &candidate != &track &&
                           candidate.targetId == newId &&
                           candidate.property == track.property;
                });
            if (duplicate != clip.tracks.end()) {
                return fail(error,
                    "Widget rename would duplicate an animation track");
            }
        }
    }
    for (UIAnimationClip& clip : _clips) {
        for (UIAnimationTrack& track : clip.tracks) {
            if (track.targetId == oldId) track.targetId = newId;
        }
    }
    return true;
}

int UIAnimationLibrary::upsertKeyframe(
    int clipIndex, int trackIndex, UIAnimationKeyframe keyframe) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_clips.size()))
        return -1;
    auto& tracks = _clips[static_cast<size_t>(clipIndex)].tracks;
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size()))
        return -1;
    UIAnimationTrack& track = tracks[static_cast<size_t>(trackIndex)];
    normalizeKeyframe(track.property, keyframe);
    auto& keys = track.keyframes;
    const auto at = std::lower_bound(
        keys.begin(), keys.end(), keyframe.timeMs,
        [](const UIAnimationKeyframe& existing, float timeMs) {
            return existing.timeMs < timeMs;
        });
    if (at != keys.end() && std::fabs(at->timeMs - keyframe.timeMs) < 0.001f) {
        const int index = static_cast<int>(at - keys.begin());
        *at = std::move(keyframe);
        return index;
    }
    if (keys.size() >= 4096u) return -1;
    return static_cast<int>(keys.insert(at, std::move(keyframe)) - keys.begin());
}

bool UIAnimationLibrary::removeKeyframe(
    int clipIndex, int trackIndex, int keyframeIndex) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_clips.size()))
        return false;
    auto& tracks = _clips[static_cast<size_t>(clipIndex)].tracks;
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size()))
        return false;
    auto& keys = tracks[static_cast<size_t>(trackIndex)].keyframes;
    if (keyframeIndex < 0 || keyframeIndex >= static_cast<int>(keys.size()))
        return false;
    keys.erase(keys.begin() + keyframeIndex);
    return true;
}

std::string UIAnimationLibrary::serialize(bool pretty) const {
    json encoded = json::array();
    for (const UIAnimationClip& clip : _clips) {
        json tracks = json::array();
        for (const UIAnimationTrack& track : clip.tracks) {
            json keys = json::array();
            for (const UIAnimationKeyframe& keyframe : track.keyframes) {
                json key = {
                    {"timeMs", keyframe.timeMs},
                    {"value", encodeValue(track.property, keyframe.value)},
                    {"curve", curveName(keyframe.curve)}
                };
                if (keyframe.curve == AnimationCurve::Spring &&
                    keyframe.hasSpringParameters) {
                    key["spring"] = {
                        {"mass", keyframe.spring.mass},
                        {"stiffness", keyframe.spring.stiffness},
                        {"damping", keyframe.spring.damping},
                        {"initialVelocity", keyframe.spring.initialVelocity},
                        {"clampOvershoot", keyframe.spring.clampOvershoot}
                    };
                }
                keys.push_back(std::move(key));
            }
            tracks.push_back({
                {"target", track.targetId},
                {"property", propertyName(track.property)},
                {"keyframes", std::move(keys)}
            });
        }
        encoded.push_back({
            {"name", clip.name},
            {"repeatCount", clip.repeatCount},
            {"yoyo", clip.yoyo},
            {"importance", clip.importance == AnimationImportance::Essential
                ? "essential" : "decorative"},
            {"tracks", std::move(tracks)}
        });
    }
    return pretty ? encoded.dump(4) : encoded.dump();
}

bool UIAnimationLibrary::deserialize(
    const std::string& source, std::string* error) {
    try {
        const json encoded = json::parse(source);
        if (!encoded.is_array())
            return fail(error, "Layout animations must be an array");
        if (encoded.size() > 128u)
            return fail(error, "Layout contains too many animation clips");

        UIAnimationLibrary decoded;
        for (const json& clipJson : encoded) {
            if (!clipJson.is_object())
                return fail(error, "Animation clip must be an object");
            const std::string name = clipJson.value("name", std::string{});
            const int clipIndex = decoded.addClip(name, error);
            if (clipIndex < 0) return false;
            UIAnimationClip& clip = decoded._clips[static_cast<size_t>(clipIndex)];
            clip.repeatCount = std::max(
                AnimationTimeline::RepeatForever,
                clipJson.value("repeatCount", 0));
            clip.yoyo = clipJson.value("yoyo", false);
            clip.importance = clipJson.value(
                "importance", std::string{"decorative"}) == "essential"
                ? AnimationImportance::Essential
                : AnimationImportance::Decorative;
            if (!clipJson.contains("tracks")) continue;
            if (!clipJson["tracks"].is_array() ||
                clipJson["tracks"].size() > 128u) {
                return fail(error, "Animation tracks must be a bounded array");
            }
            for (const json& trackJson : clipJson["tracks"]) {
                if (!trackJson.is_object())
                    return fail(error, "Animation track must be an object");
                UIAnimationProperty property;
                if (!propertyFromName(
                        trackJson.value("property", std::string{}), property)) {
                    return fail(error, "Animation track has an unknown property");
                }
                const int trackIndex = decoded.addTrack(
                    clipIndex, trackJson.value("target", std::string{}),
                    property, error);
                if (trackIndex < 0) return false;
                if (!trackJson.contains("keyframes")) continue;
                if (!trackJson["keyframes"].is_array() ||
                    trackJson["keyframes"].size() > 4096u) {
                    return fail(error, "Animation keyframes must be a bounded array");
                }
                for (const json& keyJson : trackJson["keyframes"]) {
                    if (!keyJson.is_object())
                        return fail(error, "Animation keyframe must be an object");
                    UIAnimationKeyframe keyframe;
                    keyframe.timeMs = keyJson.value("timeMs", 0.0f);
                    if (!keyJson.contains("value") ||
                        !decodeValue(keyJson["value"], property, keyframe.value)) {
                        return fail(error, "Animation keyframe has an invalid value");
                    }
                    if (!curveFromName(
                            keyJson.value("curve", std::string{"linear"}),
                            keyframe.curve)) {
                        return fail(error, "Animation keyframe has an unknown curve");
                    }
                    if (keyJson.contains("spring") &&
                        keyJson["spring"].is_object()) {
                        const json& spring = keyJson["spring"];
                        keyframe.spring.mass = spring.value("mass", 1.0f);
                        keyframe.spring.stiffness = spring.value(
                            "stiffness", 170.0f);
                        keyframe.spring.damping = spring.value("damping", 18.0f);
                        keyframe.spring.initialVelocity = spring.value(
                            "initialVelocity", 0.0f);
                        keyframe.spring.clampOvershoot = spring.value(
                            "clampOvershoot", false);
                        keyframe.hasSpringParameters = true;
                    }
                    if (decoded.upsertKeyframe(
                            clipIndex, trackIndex, std::move(keyframe)) < 0) {
                        return fail(error, "Animation keyframe limit exceeded");
                    }
                }
            }
        }
        _clips = std::move(decoded._clips);
        return true;
    } catch (const std::exception& ex) {
        return fail(error, std::string("Invalid animation JSON: ") + ex.what());
    }
}

AnimationTimeline UIAnimationLibrary::createTimeline(
    int clipIndex, const WidgetResolver& resolver,
    std::size_t* unresolvedTrackCount) const {
    AnimationTimeline timeline;
    std::size_t unresolved = 0;
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_clips.size())) {
        if (unresolvedTrackCount != nullptr) *unresolvedTrackCount = 0;
        return timeline;
    }
    const UIAnimationClip& clip = _clips[static_cast<size_t>(clipIndex)];
    timeline.setRepeatCount(clip.repeatCount)
            .setYoyo(clip.yoyo)
            .setImportance(clip.importance);
    for (const UIAnimationTrack& track : clip.tracks) {
        Widget* target = resolver ? resolver(track.targetId) : nullptr;
        if (target == nullptr || track.keyframes.empty()) {
            ++unresolved;
            continue;
        }
        if (track.property == UIAnimationProperty::Opacity) {
            std::vector<AnimationKeyframe<float>> keys;
            keys.reserve(track.keyframes.size());
            for (const UIAnimationKeyframe& source : track.keyframes) {
                AnimationKeyframe<float> key(source.timeMs, source.value.x,
                                              source.curve);
                key.spring = source.spring;
                key.hasSpringParameters = source.hasSpringParameters;
                keys.push_back(key);
            }
            timeline.addFloatTrack(std::move(keys), [target](float value) {
                target->setOpacity(value);
            });
        } else {
            std::vector<AnimationKeyframe<math::FVector2>> keys;
            keys.reserve(track.keyframes.size());
            for (const UIAnimationKeyframe& source : track.keyframes) {
                AnimationKeyframe<math::FVector2> key(
                    source.timeMs,
                    math::FVector2(source.value.x, source.value.y),
                    source.curve);
                key.spring = source.spring;
                key.hasSpringParameters = source.hasSpringParameters;
                keys.push_back(key);
            }
            if (track.property == UIAnimationProperty::Position) {
                timeline.addVec2Track(std::move(keys),
                    [target](const math::FVector2& value) {
                        target->setPosition(value);
                    });
            } else {
                timeline.addVec2Track(std::move(keys),
                    [target](const math::FVector2& value) {
                        target->setSize(value);
                    });
            }
        }
    }
    if (unresolvedTrackCount != nullptr) *unresolvedTrackCount = unresolved;
    return timeline;
}

} // namespace ayt::ui
