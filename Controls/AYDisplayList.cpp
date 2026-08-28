#include "AYUI/DisplayList.h"

#include <utility>

namespace ayt::ui {

struct DisplayListRecorder::RecordedPath {
    PathHandle targetHandle;
    std::vector<std::function<void(IRenderBackend&, PathHandle)>> operations;
};

void DisplayList::replay(IRenderBackend& renderer) const
{
    for (const Command& command : _commands) {
        command(renderer);
    }
}

DisplayListRecorder::DisplayListRecorder(IRenderBackend& target, DisplayList& output)
    : _target(target)
    , _output(output)
{
    _output.clear();
}

void DisplayListRecorder::append(DisplayList::Command command)
{
    _output._commands.push_back(std::move(command));
}

std::shared_ptr<DisplayListRecorder::RecordedPath>
DisplayListRecorder::findRecordedPath(PathHandle path) const
{
    const auto it = _recordedPaths.find(path.id);
    return it != _recordedPaths.end() ? it->second : nullptr;
}

void DisplayListRecorder::recordAndForwardNested(DisplayList::Command command)
{
    append(command);
    command(_target);
}

#define AYUI_RECORD_AND_FORWARD(expression) \
    do {                                      \
        append([=](IRenderBackend& backend) { backend.expression; }); \
        _target.expression;                   \
    } while (false)

#define AYUI_UNCACHEABLE_FORWARD(expression) \
    do {                                       \
        disableCaching();                      \
        _target.expression;                    \
    } while (false)

void DisplayListRecorder::beginFrame() { AYUI_UNCACHEABLE_FORWARD(beginFrame()); }
void DisplayListRecorder::endFrame() { AYUI_UNCACHEABLE_FORWARD(endFrame()); }
void DisplayListRecorder::beginCanvas(const math::FRectangle& viewport)
{
    disableCaching();
    _target.beginCanvas(viewport);
}
void DisplayListRecorder::endCanvas() { AYUI_UNCACHEABLE_FORWARD(endCanvas()); }

void DisplayListRecorder::setBlendMode(BlendMode mode)
{
    AYUI_RECORD_AND_FORWARD(setBlendMode(mode));
}
void DisplayListRecorder::pushOpacity(float alpha)
{
    AYUI_RECORD_AND_FORWARD(pushOpacity(alpha));
}
void DisplayListRecorder::popOpacity() { AYUI_RECORD_AND_FORWARD(popOpacity()); }
void DisplayListRecorder::pushTransform(const math::Float4x4& transform)
{
    const math::Float4x4 copy = transform;
    append([copy](IRenderBackend& backend) { backend.pushTransform(copy); });
    _target.pushTransform(transform);
}
void DisplayListRecorder::popTransform() { AYUI_RECORD_AND_FORWARD(popTransform()); }
void DisplayListRecorder::pushClip(const math::FRectangle& bounds)
{
    AYUI_RECORD_AND_FORWARD(pushClip(bounds));
}
void DisplayListRecorder::popClip() { AYUI_RECORD_AND_FORWARD(popClip()); }
void DisplayListRecorder::setScissor(const math::FRectangle& bounds)
{
    AYUI_RECORD_AND_FORWARD(setScissor(bounds));
}

void DisplayListRecorder::drawRect(const math::FRectangle& bounds,
                                   const math::FVector4& color)
{
    AYUI_RECORD_AND_FORWARD(drawRect(bounds, color));
}
void DisplayListRecorder::drawRect(const math::FRectangle& bounds, void* textureHandle,
                                   const math::FRectangle& uv)
{
    AYUI_RECORD_AND_FORWARD(drawRect(bounds, textureHandle, uv));
}
void DisplayListRecorder::drawText(const math::FRectangle& bounds,
                                   const std::wstring& text, int fontSize,
                                   const math::FVector4& color)
{
    const std::wstring copy = text;
    append([bounds, copy, fontSize, color](IRenderBackend& backend) {
        backend.drawText(bounds, copy, fontSize, color);
    });
    _target.drawText(bounds, text, fontSize, color);
}
void DisplayListRecorder::drawText(const math::FRectangle& bounds,
                                   const std::wstring& text, int fontSize,
                                   const TextStyle& style)
{
    const std::wstring copy = text;
    append([bounds, copy, fontSize, style](IRenderBackend& backend) {
        backend.drawText(bounds, copy, fontSize, style);
    });
    _target.drawText(bounds, text, fontSize, style);
}
void DisplayListRecorder::drawNinePatch(const math::FRectangle& bounds,
                                        void* textureHandle,
                                        const math::FRectangle& uvRegion,
                                        const math::FVector4& padding)
{
    AYUI_RECORD_AND_FORWARD(drawNinePatch(bounds, textureHandle, uvRegion, padding));
}
void DisplayListRecorder::drawGradientRect(const math::FRectangle& bounds,
                                           const math::FVector4& topLeft,
                                           const math::FVector4& topRight,
                                           const math::FVector4& bottomLeft,
                                           const math::FVector4& bottomRight)
{
    AYUI_RECORD_AND_FORWARD(drawGradientRect(bounds, topLeft, topRight, bottomLeft, bottomRight));
}
void DisplayListRecorder::drawGradientRect(const math::FRectangle& bounds,
                                           const math::FVector4& topColor,
                                           const math::FVector4& bottomColor)
{
    AYUI_RECORD_AND_FORWARD(drawGradientRect(bounds, topColor, bottomColor));
}
void DisplayListRecorder::drawRect(const math::FRectangle& bounds,
                                   const BorderStyle& border)
{
    AYUI_RECORD_AND_FORWARD(drawRect(bounds, border));
}
void DisplayListRecorder::drawBorderRect(const math::FRectangle& bounds,
                                         const math::FVector4& color,
                                         float borderWidth, float cornerRadius)
{
    AYUI_RECORD_AND_FORWARD(drawBorderRect(bounds, color, borderWidth, cornerRadius));
}
void DisplayListRecorder::drawRoundedRect(const math::FRectangle& bounds,
                                          const math::FVector4& color,
                                          float cornerRadius)
{
    AYUI_RECORD_AND_FORWARD(drawRoundedRect(bounds, color, cornerRadius));
}
void DisplayListRecorder::drawRoundedRect(const math::FRectangle& bounds,
                                          const math::FVector4& color,
                                          const CornerRadii& radii)
{
    AYUI_RECORD_AND_FORWARD(drawRoundedRect(bounds, color, radii));
}
void DisplayListRecorder::drawBorderRect(const math::FRectangle& bounds,
                                         const math::FVector4& color,
                                         float borderWidth,
                                         const CornerRadii& radii)
{
    AYUI_RECORD_AND_FORWARD(drawBorderRect(bounds, color, borderWidth, radii));
}
void DisplayListRecorder::drawCard(const math::FRectangle& bounds,
                                   const CardStyle& style)
{
    AYUI_RECORD_AND_FORWARD(drawCard(bounds, style));
}
void DisplayListRecorder::drawRectShadow(const math::FRectangle& bounds,
                                         const ShadowStyle& shadow)
{
    AYUI_RECORD_AND_FORWARD(drawRectShadow(bounds, shadow));
}
void DisplayListRecorder::pushMask() { AYUI_RECORD_AND_FORWARD(pushMask()); }
void DisplayListRecorder::popMask() { AYUI_RECORD_AND_FORWARD(popMask()); }
void DisplayListRecorder::drawWithAlpha(const math::FRectangle& bounds,
                                        void* textureHandle, float alpha)
{
    AYUI_RECORD_AND_FORWARD(drawWithAlpha(bounds, textureHandle, alpha));
}
void DisplayListRecorder::drawSprite(const math::FRectangle& bounds,
                                     void* atlasTexture,
                                     const wchar_t* spriteName)
{
    const std::wstring name = spriteName != nullptr ? spriteName : L"";
    append([bounds, atlasTexture, name](IRenderBackend& backend) {
        backend.drawSprite(bounds, atlasTexture, name.c_str());
    });
    _target.drawSprite(bounds, atlasTexture, spriteName);
}
void DisplayListRecorder::drawRectBlur(const math::FRectangle& bounds,
                                       float blurRadius, BlurType type)
{
    AYUI_RECORD_AND_FORWARD(drawRectBlur(bounds, blurRadius, type));
}

IRenderBackend::PathHandle DisplayListRecorder::createPath()
{
    const PathHandle local{_nextRecordedPathId++};
    auto record = std::make_shared<RecordedPath>();
    record->targetHandle = _target.createPath();
    _recordedPaths.emplace(local.id, std::move(record));
    return local;
}
void DisplayListRecorder::releasePath(PathHandle path)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    _target.releasePath(record->targetHandle);
    _recordedPaths.erase(path.id);
}
void DisplayListRecorder::addPathRect(PathHandle path,
                                      const math::FRectangle& bounds,
                                      PathWinding winding)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    record->operations.push_back([bounds, winding](IRenderBackend& backend, PathHandle replay) {
        backend.addPathRect(replay, bounds, winding);
    });
    _target.addPathRect(record->targetHandle, bounds, winding);
}
void DisplayListRecorder::addPathRoundedRect(PathHandle path,
                                             const math::FRectangle& bounds,
                                             float cornerRadius,
                                             PathWinding winding)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    record->operations.push_back(
        [bounds, cornerRadius, winding](IRenderBackend& backend, PathHandle replay) {
            backend.addPathRoundedRect(replay, bounds, cornerRadius, winding);
        });
    _target.addPathRoundedRect(record->targetHandle, bounds, cornerRadius, winding);
}
void DisplayListRecorder::addPathEllipse(PathHandle path,
                                         const math::FVector2& center,
                                         float radiusX, float radiusY,
                                         PathWinding winding)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    record->operations.push_back(
        [center, radiusX, radiusY, winding](IRenderBackend& backend, PathHandle replay) {
            backend.addPathEllipse(replay, center, radiusX, radiusY, winding);
        });
    _target.addPathEllipse(record->targetHandle, center, radiusX, radiusY, winding);
}
void DisplayListRecorder::addPathLine(PathHandle path,
                                      const math::FVector2& start,
                                      const math::FVector2& end)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    record->operations.push_back([start, end](IRenderBackend& backend, PathHandle replay) {
        backend.addPathLine(replay, start, end);
    });
    _target.addPathLine(record->targetHandle, start, end);
}
void DisplayListRecorder::addPathBezier(PathHandle path,
                                        const math::FVector2& start,
                                        const math::FVector2& control1,
                                        const math::FVector2& control2,
                                        const math::FVector2& end)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    record->operations.push_back(
        [start, control1, control2, end](IRenderBackend& backend, PathHandle replay) {
            backend.addPathBezier(replay, start, control1, control2, end);
        });
    _target.addPathBezier(record->targetHandle, start, control1, control2, end);
}
void DisplayListRecorder::addPathArc(PathHandle path,
                                     const math::FVector2& center, float radius,
                                     float startAngle, float endAngle,
                                     PathWinding winding)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    record->operations.push_back(
        [center, radius, startAngle, endAngle, winding](IRenderBackend& backend,
                                                        PathHandle replay) {
            backend.addPathArc(replay, center, radius, startAngle, endAngle, winding);
        });
    _target.addPathArc(record->targetHandle, center, radius,
                       startAngle, endAngle, winding);
}
void DisplayListRecorder::addPathPolygon(PathHandle path,
                                         const math::FVector2* points, int count,
                                         PathWinding winding)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr || points == nullptr || count <= 0) return;
    const std::vector<math::FVector2> copy(points, points + count);
    record->operations.push_back(
        [copy, winding](IRenderBackend& backend, PathHandle replay) {
            backend.addPathPolygon(replay, copy.data(),
                                   static_cast<int>(copy.size()), winding);
        });
    _target.addPathPolygon(record->targetHandle, points, count, winding);
}
void DisplayListRecorder::setPathFillColor(PathHandle path,
                                           const math::FVector4& color)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    record->operations.push_back([color](IRenderBackend& backend, PathHandle replay) {
        backend.setPathFillColor(replay, color);
    });
    _target.setPathFillColor(record->targetHandle, color);
}
void DisplayListRecorder::setPathStrokeColor(PathHandle path,
                                             const math::FVector4& color)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    record->operations.push_back([color](IRenderBackend& backend, PathHandle replay) {
        backend.setPathStrokeColor(replay, color);
    });
    _target.setPathStrokeColor(record->targetHandle, color);
}
void DisplayListRecorder::setPathStrokeWidth(PathHandle path, float width)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    record->operations.push_back([width](IRenderBackend& backend, PathHandle replay) {
        backend.setPathStrokeWidth(replay, width);
    });
    _target.setPathStrokeWidth(record->targetHandle, width);
}
void DisplayListRecorder::drawPath(PathHandle path, PathFillMode mode)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    const auto recipe = record->operations;
    append([recipe, mode](IRenderBackend& backend) {
        const PathHandle replay = backend.createPath();
        for (const auto& operation : recipe) operation(backend, replay);
        backend.drawPath(replay, mode);
        backend.releasePath(replay);
    });
    _target.drawPath(record->targetHandle, mode);
}
void DisplayListRecorder::pushPathClip(PathHandle path)
{
    const auto record = findRecordedPath(path);
    if (record == nullptr) return;
    const auto recipe = record->operations;
    append([recipe](IRenderBackend& backend) {
        const PathHandle replay = backend.createPath();
        for (const auto& operation : recipe) operation(backend, replay);
        backend.pushPathClip(replay);
        backend.releasePath(replay);
    });
    _target.pushPathClip(record->targetHandle);
}
void DisplayListRecorder::drawRectBlurWithMask(const math::FRectangle& bounds,
                                               PathHandle maskPath,
                                               float blurRadius, BlurType type)
{
    const auto record = findRecordedPath(maskPath);
    if (record == nullptr) return;
    const auto recipe = record->operations;
    append([recipe, bounds, blurRadius, type](IRenderBackend& backend) {
        const PathHandle replay = backend.createPath();
        for (const auto& operation : recipe) operation(backend, replay);
        backend.drawRectBlurWithMask(bounds, replay, blurRadius, type);
        backend.releasePath(replay);
    });
    _target.drawRectBlurWithMask(bounds, record->targetHandle, blurRadius, type);
}

IRenderBackend::ParticleHandle DisplayListRecorder::createParticleSystem(int maxParticles)
{
    disableCaching();
    return _target.createParticleSystem(maxParticles);
}
void DisplayListRecorder::releaseParticleSystem(ParticleHandle system)
{
    AYUI_UNCACHEABLE_FORWARD(releaseParticleSystem(system));
}
void DisplayListRecorder::setParticleStyle(ParticleHandle system,
                                           const ParticleStyle& style)
{
    disableCaching();
    _target.setParticleStyle(system, style);
}
void DisplayListRecorder::addParticle(ParticleHandle system,
                                      const math::FVector2& position,
                                      const math::FVector2& velocity,
                                      float life, float size)
{
    disableCaching();
    _target.addParticle(system, position, velocity, life, size);
}
void DisplayListRecorder::updateParticleSystem(ParticleHandle system,
                                               float deltaTime)
{
    disableCaching();
    _target.updateParticleSystem(system, deltaTime);
}
void DisplayListRecorder::drawParticleSystem(ParticleHandle system)
{
    AYUI_UNCACHEABLE_FORWARD(drawParticleSystem(system));
}

bool DisplayListRecorder::supportsRenderTargets() const
{
    return _target.supportsRenderTargets();
}
IRenderBackend::RenderTargetHandle DisplayListRecorder::createRenderTarget(
    int width, int height, bool hasAlpha)
{
    disableCaching();
    return _target.createRenderTarget(width, height, hasAlpha);
}
IRenderBackend::RenderTargetHandle DisplayListRecorder::createRenderTarget(
    const RenderTargetDesc& desc)
{
    disableCaching();
    return _target.createRenderTarget(desc);
}
bool DisplayListRecorder::resizeRenderTarget(RenderTargetHandle target,
                                             const RenderTargetDesc& desc)
{
    disableCaching();
    return _target.resizeRenderTarget(target, desc);
}
void DisplayListRecorder::releaseRenderTarget(RenderTargetHandle target)
{
    AYUI_UNCACHEABLE_FORWARD(releaseRenderTarget(target));
}
void DisplayListRecorder::bindRenderTarget(RenderTargetHandle target)
{
    AYUI_UNCACHEABLE_FORWARD(bindRenderTarget(target));
}
void* DisplayListRecorder::getRenderTargetTexture(RenderTargetHandle target)
{
    disableCaching();
    return _target.getRenderTargetTexture(target);
}
void DisplayListRecorder::blitRenderTarget(RenderTargetHandle source,
                                           const math::FRectangle& destBounds)
{
    disableCaching();
    _target.blitRenderTarget(source, destBounds);
}

IRenderBackend::LayerHandle DisplayListRecorder::createLayer(const LayerDesc& desc)
{
    disableCaching();
    return _target.createLayer(desc);
}
void DisplayListRecorder::releaseLayer(LayerHandle layer)
{
    AYUI_UNCACHEABLE_FORWARD(releaseLayer(layer));
}
bool DisplayListRecorder::updateLayer(LayerHandle layer, const LayerDesc& desc)
{
    disableCaching();
    return _target.updateLayer(layer, desc);
}
bool DisplayListRecorder::beginLayerPaint(LayerHandle layer, const LayerPaint& paint)
{
    disableCaching();
    return _target.beginLayerPaint(layer, paint);
}
void DisplayListRecorder::endLayerPaint(LayerHandle layer)
{
    AYUI_UNCACHEABLE_FORWARD(endLayerPaint(layer));
}
void DisplayListRecorder::compositeLayer(LayerHandle layer,
                                         const math::FRectangle& destBounds,
                                         float opacity)
{
    disableCaching();
    _target.compositeLayer(layer, destBounds, opacity);
}
void DisplayListRecorder::invalidateLayer(LayerHandle layer,
                                          const math::FRectangle& damage)
{
    disableCaching();
    _target.invalidateLayer(layer, damage);
}

IRenderBackend::AnimationHandle DisplayListRecorder::createAnimation(
    float from, float to, float duration, AnimationCurve curve)
{
    disableCaching();
    return _target.createAnimation(from, to, duration, curve);
}
IRenderBackend::AnimationHandle DisplayListRecorder::createAnimationVec2(
    const math::FVector2& from, const math::FVector2& to, float duration,
    AnimationCurve curve)
{
    disableCaching();
    return _target.createAnimationVec2(from, to, duration, curve);
}
IRenderBackend::AnimationHandle DisplayListRecorder::createAnimationVec4(
    const math::FVector4& from, const math::FVector4& to, float duration,
    AnimationCurve curve)
{
    disableCaching();
    return _target.createAnimationVec4(from, to, duration, curve);
}
void DisplayListRecorder::releaseAnimation(AnimationHandle anim)
{
    AYUI_UNCACHEABLE_FORWARD(releaseAnimation(anim));
}
void DisplayListRecorder::setAnimationFlags(AnimationHandle anim,
                                            AnimationFlags flags)
{
    disableCaching();
    _target.setAnimationFlags(anim, flags);
}
bool DisplayListRecorder::updateAnimation(AnimationHandle anim, float deltaTime)
{
    disableCaching();
    return _target.updateAnimation(anim, deltaTime);
}
float DisplayListRecorder::getAnimationValue(AnimationHandle anim) const
{
    return _target.getAnimationValue(anim);
}
math::FVector2 DisplayListRecorder::getAnimationValueVec2(AnimationHandle anim) const
{
    return _target.getAnimationValueVec2(anim);
}
math::FVector4 DisplayListRecorder::getAnimationValueVec4(AnimationHandle anim) const
{
    return _target.getAnimationValueVec4(anim);
}

bool DisplayListRecorder::addColoredQuad(const math::FRectangle& bounds,
                                         const math::FVector4& color)
{
    append([bounds, color](IRenderBackend& backend) {
        (void)backend.addColoredQuad(bounds, color);
    });
    return _target.addColoredQuad(bounds, color);
}
bool DisplayListRecorder::addTexturedQuad(const math::FRectangle& bounds,
                                          void* textureHandle,
                                          const math::FRectangle& uv,
                                          const math::FVector4& tint)
{
    append([bounds, textureHandle, uv, tint](IRenderBackend& backend) {
        (void)backend.addTexturedQuad(bounds, textureHandle, uv, tint);
    });
    return _target.addTexturedQuad(bounds, textureHandle, uv, tint);
}
void DisplayListRecorder::flushBatches() { AYUI_RECORD_AND_FORWARD(flushBatches()); }
void DisplayListRecorder::flush() { AYUI_RECORD_AND_FORWARD(flush()); }
int DisplayListRecorder::getDrawCallCount() const { return _target.getDrawCallCount(); }
int DisplayListRecorder::getTriangleCount() const { return _target.getTriangleCount(); }
int DisplayListRecorder::getVertexCount() const { return _target.getVertexCount(); }
void DisplayListRecorder::pushPerformanceMarker(const char* name)
{
    const std::string copy = name != nullptr ? name : "";
    append([copy](IRenderBackend& backend) { backend.pushPerformanceMarker(copy.c_str()); });
    _target.pushPerformanceMarker(name);
}
void DisplayListRecorder::popPerformanceMarker()
{
    AYUI_RECORD_AND_FORWARD(popPerformanceMarker());
}

ayt::font::FontHandle DisplayListRecorder::loadFont(const wchar_t* path, int baseSize)
{
    disableCaching();
    return _target.loadFont(path, baseSize);
}
void DisplayListRecorder::releaseFont(FontHandle font)
{
    AYUI_UNCACHEABLE_FORWARD(releaseFont(font));
}
ayt::font::FontHandle DisplayListRecorder::getFontHandle(const wchar_t* familyName,
                                                         int baseSize)
{
    return _target.getFontHandle(familyName, baseSize);
}
ayt::font::FontHandle DisplayListRecorder::registerFontFromMemory(
    const void* data, size_t dataSize, int baseSize)
{
    disableCaching();
    return _target.registerFontFromMemory(data, dataSize, baseSize);
}
IRenderBackend::TextMetrics DisplayListRecorder::measureText(
    const std::wstring& text, int fontSize, float maxWidth) const
{
    return _target.measureText(text, fontSize, maxWidth);
}
ayt::font::FontMetrics DisplayListRecorder::getFontMetrics(FontHandle font) const
{
    return _target.getFontMetrics(font);
}
size_t DisplayListRecorder::getAvailableVideoMemory() const
{
    return _target.getAvailableVideoMemory();
}
std::string DisplayListRecorder::getDriverVersion() const
{
    return _target.getDriverVersion();
}
std::string DisplayListRecorder::getBackendName() const
{
    return _target.getBackendName();
}

#undef AYUI_UNCACHEABLE_FORWARD
#undef AYUI_RECORD_AND_FORWARD

} // namespace ayt::ui
