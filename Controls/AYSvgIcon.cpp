#include "AYUI/SvgIcon.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ayt::ui {

namespace detail {

enum class SvgPaintKind { None, CurrentColor, FixedColor };

struct SvgPaint {
    SvgPaintKind kind = SvgPaintKind::None;
    math::FVector4 color{0.0f, 0.0f, 0.0f, 1.0f};
};

enum class SvgCommandType { Move, Line, Cubic, Quadratic, Arc, Close };

struct SvgCommand {
    SvgCommandType type = SvgCommandType::Move;
    math::FVector2 p1{0.0f, 0.0f};
    math::FVector2 p2{0.0f, 0.0f};
    math::FVector2 p3{0.0f, 0.0f};
    float radiusX = 0.0f;
    float radiusY = 0.0f;
    float rotationDegrees = 0.0f;
    bool largeArc = false;
    bool sweep = false;
};

struct SvgPathStyle {
    SvgPaint fill{SvgPaintKind::FixedColor, math::FVector4(0, 0, 0, 1)};
    SvgPaint stroke{};
    float strokeWidth = 1.0f;
    PathStrokeCap strokeCap = PathStrokeCap::Butt;
    PathStrokeJoin strokeJoin = PathStrokeJoin::Miter;
    float miterLimit = 4.0f;
    float opacity = 1.0f;
    float fillOpacity = 1.0f;
    float strokeOpacity = 1.0f;
};

struct SvgPathData {
    std::vector<SvgCommand> commands;
    SvgPathStyle style;
};

struct SvgDocumentData {
    math::FRectangle viewBox;
    std::vector<SvgPathData> paths;
};

} // namespace detail

namespace {

using Attributes = std::unordered_map<std::string, std::string>;
using detail::SvgCommand;
using detail::SvgCommandType;
using detail::SvgDocumentData;
using detail::SvgPaint;
using detail::SvgPaintKind;
using detail::SvgPathData;
using detail::SvgPathStyle;

constexpr float kPi = 3.14159265358979323846f;
constexpr size_t kMaxSvgBytes = 4u * 1024u * 1024u;
constexpr size_t kMaxPathCount = 4096u;
constexpr size_t kMaxCommandCount = 100000u;

void setError(std::string* error, const std::string& message)
{
    if (error != nullptr) *error = message;
}

std::string_view trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

bool isNameChar(char c)
{
    const unsigned char value = static_cast<unsigned char>(c);
    return std::isalnum(value) != 0 || c == '-' || c == '_' || c == ':';
}

size_t findTagEnd(std::string_view source, size_t start)
{
    char quote = 0;
    for (size_t i = start; i < source.size(); ++i) {
        const char c = source[i];
        if (quote != 0) {
            if (c == quote) quote = 0;
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '>') {
            return i;
        }
    }
    return std::string_view::npos;
}

size_t findElement(std::string_view source, std::string_view name, size_t from = 0)
{
    const std::string needle = "<" + std::string(name);
    size_t position = from;
    while ((position = source.find(needle, position)) != std::string_view::npos) {
        const size_t boundary = position + needle.size();
        if (boundary >= source.size()
            || std::isspace(static_cast<unsigned char>(source[boundary]))
            || source[boundary] == '>' || source[boundary] == '/') {
            return position;
        }
        position = boundary;
    }
    return std::string_view::npos;
}

bool parseAttributes(std::string_view tag, Attributes& attributes,
                     std::string* error)
{
    attributes.clear();
    size_t cursor = 1u;
    while (cursor < tag.size() && !std::isspace(static_cast<unsigned char>(tag[cursor]))
           && tag[cursor] != '>' && tag[cursor] != '/') {
        ++cursor;
    }
    while (cursor < tag.size()) {
        while (cursor < tag.size()
               && (std::isspace(static_cast<unsigned char>(tag[cursor]))
                   || tag[cursor] == '/')) {
            ++cursor;
        }
        if (cursor >= tag.size() || tag[cursor] == '>') break;
        const size_t nameBegin = cursor;
        while (cursor < tag.size() && isNameChar(tag[cursor])) ++cursor;
        if (cursor == nameBegin) {
            setError(error, "invalid SVG attribute name");
            return false;
        }
        std::string name(tag.substr(nameBegin, cursor - nameBegin));
        while (cursor < tag.size()
               && std::isspace(static_cast<unsigned char>(tag[cursor]))) ++cursor;
        if (cursor >= tag.size() || tag[cursor] != '=') {
            setError(error, "SVG attribute '" + name + "' has no value");
            return false;
        }
        ++cursor;
        while (cursor < tag.size()
               && std::isspace(static_cast<unsigned char>(tag[cursor]))) ++cursor;
        if (cursor >= tag.size() || (tag[cursor] != '\'' && tag[cursor] != '"')) {
            setError(error, "SVG attribute '" + name + "' is not quoted");
            return false;
        }
        const char quote = tag[cursor++];
        const size_t valueBegin = cursor;
        while (cursor < tag.size() && tag[cursor] != quote) ++cursor;
        if (cursor >= tag.size()) {
            setError(error, "unterminated SVG attribute '" + name + "'");
            return false;
        }
        attributes[std::move(name)] = std::string(tag.substr(valueBegin, cursor - valueBegin));
        ++cursor;
    }
    return true;
}

bool parseFloatList(std::string_view source, std::vector<float>& values)
{
    values.clear();
    const std::string copy(source);
    const char* cursor = copy.c_str();
    const char* end = cursor + copy.size();
    while (cursor < end) {
        while (cursor < end
               && (std::isspace(static_cast<unsigned char>(*cursor)) || *cursor == ',')) {
            ++cursor;
        }
        if (cursor == end) break;
        char* parsedEnd = nullptr;
        const float value = std::strtof(cursor, &parsedEnd);
        if (parsedEnd == cursor || !std::isfinite(value)) return false;
        values.push_back(value);
        cursor = parsedEnd;
    }
    return !values.empty();
}

bool parseScalar(std::string_view source, float& value)
{
    source = trim(source);
    const std::string copy(source);
    char* end = nullptr;
    value = std::strtof(copy.c_str(), &end);
    if (end == copy.c_str() || !std::isfinite(value)) return false;
    while (*end != 0 && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (end[0] == 'p' && end[1] == 'x') end += 2;
    while (*end != 0 && std::isspace(static_cast<unsigned char>(*end))) ++end;
    return *end == 0;
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseHexByte(char high, char low, float& value)
{
    const int h = hexDigit(high);
    const int l = hexDigit(low);
    if (h < 0 || l < 0) return false;
    value = static_cast<float>(h * 16 + l) / 255.0f;
    return true;
}

bool parsePaint(std::string_view source, SvgPaint& paint)
{
    source = trim(source);
    if (source == "none") {
        paint.kind = SvgPaintKind::None;
        return true;
    }
    if (source == "currentColor") {
        paint.kind = SvgPaintKind::CurrentColor;
        return true;
    }
    if (source == "black") {
        paint = {SvgPaintKind::FixedColor, math::FVector4(0, 0, 0, 1)};
        return true;
    }
    if (source == "white") {
        paint = {SvgPaintKind::FixedColor, math::FVector4(1, 1, 1, 1)};
        return true;
    }
    if (source == "transparent") {
        paint = {SvgPaintKind::FixedColor, math::FVector4(0, 0, 0, 0)};
        return true;
    }
    if (source.empty() || source.front() != '#') return false;
    source.remove_prefix(1);
    math::FVector4 color(0, 0, 0, 1);
    if (source.size() == 3u || source.size() == 4u) {
        const int r = hexDigit(source[0]);
        const int g = hexDigit(source[1]);
        const int b = hexDigit(source[2]);
        const int a = source.size() == 4u ? hexDigit(source[3]) : 15;
        if (r < 0 || g < 0 || b < 0 || a < 0) return false;
        color = math::FVector4(r / 15.0f, g / 15.0f, b / 15.0f, a / 15.0f);
    } else if (source.size() == 6u || source.size() == 8u) {
        if (!parseHexByte(source[0], source[1], color.x)
            || !parseHexByte(source[2], source[3], color.y)
            || !parseHexByte(source[4], source[5], color.z)) return false;
        if (source.size() == 8u && !parseHexByte(source[6], source[7], color.w)) return false;
    } else {
        return false;
    }
    paint = {SvgPaintKind::FixedColor, color};
    return true;
}

bool applyStyleAttributes(const Attributes& attributes, SvgPathStyle& style,
                          std::string* error)
{
    const auto applyPaint = [&](const char* name, SvgPaint& paint) {
        const auto it = attributes.find(name);
        if (it == attributes.end()) return true;
        if (parsePaint(it->second, paint)) return true;
        setError(error, std::string("unsupported SVG paint in '") + name + "': " + it->second);
        return false;
    };
    if (!applyPaint("fill", style.fill) || !applyPaint("stroke", style.stroke)) return false;

    const auto applyUnitFloat = [&](const char* name, float& target, bool nonNegative) {
        const auto it = attributes.find(name);
        if (it == attributes.end()) return true;
        float parsed = 0.0f;
        if (!parseScalar(it->second, parsed) || (nonNegative && parsed < 0.0f)) {
            setError(error, std::string("invalid SVG '") + name + "' value");
            return false;
        }
        target = parsed;
        return true;
    };
    if (!applyUnitFloat("stroke-width", style.strokeWidth, true)
        || !applyUnitFloat("stroke-miterlimit", style.miterLimit, true)) return false;

    const auto applyOpacity = [&](const char* name, float& target) {
        const auto it = attributes.find(name);
        if (it == attributes.end()) return true;
        float parsed = 0.0f;
        if (!parseScalar(it->second, parsed)) {
            setError(error, std::string("invalid SVG '") + name + "' value");
            return false;
        }
        target = std::clamp(parsed, 0.0f, 1.0f);
        return true;
    };
    if (!applyOpacity("opacity", style.opacity)
        || !applyOpacity("fill-opacity", style.fillOpacity)
        || !applyOpacity("stroke-opacity", style.strokeOpacity)) return false;

    if (const auto it = attributes.find("stroke-linecap"); it != attributes.end()) {
        if (it->second == "butt") style.strokeCap = PathStrokeCap::Butt;
        else if (it->second == "round") style.strokeCap = PathStrokeCap::Round;
        else if (it->second == "square") style.strokeCap = PathStrokeCap::Square;
        else {
            setError(error, "unsupported SVG stroke-linecap: " + it->second);
            return false;
        }
    }
    if (const auto it = attributes.find("stroke-linejoin"); it != attributes.end()) {
        if (it->second == "miter") style.strokeJoin = PathStrokeJoin::Miter;
        else if (it->second == "round") style.strokeJoin = PathStrokeJoin::Round;
        else if (it->second == "bevel") style.strokeJoin = PathStrokeJoin::Bevel;
        else {
            setError(error, "unsupported SVG stroke-linejoin: " + it->second);
            return false;
        }
    }
    style.miterLimit = std::max(1.0f, style.miterLimit);
    return true;
}

class PathParser {
public:
    PathParser(std::string source, std::string* error)
        : _source(std::move(source)), _cursor(_source.c_str()),
          _end(_cursor + _source.size()), _error(error) {}

    bool parse(std::vector<SvgCommand>& output)
    {
        output.clear();
        char command = 0;
        while (true) {
            skipSeparators();
            if (_cursor == _end) break;
            if (std::isalpha(static_cast<unsigned char>(*_cursor))) {
                command = *_cursor++;
            } else if (command == 0) {
                return fail("SVG path data expected a command");
            }
            if (!execute(command, output)) return false;
            if (output.size() > kMaxCommandCount) return fail("SVG path has too many commands");
            if (command == 'Z' || command == 'z') command = 0;
        }
        if (output.empty()) return fail("SVG path data is empty");
        return true;
    }

private:
    void skipSeparators()
    {
        while (_cursor < _end
               && (std::isspace(static_cast<unsigned char>(*_cursor)) || *_cursor == ',')) {
            ++_cursor;
        }
    }

    bool hasNumber()
    {
        skipSeparators();
        if (_cursor == _end) return false;
        return *_cursor == '+' || *_cursor == '-' || *_cursor == '.'
            || std::isdigit(static_cast<unsigned char>(*_cursor));
    }

    bool readNumber(float& value)
    {
        skipSeparators();
        if (_cursor == _end) return fail("SVG path ended while reading a number");
        char* parsedEnd = nullptr;
        value = std::strtof(_cursor, &parsedEnd);
        if (parsedEnd == _cursor || !std::isfinite(value)) {
            return fail("SVG path contains an invalid number");
        }
        _cursor = parsedEnd;
        return true;
    }

    bool readFlag(bool& value)
    {
        float number = 0.0f;
        if (!readNumber(number)) return false;
        if (number != 0.0f && number != 1.0f) return fail("SVG arc flag must be 0 or 1");
        value = number == 1.0f;
        return true;
    }

    bool readPoint(bool relative, math::FVector2& point)
    {
        if (!readNumber(point.x) || !readNumber(point.y)) return false;
        if (relative) {
            point.x += _current.x;
            point.y += _current.y;
        }
        return true;
    }

    void resetControls()
    {
        _hasCubicControl = false;
        _hasQuadraticControl = false;
    }

    void pushMove(std::vector<SvgCommand>& output, const math::FVector2& point)
    {
        SvgCommand cmd;
        cmd.type = SvgCommandType::Move;
        cmd.p1 = point;
        output.push_back(cmd);
        _current = point;
        _subpathStart = point;
        resetControls();
    }

    void pushLine(std::vector<SvgCommand>& output, const math::FVector2& point)
    {
        SvgCommand cmd;
        cmd.type = SvgCommandType::Line;
        cmd.p1 = point;
        output.push_back(cmd);
        _current = point;
        resetControls();
    }

    bool execute(char command, std::vector<SvgCommand>& output)
    {
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        if (upper == 'Z') {
            SvgCommand close;
            close.type = SvgCommandType::Close;
            output.push_back(close);
            _current = _subpathStart;
            resetControls();
            return true;
        }

        bool consumed = false;
        do {
            if (!hasNumber()) {
                if (!consumed) return fail(std::string("SVG path command '") + command + "' has no parameters");
                break;
            }
            consumed = true;
            if (upper == 'M' || upper == 'L') {
                math::FVector2 point;
                if (!readPoint(relative, point)) return false;
                if (upper == 'M' && output.empty()) pushMove(output, point);
                else if (upper == 'M' && !consumedMove) pushMove(output, point);
                else pushLine(output, point);
                consumedMove = upper == 'M';
                if (upper == 'M') command = relative ? 'l' : 'L';
            } else if (upper == 'H') {
                float x = 0.0f;
                if (!readNumber(x)) return false;
                pushLine(output, {relative ? _current.x + x : x, _current.y});
            } else if (upper == 'V') {
                float y = 0.0f;
                if (!readNumber(y)) return false;
                pushLine(output, {_current.x, relative ? _current.y + y : y});
            } else if (upper == 'C') {
                math::FVector2 c1, c2, end;
                if (!readPoint(relative, c1) || !readPoint(relative, c2)
                    || !readPoint(relative, end)) return false;
                SvgCommand cmd;
                cmd.type = SvgCommandType::Cubic;
                cmd.p1 = c1; cmd.p2 = c2; cmd.p3 = end;
                output.push_back(cmd);
                _current = end;
                _lastCubicControl = c2;
                _hasCubicControl = true;
                _hasQuadraticControl = false;
            } else if (upper == 'S') {
                math::FVector2 c2, end;
                if (!readPoint(relative, c2) || !readPoint(relative, end)) return false;
                const math::FVector2 c1 = _hasCubicControl
                    ? math::FVector2(2.0f * _current.x - _lastCubicControl.x,
                                     2.0f * _current.y - _lastCubicControl.y)
                    : _current;
                SvgCommand cmd;
                cmd.type = SvgCommandType::Cubic;
                cmd.p1 = c1; cmd.p2 = c2; cmd.p3 = end;
                output.push_back(cmd);
                _current = end;
                _lastCubicControl = c2;
                _hasCubicControl = true;
                _hasQuadraticControl = false;
            } else if (upper == 'Q') {
                math::FVector2 control, end;
                if (!readPoint(relative, control) || !readPoint(relative, end)) return false;
                SvgCommand cmd;
                cmd.type = SvgCommandType::Quadratic;
                cmd.p1 = control; cmd.p2 = end;
                output.push_back(cmd);
                _current = end;
                _lastQuadraticControl = control;
                _hasQuadraticControl = true;
                _hasCubicControl = false;
            } else if (upper == 'T') {
                math::FVector2 end;
                if (!readPoint(relative, end)) return false;
                const math::FVector2 control = _hasQuadraticControl
                    ? math::FVector2(2.0f * _current.x - _lastQuadraticControl.x,
                                     2.0f * _current.y - _lastQuadraticControl.y)
                    : _current;
                SvgCommand cmd;
                cmd.type = SvgCommandType::Quadratic;
                cmd.p1 = control; cmd.p2 = end;
                output.push_back(cmd);
                _current = end;
                _lastQuadraticControl = control;
                _hasQuadraticControl = true;
                _hasCubicControl = false;
            } else if (upper == 'A') {
                float rx = 0.0f, ry = 0.0f, rotation = 0.0f;
                bool largeArc = false, sweep = false;
                math::FVector2 end;
                if (!readNumber(rx) || !readNumber(ry) || !readNumber(rotation)
                    || !readFlag(largeArc) || !readFlag(sweep)
                    || !readPoint(relative, end)) return false;
                SvgCommand cmd;
                cmd.type = SvgCommandType::Arc;
                cmd.p1 = end;
                cmd.radiusX = std::fabs(rx);
                cmd.radiusY = std::fabs(ry);
                cmd.rotationDegrees = rotation;
                cmd.largeArc = largeArc;
                cmd.sweep = sweep;
                output.push_back(cmd);
                _current = end;
                resetControls();
            } else {
                return fail(std::string("unsupported SVG path command '") + command + "'");
            }
        } while (hasNumber());
        consumedMove = false;
        return true;
    }

    bool fail(const std::string& message)
    {
        setError(_error, message + " near byte "
            + std::to_string(static_cast<size_t>(_cursor - _source.c_str())));
        return false;
    }

    std::string _source;
    const char* _cursor = nullptr;
    const char* _end = nullptr;
    std::string* _error = nullptr;
    math::FVector2 _current{0.0f, 0.0f};
    math::FVector2 _subpathStart{0.0f, 0.0f};
    math::FVector2 _lastCubicControl{0.0f, 0.0f};
    math::FVector2 _lastQuadraticControl{0.0f, 0.0f};
    bool _hasCubicControl = false;
    bool _hasQuadraticControl = false;
    bool consumedMove = false;
};

float pointLineDistance(const math::FVector2& point,
                        const math::FVector2& a,
                        const math::FVector2& b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1.0e-6f) {
        const float px = point.x - a.x;
        const float py = point.y - a.y;
        return std::sqrt(px * px + py * py);
    }
    return std::fabs(dx * (a.y - point.y) - (a.x - point.x) * dy) / length;
}

void flattenCubic(const math::FVector2& p0, const math::FVector2& p1,
                  const math::FVector2& p2, const math::FVector2& p3,
                  float tolerance, int depth,
                  std::vector<math::FVector2>& output)
{
    if (depth >= 12
        || std::max(pointLineDistance(p1, p0, p3),
                    pointLineDistance(p2, p0, p3)) <= tolerance) {
        output.push_back(p3);
        return;
    }
    const math::FVector2 p01((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    const math::FVector2 p12((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
    const math::FVector2 p23((p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f);
    const math::FVector2 p012((p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f);
    const math::FVector2 p123((p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f);
    const math::FVector2 mid((p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f);
    flattenCubic(p0, p01, p012, mid, tolerance, depth + 1, output);
    flattenCubic(mid, p123, p23, p3, tolerance, depth + 1, output);
}

void flattenQuadratic(const math::FVector2& p0, const math::FVector2& p1,
                      const math::FVector2& p2, float tolerance, int depth,
                      std::vector<math::FVector2>& output)
{
    if (depth >= 12 || pointLineDistance(p1, p0, p2) <= tolerance) {
        output.push_back(p2);
        return;
    }
    const math::FVector2 p01((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    const math::FVector2 p12((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
    const math::FVector2 mid((p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f);
    flattenQuadratic(p0, p01, mid, tolerance, depth + 1, output);
    flattenQuadratic(mid, p12, p2, tolerance, depth + 1, output);
}

float signedVectorAngle(float ux, float uy, float vx, float vy)
{
    return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
}

void flattenArc(const math::FVector2& start, const SvgCommand& command,
                float destinationScale, float dpiScale,
                std::vector<math::FVector2>& output)
{
    const math::FVector2 end = command.p1;
    float rx = command.radiusX;
    float ry = command.radiusY;
    if (rx <= 1.0e-6f || ry <= 1.0e-6f
        || (std::fabs(start.x - end.x) <= 1.0e-6f
            && std::fabs(start.y - end.y) <= 1.0e-6f)) {
        if (std::fabs(start.x - end.x) > 1.0e-6f
            || std::fabs(start.y - end.y) > 1.0e-6f) output.push_back(end);
        return;
    }

    const float phi = std::fmod(command.rotationDegrees, 360.0f) * kPi / 180.0f;
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    const float dx = (start.x - end.x) * 0.5f;
    const float dy = (start.y - end.y) * 0.5f;
    const float x1p = cosPhi * dx + sinPhi * dy;
    const float y1p = -sinPhi * dx + cosPhi * dy;

    float rx2 = rx * rx;
    float ry2 = ry * ry;
    const float lambda = x1p * x1p / rx2 + y1p * y1p / ry2;
    if (lambda > 1.0f) {
        const float scale = std::sqrt(lambda);
        rx *= scale;
        ry *= scale;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    const float numerator = std::max(0.0f,
        rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p);
    const float denominator = std::max(1.0e-12f,
        rx2 * y1p * y1p + ry2 * x1p * x1p);
    const float sign = command.largeArc == command.sweep ? -1.0f : 1.0f;
    const float coefficient = sign * std::sqrt(numerator / denominator);
    const float cxp = coefficient * (rx * y1p / ry);
    const float cyp = coefficient * (-ry * x1p / rx);
    const float centerX = cosPhi * cxp - sinPhi * cyp + (start.x + end.x) * 0.5f;
    const float centerY = sinPhi * cxp + cosPhi * cyp + (start.y + end.y) * 0.5f;

    const float ux = (x1p - cxp) / rx;
    const float uy = (y1p - cyp) / ry;
    const float vx = (-x1p - cxp) / rx;
    const float vy = (-y1p - cyp) / ry;
    float startAngle = std::atan2(uy, ux);
    float sweep = signedVectorAngle(ux, uy, vx, vy);
    if (!command.sweep && sweep > 0.0f) sweep -= kPi * 2.0f;
    if (command.sweep && sweep < 0.0f) sweep += kPi * 2.0f;

    const float radiusPixels = std::max(rx, ry)
        * std::max(destinationScale, 1.0e-4f) * std::max(dpiScale, 1.0f);
    const float ratio = std::clamp(1.0f - 0.15f / std::max(radiusPixels, 0.15f),
                                   -1.0f, 1.0f);
    const float step = std::max(2.0f * std::acos(ratio), kPi / 128.0f);
    const int segments = std::clamp(
        static_cast<int>(std::ceil(std::fabs(sweep) / step)), 1, 512);
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = startAngle + sweep * t;
        const float x = rx * std::cos(angle);
        const float y = ry * std::sin(angle);
        output.emplace_back(centerX + cosPhi * x - sinPhi * y,
                            centerY + sinPhi * x + cosPhi * y);
    }
}

struct FlattenedContour {
    std::vector<math::FVector2> points;
    bool closed = false;
    float signedArea = 0.0f;
};

float contourArea(const std::vector<math::FVector2>& points)
{
    if (points.size() < 3u) return 0.0f;
    float twiceArea = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1u) % points.size()];
        twiceArea += a.x * b.y - b.x * a.y;
    }
    return twiceArea * 0.5f;
}

std::vector<FlattenedContour> flattenPath(const SvgPathData& path,
                                          float destinationScale,
                                          float dpiScale)
{
    std::vector<FlattenedContour> result;
    FlattenedContour active;
    math::FVector2 current(0.0f, 0.0f);
    math::FVector2 start(0.0f, 0.0f);
    bool hasActive = false;
    const float tolerance = std::clamp(
        0.15f / std::max(destinationScale * std::max(dpiScale, 1.0f), 1.0e-4f),
        0.002f, 0.5f);

    auto finish = [&]() {
        if (hasActive && active.points.size() >= 2u) {
            active.signedArea = active.closed ? contourArea(active.points) : 0.0f;
            result.push_back(std::move(active));
        }
        active = {};
        hasActive = false;
    };

    for (const SvgCommand& command : path.commands) {
        switch (command.type) {
        case SvgCommandType::Move:
            finish();
            current = command.p1;
            start = current;
            active.points.push_back(current);
            hasActive = true;
            break;
        case SvgCommandType::Line:
            if (!hasActive) {
                active.points.push_back(current);
                start = current;
                hasActive = true;
            }
            current = command.p1;
            active.points.push_back(current);
            break;
        case SvgCommandType::Cubic:
            if (!hasActive) {
                active.points.push_back(current);
                start = current;
                hasActive = true;
            }
            flattenCubic(current, command.p1, command.p2, command.p3,
                         tolerance, 0, active.points);
            current = command.p3;
            break;
        case SvgCommandType::Quadratic:
            if (!hasActive) {
                active.points.push_back(current);
                start = current;
                hasActive = true;
            }
            flattenQuadratic(current, command.p1, command.p2,
                             tolerance, 0, active.points);
            current = command.p2;
            break;
        case SvgCommandType::Arc:
            if (!hasActive) {
                active.points.push_back(current);
                start = current;
                hasActive = true;
            }
            flattenArc(current, command, destinationScale, dpiScale, active.points);
            current = command.p1;
            break;
        case SvgCommandType::Close:
            if (hasActive) {
                active.closed = true;
                current = start;
                finish();
            }
            break;
        }
    }
    finish();
    return result;
}

math::FVector4 resolvePaint(const SvgPaint& paint,
                            const math::FVector4& currentColor,
                            float opacity)
{
    math::FVector4 color = paint.kind == SvgPaintKind::CurrentColor
        ? currentColor : paint.color;
    color.w *= opacity;
    return color;
}

bool rejectsUnsupportedFeatures(std::string_view source, std::string* error)
{
    const char* attributes[] = {
        "transform=", "style=", "fill-rule=", "clip-path=", "mask=", "filter="
    };
    for (const char* attribute : attributes) {
        if (source.find(attribute) != std::string_view::npos) {
            setError(error, std::string("unsupported SVG attribute: ") + attribute);
            return true;
        }
    }
    const char* elements[] = {
        "g", "use", "image", "text", "rect", "circle", "ellipse", "line",
        "polyline", "polygon", "defs", "style", "clipPath", "mask",
        "linearGradient", "radialGradient", "filter", "script"
    };
    for (const char* element : elements) {
        if (findElement(source, element) != std::string_view::npos) {
            setError(error, std::string("unsupported SVG element: <") + element + ">");
            return true;
        }
    }
    return false;
}

} // namespace

SvgDocument::SvgDocument(std::shared_ptr<const detail::SvgDocumentData> data)
    : _data(std::move(data)) {}

SvgDocument::Ptr SvgDocument::parse(std::string_view source, std::string* error)
{
    if (error != nullptr) error->clear();
    if (source.empty()) {
        setError(error, "SVG source is empty");
        return nullptr;
    }
    if (source.size() > kMaxSvgBytes) {
        setError(error, "SVG source exceeds the 4 MiB icon limit");
        return nullptr;
    }
    if (rejectsUnsupportedFeatures(source, error)) return nullptr;

    const size_t svgStart = findElement(source, "svg");
    if (svgStart == std::string_view::npos) {
        setError(error, "SVG root element was not found");
        return nullptr;
    }
    const size_t svgEnd = findTagEnd(source, svgStart);
    if (svgEnd == std::string_view::npos) {
        setError(error, "SVG root tag is unterminated");
        return nullptr;
    }
    Attributes rootAttributes;
    if (!parseAttributes(source.substr(svgStart, svgEnd - svgStart + 1u),
                         rootAttributes, error)) return nullptr;

    auto data = std::make_shared<SvgDocumentData>();
    std::vector<float> values;
    if (const auto it = rootAttributes.find("viewBox"); it != rootAttributes.end()) {
        if (!parseFloatList(it->second, values) || values.size() != 4u
            || values[2] <= 0.0f || values[3] <= 0.0f) {
            setError(error, "SVG viewBox must contain four finite values with positive size");
            return nullptr;
        }
        data->viewBox = math::FRectangle(values[0], values[1],
                                         values[0] + values[2], values[1] + values[3]);
    } else {
        float width = 0.0f, height = 0.0f;
        const auto widthIt = rootAttributes.find("width");
        const auto heightIt = rootAttributes.find("height");
        if (widthIt == rootAttributes.end() || heightIt == rootAttributes.end()
            || !parseScalar(widthIt->second, width)
            || !parseScalar(heightIt->second, height)
            || width <= 0.0f || height <= 0.0f) {
            setError(error, "SVG requires a valid viewBox or numeric width/height");
            return nullptr;
        }
        data->viewBox = math::FRectangle(0.0f, 0.0f, width, height);
    }

    SvgPathStyle inheritedStyle;
    if (!applyStyleAttributes(rootAttributes, inheritedStyle, error)) return nullptr;

    size_t cursor = svgEnd + 1u;
    while (true) {
        const size_t pathStart = findElement(source, "path", cursor);
        if (pathStart == std::string_view::npos) break;
        const size_t pathEnd = findTagEnd(source, pathStart);
        if (pathEnd == std::string_view::npos) {
            setError(error, "SVG path tag is unterminated");
            return nullptr;
        }
        Attributes pathAttributes;
        if (!parseAttributes(source.substr(pathStart, pathEnd - pathStart + 1u),
                             pathAttributes, error)) return nullptr;
        const auto d = pathAttributes.find("d");
        if (d == pathAttributes.end() || trim(d->second).empty()) {
            setError(error, "SVG path has no d attribute");
            return nullptr;
        }
        SvgPathData path;
        path.style = inheritedStyle;
        if (!applyStyleAttributes(pathAttributes, path.style, error)) return nullptr;
        PathParser parser(d->second, error);
        if (!parser.parse(path.commands)) return nullptr;
        data->paths.push_back(std::move(path));
        if (data->paths.size() > kMaxPathCount) {
            setError(error, "SVG contains too many paths");
            return nullptr;
        }
        cursor = pathEnd + 1u;
    }
    if (data->paths.empty()) {
        setError(error, "SVG contains no path elements");
        return nullptr;
    }
    return std::shared_ptr<const SvgDocument>(new SvgDocument(std::move(data)));
}

SvgDocument::Ptr SvgDocument::loadFromFile(const std::filesystem::path& path,
                                           std::string* error)
{
    if (error != nullptr) error->clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        setError(error, "cannot open SVG file: " + path.string());
        return nullptr;
    }
    const std::streamoff length = input.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > kMaxSvgBytes) {
        setError(error, "SVG file is empty or exceeds the 4 MiB icon limit: " + path.string());
        return nullptr;
    }
    std::string source(static_cast<size_t>(length), '\0');
    input.seekg(0, std::ios::beg);
    if (!input.read(source.data(), length)) {
        setError(error, "cannot read SVG file: " + path.string());
        return nullptr;
    }
    return parse(source, error);
}

const math::FRectangle& SvgDocument::getViewBox() const
{
    return _data->viewBox;
}

size_t SvgDocument::getPathCount() const
{
    return _data->paths.size();
}

bool SvgDocument::empty() const
{
    return _data->paths.empty();
}

bool SvgDocument::draw(IRenderBackend& renderer, const math::FRectangle& bounds,
                       const math::FVector4& currentColor) const
{
    const float targetWidth = bounds.maxX - bounds.minX;
    const float targetHeight = bounds.maxY - bounds.minY;
    const float viewWidth = _data->viewBox.maxX - _data->viewBox.minX;
    const float viewHeight = _data->viewBox.maxY - _data->viewBox.minY;
    if (targetWidth <= 0.0f || targetHeight <= 0.0f
        || viewWidth <= 0.0f || viewHeight <= 0.0f) return false;

    const float scale = std::min(targetWidth / viewWidth, targetHeight / viewHeight);
    const float originX = bounds.minX + (targetWidth - viewWidth * scale) * 0.5f
                        - _data->viewBox.minX * scale;
    const float originY = bounds.minY + (targetHeight - viewHeight * scale) * 0.5f
                        - _data->viewBox.minY * scale;
    const auto transform = [&](const math::FVector2& point) {
        return math::FVector2(originX + point.x * scale,
                              originY + point.y * scale);
    };

    bool submitted = false;
    for (const SvgPathData& path : _data->paths) {
        std::vector<FlattenedContour> contours =
            flattenPath(path, scale, renderer.getUiScale());
        for (FlattenedContour& contour : contours) {
            for (math::FVector2& point : contour.points) point = transform(point);
            if (contour.closed) contour.signedArea = contourArea(contour.points);
        }

        float referenceArea = 0.0f;
        for (const FlattenedContour& contour : contours) {
            if (contour.closed && std::fabs(contour.signedArea) > std::fabs(referenceArea)) {
                referenceArea = contour.signedArea;
            }
        }
        const float referenceSign = referenceArea < 0.0f ? -1.0f : 1.0f;

        if (path.style.fill.kind != SvgPaintKind::None) {
            const auto handle = renderer.createPath();
            bool hasFill = false;
            for (const FlattenedContour& contour : contours) {
                if (contour.points.size() < 3u) continue;
                const float sign = contour.signedArea < 0.0f ? -1.0f : 1.0f;
                const PathWinding winding = sign == referenceSign
                    ? PathWinding::CounterClockwise : PathWinding::Clockwise;
                renderer.addPathContour(handle, contour.points.data(),
                    static_cast<int>(contour.points.size()), true, winding);
                hasFill = true;
            }
            if (hasFill) {
                renderer.setPathFillColor(handle,
                    resolvePaint(path.style.fill, currentColor,
                                 path.style.opacity * path.style.fillOpacity));
                renderer.drawPath(handle, PathFillMode::Fill);
                submitted = true;
            }
            renderer.releasePath(handle);
        }

        if (path.style.stroke.kind != SvgPaintKind::None
            && path.style.strokeWidth > 0.0f) {
            const auto handle = renderer.createPath();
            bool hasStroke = false;
            for (const FlattenedContour& contour : contours) {
                if (contour.points.size() < 2u) continue;
                renderer.addPathContour(handle, contour.points.data(),
                    static_cast<int>(contour.points.size()), contour.closed,
                    PathWinding::CounterClockwise);
                hasStroke = true;
            }
            if (hasStroke) {
                renderer.setPathStrokeColor(handle,
                    resolvePaint(path.style.stroke, currentColor,
                                 path.style.opacity * path.style.strokeOpacity));
                renderer.setPathStrokeWidth(handle, path.style.strokeWidth * scale);
                renderer.setPathStrokeStyle(handle, path.style.strokeCap,
                                            path.style.strokeJoin,
                                            path.style.miterLimit);
                renderer.drawPath(handle, PathFillMode::Stroke);
                submitted = true;
            }
            renderer.releasePath(handle);
        }
    }
    return submitted;
}

SvgIcon::SvgIcon()
{
    setSize(math::FVector2(24.0f, 24.0f));
}

void SvgIcon::setDocument(SvgDocument::Ptr document)
{
    if (_document == document) return;
    _document = std::move(document);
    markDirty();
}

bool SvgIcon::loadFromFile(const std::filesystem::path& path, std::string* error)
{
    SvgDocument::Ptr document = SvgDocument::loadFromFile(path, error);
    if (document == nullptr) return false;
    setDocument(std::move(document));
    return true;
}

void SvgIcon::setColor(const math::FVector4& color)
{
    if (_color == color) return;
    _color = color;
    markDirty();
}

void SvgIcon::setContentPadding(float padding)
{
    const float clamped = std::max(0.0f, padding);
    if (_contentPadding == clamped) return;
    _contentPadding = clamped;
    markDirty();
}

void SvgIcon::onRender(IRenderBackend& renderer)
{
    if (_document == nullptr) return;
    const math::FRectangle bounds = getWorldBounds();
    const math::FRectangle content(
        bounds.minX + _contentPadding, bounds.minY + _contentPadding,
        bounds.maxX - _contentPadding, bounds.maxY - _contentPadding);
    _document->draw(renderer, content, _color);
}

} // namespace ayt::ui
