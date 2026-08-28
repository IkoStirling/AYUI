#include "AYUI/MockRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ayt::ui {

namespace {

bool mockRectEmpty(const math::FRectangle& rect) {
    return rect.maxX <= rect.minX || rect.maxY <= rect.minY;
}

} // namespace

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
    _nextLayerId = 1;
    // PR-Container-Contract-Cut2: also reset the clip stack so a
    // mid-test clear() doesn't leak stack frames into the next render.
    _clipStack.clear();
    _currentBlend = BlendMode::Normal;
    _opacityStack.assign(1, 1.0f);
    _renderTargets.clear();
    _layers.clear();
    _layerEvents.clear();
    _boundRenderTarget = RenderTargetHandle{-1};
    _activeLayer = LayerHandle{-1};
}

void MockRenderer::beginFrame() {
    // UIRenderBackend::beginFrame clears its UiItem command buffer. Mirror
    // that contract so UIManager multi-frame tests cannot accidentally rely
    // on retained draw calls that do not exist on the production backend.
    _drawCalls.clear();
    _triangleCount = 0;
    _vertexCount = 0;
    _clipStack.clear();
    _currentBlend = BlendMode::Normal;
    _opacityStack.assign(1, 1.0f);
    _layerEvents.clear();
    _boundRenderTarget = RenderTargetHandle{-1};
    _activeLayer = LayerHandle{-1};
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
    // AYUI-Audit-2026-08-26: previously this delegated to the base
    // color overload and silently dropped wrapToBounds / valign /
    // outlineWidth / outlineColor / shadowColor / shadowOffset /
    // shadowBlurRadius / letterSpacing / lineSpacing. Record every
    // TextStyle field into a DrawCall so tests can assert on them.
    DrawCall dc;
    dc.type = DrawCall::Text;
    dc.bounds = bounds;
    dc.color = tintWithOpacity(style.color);
    dc.text = text;
    dc.fontSize = fontSize;
    dc.textStyle = style;
    dc.texture = nullptr;
    dc.blendMode = _currentBlend;
    // Outline + shadow + spacing + alignment
    dc.outlineColor      = style.outlineColor;
    dc.outlineWidth      = style.outlineWidth;
    dc.shadowColor       = style.shadowColor;
    dc.shadowOffset      = style.shadowOffset;
    dc.shadowBlurRadius  = style.shadowBlurRadius;
    dc.letterSpacing     = style.letterSpacing;
    dc.lineSpacing       = style.lineSpacing;
    dc.wrapToBounds      = style.wrapToBounds;
    dc.align             = style.align;
    dc.valign            = style.valign;
    _drawCalls.push_back(dc);
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

// AYUI-Audit-2026-08-26: drawCard override. The base IRenderBackend
// implementation expands a Card into shadow → fill → border sub-calls,
// which works for visual fidelity but makes it impossible for tests to
// observe that a caller actually invoked drawCard (vs. drawRect +
// drawBorderRect manually). Record the full CardStyle into a single
// DrawCall::Card entry so round-trip tests can verify the API surface.
void MockRenderer::drawCard(const math::FRectangle& bounds, const CardStyle& style) {
    DrawCall dc;
    dc.type = DrawCall::Card;
    dc.bounds = bounds;
    dc.texture = nullptr;
    dc.blendMode = _currentBlend;
    // Fill rides in `color` (multiplied by current opacity) AND in
    // `cardFillColor` (raw). Tests that want to check the post-tint
    // color can read `color`; tests checking the raw style payload
    // read `cardFillColor`. Both are intentional.
    dc.color = tintWithOpacity(style.fillColor);
    dc.cardFillColor        = style.fillColor;
    dc.cardBorderColor      = style.borderColor;
    dc.cardBorderWidth      = style.borderWidth;
    dc.cardBorderPosition   = style.borderPosition;
    dc.cardShadowColor      = style.shadowColor;
    dc.cardShadowOffset     = style.shadowOffset;
    dc.cardShadowBlurRadius = style.shadowBlurRadius;
    dc.cardRadii[0] = style.cornerRadius.topLeft;
    dc.cardRadii[1] = style.cornerRadius.topRight;
    dc.cardRadii[2] = style.cornerRadius.bottomRight;
    dc.cardRadii[3] = style.cornerRadius.bottomLeft;
    _drawCalls.push_back(dc);
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
    RenderTargetDesc desc;
    desc.width = width;
    desc.height = height;
    desc.hasAlpha = hasAlpha;
    return createRenderTarget(desc);
}

IRenderBackend::RenderTargetHandle MockRenderer::createRenderTarget(const RenderTargetDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0 || desc.dpiScale <= 0.0f) {
        return RenderTargetHandle{-1};
    }
    RenderTargetHandle handle;
    handle.id = _nextTargetId++;
    _renderTargets.emplace(handle.id, RenderTargetData{desc});
    return handle;
}

bool MockRenderer::resizeRenderTarget(RenderTargetHandle target, const RenderTargetDesc& desc) {
    auto it = _renderTargets.find(target.id);
    if (it == _renderTargets.end() || desc.width <= 0 || desc.height <= 0
        || desc.dpiScale <= 0.0f) {
        return false;
    }
    it->second.desc = desc;
    return true;
}

void MockRenderer::releaseRenderTarget(RenderTargetHandle target) {
    _renderTargets.erase(target.id);
    if (_boundRenderTarget.id == target.id) {
        _boundRenderTarget = RenderTargetHandle{-1};
    }
}

void MockRenderer::bindRenderTarget(RenderTargetHandle target) {
    if (!target.isValid() || _renderTargets.find(target.id) != _renderTargets.end()) {
        _boundRenderTarget = target;
    }
}

void* MockRenderer::getRenderTargetTexture(RenderTargetHandle target) {
    if (_renderTargets.find(target.id) == _renderTargets.end()) {
        return nullptr;
    }
    return reinterpret_cast<void*>(static_cast<uintptr_t>(target.id) + 1u);
}

void MockRenderer::blitRenderTarget(RenderTargetHandle source, const math::FRectangle& destBounds) {
    if (_renderTargets.find(source.id) == _renderTargets.end()) {
        return;
    }
    DrawCall dc;
    dc.type = DrawCall::Image;
    dc.bounds = destBounds;
    dc.texture = getRenderTargetTexture(source);
    dc.color = math::FVector4(1, 1, 1, _opacityStack.back());
    dc.blendMode = _currentBlend;
    _drawCalls.push_back(dc);
    _triangleCount += 2;
    _vertexCount += 6;
}

IRenderBackend::LayerHandle MockRenderer::createLayer(const LayerDesc& desc) {
    const float width = desc.logicalBounds.maxX - desc.logicalBounds.minX;
    const float height = desc.logicalBounds.maxY - desc.logicalBounds.minY;
    if (width <= 0.0f || height <= 0.0f || desc.dpiScale <= 0.0f) {
        return LayerHandle{-1};
    }

    RenderTargetDesc targetDesc;
    targetDesc.width = std::max(1, static_cast<int>(std::ceil(width * desc.dpiScale)));
    targetDesc.height = std::max(1, static_cast<int>(std::ceil(height * desc.dpiScale)));
    targetDesc.dpiScale = desc.dpiScale;
    targetDesc.hasAlpha = desc.hasAlpha;
    targetDesc.preserveContents = desc.clearMode == LayerClearMode::Preserve;
    const RenderTargetHandle target = createRenderTarget(targetDesc);
    if (!target.isValid()) {
        return LayerHandle{-1};
    }

    LayerHandle handle{_nextLayerId++};
    LayerData data;
    data.desc = desc;
    data.target = target;
    data.dirty = true;
    _layers.emplace(handle.id, data);
    _layerEvents.push_back({LayerEvent::Created, handle, desc.logicalBounds,
                            math::FRectangle(), 1.0f, true, desc.overlay});
    return handle;
}

void MockRenderer::releaseLayer(LayerHandle layer) {
    auto it = _layers.find(layer.id);
    if (it == _layers.end()) {
        return;
    }
    const LayerDesc desc = it->second.desc;
    releaseRenderTarget(it->second.target);
    _layers.erase(it);
    if (_activeLayer.id == layer.id) {
        _activeLayer = LayerHandle{-1};
    }
    _layerEvents.push_back({LayerEvent::Released, layer, desc.logicalBounds,
                            math::FRectangle(), 1.0f, false, desc.overlay});
}

bool MockRenderer::updateLayer(LayerHandle layer, const LayerDesc& desc) {
    auto it = _layers.find(layer.id);
    if (it == _layers.end()) {
        return false;
    }
    const float width = desc.logicalBounds.maxX - desc.logicalBounds.minX;
    const float height = desc.logicalBounds.maxY - desc.logicalBounds.minY;
    if (width <= 0.0f || height <= 0.0f || desc.dpiScale <= 0.0f) {
        return false;
    }
    RenderTargetDesc targetDesc;
    targetDesc.width = std::max(1, static_cast<int>(std::ceil(width * desc.dpiScale)));
    targetDesc.height = std::max(1, static_cast<int>(std::ceil(height * desc.dpiScale)));
    targetDesc.dpiScale = desc.dpiScale;
    targetDesc.hasAlpha = desc.hasAlpha;
    targetDesc.preserveContents = desc.clearMode == LayerClearMode::Preserve;
    if (!resizeRenderTarget(it->second.target, targetDesc)) {
        return false;
    }
    it->second.desc = desc;
    it->second.dirty = true;
    it->second.damage = math::FRectangle();
    _layerEvents.push_back({LayerEvent::Updated, layer, desc.logicalBounds,
                            math::FRectangle(), 1.0f, true, desc.overlay});
    return true;
}

bool MockRenderer::beginLayerPaint(LayerHandle layer, const LayerPaint& paint) {
    auto it = _layers.find(layer.id);
    if (it == _layers.end() || _activeLayer.isValid()) {
        return false;
    }
    it->second.painting = true;
    _activeLayer = layer;
    bindRenderTarget(it->second.target);
    _layerEvents.push_back({LayerEvent::PaintBegan, layer,
                            it->second.desc.logicalBounds, paint.damage,
                            1.0f, paint.fullRedraw, it->second.desc.overlay});
    return true;
}

void MockRenderer::endLayerPaint(LayerHandle layer) {
    auto it = _layers.find(layer.id);
    if (it == _layers.end() || _activeLayer.id != layer.id) {
        return;
    }
    it->second.painting = false;
    it->second.dirty = false;
    it->second.damage = math::FRectangle();
    _activeLayer = LayerHandle{-1};
    bindRenderTarget(RenderTargetHandle{-1});
    _layerEvents.push_back({LayerEvent::PaintEnded, layer,
                            it->second.desc.logicalBounds, math::FRectangle(),
                            1.0f, false, it->second.desc.overlay});
}

void MockRenderer::compositeLayer(LayerHandle layer,
                                  const math::FRectangle& destBounds,
                                  float opacity) {
    auto it = _layers.find(layer.id);
    if (it == _layers.end() || it->second.dirty || it->second.painting) {
        return;
    }
    const float alpha = std::clamp(opacity, 0.0f, 1.0f);
    DrawCall dc;
    dc.type = DrawCall::Layer;
    dc.bounds = destBounds;
    dc.color = math::FVector4(1, 1, 1, alpha * _opacityStack.back());
    dc.texture = getRenderTargetTexture(it->second.target);
    dc.blendMode = _currentBlend;
    _drawCalls.push_back(dc);
    _triangleCount += 2;
    _vertexCount += 6;
    _layerEvents.push_back({LayerEvent::Composited, layer, destBounds,
                            math::FRectangle(), alpha, false,
                            it->second.desc.overlay});
}

void MockRenderer::invalidateLayer(LayerHandle layer,
                                   const math::FRectangle& damage) {
    auto it = _layers.find(layer.id);
    if (it == _layers.end()) {
        return;
    }
    it->second.dirty = true;
    if (!mockRectEmpty(damage)) {
        if (mockRectEmpty(it->second.damage)) {
            it->second.damage = damage;
        } else {
            it->second.damage = math::FRectangle::fromMinMax(
                math::FVector2(std::min(it->second.damage.minX, damage.minX),
                               std::min(it->second.damage.minY, damage.minY)),
                math::FVector2(std::max(it->second.damage.maxX, damage.maxX),
                               std::max(it->second.damage.maxY, damage.maxY)));
        }
    } else {
        it->second.damage = math::FRectangle();
    }
    _layerEvents.push_back({LayerEvent::Invalidated, layer,
                            it->second.desc.logicalBounds, damage,
                            1.0f, mockRectEmpty(damage),
                            it->second.desc.overlay});
}

bool MockRenderer::isLayerDirty(LayerHandle layer) const {
    const auto it = _layers.find(layer.id);
    return it == _layers.end() || it->second.dirty;
}

IRenderBackend::RenderTargetDesc MockRenderer::getRenderTargetDesc(
    RenderTargetHandle target) const {
    const auto it = _renderTargets.find(target.id);
    return it == _renderTargets.end() ? RenderTargetDesc{} : it->second.desc;
}

IRenderBackend::RenderTargetHandle MockRenderer::getLayerRenderTarget(
    LayerHandle layer) const {
    const auto it = _layers.find(layer.id);
    return it == _layers.end() ? RenderTargetHandle{-1} : it->second.target;
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
