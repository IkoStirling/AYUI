#include "AYUI/MockRenderer.h"

namespace ayt::ui {

MockRenderer::MockRenderer() {
    clear();
}

MockRenderer::~MockRenderer() {
    _paths.clear();
    _animations.clear();
}

void MockRenderer::clear() {
    _drawCalls.clear();
    _paths.clear();
    _animations.clear();
    _triangleCount = 0;
    _vertexCount = 0;
    _nextPathId = 1;
    _nextParticleId = 1;
    _nextAnimId = 1;
    _nextFontId = 1;
    _nextTargetId = 1;
    // PR-Container-Contract-Cut2: also reset the clip stack so a
    // mid-test clear() doesn't leak stack frames into the next render.
    _clipStack.clear();
}

// PR-Container-Contract-Cut2: clip-stack recording overrides. Pre-PR
// these were IRenderBackend defaults (`{}` no-op); tests could not detect
// a forgotten popClip. Now they record the rect + depth so tests can
// assert container renderChildren implementations push/pop in balance.
void MockRenderer::pushClip(const math::FRectangle& bounds) {
    _clipStack.push_back({bounds});
}

void MockRenderer::popClip() {
    // If a caller forgot a matching push, popClip would underflow the
    // stack. Mock-only safeguard: leave the stack unchanged so the
    // test sees an explicit depth delta and a non-empty stack rather
    // than a silent underflow. Production backends crash on popClip
    // underflow differently; we mimic "balanced-or-leaked" rather than
    // UB.
    if (!_clipStack.empty()) {
        _clipStack.pop_back();
    }
}

void MockRenderer::setBlendMode(BlendMode mode) {
    // Stamped onto every DrawCall recorded while active; beginFrame reset
    // is a UIManager-level concern (the mock has no frame concept).
    _currentBlend = mode;
}

void MockRenderer::pushOpacity(float alpha) {
    // Clamp to [0,1]: 1.0 is a no-op, anything outside the range is a
    // caller bug and would otherwise stack nonsense (alpha > 1 makes
    // draws opaque-er, alpha < 0 would break the multiply).
    const float a = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    // COMPOUND: the stack stores the CUMULATIVE product, not the raw
    // frame — parent push(0.5) then child push(0.5) yields stack top
    // 0.25 (tree opacity multiplies). pop() restores the parent frame.
    _opacityStack.push_back(_opacityStack.back() * a);
}

void MockRenderer::popOpacity() {
    // The stack base (1.0) is never popped — an unbalanced pop on a
    // bare renderer must not crash or corrupt the frame.
    if (_opacityStack.size() > 1) {
        _opacityStack.pop_back();
    }
}

void MockRenderer::drawRect(const math::FRectangle& bounds, const math::FVector4& color) {
    DrawCall dc;
    dc.type = DrawCall::Rect;
    dc.bounds = bounds;
    dc.color = tintWithOpacity(color);
    dc.texture = nullptr;
    dc.blendMode = _currentBlend;
    _drawCalls.push_back(dc);
    _triangleCount += 2;  // 2 triangles per rect
    _vertexCount += 6;
}

void MockRenderer::drawRect(const math::FRectangle& bounds, void* textureHandle, const math::FRectangle& uv) {
    AYUNREFERENCED_PARAM(uv);
    DrawCall dc;
    dc.type = DrawCall::Image;
    dc.bounds = bounds;
    dc.color = math::FVector4(1.0f, 1.0f, 1.0f, _opacityStack.back());
    dc.texture = textureHandle;
    dc.blendMode = _currentBlend;
    _drawCalls.push_back(dc);
    _triangleCount += 2;
    _vertexCount += 6;
}

void MockRenderer::drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize, const math::FVector4& color) {
    AYUNREFERENCED_PARAM(fontSize);
    DrawCall dc;
    dc.type = DrawCall::Text;
    dc.bounds = bounds;
    dc.color = tintWithOpacity(color);
    dc.text = text;
    dc.texture = nullptr;
    dc.blendMode = _currentBlend;
    _drawCalls.push_back(dc);
}

void MockRenderer::drawWithAlpha(const math::FRectangle& bounds, void* textureHandle, float alpha) {
    AYUNREFERENCED_PARAM(alpha);
    DrawCall dc;
    dc.type = DrawCall::Image;
    dc.bounds = bounds;
    dc.color = math::FVector4(1.0f, 1.0f, 1.0f, alpha * _opacityStack.back());
    dc.texture = textureHandle;
    dc.blendMode = _currentBlend;
    _drawCalls.push_back(dc);
    _triangleCount += 2;
    _vertexCount += 6;
}

void MockRenderer::drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize, const TextStyle& style) {
    drawText(bounds, text, fontSize, style.color);
}

void MockRenderer::drawGradientRect(const math::FRectangle& bounds,
                                    const math::FVector4& topLeft, const math::FVector4& topRight,
                                    const math::FVector4& bottomLeft, const math::FVector4& bottomRight) {
    DrawCall dc;
    dc.type = DrawCall::Rect;
    dc.bounds = bounds;
    dc.color = math::FVector4(
        (topLeft.x + topRight.x + bottomLeft.x + bottomRight.x) * 0.25f,
        (topLeft.y + topRight.y + bottomLeft.y + bottomRight.y) * 0.25f,
        (topLeft.z + topRight.z + bottomLeft.z + bottomRight.z) * 0.25f,
        (topLeft.w + topRight.w + bottomLeft.w + bottomRight.w) * 0.25f
    );
    dc.color = tintWithOpacity(dc.color);
    dc.texture = nullptr;
    dc.blendMode = _currentBlend;
    // P1: per-corner colors in param order TL TR BL BR.
    dc.cornerColors[0] = tintWithOpacity(topLeft);
    dc.cornerColors[1] = tintWithOpacity(topRight);
    dc.cornerColors[2] = tintWithOpacity(bottomLeft);
    dc.cornerColors[3] = tintWithOpacity(bottomRight);
    _drawCalls.push_back(dc);
    _triangleCount += 2;
    _vertexCount += 6;
}

void MockRenderer::drawGradientRect(const math::FRectangle& bounds,
                                    const math::FVector4& topColor, const math::FVector4& bottomColor) {
    drawGradientRect(bounds, topColor, topColor, bottomColor, bottomColor);
}

void MockRenderer::drawRect(const math::FRectangle& bounds, const BorderStyle& border) {
    drawBorderRect(bounds, border.color, border.width, border.cornerRadius);
}

void MockRenderer::drawBorderRect(const math::FRectangle& bounds, const math::FVector4& color, float borderWidth, float cornerRadius) {
    IRenderBackend::drawBorderRect(bounds, color, borderWidth, cornerRadius);
}

void MockRenderer::drawRoundedRect(const math::FRectangle& bounds, const math::FVector4& color, float cornerRadius) {
    // Recorded as type=Rect on purpose: hundreds of tests count
    // DrawCall::Rect occurrences and must not see a new enum value.
    // The radius rides in floatParam1 so rounded-fill assertions can
    // read it back. Production UIRenderBackend draws one SDF item.
    DrawCall dc;
    dc.type = DrawCall::Rect;
    dc.bounds = bounds;
    dc.color = tintWithOpacity(color);
    dc.texture = nullptr;
    dc.blendMode = _currentBlend;
    dc.floatParam1 = cornerRadius;
    _drawCalls.push_back(dc);
    _triangleCount += 2;
    _vertexCount += 6;
}

void MockRenderer::drawRectShadow(const math::FRectangle& bounds, const ShadowStyle& shadow) {
    math::FRectangle shadowBounds(
        bounds.minX + shadow.offset.x,
        bounds.minY + shadow.offset.y,
        bounds.maxX + shadow.offset.x,
        bounds.maxY + shadow.offset.y
    );
    drawRect(shadowBounds, shadow.color);
}

// Path methods / 路径方法
IRenderBackend::PathHandle MockRenderer::createPath() {
    PathHandle handle;
    handle.id = _nextPathId++;
    _paths[handle.id] = PathData();
    return handle;
}

void MockRenderer::releasePath(PathHandle path) {
    _paths.erase(path.id);
}

void MockRenderer::addPathRect(PathHandle path, const math::FRectangle& bounds, PathWinding winding) {
    AYUNREFERENCED_PARAM(winding);
    auto it = _paths.find(path.id);
    if (it != _paths.end()) {
        it->second.bounds = bounds;
    }
}

void MockRenderer::addPathRoundedRect(PathHandle path, const math::FRectangle& bounds, float cornerRadius, PathWinding winding) {
    AYUNREFERENCED_PARAM(cornerRadius);
    addPathRect(path, bounds, winding);
}

void MockRenderer::addPathEllipse(PathHandle path, const math::FVector2& center, float radiusX, float radiusY, PathWinding winding) {
    AYUNREFERENCED_PARAM(winding);
    auto it = _paths.find(path.id);
    if (it != _paths.end()) {
        it->second.bounds = math::FRectangle(
            center.x - radiusX, center.y - radiusY,
            center.x + radiusX, center.y + radiusY
        );
    }
}

void MockRenderer::addPathLine(PathHandle path, const math::FVector2& start, const math::FVector2& end) {
    AYUNREFERENCED_PARAM(path);
    AYUNREFERENCED_PARAM(start);
    AYUNREFERENCED_PARAM(end);
}

void MockRenderer::addPathBezier(PathHandle path, const math::FVector2& start, const math::FVector2& control1, const math::FVector2& control2, const math::FVector2& end) {
    AYUNREFERENCED_PARAM(path);
    AYUNREFERENCED_PARAM(start);
    AYUNREFERENCED_PARAM(control1);
    AYUNREFERENCED_PARAM(control2);
    AYUNREFERENCED_PARAM(end);
}

void MockRenderer::addPathArc(PathHandle path, const math::FVector2& center, float radius, float startAngle, float endAngle, PathWinding winding) {
    AYUNREFERENCED_PARAM(path);
    AYUNREFERENCED_PARAM(center);
    AYUNREFERENCED_PARAM(radius);
    AYUNREFERENCED_PARAM(startAngle);
    AYUNREFERENCED_PARAM(endAngle);
    AYUNREFERENCED_PARAM(winding);
}

void MockRenderer::addPathPolygon(PathHandle path, const math::FVector2* points, int count, PathWinding winding) {
    AYUNREFERENCED_PARAM(path);
    AYUNREFERENCED_PARAM(points);
    AYUNREFERENCED_PARAM(count);
    AYUNREFERENCED_PARAM(winding);
}

void MockRenderer::setPathFillColor(PathHandle path, const math::FVector4& color) {
    auto it = _paths.find(path.id);
    if (it != _paths.end()) {
        it->second.fillColor = color;
    }
}

void MockRenderer::setPathStrokeColor(PathHandle path, const math::FVector4& color) {
    auto it = _paths.find(path.id);
    if (it != _paths.end()) {
        it->second.strokeColor = color;
    }
}

void MockRenderer::setPathStrokeWidth(PathHandle path, float width) {
    auto it = _paths.find(path.id);
    if (it != _paths.end()) {
        it->second.strokeWidth = width;
    }
}

void MockRenderer::drawPath(PathHandle path, PathFillMode mode) {
    AYUNREFERENCED_PARAM(mode);
    auto it = _paths.find(path.id);
    if (it != _paths.end()) {
        DrawCall dc;
        dc.type = DrawCall::Path;
        dc.bounds = it->second.bounds;
        dc.color = it->second.fillColor;
        dc.pathHandle = path;
        _drawCalls.push_back(dc);
    }
}

void MockRenderer::pushPathClip(PathHandle path) {
    auto it = _paths.find(path.id);
    if (it != _paths.end()) {
        pushClip(it->second.bounds);
    }
}

// Blur methods / 模糊方法
void MockRenderer::drawRectBlur(const math::FRectangle& bounds, float blurRadius, BlurType type) {
    DrawCall dc;
    dc.type = DrawCall::Blur;
    dc.bounds = bounds;
    dc.floatParam1 = blurRadius;
    dc.blurType = type;
    _drawCalls.push_back(dc);
}

void MockRenderer::drawRectBlurWithMask(const math::FRectangle& bounds, PathHandle maskPath, float blurRadius, BlurType type) {
    AYUNREFERENCED_PARAM(maskPath);
    drawRectBlur(bounds, blurRadius, type);
}

// Particle system methods / 粒子系统方法
IRenderBackend::ParticleHandle MockRenderer::createParticleSystem(int maxParticles) {
    AYUNREFERENCED_PARAM(maxParticles);
    ParticleHandle handle;
    handle.id = _nextParticleId++;
    return handle;
}

void MockRenderer::releaseParticleSystem(ParticleHandle system) {
    AYUNREFERENCED_PARAM(system);
}

void MockRenderer::setParticleStyle(ParticleHandle system, const ParticleStyle& style) {
    AYUNREFERENCED_PARAM(system);
    AYUNREFERENCED_PARAM(style);
}

void MockRenderer::addParticle(ParticleHandle system, const math::FVector2& position, const math::FVector2& velocity, float life, float size) {
    AYUNREFERENCED_PARAM(system);
    AYUNREFERENCED_PARAM(position);
    AYUNREFERENCED_PARAM(velocity);
    AYUNREFERENCED_PARAM(life);
    AYUNREFERENCED_PARAM(size);
}

void MockRenderer::updateParticleSystem(ParticleHandle system, float deltaTime) {
    AYUNREFERENCED_PARAM(system);
    AYUNREFERENCED_PARAM(deltaTime);
}

void MockRenderer::drawParticleSystem(ParticleHandle system) {
    AYUNREFERENCED_PARAM(system);
}

// Render target methods / 渲染目标方法
IRenderBackend::RenderTargetHandle MockRenderer::createRenderTarget(int width, int height, bool hasAlpha) {
    AYUNREFERENCED_PARAM(width);
    AYUNREFERENCED_PARAM(height);
    AYUNREFERENCED_PARAM(hasAlpha);
    RenderTargetHandle handle;
    handle.id = _nextTargetId++;
    return handle;
}

void MockRenderer::releaseRenderTarget(RenderTargetHandle target) {
    AYUNREFERENCED_PARAM(target);
}

void MockRenderer::bindRenderTarget(RenderTargetHandle target) {
    AYUNREFERENCED_PARAM(target);
}

void* MockRenderer::getRenderTargetTexture(RenderTargetHandle target) {
    AYUNREFERENCED_PARAM(target);
    return nullptr;
}

void MockRenderer::blitRenderTarget(RenderTargetHandle source, const math::FRectangle& destBounds) {
    AYUNREFERENCED_PARAM(source);
    AYUNREFERENCED_PARAM(destBounds);
}

// Animation methods / 动画方法
IRenderBackend::AnimationHandle MockRenderer::createAnimation(float from, float to, float duration, AnimationCurve curve) {
    AnimationHandle handle;
    handle.id = _nextAnimId++;
    AnimationData data;
    data.type = AnimationData::Float;
    data.from = from;
    data.to = to;
    data.current = from;
    data.duration = duration;
    data.elapsed = 0.0f;
    data.curve = curve;
    data.flags = AnimationFlags::None;
    _animations[handle.id] = data;
    return handle;
}

IRenderBackend::AnimationHandle MockRenderer::createAnimationVec2(const math::FVector2& from, const math::FVector2& to, float duration, AnimationCurve curve) {
    AnimationHandle handle;
    handle.id = _nextAnimId++;
    AnimationData data;
    data.type = AnimationData::Vec2;
    data.vec2From = from;
    data.vec2To = to;
    data.vec2Current = from;
    data.duration = duration;
    data.elapsed = 0.0f;
    data.curve = curve;
    data.flags = AnimationFlags::None;
    _animations[handle.id] = data;
    return handle;
}

IRenderBackend::AnimationHandle MockRenderer::createAnimationVec4(const math::FVector4& from, const math::FVector4& to, float duration, AnimationCurve curve) {
    AnimationHandle handle;
    handle.id = _nextAnimId++;
    AnimationData data;
    data.type = AnimationData::Vec4;
    data.vec4From = from;
    data.vec4To = to;
    data.vec4Current = from;
    data.duration = duration;
    data.elapsed = 0.0f;
    data.curve = curve;
    data.flags = AnimationFlags::None;
    _animations[handle.id] = data;
    return handle;
}

void MockRenderer::releaseAnimation(AnimationHandle anim) {
    _animations.erase(anim.id);
}

void MockRenderer::setAnimationFlags(AnimationHandle anim, AnimationFlags flags) {
    auto it = _animations.find(anim.id);
    if (it != _animations.end()) {
        it->second.flags = flags;
    }
}

bool MockRenderer::updateAnimation(AnimationHandle anim, float deltaTime) {
    auto it = _animations.find(anim.id);
    if (it == _animations.end()) return false;

    AnimationData& data = it->second;
    data.elapsed += deltaTime;

    if (data.elapsed >= data.duration) {
        if (data.flags == AnimationFlags::Loop) {
            data.elapsed = math::fmod(data.elapsed, data.duration);
        } else if (data.flags == AnimationFlags::PingPong) {
            // Simplified ping-pong: just reverse
            std::swap(data.from, data.to);
            data.elapsed = 0.0f;
        } else {
            return false; // Animation complete
        }
    }

    float t = data.elapsed / data.duration;
    switch (data.curve) {
        case AnimationCurve::EaseIn:
            t = t * t;
            break;
        case AnimationCurve::EaseOut:
            t = 1.0f - (1.0f - t) * (1.0f - t);
            break;
        case AnimationCurve::EaseInOut:
            t = t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
            break;
        case AnimationCurve::Spring:
            // Simplified spring
            t = t + math::sin(t * 6.28f) * 0.1f * (1.0f - t);
            break;
        case AnimationCurve::Linear:
        default:
            break;
    }

    if (data.type == AnimationData::Float) {
        data.current = data.from + (data.to - data.from) * t;
    } else if (data.type == AnimationData::Vec2) {
        data.vec2Current = math::lerp(data.vec2From, data.vec2To, t);
    } else if (data.type == AnimationData::Vec4) {
        data.vec4Current = math::lerp(data.vec4From, data.vec4To, t);
    }

    return true;
}

float MockRenderer::getAnimationValue(AnimationHandle anim) const {
    auto it = _animations.find(anim.id);
    if (it != _animations.end() && it->second.type == AnimationData::Float) {
        return it->second.current;
    }
    return 0.0f;
}

math::FVector2 MockRenderer::getAnimationValueVec2(AnimationHandle anim) const {
    auto it = _animations.find(anim.id);
    if (it != _animations.end() && it->second.type == AnimationData::Vec2) {
        return it->second.vec2Current;
    }
    return math::FVector2(0, 0);
}

math::FVector4 MockRenderer::getAnimationValueVec4(AnimationHandle anim) const {
    auto it = _animations.find(anim.id);
    if (it != _animations.end() && it->second.type == AnimationData::Vec4) {
        return it->second.vec4Current;
    }
    return math::FVector4(0, 0, 0, 0);
}

// Font methods / 字体方法
FontHandle MockRenderer::loadFont(const wchar_t* path, int baseSize) {
    AYUNREFERENCED_PARAM(path);
    AYUNREFERENCED_PARAM(baseSize);
    FontHandle handle;
    handle.id = _nextFontId++;
    return handle;
}

void MockRenderer::releaseFont(FontHandle font) {
    AYUNREFERENCED_PARAM(font);
}

FontHandle MockRenderer::getFontHandle(const wchar_t* familyName, int baseSize) {
    AYUNREFERENCED_PARAM(familyName);
    AYUNREFERENCED_PARAM(baseSize);
    FontHandle handle;
    handle.id = -1;
    return handle;
}

FontHandle MockRenderer::registerFontFromMemory(const void* data, size_t dataSize, int baseSize) {
    AYUNREFERENCED_PARAM(data);
    AYUNREFERENCED_PARAM(dataSize);
    AYUNREFERENCED_PARAM(baseSize);
    FontHandle handle;
    handle.id = _nextFontId++;
    return handle;
}

// Metrics methods / 度量方法
IRenderBackend::TextMetrics MockRenderer::measureText(const std::wstring& text, int fontSize, float maxWidth) const {
    AYUNREFERENCED_PARAM(maxWidth);
    TextMetrics metrics;
    metrics.width = static_cast<float>(text.length()) * fontSize * 0.6f;
    metrics.height = static_cast<float>(fontSize);
    metrics.ascent = metrics.height * 0.8f;
    metrics.descent = metrics.height * 0.2f;
    return metrics;
}

ayt::font::FontMetrics MockRenderer::getFontMetrics(FontHandle font) const {
    AYUNREFERENCED_PARAM(font);
    ayt::font::FontMetrics metrics;
    metrics.lineHeight = 1.2f;
    metrics.ascent = 0.8f;
    metrics.descent = 0.2f;
    metrics.underlinePos = 0.0f;
    metrics.strikethroughPos = 0.4f;
    metrics.lineGap = 0.1f;
    return metrics;
}

size_t MockRenderer::getAvailableVideoMemory() const {
    return 1024 * 1024 * 1024; // 1GB mock
}

std::string MockRenderer::getDriverVersion() const {
    return "MockRenderer 1.0";
}

std::string MockRenderer::getBackendName() const {
    return "Mock";
}

void MockRenderer::pushPerformanceMarker(const char* name) {
    AYUNREFERENCED_PARAM(name);
}

void MockRenderer::popPerformanceMarker() {
}

} // namespace ayt::ui
