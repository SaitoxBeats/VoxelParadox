// shader_node_graph.cpp
// Data model and GLSL codegen for the ShaderEditor visual node graph.
// Emits a snippet compatible with the block shader wrapper in block_registry.cpp.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

// 2. Third-party Libraries
#include <nlohmann/json.hpp>

// 3. Local Project Modules
#include "shader_node_graph.hpp"

#pragma endregion

namespace ShaderEditor {

#pragma region 1. Pin Type Helpers

namespace {

int componentCount(PinDataType type) {
    switch (type) {
    case PinDataType::FLOAT: return 1;
    case PinDataType::VEC2: return 2;
    case PinDataType::VEC3: return 3;
    case PinDataType::VEC4: return 4;
    }
    return 1;
}

PinDataType widestType(PinDataType a, PinDataType b) {
    return componentCount(a) >= componentCount(b) ? a : b;
}

std::string glslTypeName(PinDataType type) {
    switch (type) {
    case PinDataType::FLOAT: return "float";
    case PinDataType::VEC2: return "vec2";
    case PinDataType::VEC3: return "vec3";
    case PinDataType::VEC4: return "vec4";
    }
    return "float";
}

std::string formatFloat(float value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out << std::setprecision(6) << value;
    std::string text = out.str();
    // Trim trailing zeroes but keep at least "x.0" for GLSL-friendliness.
    const auto dot = text.find('.');
    if (dot != std::string::npos) {
        std::size_t end = text.size();
        while (end > dot + 2 && text[end - 1] == '0') {
            --end;
        }
        text.resize(end);
    } else {
        text += ".0";
    }
    return text;
}

std::string vec4Literal(const glm::vec4& value) {
    return "vec4(" + formatFloat(value.x) + ", " +
           formatFloat(value.y) + ", " +
           formatFloat(value.z) + ", " +
           formatFloat(value.w) + ")";
}

std::vector<GradientStop> defaultGradientStops() {
    return {
        { 0.0f, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) },
        { 1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) },
    };
}

std::vector<GradientStop> sortedGradientStops(const std::vector<GradientStop>& stops) {
    std::vector<GradientStop> result = stops;
    for (GradientStop& stop : result) {
        stop.position = std::clamp(stop.position, 0.0f, 1.0f);
    }
    std::stable_sort(result.begin(), result.end(),
        [](const GradientStop& a, const GradientStop& b) {
            return a.position < b.position;
        });
    return result;
}

constexpr int kAppendModeVector2 = 0;
constexpr int kAppendModeVector3 = 1;
constexpr int kAppendModeVector4 = 2;
constexpr int kAppendModeColor = 3;

int clampAppendMode(int mode) {
    return std::clamp(mode, kAppendModeVector2, kAppendModeColor);
}

PinDataType appendOutputTypeForMode(int mode) {
    switch (clampAppendMode(mode)) {
    case kAppendModeVector2: return PinDataType::VEC2;
    case kAppendModeVector3: return PinDataType::VEC3;
    case kAppendModeVector4: return PinDataType::VEC4;
    case kAppendModeColor:   return PinDataType::VEC3;
    }
    return PinDataType::VEC2;
}

const char* appendOutputTypeNameForMode(int mode) {
    switch (clampAppendMode(mode)) {
    case kAppendModeVector2: return "Vector2";
    case kAppendModeVector3: return "Vector3";
    case kAppendModeVector4: return "Vector4";
    case kAppendModeColor:   return "Color";
    }
    return "Vector2";
}

void syncAppendNodeOutput(Node& node) {
    node.enumOption = clampAppendMode(node.enumOption);
    if (!node.outputs.empty()) {
        node.outputs.front().type = appendOutputTypeForMode(node.enumOption);
    }
}

constexpr int kBlendModeNormal = 0;
constexpr int kBlendModeMultiply = 1;
constexpr int kBlendModeScreen = 2;
constexpr int kBlendModeOverlay = 3;
constexpr int kBlendModeDarken = 4;
constexpr int kBlendModeLighten = 5;
constexpr int kBlendModeColorBurn = 6;
constexpr int kBlendModeColorDodge = 7;
constexpr int kBlendModeLinearBurn = 8;
constexpr int kBlendModeLinearDodge = 9;
constexpr int kBlendModeDifference = 10;
constexpr int kBlendModeExclusion = 11;
constexpr int kBlendModeHardLight = 12;
constexpr int kBlendModeSoftLight = 13;
constexpr int kBlendModeSubtract = 14;
constexpr int kBlendModeDivide = 15;

constexpr std::array<const char*, 16> kBlendOperationNames = {
    "Normal",
    "Multiply",
    "Screen",
    "Overlay",
    "Darken",
    "Lighten",
    "Color Burn",
    "Color Dodge",
    "Linear Burn",
    "Linear Dodge (Add)",
    "Difference",
    "Exclusion",
    "Hard Light",
    "Soft Light",
    "Subtract",
    "Divide",
};

int clampBlendMode(int mode) {
    return std::clamp(mode, kBlendModeNormal, kBlendModeDivide);
}

const std::array<const char*, 16>& blendOperationNamesForMode() {
    return kBlendOperationNames;
}

const char* blendOperationNameForMode(int mode) {
    return blendOperationNamesForMode()[clampBlendMode(mode)];
}

constexpr int kVoronoiMethodCells = 0;
constexpr int kVoronoiMethodDistance = 1;
constexpr int kVoronoiMethodBorders = 2;
constexpr int kVoronoiMethodSmooth = 3;

constexpr int kVoronoiDistanceEuclidian2 = 0;
constexpr int kVoronoiDistanceEuclidian = 1;
constexpr int kVoronoiDistanceManhattan = 2;
constexpr int kVoronoiDistanceChebyshev = 3;

constexpr int kVoronoiMethodMask = 0x3;
constexpr int kVoronoiTileableMask = 0x10;
constexpr int kVoronoiSmoothMask = 0x20;

constexpr std::array<const char*, 4> kVoronoiMethodNames = {
    "Cells",
    "Distance",
    "Borders",
    "Smooth",
};

constexpr std::array<const char*, 4> kVoronoiDistanceNames = {
    "Euclidian2",
    "Euclidian",
    "Manhattan",
    "Chebyshev",
};

constexpr std::array<const char*, 4> kVoronoiSearchQualityNames = {
    "1 Cell",
    "4 Cells",
    "9 Cells",
    "16 Cells",
};

int clampVoronoiMethod(int mode) {
    return std::clamp(mode, kVoronoiMethodCells, kVoronoiMethodSmooth);
}

int clampVoronoiDistance(int mode) {
    return std::clamp(mode, kVoronoiDistanceEuclidian2, kVoronoiDistanceChebyshev);
}

int clampVoronoiSearchQuality(int mode) {
    return std::clamp(mode, 0, static_cast<int>(kVoronoiSearchQualityNames.size()) - 1);
}

int clampVoronoiOctaves(int mode) {
    return std::clamp(mode, 1, 8);
}

int voronoiMethodFromOption(int option) {
    return option & kVoronoiMethodMask;
}

int voronoiDistanceFromOption(int option) {
    return (option >> 2) & 0x3;
}

bool voronoiTileableFromOption(int option) {
    return (option & kVoronoiTileableMask) != 0;
}

bool voronoiSmoothFromOption(int option) {
    return (option & kVoronoiSmoothMask) != 0;
}

int voronoiOptionFor(int method, int distance, bool tileable, bool smooth) {
    int option = (clampVoronoiMethod(method) & kVoronoiMethodMask) |
                 ((clampVoronoiDistance(distance) & 0x3) << 2);
    if (tileable) {
        option |= kVoronoiTileableMask;
    }
    if (smooth) {
        option |= kVoronoiSmoothMask;
    }
    return option;
}

std::string voronoiDistanceExpressionForMode(int mode, const std::string& deltaExpr) {
    const std::string delta = "(" + deltaExpr + ")";
    switch (clampVoronoiDistance(mode)) {
    case kVoronoiDistanceEuclidian2:
        return "dot(" + delta + ", " + delta + ")";
    case kVoronoiDistanceEuclidian:
        return "length(" + delta + ")";
    case kVoronoiDistanceManhattan:
        return "abs(" + delta + ").x + abs(" + delta + ").y";
    case kVoronoiDistanceChebyshev:
        return "max(abs(" + delta + ").x, abs(" + delta + ").y)";
    }
    return "length(" + delta + ")";
}

std::string voronoiDistanceNormalizationForMode(int mode, int radius) {
    const float span = static_cast<float>(std::max(radius + 1, 1));
    switch (clampVoronoiDistance(mode)) {
    case kVoronoiDistanceEuclidian2:
        return formatFloat(2.0f * span * span);
    case kVoronoiDistanceEuclidian:
        return formatFloat(1.41421356f * span);
    case kVoronoiDistanceManhattan:
        return formatFloat(2.0f * span);
    case kVoronoiDistanceChebyshev:
        return formatFloat(span);
    }
    return formatFloat(span);
}

std::string blendOperationExpressionForMode(int mode,
                                            const std::string& sourceExpr,
                                            const std::string& blendExpr) {
    const std::string source = "(" + sourceExpr + ")";
    const std::string blend = "(" + blendExpr + ")";

    switch (clampBlendMode(mode)) {
    case kBlendModeNormal:
        return blend;
    case kBlendModeMultiply:
        return source + " * " + blend;
    case kBlendModeScreen:
        return "vec3(1.0) - (vec3(1.0) - " + source + ") * (vec3(1.0) - " + blend + ")";
    case kBlendModeOverlay:
        return "mix(2.0 * " + source + " * " + blend + ", vec3(1.0) - 2.0 * "
               "(vec3(1.0) - " + source + ") * (vec3(1.0) - " + blend + "), "
               "step(vec3(0.5), " + source + "))";
    case kBlendModeDarken:
        return "min(" + source + ", " + blend + ")";
    case kBlendModeLighten:
        return "max(" + source + ", " + blend + ")";
    case kBlendModeColorBurn:
        return "vec3(1.0) - (vec3(1.0) - " + source + ") / max(" + blend + ", vec3(0.00001))";
    case kBlendModeColorDodge:
        return source + " / max(vec3(1.0) - " + blend + ", vec3(0.00001))";
    case kBlendModeLinearBurn:
        return source + " + " + blend + " - vec3(1.0)";
    case kBlendModeLinearDodge:
        return source + " + " + blend;
    case kBlendModeDifference:
        return "abs(" + source + " - " + blend + ")";
    case kBlendModeExclusion:
        return source + " + " + blend + " - 2.0 * " + source + " * " + blend;
    case kBlendModeHardLight:
        return "mix(2.0 * " + source + " * " + blend + ", vec3(1.0) - 2.0 * "
               "(vec3(1.0) - " + source + ") * (vec3(1.0) - " + blend + "), "
               "step(vec3(0.5), " + blend + "))";
    case kBlendModeSoftLight:
        return "mix(" + source + " - (vec3(1.0) - 2.0 * " + blend + ") * " + source +
               " * (vec3(1.0) - " + source + "), " + source +
               " + (2.0 * " + blend + " - vec3(1.0)) * "
               "(sqrt(max(" + source + ", vec3(0.0))) - " + source + "), "
               "step(vec3(0.5), " + blend + "))";
    case kBlendModeSubtract:
        return source + " - " + blend;
    case kBlendModeDivide:
        return source + " / max(" + blend + ", vec3(0.00001))";
    }
    return blend;
}

// Emit a GLSL literal for the value stored in an unconnected input pin.
std::string literalForPin(const InputPin& pin) {
    switch (pin.type) {
    case PinDataType::FLOAT:
        return formatFloat(pin.defaultValue.x);
    case PinDataType::VEC2:
        return "vec2(" + formatFloat(pin.defaultValue.x) + ", " +
               formatFloat(pin.defaultValue.y) + ")";
    case PinDataType::VEC3:
        return "vec3(" + formatFloat(pin.defaultValue.x) + ", " +
               formatFloat(pin.defaultValue.y) + ", " +
               formatFloat(pin.defaultValue.z) + ")";
    case PinDataType::VEC4:
        return "vec4(" + formatFloat(pin.defaultValue.x) + ", " +
               formatFloat(pin.defaultValue.y) + ", " +
               formatFloat(pin.defaultValue.z) + ", " +
               formatFloat(pin.defaultValue.w) + ")";
    }
    return "0.0";
}

// Convert a GLSL expression of `from` type to `to` type with the least surprise.
std::string convertExpression(const std::string& expr, PinDataType from, PinDataType to) {
    if (from == to) {
        return expr;
    }

    const int fromC = componentCount(from);
    const int toC = componentCount(to);

    if (fromC == 1) {
        // Scalar splat.
        return glslTypeName(to) + "(" + expr + ")";
    }

    if (fromC >= toC) {
        static const char* kSuffix[] = { "", ".x", ".xy", ".xyz", "" };
        if (toC == 1) return "(" + expr + ").x";
        if (toC == 2) return "(" + expr + ").xy";
        if (toC == 3) return "(" + expr + ").xyz";
        return expr;
    }

    // fromC < toC (and fromC > 1): widen by zero-padding through explicit constructor.
    std::string base = "(" + expr + ")";
    switch (to) {
    case PinDataType::VEC3:
        if (from == PinDataType::VEC2) {
            return "vec3(" + base + ", 0.0)";
        }
        break;
    case PinDataType::VEC4:
        if (from == PinDataType::VEC2) {
            return "vec4(" + base + ", 0.0, 1.0)";
        }
        if (from == PinDataType::VEC3) {
            return "vec4(" + base + ", 1.0)";
        }
        break;
    default: break;
    }
    return glslTypeName(to) + "(" + base + ")";
}

} // namespace

#pragma endregion

#pragma region 2. Node Catalog

// Per-kind configuration of inputs/outputs/defaults. Keep in one table so the UI,
// codegen, and serializer all agree on node shape.
void ShaderNodeGraph::configureNode(Node& node) {
    node.inputs.clear();
    node.outputs.clear();

    auto makeIn = [](const char* name, PinDataType type, glm::vec4 def = glm::vec4(0.0f)) {
        InputPin p;
        p.name = name;
        p.type = type;
        p.defaultValue = def;
        return p;
    };
    auto makeOut = [](const char* name, PinDataType type) {
        OutputPin p;
        p.name = name;
        p.type = type;
        return p;
    };

    switch (node.kind) {
    case NodeKind::IN_BASE_COLOR:      node.outputs.push_back(makeOut("base", PinDataType::VEC3)); break;
    case NodeKind::IN_UV:              node.outputs.push_back(makeOut("uv", PinDataType::VEC2)); break;
    case NodeKind::IN_LOCAL_UV:        node.outputs.push_back(makeOut("localUv", PinDataType::VEC2)); break;
    case NodeKind::IN_CENTERED_UV:     node.outputs.push_back(makeOut("centeredUv", PinDataType::VEC2)); break;
    case NodeKind::IN_WORLD_POS:       node.outputs.push_back(makeOut("worldPos", PinDataType::VEC3)); break;
    case NodeKind::IN_WORLD_NORMAL:    node.outputs.push_back(makeOut("worldNormal", PinDataType::VEC3)); break;
    case NodeKind::IN_FACE_NORMAL:     node.outputs.push_back(makeOut("faceNormal", PinDataType::VEC3)); break;
    case NodeKind::IN_VIEW_DIR:        node.outputs.push_back(makeOut("viewDir", PinDataType::VEC3)); break;
    case NodeKind::IN_BLOCK_COLOR:     node.outputs.push_back(makeOut("blockColor", PinDataType::VEC3)); break;
    case NodeKind::IN_BLOCK_ALPHA:     node.outputs.push_back(makeOut("blockAlpha", PinDataType::FLOAT)); break;
    case NodeKind::IN_BLOCK_TEXEL:
        // Optional UV override; leave unconnected to sample with the block's
        // native localUv (i.e. the precomputed blockTexel local).
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.outputs.push_back(makeOut("blockTexel", PinDataType::VEC4));
        break;
    case NodeKind::IN_CELL:            node.outputs.push_back(makeOut("cell", PinDataType::VEC3)); break;
    case NodeKind::IN_CELL_HASH:       node.outputs.push_back(makeOut("cellHash", PinDataType::FLOAT)); break;
    case NodeKind::IN_TIME:
        node.inputs.push_back(makeIn("Scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("uTime", PinDataType::FLOAT));
        break;
    case NodeKind::IN_BIOME_TINT:      node.outputs.push_back(makeOut("biomeTint", PinDataType::VEC3)); break;

    case NodeKind::CONST_FLOAT:
        node.outputs.push_back(makeOut("value", PinDataType::FLOAT));
        node.constValue = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        break;
    case NodeKind::CONST_VEC2:
        node.outputs.push_back(makeOut("value", PinDataType::VEC2));
        node.constValue = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        break;
    case NodeKind::CONST_VEC3:
        node.outputs.push_back(makeOut("value", PinDataType::VEC3));
        node.constValue = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        break;
    case NodeKind::CONST_COLOR:
        node.outputs.push_back(makeOut("color", PinDataType::VEC3));
        node.constValue = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        break;

    case NodeKind::TEXTURE_COORDINATES:
        node.inputs.push_back(makeIn("Tiling", PinDataType::VEC2, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("Offset", PinDataType::VEC2));
        node.outputs.push_back(makeOut("UV", PinDataType::VEC2));
        node.outputs.push_back(makeOut("U", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("V", PinDataType::FLOAT));
        break;

    case NodeKind::OP_ADD:
    case NodeKind::OP_SUB:
    case NodeKind::OP_MUL:
    case NodeKind::OP_DIV:
        node.inputs.push_back(makeIn("a", PinDataType::VEC3, glm::vec4(0.0f)));
        node.inputs.push_back(makeIn("b", PinDataType::VEC3, glm::vec4(0.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::VEC3));
        break;
    case NodeKind::OP_MIX:
        node.inputs.push_back(makeIn("a", PinDataType::VEC3, glm::vec4(0.0f)));
        node.inputs.push_back(makeIn("b", PinDataType::VEC3, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("t", PinDataType::FLOAT, glm::vec4(0.5f)));
        node.outputs.push_back(makeOut("result", PinDataType::VEC3));
        break;
    case NodeKind::OP_CLAMP:
        node.inputs.push_back(makeIn("x", PinDataType::VEC3));
        node.inputs.push_back(makeIn("min", PinDataType::VEC3, glm::vec4(0.0f)));
        node.inputs.push_back(makeIn("max", PinDataType::VEC3, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::VEC3));
        break;
    case NodeKind::OP_SATURATE:
        node.inputs.push_back(makeIn("x", PinDataType::VEC3));
        node.outputs.push_back(makeOut("result", PinDataType::VEC3));
        break;
    case NodeKind::OP_SMOOTHSTEP:
        node.inputs.push_back(makeIn("edge0", PinDataType::FLOAT, glm::vec4(0.0f)));
        node.inputs.push_back(makeIn("edge1", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("x", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::OP_POWER:
        node.inputs.push_back(makeIn("base", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("exp", PinDataType::FLOAT, glm::vec4(2.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::OP_SIN:
    case NodeKind::OP_COS:
    case NodeKind::OP_FRACT:
    case NodeKind::OP_FLOOR:
    case NodeKind::OP_ABS:
        node.inputs.push_back(makeIn("x", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::OP_STEP:
        node.inputs.push_back(makeIn("edge", PinDataType::FLOAT, glm::vec4(0.5f)));
        node.inputs.push_back(makeIn("x", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::OP_DOT:
        node.inputs.push_back(makeIn("a", PinDataType::VEC3));
        node.inputs.push_back(makeIn("b", PinDataType::VEC3));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::OP_NORMALIZE:
        node.inputs.push_back(makeIn("v", PinDataType::VEC3));
        node.outputs.push_back(makeOut("result", PinDataType::VEC3));
        break;
    case NodeKind::OP_LENGTH:
        node.inputs.push_back(makeIn("v", PinDataType::VEC3));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::OP_FRESNEL:
        node.inputs.push_back(makeIn("normal", PinDataType::VEC3));
        node.inputs.push_back(makeIn("viewDir", PinDataType::VEC3));
        node.inputs.push_back(makeIn("power", PinDataType::FLOAT, glm::vec4(4.0f)));
        // Keep the original input order stable so saved graphs keep their links.
        node.inputs.push_back(makeIn("bias", PinDataType::FLOAT, glm::vec4(0.0f)));
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::OP_GRADIENT:
        // Project a 2D position onto a segment, then sample the editable ramp.
        node.inputs.push_back(makeIn("position", PinDataType::VEC2));
        node.inputs.push_back(makeIn("start", PinDataType::VEC2));
        node.inputs.push_back(makeIn("end", PinDataType::VEC2,
                                     glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
        node.inputs.push_back(makeIn("ramp", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("color", PinDataType::VEC4));
        if (node.gradientStops.empty()) {
            node.gradientStops = defaultGradientStops();
        }
        break;

    case NodeKind::FN_HASH21:
        node.inputs.push_back(makeIn("p", PinDataType::VEC2));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::FN_HASH31:
        node.inputs.push_back(makeIn("p", PinDataType::VEC3));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::FN_NOISE21:
        node.inputs.push_back(makeIn("p", PinDataType::VEC2));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::FN_FBM21:
        node.inputs.push_back(makeIn("p", PinDataType::VEC2));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;

    case NodeKind::COMBINE_V2:
        node.inputs.push_back(makeIn("x", PinDataType::FLOAT));
        node.inputs.push_back(makeIn("y", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("v", PinDataType::VEC2));
        break;
    case NodeKind::COMBINE_V3:
        node.inputs.push_back(makeIn("x", PinDataType::FLOAT));
        node.inputs.push_back(makeIn("y", PinDataType::FLOAT));
        node.inputs.push_back(makeIn("z", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("v", PinDataType::VEC3));
        break;
    case NodeKind::SPLIT_V2:
        node.inputs.push_back(makeIn("v", PinDataType::VEC2));
        node.outputs.push_back(makeOut("x", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("y", PinDataType::FLOAT));
        break;
    case NodeKind::SPLIT_V3:
        node.inputs.push_back(makeIn("v", PinDataType::VEC3));
        node.outputs.push_back(makeOut("x", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("y", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("z", PinDataType::FLOAT));
        break;
    case NodeKind::SWIZZLE_TO_VEC3_RGB:
        node.inputs.push_back(makeIn("rgba", PinDataType::VEC4));
        node.outputs.push_back(makeOut("rgb", PinDataType::VEC3));
        break;

    case NodeKind::APPEND:
        node.inputs.push_back(makeIn("X", PinDataType::FLOAT));
        node.inputs.push_back(makeIn("Y", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("V", appendOutputTypeForMode(node.enumOption)));
        syncAppendNodeOutput(node);
        break;

    case NodeKind::BLEND_OPERATIONS:
        node.inputs.push_back(makeIn("Source", PinDataType::VEC3));
        node.inputs.push_back(makeIn("Blend", PinDataType::VEC3));
        node.inputs.push_back(makeIn("Opacity Mask", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::VEC3));
        node.enumOption = kBlendModeColorBurn;
        node.constValue = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
        break;

    case NodeKind::OUTPUT_MATERIAL:
        node.inputs.push_back(makeIn("albedo", PinDataType::VEC3, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("roughness", PinDataType::FLOAT, glm::vec4(0.8f)));
        node.inputs.push_back(makeIn("specular", PinDataType::FLOAT, glm::vec4(0.1f)));
        node.inputs.push_back(makeIn("emissive", PinDataType::FLOAT, glm::vec4(0.0f)));
        break;

    case NodeKind::FN_NOISE_SIMPLEX_2D:
        node.inputs.push_back(makeIn("p", PinDataType::VEC2));
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("01Range", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::FN_NOISE_SIMPLEX_3D:
        node.inputs.push_back(makeIn("p", PinDataType::VEC3));
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("01Range", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::FN_NOISE_GRADIENT:
        node.inputs.push_back(makeIn("p", PinDataType::VEC2));
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("01Range", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::FN_NOISE_SIMPLE:
        node.inputs.push_back(makeIn("p", PinDataType::VEC2));
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("01Range", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;
    case NodeKind::FN_VORONOI:
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.inputs.push_back(makeIn("angle", PinDataType::FLOAT, glm::vec4(0.0f)));
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(10.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        node.enumOption = voronoiOptionFor(kVoronoiMethodCells, kVoronoiDistanceEuclidian2,
                                           false, false);
        // constValue.z = search quality (radius), constValue.w = fbm octaves.
        node.constValue = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
        node.previewEnabled = true;
        break;

    case NodeKind::FN_TRUCHET_MAZE:
    case NodeKind::FN_TRUCHET_CIRCLES:
    case NodeKind::FN_TRUCHET_TRIANGLES:
        node.inputs.push_back(makeIn("p", PinDataType::VEC2));
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("resolution", PinDataType::FLOAT, glm::vec4(4.0f)));
        node.outputs.push_back(makeOut("result", PinDataType::FLOAT));
        break;

    case NodeKind::FX_DISTORT_UV:
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.inputs.push_back(makeIn("strength", PinDataType::FLOAT, glm::vec4(0.1f)));
        node.inputs.push_back(makeIn("time", PinDataType::FLOAT, glm::vec4(0.0f)));
        node.outputs.push_back(makeOut("uv", PinDataType::VEC2));
        break;
    case NodeKind::FX_SWIRL:
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.inputs.push_back(makeIn("strength", PinDataType::FLOAT, glm::vec4(2.0f)));
        node.outputs.push_back(makeOut("uv", PinDataType::VEC2));
        break;
    case NodeKind::FX_RIPPLE:
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.inputs.push_back(makeIn("frequency", PinDataType::FLOAT, glm::vec4(18.0f)));
        node.inputs.push_back(makeIn("amplitude", PinDataType::FLOAT, glm::vec4(0.02f)));
        node.inputs.push_back(makeIn("time", PinDataType::FLOAT, glm::vec4(0.0f)));
        node.outputs.push_back(makeOut("uv", PinDataType::VEC2));
        break;
    case NodeKind::FX_RADIAL_SHEAR:
        // Shear UVs more strongly as they move farther from the chosen center.
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.inputs.push_back(makeIn("center", PinDataType::VEC2, glm::vec4(0.5f)));
        node.inputs.push_back(makeIn("strength", PinDataType::FLOAT, glm::vec4(0.25f)));
        node.inputs.push_back(makeIn("offset", PinDataType::FLOAT, glm::vec4(0.0f)));
        node.outputs.push_back(makeOut("uv", PinDataType::VEC2));
        break;
    case NodeKind::FX_PANNER:
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.inputs.push_back(makeIn("speed", PinDataType::VEC2,
                                     glm::vec4(0.1f, 0.1f, 0.0f, 0.0f)));
        node.inputs.push_back(makeIn("time", PinDataType::FLOAT, glm::vec4(0.0f)));
        node.outputs.push_back(makeOut("uv", PinDataType::VEC2));
        break;
    case NodeKind::FX_PIXELATE_UV:
        node.inputs.push_back(makeIn("UV", PinDataType::VEC2));
        node.inputs.push_back(makeIn("Pixels X", PinDataType::FLOAT, glm::vec4(16.0f)));
        node.inputs.push_back(makeIn("Pixels Y", PinDataType::FLOAT, glm::vec4(16.0f)));
        node.outputs.push_back(makeOut("uv", PinDataType::VEC2));
        break;
    case NodeKind::FX_ROTATOR:
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.inputs.push_back(makeIn("anchor", PinDataType::VEC2, glm::vec4(0.5f)));
        node.inputs.push_back(makeIn("time", PinDataType::FLOAT, glm::vec4(0.0f)));
        node.outputs.push_back(makeOut("uv", PinDataType::VEC2));
        break;

    case NodeKind::PRESET_STONE_ROUGH:
        node.inputs.push_back(makeIn("color", PinDataType::VEC3,
                                     glm::vec4(0.45f, 0.42f, 0.50f, 1.0f)));
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.outputs.push_back(makeOut("albedo", PinDataType::VEC3));
        node.outputs.push_back(makeOut("roughness", PinDataType::FLOAT));
        break;
    case NodeKind::PRESET_STONE_CRACKED:
        node.inputs.push_back(makeIn("color", PinDataType::VEC3,
                                     glm::vec4(0.55f, 0.50f, 0.45f, 1.0f)));
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("cracks", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.inputs.push_back(makeIn("uv", PinDataType::VEC2));
        node.outputs.push_back(makeOut("albedo", PinDataType::VEC3));
        node.outputs.push_back(makeOut("roughness", PinDataType::FLOAT));
        node.outputs.push_back(makeOut("cracks", PinDataType::FLOAT));
        break;

    case NodeKind::SEAMLESS_UV:
        // Projects worldPos onto the face plane via faceNormal so the UV is
        // continuous across block boundaries — no seams at block edges.
        node.inputs.push_back(makeIn("scale", PinDataType::FLOAT, glm::vec4(1.0f)));
        node.outputs.push_back(makeOut("uv", PinDataType::VEC2));
        break;

    case NodeKind::COUNT_:
        break;
    }
}

const char* ShaderNodeGraph::nodeDisplayName(NodeKind kind) {
    switch (kind) {
    case NodeKind::IN_BASE_COLOR: return "Base Color (base)";
    case NodeKind::IN_UV: return "UV";
    case NodeKind::IN_LOCAL_UV: return "Local UV";
    case NodeKind::IN_CENTERED_UV: return "Centered UV";
    case NodeKind::IN_WORLD_POS: return "World Position";
    case NodeKind::IN_WORLD_NORMAL: return "World Normal";
    case NodeKind::IN_FACE_NORMAL: return "Face Normal";
    case NodeKind::IN_VIEW_DIR: return "View Dir";
    case NodeKind::IN_BLOCK_COLOR: return "Block Color (texel.rgb)";
    case NodeKind::IN_BLOCK_ALPHA: return "Block Alpha";
    case NodeKind::IN_BLOCK_TEXEL: return "Block Texel (rgba)";
    case NodeKind::IN_CELL: return "Cell";
    case NodeKind::IN_CELL_HASH: return "Cell Hash";
    case NodeKind::IN_TIME: return "Time (uTime)";
    case NodeKind::IN_BIOME_TINT: return "Biome Tint";
    case NodeKind::CONST_FLOAT: return "Float";
    case NodeKind::CONST_VEC2: return "Vec2";
    case NodeKind::CONST_VEC3: return "Vec3";
    case NodeKind::CONST_COLOR: return "Color";
    case NodeKind::OP_ADD: return "Add";
    case NodeKind::OP_SUB: return "Subtract";
    case NodeKind::OP_MUL: return "Multiply";
    case NodeKind::OP_DIV: return "Divide";
    case NodeKind::OP_MIX: return "Mix (lerp)";
    case NodeKind::OP_CLAMP: return "Clamp";
    case NodeKind::OP_SATURATE: return "Saturate";
    case NodeKind::OP_SMOOTHSTEP: return "Smoothstep";
    case NodeKind::OP_POWER: return "Power";
    case NodeKind::OP_SIN: return "Sin";
    case NodeKind::OP_COS: return "Cos";
    case NodeKind::OP_FRACT: return "Fract";
    case NodeKind::OP_FLOOR: return "Floor";
    case NodeKind::OP_ABS: return "Abs";
    case NodeKind::OP_STEP: return "Step";
    case NodeKind::OP_DOT: return "Dot";
    case NodeKind::OP_NORMALIZE: return "Normalize";
    case NodeKind::OP_LENGTH: return "Length";
    case NodeKind::OP_FRESNEL: return "Fresnel";
    case NodeKind::OP_GRADIENT: return "Gradient";
    case NodeKind::FN_HASH21: return "Hash21";
    case NodeKind::FN_HASH31: return "Hash31";
    case NodeKind::FN_NOISE21: return "Noise21";
    case NodeKind::FN_FBM21: return "FBM21";
    case NodeKind::COMBINE_V2: return "Combine Vec2";
    case NodeKind::COMBINE_V3: return "Combine Vec3";
    case NodeKind::SPLIT_V2: return "Split Vec2";
    case NodeKind::SPLIT_V3: return "Split Vec3";
    case NodeKind::SWIZZLE_TO_VEC3_RGB: return "Vec4 -> RGB";
    case NodeKind::OUTPUT_MATERIAL: return "Material Output";
    case NodeKind::FN_NOISE_SIMPLEX_2D: return "Simplex Noise 2D";
    case NodeKind::FN_NOISE_SIMPLEX_3D: return "Simplex Noise 3D";
    case NodeKind::FN_NOISE_GRADIENT: return "Gradient Noise";
    case NodeKind::FN_NOISE_SIMPLE: return "Simple Noise";
    case NodeKind::FN_VORONOI: return "Voronoi";
    case NodeKind::FN_TRUCHET_MAZE: return "Truchet Maze";
    case NodeKind::FN_TRUCHET_CIRCLES: return "Truchet Circles";
    case NodeKind::FN_TRUCHET_TRIANGLES: return "Truchet Triangles";
    case NodeKind::FX_DISTORT_UV: return "Distort UV (fbm)";
    case NodeKind::FX_SWIRL: return "Swirl UV";
    case NodeKind::FX_RIPPLE: return "Ripple UV";
    case NodeKind::FX_RADIAL_SHEAR: return "Radial Shear";
    case NodeKind::FX_PANNER: return "Panner";
    case NodeKind::FX_PIXELATE_UV: return "Pixelate UV";
    case NodeKind::FX_ROTATOR: return "Rotator";
    case NodeKind::TEXTURE_COORDINATES: return "Texture Coordinates";
    case NodeKind::APPEND: return "Append";
    case NodeKind::BLEND_OPERATIONS: return "Blend Operations";
    case NodeKind::PRESET_STONE_ROUGH: return "Stone Rough (preset)";
    case NodeKind::PRESET_STONE_CRACKED: return "Stone Cracked (preset)";
    case NodeKind::SEAMLESS_UV: return "Seamless UV";
    case NodeKind::COUNT_: break;
    }
    return "?";
}

const char* ShaderNodeGraph::nodeCategory(NodeKind kind) {
    switch (kind) {
    case NodeKind::IN_BASE_COLOR:
    case NodeKind::IN_UV:
    case NodeKind::IN_LOCAL_UV:
    case NodeKind::IN_CENTERED_UV:
    case NodeKind::IN_WORLD_POS:
    case NodeKind::IN_WORLD_NORMAL:
    case NodeKind::IN_FACE_NORMAL:
    case NodeKind::IN_VIEW_DIR:
    case NodeKind::IN_BLOCK_COLOR:
    case NodeKind::IN_BLOCK_ALPHA:
    case NodeKind::IN_BLOCK_TEXEL:
    case NodeKind::IN_CELL:
    case NodeKind::IN_CELL_HASH:
    case NodeKind::IN_TIME:
    case NodeKind::IN_BIOME_TINT:
    case NodeKind::TEXTURE_COORDINATES:
        return "Inputs";
    case NodeKind::CONST_FLOAT:
    case NodeKind::CONST_VEC2:
    case NodeKind::CONST_VEC3:
    case NodeKind::CONST_COLOR:
        return "Constants";
    case NodeKind::OP_ADD:
    case NodeKind::OP_SUB:
    case NodeKind::OP_MUL:
    case NodeKind::OP_DIV:
    case NodeKind::OP_MIX:
    case NodeKind::OP_CLAMP:
    case NodeKind::OP_SATURATE:
    case NodeKind::OP_SMOOTHSTEP:
    case NodeKind::OP_POWER:
    case NodeKind::OP_SIN:
    case NodeKind::OP_COS:
    case NodeKind::OP_FRACT:
    case NodeKind::OP_FLOOR:
    case NodeKind::OP_ABS:
    case NodeKind::OP_STEP:
    case NodeKind::OP_DOT:
    case NodeKind::OP_NORMALIZE:
    case NodeKind::OP_LENGTH:
    case NodeKind::OP_FRESNEL:
    case NodeKind::OP_GRADIENT:
        return "Math";
    case NodeKind::FN_HASH21:
    case NodeKind::FN_HASH31:
    case NodeKind::FN_NOISE21:
    case NodeKind::FN_FBM21:
    case NodeKind::FN_NOISE_SIMPLEX_2D:
    case NodeKind::FN_NOISE_SIMPLEX_3D:
    case NodeKind::FN_NOISE_GRADIENT:
    case NodeKind::FN_NOISE_SIMPLE:
    case NodeKind::FN_VORONOI:
        return "Noise";
    case NodeKind::FN_TRUCHET_MAZE:
    case NodeKind::FN_TRUCHET_CIRCLES:
    case NodeKind::FN_TRUCHET_TRIANGLES:
        return "Truchet";
    case NodeKind::FX_DISTORT_UV:
    case NodeKind::FX_SWIRL:
    case NodeKind::FX_RIPPLE:
    case NodeKind::FX_RADIAL_SHEAR:
    case NodeKind::FX_PANNER:
    case NodeKind::FX_PIXELATE_UV:
    case NodeKind::FX_ROTATOR:
    case NodeKind::BLEND_OPERATIONS:
        return "Effects";
    case NodeKind::COMBINE_V2:
    case NodeKind::COMBINE_V3:
    case NodeKind::SPLIT_V2:
    case NodeKind::SPLIT_V3:
    case NodeKind::SWIZZLE_TO_VEC3_RGB:
    case NodeKind::APPEND:
        return "Channels";
    case NodeKind::PRESET_STONE_ROUGH:
    case NodeKind::PRESET_STONE_CRACKED:
        return "Presets";
    case NodeKind::SEAMLESS_UV:
        return "Effects";
    case NodeKind::OUTPUT_MATERIAL:
        return "Output";
    case NodeKind::COUNT_:
        break;
    }
    return "Misc";
}

const char* ShaderNodeGraph::pinTypeName(PinDataType type) {
    switch (type) {
    case PinDataType::FLOAT: return "float";
    case PinDataType::VEC2: return "vec2";
    case PinDataType::VEC3: return "vec3";
    case PinDataType::VEC4: return "vec4";
    }
    return "?";
}

PinDataType ShaderNodeGraph::appendOutputType(int mode) {
    return appendOutputTypeForMode(mode);
}

const char* ShaderNodeGraph::appendOutputTypeName(int mode) {
    return appendOutputTypeNameForMode(mode);
}

const std::array<const char*, 16>& ShaderNodeGraph::blendOperationNames() {
    return blendOperationNamesForMode();
}

const char* ShaderNodeGraph::blendOperationName(int mode) {
    return blendOperationNameForMode(mode);
}

const std::array<const char*, 4>& ShaderNodeGraph::voronoiMethodNames() {
    return kVoronoiMethodNames;
}

const char* ShaderNodeGraph::voronoiMethodName(int mode) {
    return voronoiMethodNames()[clampVoronoiMethod(mode)];
}

const std::array<const char*, 4>& ShaderNodeGraph::voronoiDistanceNames() {
    return kVoronoiDistanceNames;
}

const char* ShaderNodeGraph::voronoiDistanceName(int mode) {
    return voronoiDistanceNames()[clampVoronoiDistance(mode)];
}

const std::array<const char*, 4>& ShaderNodeGraph::voronoiSearchQualityNames() {
    return kVoronoiSearchQualityNames;
}

const char* ShaderNodeGraph::voronoiSearchQualityName(int mode) {
    return voronoiSearchQualityNames()[clampVoronoiSearchQuality(mode)];
}

#pragma endregion

#pragma region 3. Graph Mutation

ShaderNodeGraph::ShaderNodeGraph() {
    resetToDefault();
}

void ShaderNodeGraph::resetToDefault() {
    nodes_.clear();
    links_.clear();
    nextNodeId_ = 1;
    nextLinkId_ = 1;

    const int baseNode = addNode(NodeKind::IN_BASE_COLOR, glm::vec2(-280.0f, -120.0f));
    const int texelNode = addNode(NodeKind::IN_BLOCK_COLOR, glm::vec2(-280.0f, -20.0f));
    const int mulNode = addNode(NodeKind::OP_MUL, glm::vec2(-40.0f, -80.0f));
    const int outNode = addNode(NodeKind::OUTPUT_MATERIAL, glm::vec2(220.0f, -80.0f));

    auto connectHelper = [&](int fromNode, int fromPin, int toNode, int toPin) {
        PinRef a{ fromNode, fromPin };
        PinRef b{ toNode, toPin };
        connect(a, b);
    };

    connectHelper(baseNode, 0, mulNode, 0);
    connectHelper(texelNode, 0, mulNode, 1);
    connectHelper(mulNode, 0, outNode, 0);
}

int ShaderNodeGraph::addNode(NodeKind kind, const glm::vec2& position) {
    Node node;
    node.id = nextNodeId_++;
    node.kind = kind;
    node.position = position;
    configureNode(node);
    nodes_.push_back(std::move(node));
    return nodes_.back().id;
}

void ShaderNodeGraph::removeNode(int nodeId) {
    // OUTPUT_MATERIAL is load-bearing — refuse to delete the final one.
    int outputCount = 0;
    for (const Node& n : nodes_) {
        if (n.kind == NodeKind::OUTPUT_MATERIAL) ++outputCount;
    }
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [&](const Node& n) { return n.id == nodeId; });
    if (it == nodes_.end()) return;
    if (it->kind == NodeKind::OUTPUT_MATERIAL && outputCount <= 1) return;

    // Drop any link referencing this node.
    links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const Link& link) {
        return link.from.nodeId == nodeId || link.to.nodeId == nodeId;
    }), links_.end());

    // Clear the input-side bookkeeping on remaining nodes.
    for (Node& n : nodes_) {
        for (InputPin& pin : n.inputs) {
            if (pin.connectedLinkId > 0) {
                const Link* link = findLink(pin.connectedLinkId);
                if (!link) pin.connectedLinkId = -1;
            }
        }
    }

    nodes_.erase(it);
}

bool ShaderNodeGraph::connect(const PinRef& from, const PinRef& to) {
    if (!from.valid() || !to.valid()) return false;
    Node* fromNode = findNode(from.nodeId);
    Node* toNode = findNode(to.nodeId);
    if (!fromNode || !toNode) return false;
    if (from.pinIndex >= static_cast<int>(fromNode->outputs.size())) return false;
    if (to.pinIndex >= static_cast<int>(toNode->inputs.size())) return false;
    if (fromNode == toNode) return false;

    // Replace any existing connection on the target input.
    disconnectInput(to.nodeId, to.pinIndex);

    Link link;
    link.id = nextLinkId_++;
    link.from = from;
    link.to = to;
    links_.push_back(link);

    toNode->inputs[to.pinIndex].connectedLinkId = link.id;
    return true;
}

void ShaderNodeGraph::disconnect(int linkId) {
    auto it = std::find_if(links_.begin(), links_.end(),
        [&](const Link& link) { return link.id == linkId; });
    if (it == links_.end()) return;

    Node* toNode = findNode(it->to.nodeId);
    if (toNode && it->to.pinIndex < static_cast<int>(toNode->inputs.size())) {
        toNode->inputs[it->to.pinIndex].connectedLinkId = -1;
    }
    links_.erase(it);
}

void ShaderNodeGraph::disconnectInput(int nodeId, int inputIndex) {
    Node* node = findNode(nodeId);
    if (!node || inputIndex >= static_cast<int>(node->inputs.size())) return;
    const int linkId = node->inputs[inputIndex].connectedLinkId;
    if (linkId > 0) {
        disconnect(linkId);
    }
}

void ShaderNodeGraph::clear() {
    nodes_.clear();
    links_.clear();
    nextNodeId_ = 1;
    nextLinkId_ = 1;
}

Node* ShaderNodeGraph::findNode(int nodeId) {
    for (Node& n : nodes_) {
        if (n.id == nodeId) return &n;
    }
    return nullptr;
}

const Node* ShaderNodeGraph::findNode(int nodeId) const {
    for (const Node& n : nodes_) {
        if (n.id == nodeId) return &n;
    }
    return nullptr;
}

const Link* ShaderNodeGraph::findLink(int linkId) const {
    for (const Link& l : links_) {
        if (l.id == linkId) return &l;
    }
    return nullptr;
}

int ShaderNodeGraph::outputNodeId() const {
    // The most recently added OUTPUT_MATERIAL wins; the graph should have exactly one.
    for (auto it = nodes_.rbegin(); it != nodes_.rend(); ++it) {
        if (it->kind == NodeKind::OUTPUT_MATERIAL) return it->id;
    }
    return -1;
}

#pragma endregion

#pragma region 4. Codegen

namespace {

// Emit a GLSL expression that returns the value of `outputPin` of `node` and
// assign it to a new temporary. Results are cached per (nodeId, outputIndex).
struct Codegen {
    const ShaderNodeGraph& graph;
    std::ostringstream body;
    std::unordered_map<std::uint64_t, std::string> cache;
    std::unordered_set<int> visiting;
    int tempCounter = 0;
    std::string error;

    static std::uint64_t key(int nodeId, int outputIndex) {
        return (static_cast<std::uint64_t>(nodeId) << 16) |
               static_cast<std::uint32_t>(outputIndex & 0xFFFF);
    }

    std::string newTemp(const char* prefix = "v") {
        std::ostringstream out;
        out << prefix << "_" << (tempCounter++);
        return out.str();
    }

    // Resolve the GLSL expression for an input pin, inserting the upstream
    // subtree and applying a type conversion if needed.
    std::string resolveInput(const Node& node, int inputIndex) {
        const InputPin& pin = node.inputs[inputIndex];
        if (pin.connectedLinkId > 0) {
            const Link* link = graph.findLink(pin.connectedLinkId);
            if (!link) {
                return literalForPin(pin);
            }
            std::string upstream = emit(link->from.nodeId, link->from.pinIndex);
            const Node* fromNode = graph.findNode(link->from.nodeId);
            if (!fromNode ||
                link->from.pinIndex >= static_cast<int>(fromNode->outputs.size())) {
                return literalForPin(pin);
            }
            const PinDataType fromType = fromNode->outputs[link->from.pinIndex].type;
            return convertExpression(upstream, fromType, pin.type);
        }
        return literalForPin(pin);
    }

    // Recursive emit with cycle protection.
    std::string emit(int nodeId, int outputIndex) {
        const std::uint64_t k = key(nodeId, outputIndex);
        const auto cached = cache.find(k);
        if (cached != cache.end()) {
            return cached->second;
        }

        if (visiting.count(nodeId)) {
            if (error.empty()) {
                error = "Cycle detected in node graph.";
            }
            return "0.0";
        }
        visiting.insert(nodeId);

        const Node* nodePtr = graph.findNode(nodeId);
        if (!nodePtr) {
            visiting.erase(nodeId);
            return "0.0";
        }
        const Node& node = *nodePtr;

        std::string resultExpr;
        switch (node.kind) {
        case NodeKind::IN_BASE_COLOR:     resultExpr = "base"; break;
        case NodeKind::IN_UV:             resultExpr = "uv"; break;
        case NodeKind::IN_LOCAL_UV:       resultExpr = "localUv"; break;
        case NodeKind::IN_CENTERED_UV:    resultExpr = "centeredUv"; break;
        case NodeKind::IN_WORLD_POS:      resultExpr = "worldPos"; break;
        case NodeKind::IN_WORLD_NORMAL:   resultExpr = "worldNormal"; break;
        case NodeKind::IN_FACE_NORMAL:    resultExpr = "faceNormal"; break;
        case NodeKind::IN_VIEW_DIR:       resultExpr = "viewDir"; break;
        case NodeKind::IN_BLOCK_COLOR:    resultExpr = "blockColor"; break;
        case NodeKind::IN_BLOCK_ALPHA:    resultExpr = "blockAlpha"; break;
        case NodeKind::IN_BLOCK_TEXEL: {
            // With a custom UV, re-sample the block's atlas so the node
            // produces the texel at the provided coordinates. Without a
            // connection, fall back to the precomputed local.
            if (!node.inputs.empty() && node.inputs[0].connectedLinkId > 0) {
                const std::string uvExpr = resolveInput(node, 0);
                resultExpr = "sampleBlockTexture(materialId, " + uvExpr + ")";
            } else {
                resultExpr = "blockTexel";
            }
            break;
        }
        case NodeKind::IN_CELL:           resultExpr = "cell"; break;
        case NodeKind::IN_CELL_HASH:      resultExpr = "cellHash"; break;
        case NodeKind::IN_TIME: {
            const std::string scale = resolveInput(node, 0);
            resultExpr = "uTime * (" + scale + ")";
            break;
        }
        case NodeKind::IN_BIOME_TINT:     resultExpr = "uBiomeTint.rgb"; break;

        case NodeKind::CONST_FLOAT: {
            resultExpr = formatFloat(node.constValue.x);
            break;
        }
        case NodeKind::CONST_VEC2: {
            resultExpr = "vec2(" + formatFloat(node.constValue.x) + ", " +
                         formatFloat(node.constValue.y) + ")";
            break;
        }
        case NodeKind::CONST_VEC3:
        case NodeKind::CONST_COLOR: {
            resultExpr = "vec3(" + formatFloat(node.constValue.x) + ", " +
                         formatFloat(node.constValue.y) + ", " +
                         formatFloat(node.constValue.z) + ")";
            break;
        }

        case NodeKind::OP_ADD:
        case NodeKind::OP_SUB:
        case NodeKind::OP_MUL:
        case NodeKind::OP_DIV: {
            const std::string a = resolveInput(node, 0);
            const std::string b = resolveInput(node, 1);
            const char* op =
                node.kind == NodeKind::OP_ADD ? "+" :
                node.kind == NodeKind::OP_SUB ? "-" :
                node.kind == NodeKind::OP_MUL ? "*" : "/";
            resultExpr = "(" + a + " " + op + " " + b + ")";
            break;
        }
        case NodeKind::OP_MIX: {
            const std::string a = resolveInput(node, 0);
            const std::string b = resolveInput(node, 1);
            const std::string t = resolveInput(node, 2);
            resultExpr = "mix(" + a + ", " + b + ", " + t + ")";
            break;
        }
        case NodeKind::OP_CLAMP: {
            const std::string x = resolveInput(node, 0);
            const std::string lo = resolveInput(node, 1);
            const std::string hi = resolveInput(node, 2);
            resultExpr = "clamp(" + x + ", " + lo + ", " + hi + ")";
            break;
        }
        case NodeKind::OP_SATURATE: {
            const std::string x = resolveInput(node, 0);
            resultExpr = "clamp(" + x + ", vec3(0.0), vec3(1.0))";
            break;
        }
        case NodeKind::OP_SMOOTHSTEP: {
            const std::string e0 = resolveInput(node, 0);
            const std::string e1 = resolveInput(node, 1);
            const std::string x = resolveInput(node, 2);
            resultExpr = "smoothstep(" + e0 + ", " + e1 + ", " + x + ")";
            break;
        }
        case NodeKind::OP_POWER: {
            const std::string base = resolveInput(node, 0);
            const std::string exp = resolveInput(node, 1);
            resultExpr = "pow(max(" + base + ", 0.0), " + exp + ")";
            break;
        }
        case NodeKind::OP_SIN: {
            resultExpr = "sin(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::OP_COS: {
            resultExpr = "cos(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::OP_FRACT: {
            resultExpr = "fract(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::OP_FLOOR: {
            resultExpr = "floor(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::OP_ABS: {
            resultExpr = "abs(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::OP_STEP: {
            const std::string edge = resolveInput(node, 0);
            const std::string x = resolveInput(node, 1);
            resultExpr = "step(" + edge + ", " + x + ")";
            break;
        }
        case NodeKind::OP_DOT: {
            const std::string a = resolveInput(node, 0);
            const std::string b = resolveInput(node, 1);
            resultExpr = "dot(" + a + ", " + b + ")";
            break;
        }
        case NodeKind::OP_NORMALIZE: {
            resultExpr = "normalize(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::OP_LENGTH: {
            resultExpr = "length(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::OP_FRESNEL: {
            const std::string n = resolveInput(node, 0);
            const std::string v = resolveInput(node, 1);
            const std::string p = resolveInput(node, 2);
            const std::string bias = resolveInput(node, 3);
            const std::string scale = resolveInput(node, 4);
            resultExpr = "(" + bias + " + (" + scale + " * pow(1.0 - max(dot(" +
                         n + ", " + v + "), 0.0), " + p + ")))";
            break;
        }
        case NodeKind::OP_GRADIENT: {
            const std::string position = resolveInput(node, 0);
            const std::string start = resolveInput(node, 1);
            const std::string end = resolveInput(node, 2);
            const std::string ramp = resolveInput(node, 3);
            const std::string tmp = newTemp("grad");
            const std::vector<GradientStop> stops = sortedGradientStops(node.gradientStops);

            body << "    vec2 " << tmp << "_delta = (" << end << ") - (" << start << ");\n";
            body << "    float " << tmp << "_length = max(length(" << tmp
                 << "_delta), 0.0001);\n";
            body << "    vec2 " << tmp << "_direction = " << tmp << "_delta / "
                 << tmp << "_length;\n";
            body << "    float " << tmp << "_t = clamp(dot((" << position << ") - ("
                 << start << "), " << tmp << "_direction) / " << tmp
                 << "_length, 0.0, 1.0);\n";
            body << "    float " << tmp << "_shape = pow(" << tmp << "_t, max(("
                 << ramp << "), 0.0001));\n";

            if (stops.empty()) {
                body << "    vec4 " << tmp << " = vec4(1.0);\n";
            } else if (stops.size() == 1) {
                body << "    vec4 " << tmp << " = " << vec4Literal(stops.front().color) << ";\n";
            } else {
                body << "    vec4 " << tmp << ";\n";
                body << "    if (" << tmp << "_shape <= "
                     << formatFloat(stops.front().position) << ") {\n";
                body << "        " << tmp << " = "
                     << vec4Literal(stops.front().color) << ";\n";
                body << "    }\n";

                for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
                    const GradientStop& a = stops[i];
                    const GradientStop& b = stops[i + 1];
                    body << "    else if (" << tmp << "_shape < "
                         << formatFloat(b.position) << ") {\n";
                    body << "        float " << tmp << "_mix = clamp(("
                         << tmp << "_shape - " << formatFloat(a.position) << ") / max("
                         << formatFloat(b.position - a.position) << ", 0.0001), 0.0, 1.0);\n";
                    body << "        " << tmp << " = mix("
                         << vec4Literal(a.color) << ", "
                         << vec4Literal(b.color) << ", "
                         << tmp << "_mix);\n";
                    body << "    }\n";
                }

                body << "    else {\n";
                body << "        " << tmp << " = "
                     << vec4Literal(stops.back().color) << ";\n";
                body << "    }\n";
            }

            cache[key(node.id, 0)] = tmp;
            visiting.erase(nodeId);
            return tmp;
        }

        case NodeKind::FN_HASH21: {
            resultExpr = "hash21(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::FN_HASH31: {
            resultExpr = "hash31(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::FN_NOISE21: {
            resultExpr = "noise21(" + resolveInput(node, 0) + ")";
            break;
        }
        case NodeKind::FN_FBM21: {
            resultExpr = "fbm21(" + resolveInput(node, 0) + ")";
            break;
        }

        case NodeKind::COMBINE_V2: {
            const std::string x = resolveInput(node, 0);
            const std::string y = resolveInput(node, 1);
            resultExpr = "vec2(" + x + ", " + y + ")";
            break;
        }
        case NodeKind::COMBINE_V3: {
            const std::string x = resolveInput(node, 0);
            const std::string y = resolveInput(node, 1);
            const std::string z = resolveInput(node, 2);
            resultExpr = "vec3(" + x + ", " + y + ", " + z + ")";
            break;
        }
        case NodeKind::SPLIT_V2: {
            const std::string v = resolveInput(node, 0);
            // Materialize once so repeated outputs share the same upstream eval.
            const std::string tmp = newTemp("split2");
            body << "    vec2 " << tmp << " = " << v << ";\n";
            cache[key(node.id, 0)] = tmp + ".x";
            cache[key(node.id, 1)] = tmp + ".y";
            visiting.erase(nodeId);
            return cache[key(node.id, outputIndex)];
        }
        case NodeKind::SPLIT_V3: {
            const std::string v = resolveInput(node, 0);
            const std::string tmp = newTemp("split3");
            body << "    vec3 " << tmp << " = " << v << ";\n";
            cache[key(node.id, 0)] = tmp + ".x";
            cache[key(node.id, 1)] = tmp + ".y";
            cache[key(node.id, 2)] = tmp + ".z";
            visiting.erase(nodeId);
            return cache[key(node.id, outputIndex)];
        }
        case NodeKind::SWIZZLE_TO_VEC3_RGB: {
            const std::string v = resolveInput(node, 0);
            resultExpr = "(" + v + ").rgb";
            break;
        }

        case NodeKind::TEXTURE_COORDINATES: {
            const std::string tiling = resolveInput(node, 0);
            const std::string offset = resolveInput(node, 1);
            const std::string tmp = newTemp("tex");
            body << "    vec2 " << tmp << " = uv * (" << tiling << ") + (" << offset << ");\n";
            cache[key(node.id, 0)] = tmp;
            cache[key(node.id, 1)] = tmp + ".x";
            cache[key(node.id, 2)] = tmp + ".y";
            visiting.erase(nodeId);
            return cache[key(node.id, outputIndex)];
        }

        case NodeKind::APPEND: {
            const std::string x = resolveInput(node, 0);
            const std::string y = resolveInput(node, 1);
            const int mode = clampAppendMode(node.enumOption);

            switch (mode) {
            case kAppendModeVector2:
                resultExpr = "vec2(" + x + ", " + y + ")";
                break;
            case kAppendModeVector3:
                resultExpr = "vec3(" + x + ", " + y + ", 0.0)";
                break;
            case kAppendModeVector4:
                resultExpr = "vec4(" + x + ", " + y + ", 0.0, 1.0)";
                break;
            case kAppendModeColor:
                resultExpr = "vec3(" + x + ", " + y + ", 0.0)";
                break;
            }
            break;
        }

        case NodeKind::BLEND_OPERATIONS: {
            const std::string source = resolveInput(node, 0);
            const std::string blend = resolveInput(node, 1);
            const std::string opacityMask = resolveInput(node, 2);
            const std::string blended = blendOperationExpressionForMode(
                node.enumOption, source, blend);
            const float opacity = std::clamp(node.constValue.x, 0.0f, 1.0f);
            const bool saturate = node.constValue.y >= 0.5f;
            const std::string finalBlend = saturate
                ? "clamp(" + blended + ", vec3(0.0), vec3(1.0))"
                : blended;
            resultExpr = "mix(" + source + ", " + finalBlend + ", clamp((" +
                formatFloat(opacity) + " * " + opacityMask + "), 0.0, 1.0))";
            break;
        }

        case NodeKind::OUTPUT_MATERIAL: {
            visiting.erase(nodeId);
            return "";  // Not evaluated via emit(); handled in finalize().
        }

        case NodeKind::FN_NOISE_SIMPLEX_2D: {
            const std::string p = resolveInput(node, 0);
            const std::string scale = resolveInput(node, 1);
            const std::string range = resolveInput(node, 2);
            const std::string tmp = newTemp("sn2");
            body << "    float " << tmp << "_raw = snoise2((" << p
                 << ") * (" << scale << "));\n";
            body << "    float " << tmp << " = mix(" << tmp
                 << "_raw, 0.5 + 0.5 * " << tmp << "_raw, step(0.5, "
                 << range << "));\n";
            cache[key(node.id, 0)] = tmp;
            visiting.erase(nodeId);
            return tmp;
        }
        case NodeKind::FN_NOISE_SIMPLEX_3D: {
            const std::string p = resolveInput(node, 0);
            const std::string scale = resolveInput(node, 1);
            const std::string range = resolveInput(node, 2);
            const std::string tmp = newTemp("sn3");
            body << "    float " << tmp << "_raw = snoise3((" << p
                 << ") * (" << scale << "));\n";
            body << "    float " << tmp << " = mix(" << tmp
                 << "_raw, 0.5 + 0.5 * " << tmp << "_raw, step(0.5, "
                 << range << "));\n";
            cache[key(node.id, 0)] = tmp;
            visiting.erase(nodeId);
            return tmp;
        }
        case NodeKind::FN_NOISE_GRADIENT: {
            const std::string p = resolveInput(node, 0);
            const std::string scale = resolveInput(node, 1);
            const std::string range = resolveInput(node, 2);
            const std::string tmp = newTemp("gn2");
            body << "    float " << tmp << "_raw = gnoise2((" << p
                 << ") * (" << scale << "));\n";
            body << "    float " << tmp << " = mix(" << tmp
                 << "_raw, 0.5 + 0.5 * " << tmp << "_raw, step(0.5, "
                 << range << "));\n";
            cache[key(node.id, 0)] = tmp;
            visiting.erase(nodeId);
            return tmp;
        }
        case NodeKind::FN_NOISE_SIMPLE: {
            const std::string p = resolveInput(node, 0);
            const std::string scale = resolveInput(node, 1);
            const std::string range = resolveInput(node, 2);
            const std::string tmp = newTemp("sv2");
            body << "    float " << tmp << "_raw = simpleNoise2((" << p
                 << ") * (" << scale << "));\n";
            // simpleNoise2 already returns [0,1]; honor the toggle by shifting to [-1,1] when off.
            body << "    float " << tmp << " = mix(" << tmp
                 << "_raw * 2.0 - 1.0, " << tmp << "_raw, step(0.5, "
                 << range << "));\n";
            cache[key(node.id, 0)] = tmp;
            visiting.erase(nodeId);
            return tmp;
        }
        case NodeKind::FN_VORONOI: {
            const int method = voronoiMethodFromOption(node.enumOption);
            const int distanceMode = voronoiDistanceFromOption(node.enumOption);
            const bool tileable = voronoiTileableFromOption(node.enumOption);
            const bool smooth = voronoiSmoothFromOption(node.enumOption);
            const int searchQuality = clampVoronoiSearchQuality(
                static_cast<int>(std::round(node.constValue.z)));
            const int octaves = clampVoronoiOctaves(
                static_cast<int>(std::round(node.constValue.w)));
            const int radius = searchQuality;
            const std::string tmp = newTemp("vor");

            const std::string uvIn = resolveInput(node, 0);
            const std::string angleIn = resolveInput(node, 1);
            const std::string scaleIn = resolveInput(node, 2);

            // Angle input is in radians; scale multiplies uv before sampling.
            body << "    float " << tmp << "_ang = (" << angleIn << ");\n";
            body << "    float " << tmp << "_scl = max((" << scaleIn << "), 1.0e-4);\n";
            body << "    vec2 " << tmp << "_p = (" << uvIn << ") * " << tmp << "_scl;\n";
            body << "    mat2 " << tmp << "_rot = mat2(cos(" << tmp << "_ang), -sin("
                 << tmp << "_ang), sin(" << tmp << "_ang), cos(" << tmp << "_ang));\n";
            body << "    " << tmp << "_p = " << tmp << "_rot * " << tmp << "_p;\n";
            body << "    float " << tmp << "_result = 0.0;\n";
            body << "    float " << tmp << "_amp = 1.0;\n";
            body << "    float " << tmp << "_norm = 0.0;\n";
            body << "    for (int octave = 0; octave < " << octaves << "; octave++) {\n";
            body << "        vec2 " << tmp << "_q = " << tmp << "_p * pow(2.0, float(octave));\n";
            if (tileable) {
                body << "        " << tmp << "_q = fract(" << tmp << "_q);\n";
            }
            body << "        vec2 " << tmp << "_cell = floor(" << tmp << "_q);\n";
            body << "        vec2 " << tmp << "_f = fract(" << tmp << "_q);\n";
            body << "        float " << tmp << "_best = 1.0e9;\n";
            body << "        float " << tmp << "_second = 1.0e9;\n";
            body << "        vec2 " << tmp << "_bestCell = vec2(0.0);\n";
            body << "        for (int j = -" << radius << "; j <= " << radius << "; j++) {\n";
            body << "            for (int i = -" << radius << "; i <= " << radius << "; i++) {\n";
            body << "                vec2 " << tmp << "_g = vec2(float(i), float(j));\n";
            body << "                vec2 " << tmp << "_candidateCell = " << tmp << "_cell + " << tmp << "_g;\n";
            body << "                vec2 " << tmp << "_seed = fract(sin(vec2("
                 << "dot(" << tmp << "_candidateCell, vec2(127.1, 311.7)), "
                 << "dot(" << tmp << "_candidateCell, vec2(269.5, 183.3)))) * 43758.5453);\n";
            body << "                vec2 " << tmp << "_feature = " << tmp << "_candidateCell + " << tmp << "_seed;\n";
            body << "                vec2 " << tmp << "_delta = " << tmp << "_feature - " << tmp << "_q;\n";
            body << "                float " << tmp << "_dist = " << voronoiDistanceExpressionForMode(distanceMode, tmp + std::string("_delta")) << ";\n";
            body << "                if (" << tmp << "_dist < " << tmp << "_best) {\n";
            body << "                    " << tmp << "_second = " << tmp << "_best;\n";
            body << "                    " << tmp << "_best = " << tmp << "_dist;\n";
            body << "                    " << tmp << "_bestCell = " << tmp << "_candidateCell;\n";
            body << "                } else if (" << tmp << "_dist < " << tmp << "_second) {\n";
            body << "                    " << tmp << "_second = " << tmp << "_dist;\n";
            body << "                }\n";
            body << "            }\n";
            body << "        }\n";

            const std::string bestValue = [&]() {
                switch (clampVoronoiMethod(method)) {
                case kVoronoiMethodCells:
                    return std::string("fract(sin(dot(") + tmp + "_bestCell, vec2(12.9898, 78.233))) * 43758.5453)";
                case kVoronoiMethodDistance:
                    return std::string("1.0 - clamp(") + tmp + "_best / " +
                        voronoiDistanceNormalizationForMode(distanceMode, radius) +
                        ", 0.0, 1.0)";
                case kVoronoiMethodBorders:
                    return std::string("clamp((") + tmp + "_second - " + tmp + "_best) / " +
                        voronoiDistanceNormalizationForMode(distanceMode, radius) +
                        ", 0.0, 1.0)";
                case kVoronoiMethodSmooth:
                    return std::string("smoothstep(0.0, 1.0, clamp(1.0 - ") + tmp +
                        "_best / " + voronoiDistanceNormalizationForMode(distanceMode, radius) +
                        ", 0.0, 1.0))";
                }
                return std::string("0.0");
            }();

            body << "        float " << tmp << "_layer = " << bestValue << ";\n";
            if (smooth) {
                body << "        " << tmp << "_layer = smoothstep(0.0, 1.0, " << tmp
                     << "_layer);\n";
            }
            body << "        " << tmp << "_result += " << tmp << "_layer * " << tmp
                 << "_amp;\n";
            body << "        " << tmp << "_norm += " << tmp << "_amp;\n";
            body << "        " << tmp << "_amp *= 0.5;\n";
            body << "    }\n";
            body << "    float " << tmp << " = " << tmp << "_norm > 0.0 ? "
                 << tmp << "_result / " << tmp << "_norm : 0.0;\n";
            cache[key(node.id, 0)] = tmp;
            visiting.erase(nodeId);
            return tmp;
        }

        case NodeKind::FN_TRUCHET_MAZE: {
            const std::string p = resolveInput(node, 0);
            const std::string scale = resolveInput(node, 1);
            const std::string res = resolveInput(node, 2);
            resultExpr = "truchetMaze((" + p + ") * (" + scale + "), " + res + ")";
            break;
        }
        case NodeKind::FN_TRUCHET_CIRCLES: {
            const std::string p = resolveInput(node, 0);
            const std::string scale = resolveInput(node, 1);
            const std::string res = resolveInput(node, 2);
            resultExpr = "truchetCircles((" + p + ") * (" + scale + "), " + res + ")";
            break;
        }
        case NodeKind::FN_TRUCHET_TRIANGLES: {
            const std::string p = resolveInput(node, 0);
            const std::string scale = resolveInput(node, 1);
            const std::string res = resolveInput(node, 2);
            resultExpr = "truchetTriangles((" + p + ") * (" + scale + "), " + res + ")";
            break;
        }

        case NodeKind::FX_DISTORT_UV: {
            const std::string uv = resolveInput(node, 0);
            const std::string strength = resolveInput(node, 1);
            const std::string t = resolveInput(node, 2);
            resultExpr = "distortUvFbm(" + uv + ", " + strength + ", " + t + ")";
            break;
        }
        case NodeKind::FX_SWIRL: {
            const std::string uv = resolveInput(node, 0);
            const std::string strength = resolveInput(node, 1);
            resultExpr = "swirlUv(" + uv + ", " + strength + ")";
            break;
        }
        case NodeKind::FX_RIPPLE: {
            const std::string uv = resolveInput(node, 0);
            const std::string freq = resolveInput(node, 1);
            const std::string amp = resolveInput(node, 2);
            const std::string t = resolveInput(node, 3);
            resultExpr = "rippleUv(" + uv + ", " + freq + ", " + amp + ", " + t + ")";
            break;
        }
        case NodeKind::FX_RADIAL_SHEAR: {
            const std::string uv = resolveInput(node, 0);
            const std::string center = resolveInput(node, 1);
            const std::string strength = resolveInput(node, 2);
            const std::string offset = resolveInput(node, 3);
            const std::string tmp = newTemp("rsh");
            body << "    vec2 " << tmp << "_delta = (" << uv << ") - (" << center << ");\n";
            body << "    float " << tmp << "_radius = max(length(" << tmp
                 << "_delta) + (" << offset << "), 0.0001);\n";
            body << "    vec2 " << tmp << "_shear = vec2(" << tmp << "_delta.y, "
                 << tmp << "_delta.x) * (" << strength << ") * " << tmp
                 << "_radius;\n";
            body << "    vec2 " << tmp << " = (" << center << ") + " << tmp
                 << "_delta + " << tmp << "_shear;\n";
            cache[key(node.id, 0)] = tmp;
            visiting.erase(nodeId);
            return tmp;
        }

        case NodeKind::FX_PANNER: {
            const std::string uv = resolveInput(node, 0);
            const std::string speed = resolveInput(node, 1);
            const std::string time = resolveInput(node, 2);
            resultExpr = "(" + uv + ") + (" + speed + ") * (" + time + ")";
            break;
        }
        case NodeKind::FX_PIXELATE_UV: {
            const std::string uv = resolveInput(node, 0);
            const std::string px = resolveInput(node, 1);
            const std::string py = resolveInput(node, 2);
            const std::string tmp = newTemp("pix");
            body << "    vec2 " << tmp << "_cells = max(vec2((" << px << "), ("
                 << py << ")), vec2(1.0));\n";
            body << "    vec2 " << tmp << " = floor((" << uv << ") * " << tmp
                 << "_cells) / " << tmp << "_cells;\n";
            cache[key(node.id, 0)] = tmp;
            visiting.erase(nodeId);
            return tmp;
        }
        case NodeKind::FX_ROTATOR: {
            const std::string uv = resolveInput(node, 0);
            const std::string anchor = resolveInput(node, 1);
            const std::string time = resolveInput(node, 2);
            const std::string tmp = newTemp("rot");
            body << "    vec2 " << tmp << "_anchor = (" << anchor << ");\n";
            body << "    vec2 " << tmp << "_delta = (" << uv << ") - " << tmp
                 << "_anchor;\n";
            body << "    float " << tmp << "_a = (" << time << ");\n";
            body << "    float " << tmp << "_s = sin(" << tmp << "_a);\n";
            body << "    float " << tmp << "_c = cos(" << tmp << "_a);\n";
            body << "    vec2 " << tmp << " = vec2("
                 << tmp << "_delta.x * " << tmp << "_c - " << tmp << "_delta.y * " << tmp << "_s, "
                 << tmp << "_delta.x * " << tmp << "_s + " << tmp << "_delta.y * " << tmp << "_c"
                 << ") + " << tmp << "_anchor;\n";
            cache[key(node.id, 0)] = tmp;
            visiting.erase(nodeId);
            return tmp;
        }
        case NodeKind::SEAMLESS_UV: {
            // faceUv(worldPos, faceNormal) projects world-space position onto the
            // dominant face plane, giving a UV that is continuous across block
            // boundaries — neighbouring blocks share the same world coordinates at
            // their shared edges, so there are no visible seams.
            const std::string scale = resolveInput(node, 0);
            resultExpr = "faceUv(worldPos, faceNormal) * (" + scale + ")";
            break;
        }

        case NodeKind::PRESET_STONE_ROUGH: {
            const std::string color = resolveInput(node, 0);
            const std::string scale = resolveInput(node, 1);
            const bool hasUvIn_rough = (node.inputs.size() > 2 &&
                                        node.inputs[2].connectedLinkId > 0);
            const std::string uvIn_rough = hasUvIn_rough ? resolveInput(node, 2) : "uv";
            const std::string tmp = newTemp("stone");
            body << "    vec3 " << tmp << "_alb;\n";
            body << "    float " << tmp << "_rough;\n";
            body << "    {\n";
            body << "        vec2 sp = (" << uvIn_rough << ") * (" << scale << ");\n";
            body << "        float strata = fbm21(vec2(sp.x * 3.4, sp.y * 1.4));\n";
            body << "        float grain = simpleNoise2(localUv * 11.0);\n";
            body << "        vec3 alb = (" << color
                 << ") * mix(0.72, 1.15, strata);\n";
            body << "        alb += vec3((grain - 0.5) * 0.06);\n";
            body << "        " << tmp
                 << "_alb = clamp(alb, vec3(0.0), vec3(1.4));\n";
            body << "        " << tmp << "_rough = 0.92;\n";
            body << "    }\n";
            cache[key(node.id, 0)] = tmp + "_alb";
            cache[key(node.id, 1)] = tmp + "_rough";
            visiting.erase(nodeId);
            return cache[key(node.id, outputIndex)];
        }
        case NodeKind::PRESET_STONE_CRACKED: {
            const std::string color = resolveInput(node, 0);
            const std::string scale = resolveInput(node, 1);
            const std::string crackIntensity = resolveInput(node, 2);
            const bool hasUvIn_crack = (node.inputs.size() > 3 &&
                                        node.inputs[3].connectedLinkId > 0);
            const std::string uvIn_crack = hasUvIn_crack ? resolveInput(node, 3) : "uv";
            const std::string tmp = newTemp("stoneX");
            body << "    vec3 " << tmp << "_alb;\n";
            body << "    float " << tmp << "_rough;\n";
            body << "    float " << tmp << "_crack;\n";
            body << "    {\n";
            body << "        vec2 sp = (" << uvIn_crack << ") * (" << scale << ");\n";
            body << "        float strata = fbm21(sp * 2.1 + vec2(3.0, 7.0));\n";
            body << "        float macro = fbm21(sp * 0.6);\n";
            body << "        float cr = smoothstep(0.55, 0.82, "
                    "fbm21(sp * 5.2 + vec2(13.0, 17.0)));\n";
            body << "        cr *= clamp((" << crackIntensity
                 << "), 0.0, 4.0);\n";
            body << "        vec3 alb = (" << color
                 << ") * mix(0.62, 1.10, strata);\n";
            body << "        alb = mix(alb, alb * 0.35, clamp(cr, 0.0, 1.0));\n";
            body << "        alb *= mix(0.94, 1.05, macro);\n";
            body << "        " << tmp
                 << "_alb = clamp(alb, vec3(0.0), vec3(1.4));\n";
            body << "        " << tmp
                 << "_rough = clamp(0.88 + cr * 0.08, 0.0, 1.0);\n";
            body << "        " << tmp << "_crack = clamp(cr, 0.0, 1.0);\n";
            body << "    }\n";
            cache[key(node.id, 0)] = tmp + "_alb";
            cache[key(node.id, 1)] = tmp + "_rough";
            cache[key(node.id, 2)] = tmp + "_crack";
            visiting.erase(nodeId);
            return cache[key(node.id, outputIndex)];
        }

        case NodeKind::COUNT_:
            break;
        }

        // Materialize to a temporary so downstream nodes can share the computation
        // and the GLSL stays readable.
        if (outputIndex >= static_cast<int>(node.outputs.size())) {
            visiting.erase(nodeId);
            return "0.0";
        }
        const PinDataType outType = node.outputs[outputIndex].type;
        const std::string tmp = newTemp("n");
        body << "    " << glslTypeName(outType) << " " << tmp << " = "
             << resultExpr << ";\n";
        cache[key(node.id, outputIndex)] = tmp;
        visiting.erase(nodeId);
        return tmp;
    }
};

} // namespace

ShaderNodeGraph::CodegenResult ShaderNodeGraph::buildGlsl() const {
    CodegenResult result;

    const int outId = outputNodeId();
    if (outId < 0) {
        result.error = "Graph has no Material Output node.";
        return result;
    }

    const Node* outNode = findNode(outId);
    if (!outNode) {
        result.error = "Output node not found.";
        return result;
    }

    Codegen cg{*this};

    // Resolve the four output channels.
    auto resolveOrLiteral = [&](const InputPin& pin, PinDataType target) {
        if (pin.connectedLinkId > 0) {
            const Link* link = findLink(pin.connectedLinkId);
            if (!link) return literalForPin(pin);
            const std::string upstream = cg.emit(link->from.nodeId, link->from.pinIndex);
            const Node* fromNode = findNode(link->from.nodeId);
            if (!fromNode ||
                link->from.pinIndex >= static_cast<int>(fromNode->outputs.size())) {
                return literalForPin(pin);
            }
            return convertExpression(upstream, fromNode->outputs[link->from.pinIndex].type,
                                     target);
        }
        return literalForPin(pin);
    };

    const std::string albedoExpr = resolveOrLiteral(outNode->inputs[0], PinDataType::VEC3);
    const std::string roughExpr = resolveOrLiteral(outNode->inputs[1], PinDataType::FLOAT);
    const std::string specExpr = resolveOrLiteral(outNode->inputs[2], PinDataType::FLOAT);
    const std::string emissExpr = resolveOrLiteral(outNode->inputs[3], PinDataType::FLOAT);

    if (!cg.error.empty()) {
        result.error = cg.error;
        return result;
    }

    std::ostringstream out;
    out << "// Generated by VoxelParadox.ShaderEditor node graph.\n";
    out << "// Edit via the ShaderEditor Node Graph panel; direct edits may be overwritten.\n";
    out << cg.body.str();
    out << "    vec3 generatedAlbedo = clamp(" << albedoExpr << ", vec3(0.0), vec3(2.0));\n";
    out << "    float generatedRoughness = clamp(" << roughExpr << ", 0.0, 1.0);\n";
    out << "    float generatedSpecular = clamp(" << specExpr << ", 0.0, 2.0);\n";
    out << "    float generatedEmissive = max(" << emissExpr << ", 0.0);\n";
    out << "    return makeSample(generatedAlbedo, generatedRoughness, "
           "generatedSpecular, generatedEmissive);\n";

    result.glsl = out.str();
    result.ok = true;
    return result;
}

#pragma endregion

#pragma region 5. Serialization

std::string ShaderNodeGraph::serialize() const {
    nlohmann::json doc;
    doc["version"] = 2;
    doc["nextNodeId"] = nextNodeId_;
    doc["nextLinkId"] = nextLinkId_;

    nlohmann::json nodes = nlohmann::json::array();
    for (const Node& node : nodes_) {
        nlohmann::json n;
        n["id"] = node.id;
        n["kind"] = static_cast<int>(node.kind);
        n["pos"] = { node.position.x, node.position.y };
        n["constValue"] = { node.constValue.x, node.constValue.y,
                            node.constValue.z, node.constValue.w };
        n["enumOption"] = node.enumOption;
        n["preview"] = node.previewEnabled;

        if (node.kind == NodeKind::OP_GRADIENT) {
            nlohmann::json gradientStops = nlohmann::json::array();
            for (const GradientStop& stop : node.gradientStops) {
                nlohmann::json s;
                s["position"] = stop.position;
                s["color"] = { stop.color.x, stop.color.y,
                               stop.color.z, stop.color.w };
                gradientStops.push_back(s);
            }
            n["gradientStops"] = gradientStops;
        }

        nlohmann::json inputs = nlohmann::json::array();
        for (const InputPin& pin : node.inputs) {
            nlohmann::json p;
            p["default"] = { pin.defaultValue.x, pin.defaultValue.y,
                             pin.defaultValue.z, pin.defaultValue.w };
            inputs.push_back(p);
        }
        n["inputs"] = inputs;
        nodes.push_back(n);
    }
    doc["nodes"] = nodes;

    nlohmann::json links = nlohmann::json::array();
    for (const Link& link : links_) {
        nlohmann::json l;
        l["id"] = link.id;
        l["from"] = { link.from.nodeId, link.from.pinIndex };
        l["to"] = { link.to.nodeId, link.to.pinIndex };
        links.push_back(l);
    }
    doc["links"] = links;

    return doc.dump(2);
}

bool ShaderNodeGraph::deserialize(const std::string& json, std::string& outError) {
    try {
        const nlohmann::json doc = nlohmann::json::parse(json);
        clear();

        nextNodeId_ = doc.value("nextNodeId", 1);
        nextLinkId_ = doc.value("nextLinkId", 1);

        for (const auto& n : doc.value("nodes", nlohmann::json::array())) {
            Node node;
            node.id = n.value("id", 0);
            node.kind = static_cast<NodeKind>(n.value("kind", 0));
            if (n.contains("pos") && n["pos"].is_array() && n["pos"].size() >= 2) {
                node.position.x = n["pos"][0].get<float>();
                node.position.y = n["pos"][1].get<float>();
            }

            // Build the node shape first, then restore the saved editable state.
            configureNode(node);

            if (n.contains("constValue") && n["constValue"].is_array()) {
                for (std::size_t i = 0; i < n["constValue"].size() && i < 4; ++i) {
                    (&node.constValue.x)[i] = n["constValue"][i].get<float>();
                }
            }
            node.enumOption = n.value("enumOption", 0);
            node.previewEnabled = n.value("preview", false);
            if (node.kind == NodeKind::APPEND) {
                syncAppendNodeOutput(node);
            }
            if (node.kind == NodeKind::OP_GRADIENT && n.contains("gradientStops") &&
                n["gradientStops"].is_array()) {
                node.gradientStops.clear();
                for (const auto& savedStop : n["gradientStops"]) {
                    GradientStop stop;
                    stop.position = savedStop.value("position", 0.0f);
                    if (savedStop.contains("color") && savedStop["color"].is_array()) {
                        for (std::size_t i = 0; i < savedStop["color"].size() && i < 4; ++i) {
                            (&stop.color.x)[i] = savedStop["color"][i].get<float>();
                        }
                    }
                    node.gradientStops.push_back(stop);
                }
                if (node.gradientStops.empty()) {
                    node.gradientStops = defaultGradientStops();
                }
            }
            if (n.contains("inputs") && n["inputs"].is_array()) {
                for (std::size_t i = 0; i < n["inputs"].size() && i < node.inputs.size(); ++i) {
                    const auto& saved = n["inputs"][i];
                    if (saved.contains("default") && saved["default"].is_array()) {
                        for (std::size_t j = 0; j < saved["default"].size() && j < 4; ++j) {
                            (&node.inputs[i].defaultValue.x)[j] =
                                saved["default"][j].get<float>();
                        }
                    }
                }
            }
            nodes_.push_back(std::move(node));
        }

        for (const auto& l : doc.value("links", nlohmann::json::array())) {
            Link link;
            link.id = l.value("id", 0);
            if (l.contains("from") && l["from"].is_array() && l["from"].size() >= 2) {
                link.from.nodeId = l["from"][0].get<int>();
                link.from.pinIndex = l["from"][1].get<int>();
            }
            if (l.contains("to") && l["to"].is_array() && l["to"].size() >= 2) {
                link.to.nodeId = l["to"][0].get<int>();
                link.to.pinIndex = l["to"][1].get<int>();
            }
            // Validate and hook the input pin back up.
            Node* toNode = findNode(link.to.nodeId);
            if (!toNode || link.to.pinIndex < 0 ||
                link.to.pinIndex >= static_cast<int>(toNode->inputs.size())) {
                continue;
            }
            toNode->inputs[link.to.pinIndex].connectedLinkId = link.id;
            links_.push_back(link);
        }

        // Ensure id counters cannot collide with restored content.
        for (const Node& node : nodes_) {
            if (node.id >= nextNodeId_) nextNodeId_ = node.id + 1;
        }
        for (const Link& link : links_) {
            if (link.id >= nextLinkId_) nextLinkId_ = link.id + 1;
        }

        return true;
    }
    catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool ShaderNodeGraph::saveToFile(const std::filesystem::path& path,
                                 std::string& outError) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        outError = "Failed to open graph file for writing: " + path.string();
        return false;
    }
    const std::string payload = serialize();
    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    stream.flush();
    return stream.good();
}

bool ShaderNodeGraph::loadFromFile(const std::filesystem::path& path,
                                   std::string& outError) {
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        outError = "Failed to open graph file: " + path.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return deserialize(buffer.str(), outError);
}

#pragma endregion

} // namespace ShaderEditor
