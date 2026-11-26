#include "app/automation_mcp.hpp"
#include "stb/stb_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lps {

namespace {

// ============================================================================
// Self-contained C++20 JSON Value / Parser / Serializer
// ============================================================================
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool bVal = false;
    double nVal = 0.0;
    std::string sVal;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    const JsonValue* find(const char* key) const {
        if (type != Type::Object || !key) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    JsonValue* findMut(const char* key) {
        if (type != Type::Object || !key) return nullptr;
        for (auto& kv : obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    std::string getStr(const char* key, const std::string& def = {}) const {
        const JsonValue* v = find(key);
        return (v && v->isString()) ? v->sVal : def;
    }

    bool getBool(const char* key, bool def = false) const {
        const JsonValue* v = find(key);
        if (!v) return def;
        if (v->isBool()) return v->bVal;
        if (v->isNumber()) return v->nVal != 0.0;
        return def;
    }

    double getNum(const char* key, double def = 0.0) const {
        const JsonValue* v = find(key);
        return (v && v->isNumber()) ? v->nVal : def;
    }

    int getInt(const char* key, int def = 0) const {
        const JsonValue* v = find(key);
        return (v && v->isNumber()) ? static_cast<int>(std::llround(v->nVal)) : def;
    }

    static JsonValue makeNull() { return JsonValue{}; }
    static JsonValue makeBool(bool b) { JsonValue v; v.type = Type::Bool; v.bVal = b; return v; }
    static JsonValue makeNum(double n) { JsonValue v; v.type = Type::Number; v.nVal = n; return v; }
    static JsonValue makeStr(std::string s) { JsonValue v; v.type = Type::String; v.sVal = std::move(s); return v; }
    static JsonValue makeArray() { JsonValue v; v.type = Type::Array; return v; }
    static JsonValue makeObject() { JsonValue v; v.type = Type::Object; return v; }

    void set(std::string key, JsonValue val) {
        if (type != Type::Object) type = Type::Object;
        for (auto& kv : obj) {
            if (kv.first == key) {
                kv.second = std::move(val);
                return;
            }
        }
        obj.emplace_back(std::move(key), std::move(val));
    }

    void push(JsonValue val) {
        if (type != Type::Array) type = Type::Array;
        arr.push_back(std::move(val));
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : s_(src) {}

    bool parse(JsonValue& out, std::string& err) {
        skipWs();
        if (!parseValue(out, err)) return false;
        skipWs();
        if (pos_ < s_.size()) {
            err = "Unexpected trailing characters at byte " + std::to_string(pos_);
            return false;
        }
        return true;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    void skipWs() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool parseValue(JsonValue& out, std::string& err) {
        skipWs();
        if (pos_ >= s_.size()) {
            err = "Unexpected end of JSON";
            return false;
        }
        char c = s_[pos_];
        if (c == '{') return parseObject(out, err);
        if (c == '[') return parseArray(out, err);
        if (c == '"') {
            std::string str;
            if (!parseString(str, err)) return false;
            out = JsonValue::makeStr(std::move(str));
            return true;
        }
        if (c == 't' && s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out = JsonValue::makeBool(true);
            return true;
        }
        if (c == 'f' && s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out = JsonValue::makeBool(false);
            return true;
        }
        if (c == 'n' && s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out = JsonValue::makeNull();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parseNumber(out, err);
        }
        err = std::string("Invalid JSON character '") + c + "' at byte " + std::to_string(pos_);
        return false;
    }

    static void appendUtf8(std::string& dst, uint32_t cp) {
        if (cp <= 0x7Fu) {
            dst.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            dst.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            dst.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            dst.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            dst.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            dst.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            dst.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            dst.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            dst.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            dst.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }

    bool parseHex4(uint32_t& out) {
        if (pos_ + 4 > s_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(10 + c - 'a');
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(10 + c - 'A');
            else return false;
        }
        return true;
    }

    bool parseString(std::string& out, std::string& err) {
        if (s_[pos_] != '"') {
            err = "Expected string quote";
            return false;
        }
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) {
                    err = "Unterminated string escape";
                    return false;
                }
                char esc = s_[pos_++];
                switch (esc) {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!parseHex4(cp)) {
                        err = "Invalid \\uXXXX escape";
                        return false;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    err = "Invalid JSON escape";
                    return false;
                }
            } else {
                out.push_back(c);
            }
        }
        err = "Unterminated JSON string";
        return false;
    }

    bool parseNumber(JsonValue& out, std::string& err) {
        const char* start = s_.c_str() + pos_;
        char* endPtr = nullptr;
        double val = std::strtod(start, &endPtr);
        if (endPtr == start) {
            err = "Invalid JSON number at byte " + std::to_string(pos_);
            return false;
        }
        pos_ = static_cast<size_t>(endPtr - s_.c_str());
        out = JsonValue::makeNum(val);
        return true;
    }

    bool parseArray(JsonValue& out, std::string& err) {
        ++pos_; // skip '['
        out = JsonValue::makeArray();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (pos_ < s_.size()) {
            JsonValue elem;
            if (!parseValue(elem, err)) return false;
            out.push(std::move(elem));
            skipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            err = "Expected ',' or ']' in array at byte " + std::to_string(pos_);
            return false;
        }
        err = "Unterminated JSON array";
        return false;
    }

    bool parseObject(JsonValue& out, std::string& err) {
        ++pos_; // skip '{'
        out = JsonValue::makeObject();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (pos_ < s_.size()) {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') {
                err = "Expected object key string at byte " + std::to_string(pos_);
                return false;
            }
            std::string key;
            if (!parseString(key, err)) return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') {
                err = "Expected ':' after object key at byte " + std::to_string(pos_);
                return false;
            }
            ++pos_;
            JsonValue val;
            if (!parseValue(val, err)) return false;
            out.set(std::move(key), std::move(val));
            skipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            err = "Expected ',' or '}' in object at byte " + std::to_string(pos_);
            return false;
        }
        err = "Unterminated JSON object";
        return false;
    }
};

void escapeJsonString(const std::string& s, std::string& out) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

void dumpJson(const JsonValue& v, std::string& out) {
    switch (v.type) {
    case JsonValue::Type::Null:
        out += "null";
        break;
    case JsonValue::Type::Bool:
        out += (v.bVal ? "true" : "false");
        break;
    case JsonValue::Type::Number: {
        if (!std::isfinite(v.nVal)) {
            out += "0";
            break;
        }
        double rInt = std::round(v.nVal);
        if (std::fabs(v.nVal - rInt) < 1e-9 && std::fabs(rInt) < 9e15) {
            out += std::to_string(static_cast<long long>(rInt));
        } else {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.6g", v.nVal);
            out += buf;
        }
        break;
    }
    case JsonValue::Type::String:
        escapeJsonString(v.sVal, out);
        break;
    case JsonValue::Type::Array:
        out.push_back('[');
        for (size_t i = 0; i < v.arr.size(); ++i) {
            if (i > 0) out.push_back(',');
            dumpJson(v.arr[i], out);
        }
        out.push_back(']');
        break;
    case JsonValue::Type::Object:
        out.push_back('{');
        for (size_t i = 0; i < v.obj.size(); ++i) {
            if (i > 0) out.push_back(',');
            escapeJsonString(v.obj[i].first, out);
            out.push_back(':');
            dumpJson(v.obj[i].second, out);
        }
        out.push_back('}');
        break;
    }
}

std::string toLowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool parseVec2(const JsonValue* v, Vec2& out) {
    if (!v || !v->isArray() || v->arr.size() < 2) return false;
    out.x = static_cast<float>(v->arr[0].isNumber() ? v->arr[0].nVal : 0.0);
    out.y = static_cast<float>(v->arr[1].isNumber() ? v->arr[1].nVal : 0.0);
    return true;
}

bool parseVec3(const JsonValue* v, Vec3& out) {
    if (!v || !v->isArray() || v->arr.size() < 3) return false;
    out.x = static_cast<float>(v->arr[0].isNumber() ? v->arr[0].nVal : 0.0);
    out.y = static_cast<float>(v->arr[1].isNumber() ? v->arr[1].nVal : 0.0);
    out.z = static_cast<float>(v->arr[2].isNumber() ? v->arr[2].nVal : 0.0);
    return true;
}

bool parseColor4(const JsonValue* v, Color4u8& out) {
    if (!v) return false;
    auto clU8 = [](double x) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(static_cast<int>(std::llround(x)), 0, 255));
    };
    if (v->isArray() && v->arr.size() >= 3) {
        out.r = clU8(v->arr[0].nVal);
        out.g = clU8(v->arr[1].nVal);
        out.b = clU8(v->arr[2].nVal);
        out.a = (v->arr.size() >= 4) ? clU8(v->arr[3].nVal) : 255;
        return true;
    }
    if (v->isString() && !v->sVal.empty()) {
        std::string hx = v->sVal;
        if (hx[0] == '#') hx.erase(hx.begin());
        if (hx.size() == 6 || hx.size() == 8) {
            unsigned long val = std::strtoul(hx.c_str(), nullptr, 16);
            if (hx.size() == 6) {
                out.r = static_cast<uint8_t>((val >> 16) & 0xFF);
                out.g = static_cast<uint8_t>((val >> 8) & 0xFF);
                out.b = static_cast<uint8_t>(val & 0xFF);
                out.a = 255;
            } else {
                out.r = static_cast<uint8_t>((val >> 24) & 0xFF);
                out.g = static_cast<uint8_t>((val >> 16) & 0xFF);
                out.b = static_cast<uint8_t>((val >> 8) & 0xFF);
                out.a = static_cast<uint8_t>(val & 0xFF);
            }
            return true;
        }
    }
    return false;
}

BlendMode parseBlendMode(const std::string& s, BlendMode def = BlendMode::Normal) {
    std::string ls = toLowerAscii(s);
    if (ls == "multiply") return BlendMode::Multiply;
    if (ls == "screen")   return BlendMode::Screen;
    if (ls == "overlay")  return BlendMode::Overlay;
    if (ls == "normal")   return BlendMode::Normal;
    return def;
}

MirrorTextMode parseMirrorMode(const std::string& s, MirrorTextMode def = MirrorTextMode::ReadableBothSides) {
    std::string ls = toLowerAscii(s);
    if (ls == "none" || ls == "off") return MirrorTextMode::None;
    if (ls == "trueflip" || ls == "true_flip" || ls == "flip") return MirrorTextMode::TrueFlip;
    if (ls == "readablebothsides" || ls == "readable_both_sides" || ls == "readable") {
        return MirrorTextMode::ReadableBothSides;
    }
    return def;
}

CameraPreset parseCameraPreset(const std::string& s, CameraPreset def = CameraPreset::ThreeQuarterBeauty) {
    std::string ls = toLowerAscii(s);
    if (ls == "front") return CameraPreset::Front;
    if (ls == "rear" || ls == "back") return CameraPreset::Rear;
    if (ls == "side" || ls == "right" || ls == "left") return CameraPreset::Side;
    if (ls == "top") return CameraPreset::Top;
    if (ls == "threequarterbeauty" || ls == "beauty" || ls == "3/4" || ls == "front34") {
        return CameraPreset::ThreeQuarterBeauty;
    }
    return def;
}

ShadingMode parseShadingMode(const std::string& s, ShadingMode def = ShadingMode::Rendered) {
    std::string ls = toLowerAscii(s);
    if (ls == "wireframe") return ShadingMode::Wireframe;
    if (ls == "clay" || ls == "solid") return ShadingMode::Clay;
    if (ls == "material") return ShadingMode::Material;
    if (ls == "rendered" || ls == "showroom") return ShadingMode::Rendered;
    return def;
}

StudioTool parseStudioTool(const std::string& s, StudioTool def = StudioTool::Brush) {
    std::string ls = toLowerAscii(s);
    if (ls == "selectmove" || ls == "select") return StudioTool::SelectMove;
    if (ls == "nodeedit" || ls == "directselect") return StudioTool::NodeEdit;
    if (ls == "orbit") return StudioTool::Orbit;
    if (ls == "brush") return StudioTool::Brush;
    if (ls == "straightruler" || ls == "ruler") return StudioTool::StraightRuler;
    if (ls == "curveblade" || ls == "pen") return StudioTool::CurveBlade;
    if (ls == "rectbox" || ls == "rect") return StudioTool::RectBox;
    if (ls == "eraser") return StudioTool::Eraser;
    if (ls == "floodfill" || ls == "fill") return StudioTool::FloodFill;
    if (ls == "eyedropper") return StudioTool::Eyedropper;
    if (ls == "text" || ls == "type") return StudioTool::Text;
    if (ls == "sponsordecal" || ls == "decal") return StudioTool::SponsorDecal;
    if (ls == "vertexedit") return StudioTool::VertexEdit;
    if (ls == "facepaintmask" || ls == "facemask") return StudioTool::FacePaintMask;
    if (ls == "handpan" || ls == "pan") return StudioTool::HandPan;
    if (ls == "zoom") return StudioTool::Zoom;
    return def;
}

std::string resolveInputPath(const std::string& raw, const std::string& baseDir) {
    if (raw.empty()) return {};
    std::error_code ec;
    if (std::filesystem::exists(raw, ec)) return raw;
    if (!baseDir.empty()) {
        std::filesystem::path alt = std::filesystem::path(baseDir) / raw;
        if (std::filesystem::exists(alt, ec)) return alt.string();
    }
    return raw;
}

void ensureParentDir(const std::string& filePath) {
    auto parent = std::filesystem::path(filePath).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }
}

int resolveLayerIndex(const CanvasEngine& canvas, const JsonValue& cmd, bool fallbackToEditableRaster = false) {
    if (const JsonValue* idxV = cmd.find("layerIndex"); idxV && idxV->isNumber()) {
        int idx = static_cast<int>(std::llround(idxV->nVal));
        if (idx >= 0 && idx < static_cast<int>(canvas.layers.size())) return idx;
    }
    std::string byName = cmd.getStr("layer");
    if (byName.empty()) byName = cmd.getStr("nameLookup");
    if (!byName.empty()) {
        for (int i = static_cast<int>(canvas.layers.size()) - 1; i >= 0; --i) {
            if (canvas.layers[i].name == byName) return i;
        }
        std::string low = toLowerAscii(byName);
        for (int i = static_cast<int>(canvas.layers.size()) - 1; i >= 0; --i) {
            if (toLowerAscii(canvas.layers[i].name) == low) return i;
        }
    }
    int act = canvas.activeLayerIdx;
    if (act >= 0 && act < static_cast<int>(canvas.layers.size())) {
        if (!fallbackToEditableRaster ||
            (canvas.layers[act].kind == LayerKind::RasterPaint && !canvas.layers[act].locked &&
             canvas.layers[act].name != "AO / Panel Shading")) {
            return act;
        }
    }
    if (fallbackToEditableRaster) {
        for (int i = static_cast<int>(canvas.layers.size()) - 1; i >= 0; --i) {
            const Layer& l = canvas.layers[i];
            if (l.kind == LayerKind::RasterPaint && !l.locked && l.name != "AO / Panel Shading") {
                return i;
            }
        }
    }
    return act;
}

bool resolveItemTarget(const CanvasEngine& canvas, const JsonValue& cmd, int& outLayerIdx, int& outItemIdx) {
    outLayerIdx = -1;
    outItemIdx = -1;
    std::string itemName = cmd.getStr("item");
    if (itemName.empty()) itemName = cmd.getStr("layer");

    if (const JsonValue* itemIdxV = cmd.find("itemIndex"); itemIdxV && itemIdxV->isNumber()) {
        int li = resolveLayerIndex(canvas, cmd, false);
        int ii = static_cast<int>(std::llround(itemIdxV->nVal));
        if (li >= 0 && li < static_cast<int>(canvas.layers.size()) &&
            ii >= 0 && ii < static_cast<int>(canvas.layers[li].items.size())) {
            outLayerIdx = li;
            outItemIdx = ii;
            return true;
        }
    }

    if (!itemName.empty()) {
        std::string low = toLowerAscii(itemName);
        for (int li = static_cast<int>(canvas.layers.size()) - 1; li >= 0; --li) {
            const auto& items = canvas.layers[li].items;
            for (int ii = static_cast<int>(items.size()) - 1; ii >= 0; --ii) {
                if (items[ii].name == itemName) {
                    outLayerIdx = li;
                    outItemIdx = ii;
                    return true;
                }
            }
        }
        for (int li = static_cast<int>(canvas.layers.size()) - 1; li >= 0; --li) {
            const auto& items = canvas.layers[li].items;
            for (int ii = static_cast<int>(items.size()) - 1; ii >= 0; --ii) {
                if (toLowerAscii(items[ii].name) == low) {
                    outLayerIdx = li;
                    outItemIdx = ii;
                    return true;
                }
            }
        }
    }

    int li = canvas.activeLayerIdx;
    int ii = canvas.selectedItemIdx;
    if (li >= 0 && li < static_cast<int>(canvas.layers.size()) &&
        ii >= 0 && ii < static_cast<int>(canvas.layers[li].items.size())) {
        outLayerIdx = li;
        outItemIdx = ii;
        return true;
    }
    return false;
}

// Compute consistent readable right tangent across Porsche LH car panels
Vec3 computeWorldSurfaceRight3D(const Vec3& hitPos, const Vec3& hitNrm) {
    Vec3 N = normalized(hitNrm);
    if (std::fabs(N.y) < 0.72f) {
        // Side doors, fenders, nose, rear bumper, wing endplates: keep -B = +Y upright
        Vec3 t = cross(N, Vec3{0.0f, 1.0f, 0.0f});
        if (lengthSq(t) > 1e-6f) return normalized(t);
    }
    // Roof / Wing top (z <= 0.3): readable from Rear/Top (+Z up). Front hood (z > 0.3): readable from Front.
    return (hitPos.z > 0.30f) ? Vec3{-1.0f, 0.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
}

std::string findMatchingFontPath(const CanvasEngine& canvas, const std::string& query) {
    if (query.empty()) return {};
    std::error_code ec;
    if (std::filesystem::exists(query, ec)) return query;
    std::string qLow = toLowerAscii(query);
    for (const FontFaceInfo& f : canvas.fonts()) {
        std::string fn = toLowerAscii(std::filesystem::path(f.filePath).stem().string());
        std::string dn = toLowerAscii(f.displayName);
        if (fn == qLow || dn == qLow) return f.filePath;
    }
    for (const FontFaceInfo& f : canvas.fonts()) {
        std::string fn = toLowerAscii(std::filesystem::path(f.filePath).filename().string());
        std::string dn = toLowerAscii(f.displayName);
        if (fn.find(qLow) != std::string::npos || dn.find(qLow) != std::string::npos) {
            return f.filePath;
        }
    }
    return {};
}

void stampRect3DInScript(CanvasEngine& canvas, const ObjMesh& mesh, int layerIdx,
                         const Vec3& centerPos, const Vec3& nCenter, const Vec3& rightHint,
                         float worldW, float worldH, float rotDeg,
                         Color4u8 fillCol, Color4u8 strokeCol, float strokeWorldW,
                         float opacity, bool mirrorX) {
    if (layerIdx < 0 || layerIdx >= static_cast<int>(canvas.layers.size()) || !mesh.loaded) return;
    Layer& l = canvas.layers[layerIdx];
    if (l.kind != LayerKind::RasterPaint || l.locked) return;

    float halfW = std::max(0.004f, worldW * 0.5f);
    float halfH = std::max(0.004f, worldH * 0.5f);
    float halfDiag = std::sqrt(halfW * halfW + halfH * halfH) + 0.04f;
    float maxDepth = std::max(0.28f, halfDiag * 0.50f);

    const int W = canvas.width;
    const int H = canvas.height;
    std::vector<int32_t> candTris;

    auto projectPass = [&](Vec3 pCenter, Vec3 nUse, Vec3 rHint, float rDeg) {
        Vec3 T = rHint - nUse * dot(rHint, nUse);
        if (lengthSq(T) < 1e-6f) {
            Vec3 fb = (std::fabs(nUse.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            T = cross(fb, nUse);
        }
        T = normalized(T);
        Vec3 B = normalized(cross(nUse, T));
        float rad = rDeg * kDeg2Rad;
        float cs = std::cos(rad), sn = std::sin(rad);
        Vec3 Tr = T * cs + B * sn;
        Vec3 Br = -T * sn + B * cs;

        candTris.clear();
        mesh.collectTrianglesInAABB(pCenter - Vec3{halfDiag, halfDiag, halfDiag},
                                    pCenter + Vec3{halfDiag, halfDiag, halfDiag}, candTris);

        for (int32_t ti : candTris) {
            const Triangle& t = mesh.tris[ti];
            if (!mesh.groups[t.groupIdx].visible) continue;
            if (dot(t.faceNrm, nUse) < 0.25f) continue;

            const Vec3& a = mesh.uniquePositions[t.p0];
            const Vec3& b = mesh.uniquePositions[t.p1];
            const Vec3& c = mesh.uniquePositions[t.p2];

            Vec2 u0{t.uv0.x * W, t.uv0.y * H};
            Vec2 u1{t.uv1.x * W, t.uv1.y * H};
            Vec2 u2{t.uv2.x * W, t.uv2.y * H};
            float area = (u1.x - u0.x) * (u2.y - u0.y) - (u1.y - u0.y) * (u2.x - u0.x);
            if (std::fabs(area) < 1e-4f) continue;

            int ux0 = std::clamp(static_cast<int>(std::floor(std::min({u0.x, u1.x, u2.x}))), 0, W - 1);
            int ux1 = std::clamp(static_cast<int>(std::ceil(std::max({u0.x, u1.x, u2.x}))),  0, W - 1);
            int uy0 = std::clamp(static_cast<int>(std::floor(std::min({u0.y, u1.y, u2.y}))), 0, H - 1);
            int uy1 = std::clamp(static_cast<int>(std::ceil(std::max({u0.y, u1.y, u2.y}))),  0, H - 1);

            for (int py = uy0; py <= uy1; ++py) {
                for (int px = ux0; px <= ux1; ++px) {
                    Vec2 p{px + 0.5f, py + 0.5f};
                    float w0 = ((u1.x - p.x) * (u2.y - p.y) - (u1.y - p.y) * (u2.x - p.x)) / area;
                    float uu = ((u2.x - p.x) * (u0.y - p.y) - (u2.y - p.y) * (u0.x - p.x)) / area;
                    float vv = 1.0f - w0 - uu;
                    if (w0 < -0.02f || uu < -0.02f || vv < -0.02f) continue;

                    Vec3 p3 = a * std::clamp(w0, 0.0f, 1.0f) +
                              b * std::clamp(uu, 0.0f, 1.0f) +
                              c * std::clamp(vv, 0.0f, 1.0f);
                    Vec3 d = p3 - pCenter;
                    if (std::fabs(dot(d, nUse)) > maxDepth) continue;

                    float lx = std::fabs(dot(d, Tr));
                    float ly = std::fabs(dot(d, Br));
                    if (lx > halfW || ly > halfH) continue;

                    float edgeDist = std::min(halfW - lx, halfH - ly);
                    bool onEdge = (strokeWorldW > 0.0005f) && (edgeDist < strokeWorldW);
                    Color4u8 col = onEdge ? strokeCol : fillCol;

                    float alpha = std::clamp((col.a / 255.0f) * opacity, 0.0f, 1.0f);
                    if (alpha <= 1e-4f) continue;

                    size_t dstIdx = (static_cast<size_t>(py) * W + px) * 4;
                    float da = l.rgba[dstIdx + 3] / 255.0f;
                    float outA = alpha + da * (1.0f - alpha);
                    if (outA < 1e-5f) continue;
                    auto cl = [](float v) -> uint8_t {
                        return static_cast<uint8_t>(std::clamp(static_cast<int>(v + 0.5f), 0, 255));
                    };
                    l.rgba[dstIdx + 0] = cl((col.r * alpha + l.rgba[dstIdx + 0] * da * (1.0f - alpha)) / outA);
                    l.rgba[dstIdx + 1] = cl((col.g * alpha + l.rgba[dstIdx + 1] * da * (1.0f - alpha)) / outA);
                    l.rgba[dstIdx + 2] = cl((col.b * alpha + l.rgba[dstIdx + 2] * da * (1.0f - alpha)) / outA);
                    l.rgba[dstIdx + 3] = cl(outA * 255.0f);
                }
            }
        }
    };

    projectPass(centerPos, nCenter, rightHint, rotDeg);
    if (mirrorX) {
        Vec3 mPos{2.0f * mesh.centerSymmetryX - centerPos.x, centerPos.y, centerPos.z};
        Vec3 mNrm{-nCenter.x, nCenter.y, nCenter.z};
        Vec3 mRight{-rightHint.x, rightHint.y, rightHint.z};
        projectPass(mPos, mNrm, mRight, -rotDeg);
    }
    canvas.recomposite();
}

void stampRectUvInScript(CanvasEngine& canvas, const ObjMesh* mesh, int layerIdx,
                         Vec2 uvMin, Vec2 uvMax, Color4u8 fillCol,
                         Color4u8 strokeCol, float strokePx, float opacity, bool mirrorX) {
    if (layerIdx < 0 || layerIdx >= static_cast<int>(canvas.layers.size())) return;
    Layer& l = canvas.layers[layerIdx];
    if (l.kind != LayerKind::RasterPaint || l.locked) return;

    const int W = canvas.width;
    const int H = canvas.height;
    auto stampBox = [&](Vec2 a, Vec2 b) {
        int x0 = std::clamp(static_cast<int>(std::floor(std::min(a.x, b.x) * W)), 0, W - 1);
        int x1 = std::clamp(static_cast<int>(std::ceil(std::max(a.x, b.x) * W)),  0, W - 1);
        int y0 = std::clamp(static_cast<int>(std::floor(std::min(a.y, b.y) * H)), 0, H - 1);
        int y1 = std::clamp(static_cast<int>(std::ceil(std::max(a.y, b.y) * H)),  0, H - 1);
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                float edge = static_cast<float>(std::min({px - x0, x1 - px, py - y0, y1 - py}));
                bool isStroke = (strokePx > 0.25f) && (edge < strokePx);
                Color4u8 col = isStroke ? strokeCol : fillCol;
                float alpha = std::clamp((col.a / 255.0f) * opacity, 0.0f, 1.0f);
                if (alpha <= 1e-4f) continue;
                size_t o = (static_cast<size_t>(py) * W + px) * 4;
                float da = l.rgba[o + 3] / 255.0f;
                float outA = alpha + da * (1.0f - alpha);
                if (outA < 1e-5f) continue;
                auto cl = [](float v) -> uint8_t {
                    return static_cast<uint8_t>(std::clamp(static_cast<int>(v + 0.5f), 0, 255));
                };
                l.rgba[o + 0] = cl((col.r * alpha + l.rgba[o + 0] * da * (1.0f - alpha)) / outA);
                l.rgba[o + 1] = cl((col.g * alpha + l.rgba[o + 1] * da * (1.0f - alpha)) / outA);
                l.rgba[o + 2] = cl((col.b * alpha + l.rgba[o + 2] * da * (1.0f - alpha)) / outA);
                l.rgba[o + 3] = cl(outA * 255.0f);
            }
        }
    };

    stampBox(uvMin, uvMax);
    if (mirrorX && mesh && mesh->loaded) {
        Vec2 cUv{(uvMin.x + uvMax.x) * 0.5f, (uvMin.y + uvMax.y) * 0.5f};
        Vec3 p3, n3;
        int32_t tri = -1;
        if (mesh->queryUvAtPixel(cUv.x, cUv.y, p3, n3, tri)) {
            Vec3 mp{2.0f * mesh->centerSymmetryX - p3.x, p3.y, p3.z};
            Vec3 mn{-n3.x, n3.y, n3.z};
            RayHit mh = mesh->projectPointToSurface(mp, mn, 0.30f);
            if (mh.hit && mh.triIdx != tri) {
                Vec2 half{std::fabs(uvMax.x - uvMin.x) * 0.5f, std::fabs(uvMax.y - uvMin.y) * 0.5f};
                stampBox({mh.uv.x - half.x, mh.uv.y - half.y}, {mh.uv.x + half.x, mh.uv.y + half.y});
            }
        }
    }
    canvas.recomposite();
}

void applyCameraArgs(AutomationHost& host, const JsonValue& cmd) {
    if (const JsonValue* cp = cmd.find("cameraPreset"); cp && cp->isString()) {
        if (host.setCameraPreset) {
            host.setCameraPreset(parseCameraPreset(cp->sVal));
        }
    }
    if (host.camYaw && cmd.find("yawDeg")) {
        *host.camYaw = static_cast<float>(cmd.getNum("yawDeg", 0.0)) * kDeg2Rad;
    }
    if (host.camPitch && cmd.find("pitchDeg")) {
        *host.camPitch = std::clamp(static_cast<float>(cmd.getNum("pitchDeg", 14.0)) * kDeg2Rad, -1.50f, 1.50f);
    }
    if (host.camDist && cmd.find("dist")) {
        *host.camDist = std::clamp(static_cast<float>(cmd.getNum("dist", 5.2)), 0.7f, 32.0f);
    }
    Vec3 tgt;
    if (host.camTarget && parseVec3(cmd.find("target"), tgt)) {
        *host.camTarget = tgt;
    }
    if (host.uiState) {
        if (const JsonValue* sm = cmd.find("shadingMode"); sm && sm->isString()) {
            host.uiState->shadingMode = parseShadingMode(sm->sVal, host.uiState->shadingMode);
        }
        if (cmd.find("orthographic")) {
            host.uiState->isOrthoCamera = cmd.getBool("orthographic", host.uiState->isOrthoCamera);
        }
    }
}

JsonValue buildCanvasStats(const AutomationHost& host) {
    JsonValue out = JsonValue::makeObject();
    if (!host.canvas || !host.mesh) return out;
    out.set("width", JsonValue::makeNum(host.canvas->width));
    out.set("height", JsonValue::makeNum(host.canvas->height));
    out.set("layerCount", JsonValue::makeNum(static_cast<double>(host.canvas->layers.size())));
    out.set("activeLayerIdx", JsonValue::makeNum(host.canvas->activeLayerIdx));
    out.set("meshLoaded", JsonValue::makeBool(host.mesh->loaded));
    if (host.uiState) out.set("meshPath", JsonValue::makeStr(host.uiState->activeMeshPath));
    out.set("tris", JsonValue::makeNum(static_cast<double>(host.mesh->tris.size())));

    int lumMin = 255, lumMax = 0;
    size_t opaqueCnt = 0, totalSampled = 0;
    for (int y = 0; y < host.canvas->height; y += 16) {
        for (int x = 0; x < host.canvas->width; x += 16) {
            size_t o = (static_cast<size_t>(y) * host.canvas->width + x) * 4;
            int lum = (host.canvas->compositeRgba[o + 0] + host.canvas->compositeRgba[o + 1] +
                       host.canvas->compositeRgba[o + 2]) / 3;
            lumMin = std::min(lumMin, lum);
            lumMax = std::max(lumMax, lum);
            if (host.canvas->compositeRgba[o + 3] > 10) ++opaqueCnt;
            ++totalSampled;
        }
    }
    out.set("lumMin", JsonValue::makeNum(lumMin));
    out.set("lumMax", JsonValue::makeNum(lumMax));
    out.set("nonTransparentRatio", JsonValue::makeNum(totalSampled ? static_cast<double>(opaqueCnt) / totalSampled : 0.0));

    JsonValue lArr = JsonValue::makeArray();
    for (size_t i = 0; i < host.canvas->layers.size(); ++i) {
        const Layer& l = host.canvas->layers[i];
        JsonValue lj = JsonValue::makeObject();
        lj.set("index", JsonValue::makeNum(static_cast<double>(i)));
        lj.set("name", JsonValue::makeStr(l.name));
        const char* kName = "RasterPaint";
        if (l.kind == LayerKind::Text) kName = "Text";
        else if (l.kind == LayerKind::Decal) kName = "Decal";
        else if (l.kind == LayerKind::RulerStripe) kName = "Pen";
        lj.set("kind", JsonValue::makeStr(kName));
        lj.set("visible", JsonValue::makeBool(l.visible));
        lj.set("opacity", JsonValue::makeNum(l.opacity));
        if (l.kind == LayerKind::Text) {
            lj.set("text", JsonValue::makeStr(l.text.text));
            lj.set("anchoredIn3D", JsonValue::makeBool(l.text.anchoredIn3D));
            lj.set("worldHeight", JsonValue::makeNum(l.text.worldHeight));
            lj.set("fontSizePx", JsonValue::makeNum(l.text.fontSizePx));
        } else if (l.kind == LayerKind::Decal) {
            lj.set("anchoredIn3D", JsonValue::makeBool(l.decal.anchoredIn3D));
            lj.set("worldWidth", JsonValue::makeNum(l.decal.worldWidth));
            JsonValue uvs = JsonValue::makeArray();
            uvs.push(JsonValue::makeNum(l.decal.uvScale.x));
            uvs.push(JsonValue::makeNum(l.decal.uvScale.y));
            lj.set("uvScale", std::move(uvs));
        } else if (l.kind == LayerKind::RulerStripe) {
            lj.set("nodeCount", JsonValue::makeNum(static_cast<double>(l.ruler.nodes.size())));
            lj.set("closed", JsonValue::makeBool(l.ruler.closed));
            lj.set("filled", JsonValue::makeBool(l.ruler.filled));
        }
        lj.set("itemCount", JsonValue::makeNum(static_cast<double>(l.items.size())));
        if (!l.items.empty()) {
            JsonValue itArr = JsonValue::makeArray();
            for (size_t ii = 0; ii < l.items.size(); ++ii) {
                const VectorItem& it = l.items[ii];
                JsonValue ij = JsonValue::makeObject();
                ij.set("index", JsonValue::makeNum(static_cast<double>(ii)));
                ij.set("name", JsonValue::makeStr(it.name));
                ij.set("visible", JsonValue::makeBool(it.visible));
                const char* ik = "PenPath";
                if (it.kind == VectorItemKind::RectShape) ik = "RectShape";
                else if (it.kind == VectorItemKind::Text) ik = "Text";
                else if (it.kind == VectorItemKind::Image) ik = "Image";
                ij.set("kind", JsonValue::makeStr(ik));
                itArr.push(std::move(ij));
            }
            lj.set("items", std::move(itArr));
        }
        lArr.push(std::move(lj));
    }
    out.set("selectedItemIdx", JsonValue::makeNum(host.canvas->selectedItemIdx));
    out.set("layers", std::move(lArr));
    return out;
}

bool dispatchCommand(AutomationHost& host, const std::string& op, const JsonValue& cmd,
                     const std::string& baseDir, JsonValue& outResult, std::string& outErr) {
    if (!host.canvas || !host.mesh || !host.renderer || !host.uiState || !host.uvState || !host.sceneParams) {
        outErr = "AutomationHost not initialized";
        return false;
    }

    auto doSyncCanvas = [&]() { if (host.syncCanvasGpu) host.syncCanvasGpu(); };
    auto doSyncMesh = [&]() { if (host.syncMeshGpu) host.syncMeshGpu(); };
    auto doSyncScene = [&]() { if (host.syncSceneParams) host.syncSceneParams(); };

    outResult = JsonValue::makeObject();
    outResult.set("ok", JsonValue::makeBool(true));
    outResult.set("op", JsonValue::makeStr(op));

    if (op == "new_project") {
        int size = std::clamp(cmd.getInt("size", 4096), 256, 4096);
        Color4u8 baseCol{238, 240, 244, 255};
        parseColor4(cmd.find("baseColor"), baseCol);
        parseColor4(cmd.find("color"), baseCol);
        host.canvas->init(size, baseCol);
        host.uiState->canvasResolution = size;
        host.uiState->activeProjectPath.clear();
        host.uiState->textEditSyncedLayerId = 0;
        host.uvState->needsInitialFit = true;
        doSyncCanvas();
        return true;
    }

    if (op == "load_obj" || op == "load_mesh") {
        std::string p = resolveInputPath(cmd.getStr("path"), baseDir);
        bool preserveCanvas = cmd.getBool("preserveCanvas", false);
        if (!host.loadObj || !host.loadObj(p, preserveCanvas)) {
            outErr = op + " failed: " + p;
            return false;
        }
        outResult.set("tris", JsonValue::makeNum(static_cast<double>(host.mesh->tris.size())));
        return true;
    }

    if (op == "load_ao") {
        std::string p = resolveInputPath(cmd.getStr("path"), baseDir);
        bool vis = cmd.getBool("visible", true);
        if (!host.canvas->loadAoOverlay(p, vis)) {
            outErr = "load_ao failed: " + p;
            return false;
        }
        doSyncCanvas();
        return true;
    }

    if (op == "bake_ao") {
        if (!host.mesh->loaded) {
            outErr = "bake_ao requires a loaded OBJ mesh";
            return false;
        }
        int res = std::clamp(cmd.getInt("res", 1024), 256, 2048);
        bool vis = cmd.getBool("visible", true);
        if (!host.canvas->bakeMeshAoOverlay(*host.mesh, res, vis)) {
            outErr = "bake_ao failed";
            return false;
        }
        doSyncCanvas();
        return true;
    }

    if (op == "set_base_color") {
        Color4u8 col{238, 240, 244, 255};
        if (!parseColor4(cmd.find("color"), col) && !parseColor4(cmd.find("baseColor"), col)) {
            outErr = "set_base_color missing 'color' array or hex string";
            return false;
        }
        int idx = 0;
        if (cmd.find("layer") || cmd.find("layerIndex")) {
            idx = resolveLayerIndex(*host.canvas, cmd, true);
        }
        if (idx < 0 || idx >= static_cast<int>(host.canvas->layers.size())) {
            outErr = "Invalid target layer for set_base_color";
            return false;
        }
        Layer& l = host.canvas->layers[idx];
        size_t nPx = static_cast<size_t>(host.canvas->width) * host.canvas->height;
        l.rgba.resize(nPx * 4);
        for (size_t i = 0; i < nPx; ++i) {
            l.rgba[i * 4 + 0] = col.r;
            l.rgba[i * 4 + 1] = col.g;
            l.rgba[i * 4 + 2] = col.b;
            l.rgba[i * 4 + 3] = col.a;
        }
        host.canvas->recomposite();
        doSyncCanvas();
        return true;
    }

    if (op == "add_paint_layer" || op == "add_layer") {
        std::string name = cmd.getStr("name", "Layer");
        Color4u8 bg{0, 0, 0, 0};
        bool fillBg = parseColor4(cmd.find("fillColor"), bg) || parseColor4(cmd.find("bg"), bg);
        int idx = host.canvas->addLayer(name, fillBg, bg);
        if (idx >= 0 && idx < static_cast<int>(host.canvas->layers.size())) {
            if (cmd.find("blend")) host.canvas->layers[idx].blend = parseBlendMode(cmd.getStr("blend"));
            if (cmd.find("opacity")) {
                host.canvas->layers[idx].opacity = std::clamp(static_cast<float>(cmd.getNum("opacity", 1.0)), 0.0f, 1.0f);
            }
            if (fillBg || cmd.find("blend") || cmd.find("opacity")) host.canvas->recomposite();
            doSyncCanvas();
        }
        outResult.set("layerIndex", JsonValue::makeNum(idx));
        return true;
    }

    if (op == "delete_layer") {
        int idx = resolveLayerIndex(*host.canvas, cmd, false);
        if (!host.canvas->removeLayer(idx)) {
            outErr = "delete_layer failed (invalid or last remaining layer)";
            return false;
        }
        doSyncCanvas();
        outResult.set("deletedIndex", JsonValue::makeNum(idx));
        return true;
    }

    if (op == "undo") {
        host.canvas->undo(host.mesh);
        doSyncCanvas();
        return true;
    }

    if (op == "redo") {
        host.canvas->redo(host.mesh);
        doSyncCanvas();
        return true;
    }

    if (op == "copy_item") {
        int li = -1, ii = -1;
        if (resolveItemTarget(*host.canvas, cmd, li, ii)) {
            host.canvas->activeLayerIdx = li;
            host.canvas->selectedItemIdx = ii;
        }
        if (!host.canvas->copySelectedItem()) {
            outErr = "copy_item failed: no copyable selected item";
            return false;
        }
        return true;
    }

    if (op == "cut_item") {
        int li = -1, ii = -1;
        if (resolveItemTarget(*host.canvas, cmd, li, ii)) {
            host.canvas->activeLayerIdx = li;
            host.canvas->selectedItemIdx = ii;
        }
        if (!host.canvas->cutSelectedItem(host.mesh)) {
            outErr = "cut_item failed: no cuttable selected item";
            return false;
        }
        doSyncCanvas();
        return true;
    }

    if (op == "paste_item") {
        Vec2 uv{0.5f, 0.5f};
        bool hasUv = parseVec2(cmd.find("uvPos"), uv);
        RayHit h{};
        bool hasHit = false;
        Vec3 ro, rd;
        if (host.mesh->loaded && parseVec3(cmd.find("rayOrigin"), ro) && parseVec3(cmd.find("rayDir"), rd)) {
            h = host.mesh->raycast({ro, normalized(rd)}, true);
            hasHit = h.hit;
        }
        int itemIdx = host.canvas->pasteClipboardItem(host.mesh, hasUv ? &uv : nullptr, hasHit ? &h : nullptr);
        if (itemIdx < 0) {
            outErr = "paste_item failed: clipboard empty";
            return false;
        }
        doSyncCanvas();
        outResult.set("itemIndex", JsonValue::makeNum(itemIdx));
        return true;
    }

    if (op == "set_mirror") {
        int li = -1, ii = -1;
        if (resolveItemTarget(*host.canvas, cmd, li, ii)) {
            host.canvas->activeLayerIdx = li;
            host.canvas->selectedItemIdx = ii;
        }
        bool m = cmd.getBool("mirror", cmd.getBool("mirrorX", true));
        host.canvas->setMirrorOnActiveItem(m, host.mesh);
        if (host.uiState) host.uiState->brush.mirrorX = m;
        doSyncCanvas();
        return true;
    }

    if (op == "remove_pen_point") {
        int li = -1, ii = -1;
        bool hasItem = resolveItemTarget(*host.canvas, cmd, li, ii);
        int ptIdx = cmd.getInt("pointIndex", cmd.getInt("ptIdx", -1));
        if (hasItem && host.canvas->layers[li].items[ii].kind == VectorItemKind::PenPath) {
            auto& nodes = host.canvas->layers[li].items[ii].pen.nodes;
            if (nodes.empty()) { outErr = "Pen item has no points"; return false; }
            if (ptIdx < 0 || ptIdx >= static_cast<int>(nodes.size())) ptIdx = static_cast<int>(nodes.size()) - 1;
            host.canvas->pushUndoSnapshot();
            nodes.erase(nodes.begin() + ptIdx);
            host.canvas->rasterizeLayer(li, host.mesh);
            doSyncCanvas();
            return true;
        }
        int lIdx = resolveLayerIndex(*host.canvas, cmd, false);
        if (lIdx >= 0 && lIdx < static_cast<int>(host.canvas->layers.size()) &&
            host.canvas->layers[lIdx].kind == LayerKind::RulerStripe) {
            auto& nodes = host.canvas->layers[lIdx].ruler.nodes;
            if (nodes.empty()) { outErr = "Pen layer has no points"; return false; }
            if (ptIdx < 0 || ptIdx >= static_cast<int>(nodes.size())) ptIdx = static_cast<int>(nodes.size()) - 1;
            host.canvas->pushUndoSnapshot();
            nodes.erase(nodes.begin() + ptIdx);
            host.canvas->rasterizeLayer(lIdx, host.mesh);
            doSyncCanvas();
            return true;
        }
        outErr = "remove_pen_point could not find target Pen path";
        return false;
    }

    if (op == "paint_rect_3d") {
        if (!host.mesh->loaded) {
            outErr = "paint_rect_3d requires a loaded OBJ mesh";
            return false;
        }
        int idx = resolveLayerIndex(*host.canvas, cmd, true);
        if (idx < 0 || idx >= static_cast<int>(host.canvas->layers.size())) {
            outErr = "No unlocked RasterPaint layer for paint_rect_3d";
            return false;
        }
        Color4u8 fillCol{205, 232, 32, 255};
        parseColor4(cmd.find("color"), fillCol);
        parseColor4(cmd.find("fillColor"), fillCol);
        Color4u8 strokeCol{20, 24, 30, 255};
        parseColor4(cmd.find("strokeColor"), strokeCol);
        parseColor4(cmd.find("outlineColor"), strokeCol);
        float strokeWorld = static_cast<float>(cmd.getNum("strokeWidth", cmd.getNum("worldStroke", 0.0)));
        float opacity = static_cast<float>(cmd.getNum("opacity", 1.0));
        float rotDeg = static_cast<float>(cmd.getNum("rotationDeg", 0.0));
        bool mirrorX = cmd.getBool("mirrorX", true);

        Vec3 centerPos{0, 0, 0}, nCenter{0, 1, 0};
        Vec3 rayOrig, rayDir;
        if (parseVec3(cmd.find("rayOrigin"), rayOrig) && parseVec3(cmd.find("rayDir"), rayDir)) {
            RayHit h = host.mesh->raycast({rayOrig, normalized(rayDir)}, true);
            if (!h.hit) {
                outErr = "paint_rect_3d raycast missed mesh";
                return false;
            }
            centerPos = h.pos;
            nCenter = h.normal;
        } else if (!parseVec3(cmd.find("pos3D"), centerPos)) {
            outErr = "paint_rect_3d needs rayOrigin/rayDir or pos3D";
            return false;
        } else {
            parseVec3(cmd.find("nrm3D"), nCenter);
            nCenter = normalized(nCenter);
        }

        Vec3 rightHint = computeWorldSurfaceRight3D(centerPos, nCenter);
        parseVec3(cmd.find("rightHint"), rightHint);
        float worldW = static_cast<float>(cmd.getNum("worldW", cmd.getNum("width", 0.5)));
        float worldH = static_cast<float>(cmd.getNum("worldH", cmd.getNum("height", 0.25)));

        stampRect3DInScript(*host.canvas, *host.mesh, idx, centerPos, nCenter, rightHint,
                            worldW, worldH, rotDeg, fillCol, strokeCol, strokeWorld, opacity, mirrorX);
        doSyncCanvas();
        return true;
    }

    if (op == "paint_rect_uv") {
        int idx = resolveLayerIndex(*host.canvas, cmd, true);
        if (idx < 0 || idx >= static_cast<int>(host.canvas->layers.size())) {
            outErr = "No unlocked RasterPaint layer for paint_rect_uv";
            return false;
        }
        Vec2 uvMin{0.25f, 0.25f}, uvMax{0.75f, 0.75f};
        if (!parseVec2(cmd.find("uvMin"), uvMin) || !parseVec2(cmd.find("uvMax"), uvMax)) {
            Vec2 c{0.5f, 0.5f}, sz{0.2f, 0.2f};
            if (parseVec2(cmd.find("uvCenter"), c) && parseVec2(cmd.find("uvSize"), sz)) {
                uvMin = {c.x - sz.x * 0.5f, c.y - sz.y * 0.5f};
                uvMax = {c.x + sz.x * 0.5f, c.y + sz.y * 0.5f};
            }
        }
        Color4u8 fillCol{205, 232, 32, 255};
        parseColor4(cmd.find("color"), fillCol);
        parseColor4(cmd.find("fillColor"), fillCol);
        Color4u8 strokeCol{20, 24, 30, 255};
        parseColor4(cmd.find("strokeColor"), strokeCol);
        float strokePx = static_cast<float>(cmd.getNum("strokeWidthPx", 0.0));
        float opacity = static_cast<float>(cmd.getNum("opacity", 1.0));
        bool mirrorX = cmd.getBool("mirrorX", false);

        stampRectUvInScript(*host.canvas, host.mesh, idx, uvMin, uvMax, fillCol, strokeCol, strokePx, opacity, mirrorX);
        doSyncCanvas();
        return true;
    }

    if (op == "paint_stroke_3d") {
        if (!host.mesh->loaded) {
            outErr = "paint_stroke_3d requires a loaded OBJ mesh";
            return false;
        }
        int idx = resolveLayerIndex(*host.canvas, cmd, true);
        if (idx < 0 || idx >= static_cast<int>(host.canvas->layers.size())) {
            outErr = "No unlocked RasterPaint layer for paint_stroke_3d";
            return false;
        }
        BrushSettings bs = host.uiState->brush;
        parseColor4(cmd.find("color"), bs.color);
        bs.worldRadius = static_cast<float>(cmd.getNum("worldRadius", 0.06));
        bs.hardness = static_cast<float>(cmd.getNum("hardness", 0.88));
        bs.opacity = static_cast<float>(cmd.getNum("opacity", 1.0));
        bs.spacing = static_cast<float>(cmd.getNum("spacing", 0.20));
        bs.eraser = cmd.getBool("eraser", false);
        bs.mirrorX = cmd.getBool("mirrorX", true);
        bs.project3DBrush = cmd.getBool("project3DBrush", true);

        const JsonValue* pts = cmd.find("points");
        if (!pts || !pts->isArray()) pts = cmd.find("rays");
        if (!pts || !pts->isArray() || pts->arr.empty()) {
            outErr = "paint_stroke_3d requires non-empty 'points' or 'rays' array";
            return false;
        }

        host.canvas->beginStroke(idx);
        RayHit prev{};
        int hitCount = 0;
        for (const JsonValue& pv : pts->arr) {
            RayHit hit{};
            if (pv.isObject()) {
                Vec3 ro, rd;
                if (parseVec3(pv.find("rayOrigin"), ro) && parseVec3(pv.find("rayDir"), rd)) {
                    hit = host.mesh->raycast({ro, normalized(rd)}, true);
                }
            } else if (pv.isArray()) {
                Vec3 p3;
                if (parseVec3(&pv, p3)) {
                    hit = host.mesh->projectPointToSurface(p3, Vec3{0, 1, 0}, 0.25f);
                }
            }
            if (hit.hit) {
                host.canvas->paintDabAt3D(idx, *host.mesh, hit, bs, prev.hit ? &prev : nullptr);
                prev = hit;
                ++hitCount;
            }
        }
        host.canvas->endStroke(idx);
        doSyncCanvas();
        outResult.set("hits", JsonValue::makeNum(hitCount));
        return true;
    }

    auto fillPenFromCmd = [&](RulerStripeProps& rp) -> bool {
        rp.closed = cmd.getBool("closed", false);
        rp.filled = cmd.getBool("filled", false);
        rp.hasStroke = cmd.getBool("hasStroke", true);
        rp.use3DSpace = cmd.getBool("use3DSpace", true);
        rp.mirrorX = cmd.getBool("mirrorX", cmd.getBool("mirror", true));
        rp.smoothCurve = cmd.getBool("smoothCurve", true);
        rp.widthPx = static_cast<float>(cmd.getNum("widthPx", 14.0));
        rp.worldWidth = static_cast<float>(cmd.getNum("worldWidth", 0.03));
        rp.endTaper = static_cast<float>(cmd.getNum("endTaper", 0.0));
        rp.outlineWidth = static_cast<float>(cmd.getNum("outlineWidth", 0.0));
        parseColor4(cmd.find("fillColor"), rp.fillColor);
        parseColor4(cmd.find("strokeColor"), rp.color);
        parseColor4(cmd.find("color"), rp.color);
        parseColor4(cmd.find("outlineColor"), rp.outlineColor);

        const JsonValue* nodesV = cmd.find("nodes");
        if (!nodesV || !nodesV->isArray() || nodesV->arr.empty()) {
            outErr = op + " missing non-empty 'nodes' array";
            return false;
        }

        rp.nodes.clear();
        for (size_t ni = 0; ni < nodesV->arr.size(); ++ni) {
            const JsonValue& nj = nodesV->arr[ni];
            StripeNode sn{};
            sn.smooth = nj.getBool("smooth", true);

            Vec3 ro, rd;
            if (rp.use3DSpace && host.mesh->loaded &&
                parseVec3(nj.find("rayOrigin"), ro) && parseVec3(nj.find("rayDir"), rd)) {
                RayHit h = host.mesh->raycast({ro, normalized(rd)}, true);
                if (!h.hit) {
                    outErr = op + " node #" + std::to_string(ni) + " raycast missed mesh";
                    return false;
                }
                sn.uv = h.uv;
                sn.pos3D = h.pos;
                sn.nrm3D = h.normal;
                sn.has3D = true;

                Vec3 hInOff{0, 0, 0}, hOutOff{0, 0, 0};
                bool hasInOff = parseVec3(nj.find("handleInOffset3D"), hInOff);
                bool hasOutOff = parseVec3(nj.find("handleOutOffset3D"), hOutOff);
                Vec3 hInAbs{0, 0, 0}, hOutAbs{0, 0, 0};
                bool hasInAbs = parseVec3(nj.find("handleIn3D"), hInAbs);
                bool hasOutAbs = parseVec3(nj.find("handleOut3D"), hOutAbs);

                sn.handleIn3D = hasInAbs ? hInAbs : (hasInOff ? (h.pos + hInOff) : h.pos);
                sn.handleOut3D = hasOutAbs ? hOutAbs : (hasOutOff ? (h.pos + hOutOff) : h.pos);
                sn.hasHandles = hasInOff || hasOutOff || hasInAbs || hasOutAbs;
                if (sn.hasHandles) {
                    RayHit mhIn = host.mesh->projectPointToSurface(sn.handleIn3D, h.normal, 0.30f);
                    sn.handleInUv = mhIn.hit ? mhIn.uv : h.uv;
                    RayHit mhOut = host.mesh->projectPointToSurface(sn.handleOut3D, h.normal, 0.30f);
                    sn.handleOutUv = mhOut.hit ? mhOut.uv : h.uv;
                } else {
                    sn.handleInUv = h.uv;
                    sn.handleOutUv = h.uv;
                }
            } else {
                parseVec2(nj.find("uv"), sn.uv);
                bool hasIn = parseVec2(nj.find("handleInUv"), sn.handleInUv);
                bool hasOut = parseVec2(nj.find("handleOutUv"), sn.handleOutUv);
                if (!hasIn) sn.handleInUv = sn.uv;
                if (!hasOut) sn.handleOutUv = sn.uv;
                sn.hasHandles = hasIn || hasOut;
                sn.has3D = false;
            }
            rp.nodes.push_back(sn);
        }
        return true;
    };

    if (op == "add_pen" || op == "add_pen_item") {
        std::string name = cmd.getStr("name", "Pen Path");
        RulerStripeProps rp{};
        if (!fillPenFromCmd(rp)) return false;
        int itemIdx = host.canvas->addPenItem(name, rp, host.mesh);
        doSyncCanvas();
        outResult.set("layerIndex", JsonValue::makeNum(host.canvas->activeLayerIdx));
        outResult.set("itemIndex", JsonValue::makeNum(itemIdx));
        outResult.set("nodeCount", JsonValue::makeNum(static_cast<double>(rp.nodes.size())));
        return true;
    }

    if (op == "add_pen_layer") {
        std::string name = cmd.getStr("name", "Pen Path");
        int idx = host.canvas->addRulerStripeLayer(name);
        RulerStripeProps& rp = host.canvas->layers[idx].ruler;
        if (!fillPenFromCmd(rp)) return false;

        if (cmd.find("opacity")) {
            host.canvas->layers[idx].opacity = std::clamp(static_cast<float>(cmd.getNum("opacity", 1.0)), 0.0f, 1.0f);
        }
        if (cmd.find("blend")) {
            host.canvas->layers[idx].blend = parseBlendMode(cmd.getStr("blend"));
        }

        host.canvas->rasterizeLayer(idx, host.mesh);
        doSyncCanvas();
        outResult.set("layerIndex", JsonValue::makeNum(idx));
        outResult.set("nodeCount", JsonValue::makeNum(static_cast<double>(rp.nodes.size())));
        return true;
    }

    if (op == "add_rect" || op == "add_rect_item") {
        std::string name = cmd.getStr("name", "Rectangle");
        RectShapeProps rp{};
        parseColor4(cmd.find("fillColor"), rp.fillColor);
        parseColor4(cmd.find("color"), rp.fillColor);
        parseColor4(cmd.find("strokeColor"), rp.strokeColor);
        rp.filled = cmd.getBool("filled", true);
        rp.hasStroke = cmd.getBool("hasStroke", cmd.find("strokeColor") != nullptr);
        rp.mirrorX = cmd.getBool("mirrorX", cmd.getBool("mirror", true));
        rp.rotationDeg = static_cast<float>(cmd.getNum("rotationDeg", 0.0));
        rp.cornerRadiusPx = static_cast<float>(cmd.getNum("cornerRadiusPx", 0.0));
        rp.strokeWidthPx = static_cast<float>(cmd.getNum("strokeWidthPx", 4.0));
        rp.worldWidth = static_cast<float>(cmd.getNum("worldWidth", cmd.getNum("worldW", 0.30)));
        rp.worldHeight = static_cast<float>(cmd.getNum("worldHeight", cmd.getNum("worldH", 0.16)));

        Vec3 ro, rd;
        if (host.mesh->loaded && parseVec3(cmd.find("rayOrigin"), ro) && parseVec3(cmd.find("rayDir"), rd)) {
            RayHit h = host.mesh->raycast({ro, normalized(rd)}, true);
            if (!h.hit) {
                outErr = "add_rect raycast missed mesh";
                return false;
            }
            rp.uvPos = h.uv;
            rp.pos3D = h.pos;
            rp.nrm3D = h.normal;
            rp.right3D = computeWorldSurfaceRight3D(h.pos, h.normal);
            parseVec3(cmd.find("rightHint"), rp.right3D);
            rp.anchoredIn3D = true;
        } else {
            parseVec2(cmd.find("uvPos"), rp.uvPos);
            parseVec2(cmd.find("uvScale"), rp.uvScale);
            rp.anchoredIn3D = false;
        }

        int itemIdx = host.canvas->addRectItem(name, rp, host.mesh);
        doSyncCanvas();
        outResult.set("layerIndex", JsonValue::makeNum(host.canvas->activeLayerIdx));
        outResult.set("itemIndex", JsonValue::makeNum(itemIdx));
        return true;
    }

    auto fillTextFromCmd = [&](TextLayerProps& tp) -> bool {
        tp.text = cmd.getStr("text", "Lorem ipsum");
        std::string fPath = findMatchingFontPath(*host.canvas, cmd.getStr("font"));
        if (!fPath.empty()) tp.fontPath = fPath;
        tp.fontSizePx = static_cast<float>(cmd.getNum("fontSizePx", 110.0));
        tp.worldHeight = static_cast<float>(cmd.getNum("worldHeight", 0.22));
        tp.tracking = static_cast<float>(cmd.getNum("tracking", 0.0));
        tp.italicShear = static_cast<float>(cmd.getNum("italicShear", 0.0));
        tp.outlineWidth = static_cast<float>(cmd.getNum("outlineWidth", 0.0));
        tp.rotationDeg = static_cast<float>(cmd.getNum("rotationDeg", 0.0));
        parseColor4(cmd.find("fillColor"), tp.fillColor);
        parseColor4(cmd.find("outlineColor"), tp.outlineColor);
        tp.mirrorMode = parseMirrorMode(cmd.getStr("mirrorMode", "ReadableBothSides"));
        tp.mirrorX = cmd.getBool("mirrorX", cmd.getBool("mirror", tp.mirrorMode != MirrorTextMode::None));

        Vec3 ro, rd;
        if (host.mesh->loaded && parseVec3(cmd.find("rayOrigin"), ro) && parseVec3(cmd.find("rayDir"), rd)) {
            RayHit h = host.mesh->raycast({ro, normalized(rd)}, true);
            if (!h.hit) {
                outErr = op + " raycast missed mesh";
                return false;
            }
            tp.uvPos = h.uv;
            tp.pos3D = h.pos;
            tp.nrm3D = h.normal;
            Vec3 rHint = computeWorldSurfaceRight3D(h.pos, h.normal);
            parseVec3(cmd.find("rightHint"), rHint);
            tp.right3D = rHint;
            tp.anchoredIn3D = true;
        } else {
            parseVec2(cmd.find("uvPos"), tp.uvPos);
            tp.anchoredIn3D = false;
        }
        return true;
    };

    if (op == "add_text" || op == "add_text_item") {
        std::string name = cmd.getStr("name", "Text");
        TextLayerProps tp{};
        if (!fillTextFromCmd(tp)) return false;
        int itemIdx = host.canvas->addTextItem(name, tp, host.mesh);
        doSyncCanvas();
        outResult.set("layerIndex", JsonValue::makeNum(host.canvas->activeLayerIdx));
        outResult.set("itemIndex", JsonValue::makeNum(itemIdx));
        return true;
    }

    if (op == "add_text_layer") {
        std::string name = cmd.getStr("name", "Text Layer");
        int idx = host.canvas->addTextLayer(name);
        TextLayerProps& tp = host.canvas->layers[idx].text;
        if (!fillTextFromCmd(tp)) return false;
        if (cmd.find("opacity")) {
            host.canvas->layers[idx].opacity = std::clamp(static_cast<float>(cmd.getNum("opacity", 1.0)), 0.0f, 1.0f);
        }
        if (cmd.find("blend")) {
            host.canvas->layers[idx].blend = parseBlendMode(cmd.getStr("blend"));
        }

        host.canvas->rasterizeLayer(idx, host.mesh);
        doSyncCanvas();
        outResult.set("layerIndex", JsonValue::makeNum(idx));
        return true;
    }

    if (op == "add_decal" || op == "add_image" || op == "add_image_item") {
        std::string path = resolveInputPath(cmd.getStr("path"), baseDir);
        std::string name = cmd.getStr("name", std::filesystem::path(path).stem().string());
        int w = 0, h = 0, comp = 0;
        uint8_t* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
        if (!px || w <= 0 || h <= 0) {
            if (px) stbi_image_free(px);
            outErr = op + " failed to load image: " + path;
            return false;
        }
        std::vector<uint8_t> rgba(px, px + static_cast<size_t>(w) * h * 4);
        stbi_image_free(px);

        DecalLayerProps dp{};
        dp.sourcePath = path;
        dp.worldWidth = static_cast<float>(cmd.getNum("worldWidth", 0.40));
        dp.rotationDeg = static_cast<float>(cmd.getNum("rotationDeg", 0.0));
        parseColor4(cmd.find("tint"), dp.tint);
        dp.mirrorMode = parseMirrorMode(cmd.getStr("mirrorMode", "ReadableBothSides"));
        dp.mirrorX = cmd.getBool("mirrorX", cmd.getBool("mirror", dp.mirrorMode != MirrorTextMode::None));

        Vec3 ro, rd;
        if (host.mesh->loaded && parseVec3(cmd.find("rayOrigin"), ro) && parseVec3(cmd.find("rayDir"), rd)) {
            RayHit hit = host.mesh->raycast({ro, normalized(rd)}, true);
            if (!hit.hit) {
                outErr = op + " raycast missed mesh";
                return false;
            }
            dp.uvPos = hit.uv;
            dp.pos3D = hit.pos;
            dp.nrm3D = hit.normal;
            Vec3 rHint = computeWorldSurfaceRight3D(hit.pos, hit.normal);
            parseVec3(cmd.find("rightHint"), rHint);
            dp.right3D = rHint;
            dp.anchoredIn3D = true;
        } else {
            parseVec2(cmd.find("uvPos"), dp.uvPos);
            if (parseVec2(cmd.find("uvScale"), dp.uvScale)) {
                dp.uvScale.x = std::clamp(dp.uvScale.x, 0.01f, 0.9f);
                dp.uvScale.y = std::clamp(dp.uvScale.y, 0.01f, 0.9f);
            }
            dp.anchoredIn3D = false;
        }
        int itemIdx = host.canvas->addImageItemFromRgba(name, std::move(rgba), w, h, dp, host.mesh);
        doSyncCanvas();
        outResult.set("layerIndex", JsonValue::makeNum(host.canvas->activeLayerIdx));
        outResult.set("itemIndex", JsonValue::makeNum(itemIdx));
        return true;
    }

    if (op == "add_decal_layer") {
        std::string path = resolveInputPath(cmd.getStr("path"), baseDir);
        std::string name = cmd.getStr("name", std::filesystem::path(path).stem().string());
        int idx = host.canvas->addDecalLayer(name, path);
        if (idx < 0) {
            outErr = "add_decal_layer failed to load image: " + path;
            return false;
        }
        DecalLayerProps& dp = host.canvas->layers[idx].decal;
        dp.worldWidth = static_cast<float>(cmd.getNum("worldWidth", 0.40));
        dp.rotationDeg = static_cast<float>(cmd.getNum("rotationDeg", 0.0));
        parseColor4(cmd.find("tint"), dp.tint);
        dp.mirrorMode = parseMirrorMode(cmd.getStr("mirrorMode", "ReadableBothSides"));
        if (cmd.find("opacity")) {
            host.canvas->layers[idx].opacity = std::clamp(static_cast<float>(cmd.getNum("opacity", 1.0)), 0.0f, 1.0f);
        }
        if (cmd.find("blend")) {
            host.canvas->layers[idx].blend = parseBlendMode(cmd.getStr("blend"));
        }

        Vec3 ro, rd;
        if (host.mesh->loaded && parseVec3(cmd.find("rayOrigin"), ro) && parseVec3(cmd.find("rayDir"), rd)) {
            RayHit h = host.mesh->raycast({ro, normalized(rd)}, true);
            if (!h.hit) {
                outErr = "add_decal_layer raycast missed mesh";
                return false;
            }
            dp.uvPos = h.uv;
            dp.pos3D = h.pos;
            dp.nrm3D = h.normal;
            Vec3 rHint = computeWorldSurfaceRight3D(h.pos, h.normal);
            parseVec3(cmd.find("rightHint"), rHint);
            dp.right3D = rHint;
            dp.anchoredIn3D = true;
        } else {
            parseVec2(cmd.find("uvPos"), dp.uvPos);
            if (parseVec2(cmd.find("uvScale"), dp.uvScale)) {
                dp.uvScale.x = std::clamp(dp.uvScale.x, 0.01f, 0.9f);
                dp.uvScale.y = std::clamp(dp.uvScale.y, 0.01f, 0.9f);
            }
            dp.anchoredIn3D = false;
        }

        host.canvas->rasterizeVectorLayer(idx, host.mesh);
        doSyncCanvas();
        outResult.set("layerIndex", JsonValue::makeNum(idx));
        return true;
    }

    if (op == "transform_layer_2d") {
        Vec2 dUv{0.0f, 0.0f};
        parseVec2(cmd.find("deltaUv"), dUv);
        float scaleFactor = std::clamp(static_cast<float>(cmd.getNum("scaleFactor", 1.0)), 0.1f, 8.0f);
        float dRotDeg = static_cast<float>(cmd.getNum("deltaRotDeg", 0.0));
        bool keep3D = cmd.getBool("keep3DIfHit", true);

        int tiL = -1, tiI = -1;
        if (resolveItemTarget(*host.canvas, cmd, tiL, tiI)) {
            VectorItem& it = host.canvas->layers[tiL].items[tiI];
            host.canvas->pushUndoSnapshot();
            if (it.kind == VectorItemKind::Text) {
                it.text.uvPos.x = std::clamp(it.text.uvPos.x + dUv.x, 0.0f, 1.0f);
                it.text.uvPos.y = std::clamp(it.text.uvPos.y + dUv.y, 0.0f, 1.0f);
                it.text.worldHeight = std::clamp(it.text.worldHeight * scaleFactor, 0.03f, 2.0f);
                it.text.fontSizePx = std::clamp(it.text.fontSizePx * scaleFactor, 12.0f, 512.0f);
                it.text.rotationDeg += dRotDeg;
                Vec3 p3, n3; int32_t tri = -1;
                if (keep3D && it.text.anchoredIn3D && host.mesh->loaded &&
                    host.mesh->queryUvAtPixel(it.text.uvPos.x, it.text.uvPos.y, p3, n3, tri)) {
                    it.text.pos3D = p3; it.text.nrm3D = n3;
                } else if (!it.text.anchoredIn3D) {
                    host.canvas->syncItem2DTo3D(it, host.mesh);
                }
            } else if (it.kind == VectorItemKind::Image) {
                it.image.uvPos.x = std::clamp(it.image.uvPos.x + dUv.x, 0.0f, 1.0f);
                it.image.uvPos.y = std::clamp(it.image.uvPos.y + dUv.y, 0.0f, 1.0f);
                it.image.worldWidth = std::clamp(it.image.worldWidth * scaleFactor, 0.04f, 3.0f);
                it.image.uvScale.x = std::clamp(it.image.uvScale.x * scaleFactor, 0.01f, 0.9f);
                it.image.uvScale.y = std::clamp(it.image.uvScale.y * scaleFactor, 0.01f, 0.9f);
                it.image.rotationDeg += dRotDeg;
                Vec3 p3, n3; int32_t tri = -1;
                if (keep3D && it.image.anchoredIn3D && host.mesh->loaded &&
                    host.mesh->queryUvAtPixel(it.image.uvPos.x, it.image.uvPos.y, p3, n3, tri)) {
                    it.image.pos3D = p3; it.image.nrm3D = n3;
                } else if (!it.image.anchoredIn3D) {
                    host.canvas->syncItem2DTo3D(it, host.mesh);
                }
            } else if (it.kind == VectorItemKind::RectShape) {
                it.rect.uvPos.x = std::clamp(it.rect.uvPos.x + dUv.x, 0.0f, 1.0f);
                it.rect.uvPos.y = std::clamp(it.rect.uvPos.y + dUv.y, 0.0f, 1.0f);
                it.rect.worldWidth = std::clamp(it.rect.worldWidth * scaleFactor, 0.03f, 3.0f);
                it.rect.worldHeight = std::clamp(it.rect.worldHeight * scaleFactor, 0.03f, 3.0f);
                it.rect.uvScale.x = std::clamp(it.rect.uvScale.x * scaleFactor, 0.01f, 0.9f);
                it.rect.uvScale.y = std::clamp(it.rect.uvScale.y * scaleFactor, 0.01f, 0.9f);
                it.rect.rotationDeg += dRotDeg;
                Vec3 p3, n3; int32_t tri = -1;
                if (keep3D && it.rect.anchoredIn3D && host.mesh->loaded &&
                    host.mesh->queryUvAtPixel(it.rect.uvPos.x, it.rect.uvPos.y, p3, n3, tri)) {
                    it.rect.pos3D = p3; it.rect.nrm3D = n3;
                } else if (!it.rect.anchoredIn3D) {
                    host.canvas->syncItem2DTo3D(it, host.mesh);
                }
            }
            host.canvas->rasterizeLayer(tiL, host.mesh);
            doSyncCanvas();
            return true;
        }

        int idx = resolveLayerIndex(*host.canvas, cmd, false);
        if (idx < 0 || idx >= static_cast<int>(host.canvas->layers.size())) {
            outErr = "transform_layer_2d target layer/item not found";
            return false;
        }
        Layer& l = host.canvas->layers[idx];
        if (l.kind == LayerKind::Text) {
            l.text.uvPos.x = std::clamp(l.text.uvPos.x + dUv.x, 0.0f, 1.0f);
            l.text.uvPos.y = std::clamp(l.text.uvPos.y + dUv.y, 0.0f, 1.0f);
            l.text.worldHeight = std::clamp(l.text.worldHeight * scaleFactor, 0.03f, 2.0f);
            l.text.fontSizePx = std::clamp(l.text.fontSizePx * scaleFactor, 12.0f, 512.0f);
            l.text.rotationDeg += dRotDeg;

            Vec3 p3, n3;
            int32_t tri = -1;
            if (keep3D && l.text.anchoredIn3D && host.mesh->loaded &&
                host.mesh->queryUvAtPixel(l.text.uvPos.x, l.text.uvPos.y, p3, n3, tri)) {
                l.text.pos3D = p3;
                l.text.nrm3D = n3;
            } else if (!l.text.anchoredIn3D) {
                host.canvas->syncLayer2DTo3D(idx, host.mesh);
            }
        } else if (l.kind == LayerKind::Decal) {
            l.decal.uvPos.x = std::clamp(l.decal.uvPos.x + dUv.x, 0.0f, 1.0f);
            l.decal.uvPos.y = std::clamp(l.decal.uvPos.y + dUv.y, 0.0f, 1.0f);
            l.decal.worldWidth = std::clamp(l.decal.worldWidth * scaleFactor, 0.04f, 3.0f);
            l.decal.uvScale.x = std::clamp(l.decal.uvScale.x * scaleFactor, 0.01f, 0.9f);
            l.decal.uvScale.y = std::clamp(l.decal.uvScale.y * scaleFactor, 0.01f, 0.9f);
            l.decal.rotationDeg += dRotDeg;

            Vec3 p3, n3;
            int32_t tri = -1;
            if (keep3D && l.decal.anchoredIn3D && host.mesh->loaded &&
                host.mesh->queryUvAtPixel(l.decal.uvPos.x, l.decal.uvPos.y, p3, n3, tri)) {
                l.decal.pos3D = p3;
                l.decal.nrm3D = n3;
            } else if (!l.decal.anchoredIn3D) {
                host.canvas->syncLayer2DTo3D(idx, host.mesh);
            }
        } else {
            outErr = "transform_layer_2d requires Text, Image, or RectShape layer/item";
            return false;
        }

        host.canvas->rasterizeLayer(idx, host.mesh);
        doSyncCanvas();
        return true;
    }

    if (op == "reorder_layer") {
        int fromIdx = resolveLayerIndex(*host.canvas, cmd, false);
        int n = static_cast<int>(host.canvas->layers.size());
        if (fromIdx < 0 || fromIdx >= n) {
            outErr = "reorder_layer source layer not found";
            return false;
        }
        int toIdx = fromIdx;
        if (cmd.getBool("toTop", false)) {
            toIdx = n - 1;
        } else if (cmd.getBool("toBottom", false)) {
            toIdx = 0;
        } else if (cmd.find("toIdx")) {
            toIdx = std::clamp(cmd.getInt("toIdx", fromIdx), 0, n - 1);
        } else if (cmd.find("delta")) {
            toIdx = std::clamp(fromIdx + cmd.getInt("delta", 0), 0, n - 1);
        }
        host.canvas->reorderLayer(fromIdx, toIdx);
        doSyncCanvas();
        outResult.set("fromIdx", JsonValue::makeNum(fromIdx));
        outResult.set("toIdx", JsonValue::makeNum(toIdx));
        return true;
    }

    if (op == "set_layer_props") {
        int tiL = -1, tiI = -1;
        if ((cmd.find("item") || cmd.find("itemIndex")) && resolveItemTarget(*host.canvas, cmd, tiL, tiI)) {
            VectorItem& it = host.canvas->layers[tiL].items[tiI];
            host.canvas->pushUndoSnapshot();
            if (cmd.find("visible")) it.visible = cmd.getBool("visible", it.visible);
            if (cmd.find("locked"))  it.locked = cmd.getBool("locked", it.locked);
            if (cmd.find("newName")) it.name = cmd.getStr("newName", it.name);
            if (it.kind == VectorItemKind::Text) {
                if (cmd.find("text"))        it.text.text = cmd.getStr("text");
                if (cmd.find("font"))        { auto fp = findMatchingFontPath(*host.canvas, cmd.getStr("font")); if (!fp.empty()) it.text.fontPath = fp; }
                if (cmd.find("fontSizePx"))  it.text.fontSizePx = static_cast<float>(cmd.getNum("fontSizePx", it.text.fontSizePx));
                if (cmd.find("worldHeight")) it.text.worldHeight = static_cast<float>(cmd.getNum("worldHeight", it.text.worldHeight));
                if (cmd.find("outlineWidth"))it.text.outlineWidth = static_cast<float>(cmd.getNum("outlineWidth", it.text.outlineWidth));
                if (cmd.find("rotationDeg")) it.text.rotationDeg = static_cast<float>(cmd.getNum("rotationDeg", it.text.rotationDeg));
                parseColor4(cmd.find("fillColor"), it.text.fillColor);
                parseColor4(cmd.find("outlineColor"), it.text.outlineColor);
            } else if (it.kind == VectorItemKind::PenPath) {
                if (cmd.find("closed"))     it.pen.closed = cmd.getBool("closed", it.pen.closed);
                if (cmd.find("filled"))     it.pen.filled = cmd.getBool("filled", it.pen.filled);
                if (cmd.find("hasStroke"))  it.pen.hasStroke = cmd.getBool("hasStroke", it.pen.hasStroke);
                if (cmd.find("mirrorX"))    it.pen.mirrorX = cmd.getBool("mirrorX", it.pen.mirrorX);
                if (cmd.find("widthPx"))    it.pen.widthPx = static_cast<float>(cmd.getNum("widthPx", it.pen.widthPx));
                if (cmd.find("worldWidth")) it.pen.worldWidth = static_cast<float>(cmd.getNum("worldWidth", it.pen.worldWidth));
                parseColor4(cmd.find("fillColor"), it.pen.fillColor);
                parseColor4(cmd.find("strokeColor"), it.pen.color);
                parseColor4(cmd.find("color"), it.pen.color);
            } else if (it.kind == VectorItemKind::RectShape) {
                if (cmd.find("filled"))     it.rect.filled = cmd.getBool("filled", it.rect.filled);
                if (cmd.find("hasStroke"))  it.rect.hasStroke = cmd.getBool("hasStroke", it.rect.hasStroke);
                if (cmd.find("mirrorX"))    it.rect.mirrorX = cmd.getBool("mirrorX", it.rect.mirrorX);
                if (cmd.find("rotationDeg"))it.rect.rotationDeg = static_cast<float>(cmd.getNum("rotationDeg", it.rect.rotationDeg));
                parseColor4(cmd.find("fillColor"), it.rect.fillColor);
                parseColor4(cmd.find("strokeColor"), it.rect.strokeColor);
            } else if (it.kind == VectorItemKind::Image) {
                if (cmd.find("worldWidth")) it.image.worldWidth = static_cast<float>(cmd.getNum("worldWidth", it.image.worldWidth));
                if (cmd.find("rotationDeg"))it.image.rotationDeg = static_cast<float>(cmd.getNum("rotationDeg", it.image.rotationDeg));
                parseColor4(cmd.find("tint"), it.image.tint);
            }
            host.canvas->rasterizeLayer(tiL, host.mesh);
            doSyncCanvas();
            return true;
        }

        int idx = resolveLayerIndex(*host.canvas, cmd, false);
        if (idx < 0 || idx >= static_cast<int>(host.canvas->layers.size())) {
            outErr = "set_layer_props target layer not found";
            return false;
        }
        Layer& l = host.canvas->layers[idx];
        if (cmd.find("visible")) l.visible = cmd.getBool("visible", l.visible);
        if (cmd.find("locked"))  l.locked = cmd.getBool("locked", l.locked);
        if (cmd.find("opacity")) l.opacity = std::clamp(static_cast<float>(cmd.getNum("opacity", l.opacity)), 0.0f, 1.0f);
        if (cmd.find("blend"))   l.blend = parseBlendMode(cmd.getStr("blend"), l.blend);
        if (cmd.find("newName")) l.name = cmd.getStr("newName", l.name);

        bool needRaster = false;
        if (l.kind == LayerKind::Text) {
            if (cmd.find("text"))        { l.text.text = cmd.getStr("text"); needRaster = true; }
            if (cmd.find("font"))        { auto fp = findMatchingFontPath(*host.canvas, cmd.getStr("font")); if (!fp.empty()) l.text.fontPath = fp; needRaster = true; }
            if (cmd.find("fontSizePx"))  { l.text.fontSizePx = static_cast<float>(cmd.getNum("fontSizePx", l.text.fontSizePx)); needRaster = true; }
            if (cmd.find("worldHeight")) { l.text.worldHeight = static_cast<float>(cmd.getNum("worldHeight", l.text.worldHeight)); needRaster = true; }
            if (cmd.find("outlineWidth")){ l.text.outlineWidth = static_cast<float>(cmd.getNum("outlineWidth", l.text.outlineWidth)); needRaster = true; }
            if (cmd.find("rotationDeg")) { l.text.rotationDeg = static_cast<float>(cmd.getNum("rotationDeg", l.text.rotationDeg)); needRaster = true; }
            if (parseColor4(cmd.find("fillColor"), l.text.fillColor)) needRaster = true;
            if (parseColor4(cmd.find("outlineColor"), l.text.outlineColor)) needRaster = true;
        } else if (l.kind == LayerKind::RulerStripe) {
            if (cmd.find("closed"))     { l.ruler.closed = cmd.getBool("closed", l.ruler.closed); needRaster = true; }
            if (cmd.find("filled"))     { l.ruler.filled = cmd.getBool("filled", l.ruler.filled); needRaster = true; }
            if (cmd.find("hasStroke"))  { l.ruler.hasStroke = cmd.getBool("hasStroke", l.ruler.hasStroke); needRaster = true; }
            if (cmd.find("widthPx"))    { l.ruler.widthPx = static_cast<float>(cmd.getNum("widthPx", l.ruler.widthPx)); needRaster = true; }
            if (cmd.find("worldWidth")) { l.ruler.worldWidth = static_cast<float>(cmd.getNum("worldWidth", l.ruler.worldWidth)); needRaster = true; }
            if (parseColor4(cmd.find("fillColor"), l.ruler.fillColor)) needRaster = true;
            if (parseColor4(cmd.find("strokeColor"), l.ruler.color) || parseColor4(cmd.find("color"), l.ruler.color)) needRaster = true;
        } else if (l.kind == LayerKind::Decal) {
            if (cmd.find("worldWidth"))  { l.decal.worldWidth = static_cast<float>(cmd.getNum("worldWidth", l.decal.worldWidth)); needRaster = true; }
            if (cmd.find("rotationDeg")) { l.decal.rotationDeg = static_cast<float>(cmd.getNum("rotationDeg", l.decal.rotationDeg)); needRaster = true; }
            if (parseColor4(cmd.find("tint"), l.decal.tint)) needRaster = true;
        }
        if (needRaster) {
            host.canvas->rasterizeLayer(idx, host.mesh);
        } else {
            host.canvas->recomposite();
        }
        doSyncCanvas();
        return true;
    }

    if (op == "save_project") {
        std::string path = cmd.getStr("path");
        if (path.empty()) { outErr = "save_project missing path"; return false; }
        ensureParentDir(path);
        if (!host.canvas->saveProject(path, host.uiState->activeMeshPath)) {
            outErr = "save_project failed: " + path;
            return false;
        }
        host.uiState->activeProjectPath = path;
        return true;
    }

    if (op == "load_project") {
        std::string path = resolveInputPath(cmd.getStr("path"), baseDir);
        if (path.empty()) { outErr = "load_project missing path"; return false; }
        std::string meshPath;
        if (!host.canvas->loadProject(path, meshPath)) {
            outErr = "load_project failed: " + path;
            return false;
        }
        host.uiState->activeProjectPath = path;
        host.uiState->canvasResolution = host.canvas->width;
        host.uvState->needsInitialFit = true;
        if (!meshPath.empty() && std::filesystem::exists(meshPath) && host.loadObj) {
            host.loadObj(meshPath, true);
        }
        host.canvas->rasterizeAllDirty(host.mesh);
        host.canvas->recomposite();
        doSyncCanvas();
        return true;
    }

    if (op == "export_png") {
        std::string path = cmd.getStr("path");
        if (path.empty()) { outErr = "export_png missing path"; return false; }
        int size = std::clamp(cmd.getInt("size", 4096), 256, 4096);
        ensureParentDir(path);
        if (!host.canvas->exportFlattenedPng(path, size, host.mesh)) {
            outErr = "export_png failed: " + path;
            return false;
        }
        return true;
    }

    if (op == "render_beauty") {
        std::string path = cmd.getStr("path");
        if (path.empty()) { outErr = "render_beauty missing path"; return false; }
        int w = std::clamp(cmd.getInt("width", 1920), 256, 3840);
        int h = std::clamp(cmd.getInt("height", 1080), 256, 2160);
        applyCameraArgs(host, cmd);
        doSyncCanvas();
        doSyncMesh();
        doSyncScene();
        ensureParentDir(path);
        if (!host.renderer->renderBeautyShotToPng(*host.mesh, *host.sceneParams, path, w, h)) {
            outErr = "render_beauty failed: " + host.renderer->lastError();
            return false;
        }
        return true;
    }

    if (op == "capture_ui") {
        std::string path = cmd.getStr("path");
        if (path.empty()) { outErr = "capture_ui missing path"; return false; }
        if (const JsonValue* al = cmd.find("activeLayer"); al && al->isString()) {
            JsonValue tmp = JsonValue::makeObject();
            tmp.set("layer", JsonValue::makeStr(al->sVal));
            int lIdx = resolveLayerIndex(*host.canvas, tmp, false);
            if (lIdx >= 0) {
                host.canvas->activeLayerIdx = lIdx;
                host.uiState->textEditSyncedLayerId = 0;
            } else {
                int tiL = -1, tiI = -1;
                if (resolveItemTarget(*host.canvas, tmp, tiL, tiI)) {
                    host.canvas->activeLayerIdx = tiL;
                    host.canvas->selectedItemIdx = tiI;
                    host.uiState->textEditSyncedLayerId = 0;
                }
            }
        }
        if (const JsonValue* ai = cmd.find("activeItem"); ai && ai->isString()) {
            JsonValue tmp = JsonValue::makeObject();
            tmp.set("item", JsonValue::makeStr(ai->sVal));
            int tiL = -1, tiI = -1;
            if (resolveItemTarget(*host.canvas, tmp, tiL, tiI)) {
                host.canvas->activeLayerIdx = tiL;
                host.canvas->selectedItemIdx = tiI;
                host.uiState->textEditSyncedLayerId = 0;
            }
        }
        if (const JsonValue* tl = cmd.find("tool"); tl && tl->isString()) {
            host.uiState->activeMode = StudioMode::TexturePaint;
            host.uiState->activeTool = parseStudioTool(tl->sVal, host.uiState->activeTool);
        }
        applyCameraArgs(host, cmd);
        doSyncCanvas();
        doSyncMesh();
        doSyncScene();
        ensureParentDir(path);
        if (!host.captureUiScreenshot || !host.captureUiScreenshot(path)) {
            outErr = "capture_ui failed: " + path;
            return false;
        }
        return true;
    }

    if (op == "get_canvas_stats") {
        outResult.set("stats", buildCanvasStats(host));
        return true;
    }

    outErr = "Unknown automation op: " + op;
    return false;
}

bool executeCommandsArray(AutomationHost& host, const JsonValue& commandsArr,
                          const std::string& baseDir, JsonValue& outSummary, std::string& outErr) {
    if (!commandsArr.isArray()) {
        outErr = "Script 'commands' must be a JSON array";
        return false;
    }
    outSummary = JsonValue::makeObject();
    outSummary.set("total", JsonValue::makeNum(static_cast<double>(commandsArr.arr.size())));
    JsonValue results = JsonValue::makeArray();

    for (size_t i = 0; i < commandsArr.arr.size(); ++i) {
        const JsonValue& cmd = commandsArr.arr[i];
        std::string op = cmd.getStr("op");
        if (op.empty()) {
            outErr = "Command #" + std::to_string(i) + " missing 'op'";
            return false;
        }
        JsonValue stepRes;
        std::string stepErr;
        if (!dispatchCommand(host, op, cmd, baseDir, stepRes, stepErr)) {
            outErr = "Step #" + std::to_string(i) + " (" + op + "): " + stepErr;
            return false;
        }
        results.push(std::move(stepRes));
    }
    outSummary.set("results", std::move(results));
    outSummary.set("ok", JsonValue::makeBool(true));
    return true;
}

JsonValue makeToolDef(const char* name, const char* desc, const char* reqField = nullptr) {
    JsonValue tool = JsonValue::makeObject();
    tool.set("name", JsonValue::makeStr(name));
    tool.set("description", JsonValue::makeStr(desc));
    JsonValue schema = JsonValue::makeObject();
    schema.set("type", JsonValue::makeStr("object"));
    schema.set("additionalProperties", JsonValue::makeBool(true));
    if (reqField) {
        JsonValue req = JsonValue::makeArray();
        req.push(JsonValue::makeStr(reqField));
        schema.set("required", std::move(req));
    }
    tool.set("inputSchema", std::move(schema));
    return tool;
}

} // namespace

bool executeAutomationScriptFile(AutomationHost& host, const std::string& jsonPath, std::string& outError) {
    std::ifstream ifs(jsonPath, std::ios::binary);
    if (!ifs) {
        outError = "Cannot open script file: " + jsonPath;
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string raw = oss.str();
    // Strip UTF-8 BOM if present
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }

    JsonValue root;
    JsonParser parser(raw);
    if (!parser.parse(root, outError)) {
        outError = "JSON parse error in " + jsonPath + ": " + outError;
        return false;
    }
    const JsonValue* cmds = root.find("commands");
    if (!cmds) {
        outError = "Script root missing 'commands' array";
        return false;
    }

    std::string baseDir = std::filesystem::path(jsonPath).parent_path().string();
    JsonValue summary;
    if (!executeCommandsArray(host, *cmds, baseDir, summary, outError)) {
        return false;
    }

    std::fprintf(stderr, "[SCRIPT OK] Executed %zu commands from %s\n", cmds->arr.size(), jsonPath.c_str());
    return true;
}

int runMcpStdioServer(AutomationHost& host) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    const std::string cwdStr = std::filesystem::current_path().string();
    std::string line;
    while (std::getline(std::cin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty()) continue;

        JsonValue req;
        std::string parseErr;
        JsonParser parser(line);
        if (!parser.parse(req, parseErr)) {
            JsonValue errResp = JsonValue::makeObject();
            errResp.set("jsonrpc", JsonValue::makeStr("2.0"));
            errResp.set("id", JsonValue::makeNull());
            JsonValue errObj = JsonValue::makeObject();
            errObj.set("code", JsonValue::makeNum(-32700));
            errObj.set("message", JsonValue::makeStr("Parse error: " + parseErr));
            errResp.set("error", std::move(errObj));
            std::string s;
            dumpJson(errResp, s);
            std::cout << s << '\n' << std::flush;
            continue;
        }

        const JsonValue* idPtr = req.find("id");
        bool isNotification = (!idPtr || idPtr->isNull());
        std::string method = req.getStr("method");

        if (isNotification) {
            continue;
        }

        JsonValue resp = JsonValue::makeObject();
        resp.set("jsonrpc", JsonValue::makeStr("2.0"));
        resp.set("id", *idPtr);

        if (method == "initialize") {
            JsonValue res = JsonValue::makeObject();
            res.set("protocolVersion", JsonValue::makeStr("2024-11-05"));
            JsonValue caps = JsonValue::makeObject();
            caps.set("tools", JsonValue::makeObject());
            res.set("capabilities", std::move(caps));
            JsonValue sInfo = JsonValue::makeObject();
            sInfo.set("name", JsonValue::makeStr("coatroom"));
            sInfo.set("version", JsonValue::makeStr("1.3.0"));
            res.set("serverInfo", std::move(sInfo));
            resp.set("result", std::move(res));
        } else if (method == "ping") {
            resp.set("result", JsonValue::makeObject());
        } else if (method == "tools/list") {
            JsonValue tools = JsonValue::makeArray();
            tools.push(makeToolDef("execute_script", "Run a Coatroom JSON script file or commands array", nullptr));
            tools.push(makeToolDef("coatroom_new_project", "Create a new 4K/2K/1K Coatroom canvas", nullptr));
            tools.push(makeToolDef("coatroom_load_obj", "Load a 3D car .obj or .fbx mesh into Coatroom", "path"));
            tools.push(makeToolDef("coatroom_load_mesh", "Load a 3D car .obj or .fbx mesh into Coatroom", "path"));
            tools.push(makeToolDef("coatroom_load_ao", "Load an AO/panel shading Multiply map", "path"));
            tools.push(makeToolDef("coatroom_bake_ao", "Bake BVH ambient occlusion & panel crevice shading", nullptr));
            tools.push(makeToolDef("coatroom_set_base_color", "Set solid RGBA color of Base Paint / target layer", nullptr));
            tools.push(makeToolDef("coatroom_add_layer", "Create a new container layer", nullptr));
            tools.push(makeToolDef("coatroom_add_paint_layer", "Create a new raster/container paint layer", nullptr));
            tools.push(makeToolDef("coatroom_delete_layer", "Delete a layer (undoable)", nullptr));
            tools.push(makeToolDef("coatroom_paint_rect_3d", "Project a 3D surface rectangle onto target layer", nullptr));
            tools.push(makeToolDef("coatroom_paint_rect_uv", "Paint a 2D UV rectangle on target layer", nullptr));
            tools.push(makeToolDef("coatroom_paint_stroke_3d", "Paint a 3D brush stroke along ray/surface points", "points"));
            tools.push(makeToolDef("coatroom_add_pen", "Add a cubic Bezier vector Pen item into active layer", "nodes"));
            tools.push(makeToolDef("coatroom_add_rect", "Add a rounded Rectangle shape item into active layer", nullptr));
            tools.push(makeToolDef("coatroom_add_text", "Add a Type/Text item into active layer", "text"));
            tools.push(makeToolDef("coatroom_add_image", "Import PNG/JPG image item into active layer", "path"));
            tools.push(makeToolDef("coatroom_add_decal", "Import PNG/JPG image decal item into active layer", "path"));
            tools.push(makeToolDef("coatroom_add_pen_layer", "Create a legacy cubic Bezier Pen layer", "nodes"));
            tools.push(makeToolDef("coatroom_add_text_layer", "Create a legacy Typography layer", "text"));
            tools.push(makeToolDef("coatroom_add_decal_layer", "Create a legacy Image decal layer", "path"));
            tools.push(makeToolDef("coatroom_copy_item", "Copy selected VectorItem to internal clipboard", nullptr));
            tools.push(makeToolDef("coatroom_cut_item", "Cut selected VectorItem to internal clipboard", nullptr));
            tools.push(makeToolDef("coatroom_paste_item", "Paste clipboard VectorItem at optional UV or 3D ray", nullptr));
            tools.push(makeToolDef("coatroom_set_mirror", "Toggle Mirror X on active VectorItem", "mirror"));
            tools.push(makeToolDef("coatroom_remove_pen_point", "Remove an anchor point from a Pen path", nullptr));
            tools.push(makeToolDef("coatroom_transform_layer_2d", "Move, scale, or rotate a layer/item in 2D keeping 3D link", "layer"));
            tools.push(makeToolDef("coatroom_reorder_layer", "Reorder a layer stack index cleanly", "layer"));
            tools.push(makeToolDef("coatroom_set_layer_props", "Update layer/item visibility, opacity, blend, or styling", "layer"));
            tools.push(makeToolDef("coatroom_undo", "Undo last Coatroom document action (200-step CoW)", nullptr));
            tools.push(makeToolDef("coatroom_redo", "Redo undone Coatroom document action", nullptr));
            tools.push(makeToolDef("coatroom_save_project", "Save editable .cr (CRM1) Coatroom project", "path"));
            tools.push(makeToolDef("coatroom_load_project", "Load editable .cr or .lps Coatroom project", "path"));
            tools.push(makeToolDef("coatroom_export_png", "Export flattened 4K/2K/1K PNG livery texture", "path"));
            tools.push(makeToolDef("coatroom_render_beauty", "Render 1920x1080 Photo PNG", "path"));
            tools.push(makeToolDef("coatroom_capture_ui", "Capture full borderless Coatroom desktop UI PNG", "path"));
            tools.push(makeToolDef("coatroom_get_canvas_stats", "Inspect canvas resolution, layers, items, and luminance stats", nullptr));

            JsonValue res = JsonValue::makeObject();
            res.set("tools", std::move(tools));
            resp.set("result", std::move(res));
        } else if (method == "tools/call") {
            const JsonValue* params = req.find("params");
            std::string toolName = params ? params->getStr("name") : "";
            JsonValue args = (params && params->find("arguments")) ? *params->find("arguments") : JsonValue::makeObject();
            if (toolName.rfind("coatroom_", 0) == 0) {
                toolName = toolName.substr(9);
            }

            bool ok = false;
            std::string callErr;
            JsonValue callOut;
            if (toolName == "execute_script") {
                if (const JsonValue* cmds = args.find("commands")) {
                    ok = executeCommandsArray(host, *cmds, cwdStr, callOut, callErr);
                } else {
                    std::string sp = args.getStr("path");
                    ok = executeAutomationScriptFile(host, sp, callErr);
                    callOut = JsonValue::makeObject();
                    callOut.set("ok", JsonValue::makeBool(ok));
                    callOut.set("path", JsonValue::makeStr(sp));
                }
            } else {
                ok = dispatchCommand(host, toolName, args, cwdStr, callOut, callErr);
            }

            JsonValue content = JsonValue::makeArray();
            JsonValue textItem = JsonValue::makeObject();
            textItem.set("type", JsonValue::makeStr("text"));
            if (ok) {
                std::string jsonStr;
                dumpJson(callOut, jsonStr);
                textItem.set("text", JsonValue::makeStr(jsonStr));
            } else {
                textItem.set("text", JsonValue::makeStr("Error: " + callErr));
            }
            content.push(std::move(textItem));

            JsonValue res = JsonValue::makeObject();
            res.set("content", std::move(content));
            res.set("isError", JsonValue::makeBool(!ok));
            resp.set("result", std::move(res));
        } else {
            JsonValue errObj = JsonValue::makeObject();
            errObj.set("code", JsonValue::makeNum(-32601));
            errObj.set("message", JsonValue::makeStr("Method not found: " + method));
            resp.set("error", std::move(errObj));
        }

        std::string outStr;
        dumpJson(resp, outStr);
        std::cout << outStr << '\n' << std::flush;
    }

    return 0;
}

} // namespace lps
