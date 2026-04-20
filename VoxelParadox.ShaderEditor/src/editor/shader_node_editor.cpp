// shader_node_editor.cpp
// ImGui-based visual shader node editor. Renders a custom canvas with pan + zoom,
// draggable nodes, drag-to-connect pins, context menus for add / node / link,
// inspector with per-pin disconnect, and undo/redo history. Generated GLSL is
// written to the previewed block's shader.glsl (auto-reloaded by
// BlockShaderSession) alongside a sidecar `shader.nodegraph.json` so switching
// preview blocks restores the block's own graph.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

// 3. Local Project Modules
#include "shader_node_editor.hpp"

#pragma endregion

namespace ShaderEditor {

#pragma region 1. Constants & Color Helpers

namespace {

constexpr float kBaseNodeWidth = 200.0f;
constexpr float kBasePinGap = 20.0f;
constexpr float kBasePinRadius = 5.0f;
constexpr float kBaseTitleHeight = 22.0f;
constexpr float kBaseNodePadding = 6.0f;
constexpr float kBaseNodeRounding = 4.0f;
constexpr float kBaseLinkThickness = 2.5f;
constexpr float kBaseFontSize = 13.0f;
constexpr float kInlineRowHeight = 14.0f;
constexpr float kAppendHeaderDetailHeight = 16.0f;
constexpr float kBlendHeaderDetailHeight = 32.0f;
constexpr float kVoronoiHeaderDetailHeight = 16.0f;
constexpr float kMinZoom = 0.35f;
constexpr float kMaxZoom = 2.5f;
constexpr const char* kSidecarFileName = "shader.nodegraph.json";

ImU32 colorForCategory(NodeKind kind) {
    const char* category = ShaderNodeGraph::nodeCategory(kind);
    if (std::strcmp(category, "Inputs")    == 0) return IM_COL32(45, 110, 165, 235);
    if (std::strcmp(category, "Constants") == 0) return IM_COL32(100, 125, 155, 235);
    if (std::strcmp(category, "Math")      == 0) return IM_COL32(150, 100, 55, 235);
    if (std::strcmp(category, "Noise")     == 0) return IM_COL32(150, 70, 150, 235);
    if (std::strcmp(category, "Truchet")   == 0) return IM_COL32(170, 95, 170, 235);
    if (std::strcmp(category, "Effects")   == 0) return IM_COL32(70, 130, 180, 235);
    if (std::strcmp(category, "Channels")  == 0) return IM_COL32(100, 150, 70, 235);
    if (std::strcmp(category, "Presets")   == 0) return IM_COL32(180, 130, 60, 235);
    if (std::strcmp(category, "Output")    == 0) return IM_COL32(165, 70, 70, 235);
    return IM_COL32(90, 90, 100, 235);
}

constexpr std::array<const char*, 9> kCategoryOrder = {
    "Inputs", "Constants", "Math", "Noise", "Truchet",
    "Effects", "Channels", "Presets", "Output",
};

ImU32 colorForPinType(PinDataType type) {
    switch (type) {
    case PinDataType::FLOAT: return IM_COL32(200, 200, 200, 255);
    case PinDataType::VEC2:  return IM_COL32(150, 220, 170, 255);
    case PinDataType::VEC3:  return IM_COL32(230, 180, 100, 255);
    case PinDataType::VEC4:  return IM_COL32(225, 120, 200, 255);
    }
    return IM_COL32(200, 200, 200, 255);
}

std::string formatCompactFloat(float value, int precision = 2) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    return buffer;
}

void drawBezier(ImDrawList* draw, const ImVec2& a, const ImVec2& b, ImU32 color,
                float thickness) {
    const float dx = std::max(std::abs(b.x - a.x) * 0.5f, 30.0f);
    const ImVec2 c1(a.x + dx, a.y);
    const ImVec2 c2(b.x - dx, b.y);
    draw->AddBezierCubic(a, c1, c2, b, color, thickness);
}

bool pointNearBezier(const ImVec2& p, const ImVec2& a, const ImVec2& b,
                     float tolerance) {
    // Coarse sampled approximation — good enough for hit-testing.
    const float dx = std::max(std::abs(b.x - a.x) * 0.5f, 30.0f);
    const ImVec2 c1(a.x + dx, a.y);
    const ImVec2 c2(b.x - dx, b.y);

    ImVec2 prev = a;
    const int samples = 24;
    for (int i = 1; i <= samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        const float one = 1.0f - t;
        const float w0 = one * one * one;
        const float w1 = 3.0f * one * one * t;
        const float w2 = 3.0f * one * t * t;
        const float w3 = t * t * t;
        const ImVec2 cur(
            w0 * a.x + w1 * c1.x + w2 * c2.x + w3 * b.x,
            w0 * a.y + w1 * c1.y + w2 * c2.y + w3 * b.y);

        const ImVec2 seg(cur.x - prev.x, cur.y - prev.y);
        const float segLenSq = seg.x * seg.x + seg.y * seg.y;
        if (segLenSq > 0.0001f) {
            const ImVec2 rel(p.x - prev.x, p.y - prev.y);
            float t2 = (rel.x * seg.x + rel.y * seg.y) / segLenSq;
            t2 = std::clamp(t2, 0.0f, 1.0f);
            const ImVec2 closest(prev.x + seg.x * t2, prev.y + seg.y * t2);
            const float ddx = p.x - closest.x;
            const float ddy = p.y - closest.y;
            if ((ddx * ddx + ddy * ddy) <= tolerance * tolerance) {
                return true;
            }
        }
        prev = cur;
    }
    return false;
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

glm::vec4 sampleGradientColor(const std::vector<GradientStop>& stops, float t) {
    const std::vector<GradientStop> sorted = sortedGradientStops(stops);
    if (sorted.empty()) {
        return glm::vec4(1.0f);
    }
    if (sorted.size() == 1) {
        return sorted.front().color;
    }

    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= sorted.front().position) {
        return sorted.front().color;
    }

    for (std::size_t i = 0; i + 1 < sorted.size(); ++i) {
        const GradientStop& a = sorted[i];
        const GradientStop& b = sorted[i + 1];
        if (t < b.position) {
            const float denom = std::max(b.position - a.position, 0.0001f);
            const float mixT = std::clamp((t - a.position) / denom, 0.0f, 1.0f);
            return glm::vec4(
                a.color.x + (b.color.x - a.color.x) * mixT,
                a.color.y + (b.color.y - a.color.y) * mixT,
                a.color.z + (b.color.z - a.color.z) * mixT,
                a.color.w + (b.color.w - a.color.w) * mixT);
        }
    }

    return sorted.back().color;
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
constexpr int kVoronoiDistanceMask = 0xC;
constexpr int kVoronoiTileableMask = 0x10;
constexpr int kVoronoiSmoothMask = 0x20;

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

float fractFloat(float value) {
    return value - std::floor(value);
}

glm::vec2 voronoiHash2(const glm::vec2& cell) {
    const float x = std::sin(cell.x * 127.1f + cell.y * 311.7f) * 43758.5453f;
    const float y = std::sin(cell.x * 269.5f + cell.y * 183.3f) * 43758.5453f;
    return glm::vec2(fractFloat(x), fractFloat(y));
}

float voronoiDistanceValue(int mode, const glm::vec2& delta) {
    switch (mode) {
    case kVoronoiDistanceEuclidian2:
        return delta.x * delta.x + delta.y * delta.y;
    case kVoronoiDistanceEuclidian:
        return std::sqrt(delta.x * delta.x + delta.y * delta.y);
    case kVoronoiDistanceManhattan:
        return std::abs(delta.x) + std::abs(delta.y);
    case kVoronoiDistanceChebyshev:
        return std::max(std::abs(delta.x), std::abs(delta.y));
    }
    return std::sqrt(delta.x * delta.x + delta.y * delta.y);
}

float voronoiDistanceNormalization(int mode, int radius) {
    const float span = static_cast<float>(std::max(radius + 1, 1));
    switch (mode) {
    case kVoronoiDistanceEuclidian2:
        return 2.0f * span * span;
    case kVoronoiDistanceEuclidian:
        return 1.41421356f * span;
    case kVoronoiDistanceManhattan:
        return 2.0f * span;
    case kVoronoiDistanceChebyshev:
        return span;
    }
    return span;
}

float voronoiPreviewSample(const Node& node, const glm::vec2& uv) {
    const int method = voronoiMethodFromOption(node.enumOption);
    const int distanceMode = voronoiDistanceFromOption(node.enumOption);
    const bool tileable = voronoiTileableFromOption(node.enumOption);
    const bool smooth = voronoiSmoothFromOption(node.enumOption);
    const int qualityIndex = std::clamp(
        static_cast<int>(std::round(node.constValue.z)), 0, 3);
    const int octaves = std::clamp(
        static_cast<int>(std::round(node.constValue.w)), 1, 8);
    const int radius = qualityIndex;
    const float scale = std::max(node.constValue.x, 0.0001f);
    const float angle = node.constValue.y * 0.017453292519943295769f;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const glm::mat2 rot(c, -s, s, c);

    float total = 0.0f;
    float amplitude = 1.0f;
    float normalization = 0.0f;

    for (int octave = 0; octave < octaves; ++octave) {
        glm::vec2 q = rot * (uv * scale * std::pow(2.0f, static_cast<float>(octave)));
        if (tileable) {
            q = glm::fract(q);
        }

        const glm::vec2 cell = glm::floor(q);
        float best = 1.0e9f;
        float second = 1.0e9f;
        glm::vec2 bestCell(0.0f);

        for (int j = -radius; j <= radius; ++j) {
            for (int i = -radius; i <= radius; ++i) {
                const glm::vec2 g(static_cast<float>(i), static_cast<float>(j));
                const glm::vec2 candidateCell = cell + g;
                const glm::vec2 seed = voronoiHash2(candidateCell);
                const glm::vec2 feature = candidateCell + seed;
                const glm::vec2 delta = feature - q;
                const float dist = voronoiDistanceValue(distanceMode, delta);
                if (dist < best) {
                    second = best;
                    best = dist;
                    bestCell = candidateCell;
                } else if (dist < second) {
                    second = dist;
                }
            }
        }

        const float norm = std::max(voronoiDistanceNormalization(distanceMode, radius), 0.0001f);
        float layer = 0.0f;
        switch (method) {
        case kVoronoiMethodCells:
            layer = fractFloat(std::sin(bestCell.x * 12.9898f + bestCell.y * 78.233f) *
                               43758.5453f);
            break;
        case kVoronoiMethodDistance:
            layer = 1.0f - std::clamp(best / norm, 0.0f, 1.0f);
            break;
        case kVoronoiMethodBorders:
            layer = std::clamp((second - best) / norm, 0.0f, 1.0f);
            break;
        case kVoronoiMethodSmooth:
            layer = std::clamp(1.0f - best / norm, 0.0f, 1.0f);
            layer = layer * layer * (3.0f - 2.0f * layer);
            break;
        }

        if (smooth) {
            layer = layer * layer * (3.0f - 2.0f * layer);
        }

        total += layer * amplitude;
        normalization += amplitude;
        amplitude *= 0.5f;
    }

    return normalization > 0.0f ? (total / normalization) : 0.0f;
}

ImU32 voronoiPreviewColor(const Node& node, const glm::vec2& uv) {
    const float value = std::clamp(voronoiPreviewSample(node, uv), 0.0f, 1.0f);
    const float tint = 0.22f + 0.78f * value;
    return ImGui::ColorConvertFloat4ToU32(ImVec4(tint, tint * 0.92f, tint * 1.05f, 1.0f));
}

std::string voronoiSubtitle(const Node& node) {
    return std::string("Method [") + ShaderNodeGraph::voronoiMethodName(voronoiMethodFromOption(node.enumOption)) +
        "]  Distance [" + ShaderNodeGraph::voronoiDistanceName(voronoiDistanceFromOption(node.enumOption)) + "]";
}

std::string voronoiSettingsSummary(const Node& node) {
    const int qualityIndex = std::clamp(static_cast<int>(std::round(node.constValue.z)), 0, 3);
    const int octaves = std::clamp(static_cast<int>(std::round(node.constValue.w)), 1, 8);
    const bool tileable = voronoiTileableFromOption(node.enumOption);
    const bool smooth = voronoiSmoothFromOption(node.enumOption);

    return std::string("Search [") +
        ShaderNodeGraph::voronoiSearchQualityName(qualityIndex) +
        "]  Octaves [" + std::to_string(octaves) +
        "]  Tileable [" + (tileable ? "On" : "Off") +
        "]  Smooth [" + (smooth ? "On" : "Off") + "]";
}

ImU32 colorToImU32(const glm::vec4& color) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w));
}

constexpr const char* kNodeClipboardFormat = "VoxelParadox.ShaderEditor.NodeClipboard";

nlohmann::json serializeNodeToJson(const Node& node) {
    nlohmann::json n;
    n["id"] = node.id;
    n["kind"] = static_cast<int>(node.kind);
    n["pos"] = { node.position.x, node.position.y };
    n["constValue"] = { node.constValue.x, node.constValue.y,
                        node.constValue.z, node.constValue.w };
    n["enumOption"] = node.enumOption;

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
    return n;
}

std::string serializeNodeClipboard(const ShaderNodeGraph& graph,
                                   const std::vector<int>& nodeIds) {
    if (nodeIds.empty()) {
        return {};
    }

    std::unordered_set<int> selected(nodeIds.begin(), nodeIds.end());

    nlohmann::json doc;
    doc["format"] = kNodeClipboardFormat;
    doc["version"] = 1;

    nlohmann::json nodes = nlohmann::json::array();
    for (int nodeId : nodeIds) {
        const Node* node = graph.findNode(nodeId);
        if (!node) {
            continue;
        }
        nodes.push_back(serializeNodeToJson(*node));
    }
    doc["nodes"] = nodes;

    nlohmann::json links = nlohmann::json::array();
    for (const Link& link : graph.links()) {
        if (selected.count(link.from.nodeId) == 0 ||
            selected.count(link.to.nodeId) == 0) {
            continue;
        }
        nlohmann::json l;
        l["id"] = link.id;
        l["from"] = { link.from.nodeId, link.from.pinIndex };
        l["to"] = { link.to.nodeId, link.to.pinIndex };
        links.push_back(l);
    }
    doc["links"] = links;

    return doc.dump(2);
}

struct ClipboardNodeRecord {
    int oldId = -1;
    NodeKind kind = NodeKind::CONST_FLOAT;
    glm::vec2 position{ 0.0f, 0.0f };
    glm::vec4 constValue{ 1.0f };
    int enumOption = 0;
    std::vector<GradientStop> gradientStops{};
    std::vector<glm::vec4> inputDefaults{};
};

bool validateNodeClipboardPayload(const std::string& payload, std::string& outError) {
    try {
        if (payload.find(kNodeClipboardFormat) == std::string::npos) {
            return false;
        }
        const nlohmann::json doc = nlohmann::json::parse(payload);
        if (doc.value("format", "") != kNodeClipboardFormat) {
            return false;
        }
        if (doc.value("version", 0) != 1) {
            outError = "Unsupported node clipboard version.";
            return false;
        }
        if (!doc.contains("nodes") || !doc["nodes"].is_array() ||
            doc["nodes"].empty()) {
            outError = "Clipboard does not contain any nodes.";
            return false;
        }

        for (const auto& saved : doc["nodes"]) {
            (void)saved.value("id", -1);
            const int kindValue = saved.value("kind", 0);
            if (kindValue < 0 ||
                kindValue >= static_cast<int>(NodeKind::COUNT_)) {
                outError = "Clipboard contains an unknown node type.";
                return false;
            }
            if (saved.contains("pos") && saved["pos"].is_array() &&
                saved["pos"].size() >= 2) {
                (void)saved["pos"][0].get<float>();
                (void)saved["pos"][1].get<float>();
            }
            if (saved.contains("constValue") && saved["constValue"].is_array()) {
                for (std::size_t i = 0; i < saved["constValue"].size() && i < 4; ++i) {
                    (void)saved["constValue"][i].get<float>();
                }
            }
            if (saved.contains("gradientStops") &&
                saved["gradientStops"].is_array()) {
                for (const auto& savedStop : saved["gradientStops"]) {
                    (void)savedStop.value("position", 0.0f);
                    if (savedStop.contains("color") &&
                        savedStop["color"].is_array()) {
                        for (std::size_t i = 0; i < savedStop["color"].size() && i < 4; ++i) {
                            (void)savedStop["color"][i].get<float>();
                        }
                    }
                }
            }
            if (saved.contains("inputs") && saved["inputs"].is_array()) {
                for (const auto& savedInput : saved["inputs"]) {
                    if (savedInput.contains("default") &&
                        savedInput["default"].is_array()) {
                        for (std::size_t i = 0; i < savedInput["default"].size() && i < 4; ++i) {
                            (void)savedInput["default"][i].get<float>();
                        }
                    }
                }
            }
        }

        if (doc.contains("links") && doc["links"].is_array()) {
            for (const auto& savedLink : doc["links"]) {
                if (!savedLink.contains("from") || !savedLink.contains("to") ||
                    !savedLink["from"].is_array() || !savedLink["to"].is_array() ||
                    savedLink["from"].size() < 2 || savedLink["to"].size() < 2) {
                    continue;
                }
                (void)savedLink["from"][0].get<int>();
                (void)savedLink["from"][1].get<int>();
                (void)savedLink["to"][0].get<int>();
                (void)savedLink["to"][1].get<int>();
            }
        }

        return true;
    }
    catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool importNodeClipboardPayload(ShaderNodeGraph& graph,
                                const std::string& payload,
                                const glm::vec2& anchorWorldPos,
                                std::vector<int>& outNodeIds,
                                std::string& outError) {
    try {
        const nlohmann::json doc = nlohmann::json::parse(payload);
        if (doc.value("format", "") != kNodeClipboardFormat) {
            return false;
        }
        if (doc.value("version", 0) != 1) {
            outError = "Unsupported node clipboard version.";
            return false;
        }
        if (!doc.contains("nodes") || !doc["nodes"].is_array() ||
            doc["nodes"].empty()) {
            outError = "Clipboard does not contain any nodes.";
            return false;
        }

        std::vector<ClipboardNodeRecord> records;
        records.reserve(doc["nodes"].size());

        glm::vec2 boundsMin(std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max());
        for (const auto& saved : doc["nodes"]) {
            ClipboardNodeRecord record;
            record.oldId = saved.value("id", -1);
            record.kind = static_cast<NodeKind>(saved.value("kind", 0));
            if (saved.contains("pos") && saved["pos"].is_array() &&
                saved["pos"].size() >= 2) {
                record.position.x = saved["pos"][0].get<float>();
                record.position.y = saved["pos"][1].get<float>();
            }
    if (saved.contains("constValue") && saved["constValue"].is_array()) {
        for (std::size_t i = 0; i < saved["constValue"].size() && i < 4; ++i) {
            (&record.constValue.x)[i] = saved["constValue"][i].get<float>();
        }
    }
            record.enumOption = saved.value("enumOption", 0);

            if (saved.contains("gradientStops") &&
                saved["gradientStops"].is_array()) {
                for (const auto& savedStop : saved["gradientStops"]) {
                    GradientStop stop;
                    stop.position = savedStop.value("position", 0.0f);
                    if (savedStop.contains("color") &&
                        savedStop["color"].is_array()) {
                        for (std::size_t i = 0; i < savedStop["color"].size() && i < 4; ++i) {
                            (&stop.color.x)[i] = savedStop["color"][i].get<float>();
                        }
                    }
                    record.gradientStops.push_back(stop);
                }
            }

            if (saved.contains("inputs") && saved["inputs"].is_array()) {
                for (const auto& savedInput : saved["inputs"]) {
                    glm::vec4 def{ 0.0f };
                    if (savedInput.contains("default") &&
                        savedInput["default"].is_array()) {
                        for (std::size_t i = 0; i < savedInput["default"].size() && i < 4; ++i) {
                            (&def.x)[i] = savedInput["default"][i].get<float>();
                        }
                    }
                    record.inputDefaults.push_back(def);
                }
            }

            boundsMin.x = std::min(boundsMin.x, record.position.x);
            boundsMin.y = std::min(boundsMin.y, record.position.y);
            records.push_back(std::move(record));
        }

        const glm::vec2 offset = anchorWorldPos - boundsMin;
        std::unordered_map<int, int> idMap;
        outNodeIds.clear();
        outNodeIds.reserve(records.size());

        for (const ClipboardNodeRecord& record : records) {
            const int newNodeId = graph.addNode(record.kind, record.position + offset);
            Node* node = graph.findNode(newNodeId);
            if (!node) {
                outError = "Failed to create pasted node.";
                return false;
            }

            node->constValue = record.constValue;
            node->enumOption = record.enumOption;
            if (record.kind == NodeKind::APPEND && !node->outputs.empty()) {
                node->outputs.front().type = ShaderNodeGraph::appendOutputType(node->enumOption);
            }
            if (record.kind == NodeKind::OP_GRADIENT &&
                !record.gradientStops.empty()) {
                node->gradientStops = record.gradientStops;
            }

            const std::size_t inputCount = std::min(node->inputs.size(),
                                                    record.inputDefaults.size());
            for (std::size_t i = 0; i < inputCount; ++i) {
                node->inputs[i].defaultValue = record.inputDefaults[i];
            }

            idMap[record.oldId] = newNodeId;
            outNodeIds.push_back(newNodeId);
        }

        if (doc.contains("links") && doc["links"].is_array()) {
            for (const auto& savedLink : doc["links"]) {
                if (!savedLink.contains("from") || !savedLink.contains("to") ||
                    !savedLink["from"].is_array() || !savedLink["to"].is_array() ||
                    savedLink["from"].size() < 2 || savedLink["to"].size() < 2) {
                    continue;
                }

                const int oldFromNode = savedLink["from"][0].get<int>();
                const int oldFromPin = savedLink["from"][1].get<int>();
                const int oldToNode = savedLink["to"][0].get<int>();
                const int oldToPin = savedLink["to"][1].get<int>();

                const auto fromIt = idMap.find(oldFromNode);
                const auto toIt = idMap.find(oldToNode);
                if (fromIt == idMap.end() || toIt == idMap.end()) {
                    continue;
                }

                graph.connect(
                    PinRef{ fromIt->second, oldFromPin },
                    PinRef{ toIt->second, oldToPin });
            }
        }

        return true;
    }
    catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace

#pragma endregion

#pragma region 2. Scaled Metrics (Zoom)

float ShaderNodeEditor::nodeWidth() const     { return kBaseNodeWidth * zoom_; }
float ShaderNodeEditor::pinGap() const        { return kBasePinGap * zoom_; }
float ShaderNodeEditor::pinRadius() const     { return kBasePinRadius * zoom_; }
float ShaderNodeEditor::titleHeight() const   { return kBaseTitleHeight * zoom_; }
float ShaderNodeEditor::nodePadding() const   { return kBaseNodePadding * zoom_; }
float ShaderNodeEditor::nodeRounding() const  { return kBaseNodeRounding * zoom_; }
float ShaderNodeEditor::linkThickness() const { return kBaseLinkThickness * zoom_; }
float ShaderNodeEditor::fontSize() const      { return kBaseFontSize * zoom_; }

float ShaderNodeEditor::nodeHeight(const Node& node) const {
    const int pinRows = static_cast<int>(
        std::max(node.inputs.size(), node.outputs.size()));
    const float baseBody = static_cast<float>(pinRows) * pinGap() + pinGap() * 0.5f;
    const float detailHeight = nodeHeaderDetailHeight(node);
    const float inlineH = nodeInlineEditorHeight(node);
    return titleHeight() + detailHeight + baseBody + inlineH + nodePadding();
}

float ShaderNodeEditor::nodeInlineEditorHeight(const Node& node) const {
    const float rowH = kInlineRowHeight * zoom_;
    int rows = 0;
    switch (node.kind) {
    case NodeKind::CONST_FLOAT:
    case NodeKind::CONST_VEC2:
    case NodeKind::CONST_VEC3:
    case NodeKind::CONST_COLOR:
        rows += 1;
        break;
    case NodeKind::APPEND:
        rows += 1;
        break;
    case NodeKind::FN_VORONOI:
        rows += 8;
        break;
    case NodeKind::BLEND_OPERATIONS:
        rows += 3;
        break;
    default:
        break;
    }
    // One inline default editor per unlinked input pin.
    for (const InputPin& pin : node.inputs) {
        if (pin.connectedLinkId <= 0) {
            ++rows;
        }
    }
    float h = static_cast<float>(rows) * rowH;
    if (rows > 0) h += 4.0f;
    if (node.previewEnabled) h += 52.0f;
    return h;
}

float ShaderNodeEditor::nodeHeaderDetailHeight(const Node& node) const {
    if (node.kind == NodeKind::APPEND) {
        return kAppendHeaderDetailHeight * zoom_;
    }
    if (node.kind == NodeKind::FN_VORONOI) {
        return kVoronoiHeaderDetailHeight * zoom_;
    }
    if (node.kind == NodeKind::BLEND_OPERATIONS) {
        return kBlendHeaderDetailHeight * zoom_;
    }
    return 0.0f;
}

glm::vec2 ShaderNodeEditor::nodeScreenPos(const Node& node) const {
    return glm::vec2(canvasOrigin_.x + panOffset_.x + node.position.x * zoom_,
                     canvasOrigin_.y + panOffset_.y + node.position.y * zoom_);
}

glm::vec2 ShaderNodeEditor::inputPinPos(const Node& node, int inputIndex,
                                        const glm::vec2& nodePos) const {
    return glm::vec2(nodePos.x,
                     nodePos.y + titleHeight() + nodeHeaderDetailHeight(node) +
                         pinGap() * 0.75f +
                         pinGap() * static_cast<float>(inputIndex));
}

glm::vec2 ShaderNodeEditor::outputPinPos(const Node& node, int outputIndex,
                                         const glm::vec2& nodePos) const {
    return glm::vec2(nodePos.x + nodeWidth(),
                     nodePos.y + titleHeight() + nodeHeaderDetailHeight(node) +
                         pinGap() * 0.75f +
                         pinGap() * static_cast<float>(outputIndex));
}

#pragma endregion

#pragma region 3. Lifecycle, Persistence, Undo

void ShaderNodeEditor::init() {
    graph_.resetToDefault();
    resetHistory();
}

void ShaderNodeEditor::loadForBlock(const std::filesystem::path& blockShaderPath) {
    if (blockShaderPath == currentBlockShaderPath_) {
        return;
    }
    currentBlockShaderPath_ = blockShaderPath;
    selectedNodeId_ = -1;
    selectedLinkId_ = -1;
    multiSelection_.clear();
    resetHistory();

    if (blockShaderPath.empty()) {
        graph_.resetToDefault();
        statusMessage_ = "No block shader selected; using default graph.";
        return;
    }

    const std::filesystem::path sidecar = sidecarPathFor(blockShaderPath);
    std::error_code ec;
    if (std::filesystem::exists(sidecar, ec)) {
        std::string err;
        if (graph_.loadFromFile(sidecar, err)) {
            statusMessage_ = "Loaded graph for " + blockShaderPath.parent_path()
                                                       .filename().string();
            return;
        }
        statusMessage_ = "Failed to load graph (" + err + "); using default.";
    } else {
        statusMessage_ = "No saved graph for this block; using default.";
    }
    graph_.resetToDefault();
}

std::filesystem::path ShaderNodeEditor::sidecarPathFor(
    const std::filesystem::path& shaderPath) const {
    if (shaderPath.empty()) return {};
    return shaderPath.parent_path() / kSidecarFileName;
}

bool ShaderNodeEditor::saveSidecar(std::string& outError) const {
    if (currentBlockShaderPath_.empty()) {
        outError = "No sidecar path (no current block).";
        return false;
    }
    return graph_.saveToFile(sidecarPathFor(currentBlockShaderPath_), outError);
}

void ShaderNodeEditor::pushUndoSnapshot() {
    undoStack_.push_back(graph_.serialize());
    while (undoStack_.size() > kMaxHistory) {
        undoStack_.pop_front();
    }
    redoStack_.clear();
}

void ShaderNodeEditor::resetHistory() {
    undoStack_.clear();
    redoStack_.clear();
    snapshotPendingForDrag_ = false;
}

std::vector<int> ShaderNodeEditor::selectedNodeIdsForClipboard() const {
    std::unordered_set<int> selection = multiSelection_;
    if (selection.empty() && selectedNodeId_ > 0) {
        selection.insert(selectedNodeId_);
    }

    std::vector<int> result;
    result.reserve(selection.size());
    for (const Node& node : graph_.nodes()) {
        if (selection.count(node.id) > 0) {
            result.push_back(node.id);
        }
    }
    return result;
}

glm::vec2 ShaderNodeEditor::selectedNodeBoundsMin(const std::vector<int>& nodeIds) const {
    glm::vec2 boundsMin(0.0f, 0.0f);
    bool initialized = false;
    for (int nodeId : nodeIds) {
        const Node* node = graph_.findNode(nodeId);
        if (!node) {
            continue;
        }
        if (!initialized) {
            boundsMin = node->position;
            initialized = true;
        } else {
            boundsMin.x = std::min(boundsMin.x, node->position.x);
            boundsMin.y = std::min(boundsMin.y, node->position.y);
        }
    }
    return boundsMin;
}

bool ShaderNodeEditor::copySelectedNodes() {
    const std::vector<int> nodeIds = selectedNodeIdsForClipboard();
    if (nodeIds.empty()) {
        statusMessage_ = "No nodes selected to copy.";
        return false;
    }

    nodeClipboardPayload_ = serializeNodeClipboard(graph_, nodeIds);
    if (nodeClipboardPayload_.empty()) {
        statusMessage_ = "No nodes selected to copy.";
        return false;
    }

    ImGui::SetClipboardText(nodeClipboardPayload_.c_str());
    statusMessage_ = "Copied " + std::to_string(nodeIds.size()) + " node(s).";
    return true;
}

bool ShaderNodeEditor::pasteNodesFromClipboard(const glm::vec2& anchorWorldPos) {
    std::string payload = nodeClipboardPayload_;
    if (payload.empty()) {
        const char* clipboard = ImGui::GetClipboardText();
        if (clipboard) {
            payload = clipboard;
        }
    }
    if (payload.empty()) {
        statusMessage_ = "Clipboard is empty.";
        return false;
    }

    std::string validationErr;
    if (!validateNodeClipboardPayload(payload, validationErr)) {
        if (!validationErr.empty()) {
            statusMessage_ = "Paste failed: " + validationErr;
        } else {
            statusMessage_ = "Clipboard does not contain node data.";
        }
        return false;
    }

    pushUndoSnapshot();

    std::vector<int> newNodeIds;
    std::string err;
    if (!importNodeClipboardPayload(graph_, payload, anchorWorldPos, newNodeIds, err)) {
        std::string restoreErr;
        if (!undoStack_.empty()) {
            const std::string snapshot = undoStack_.back();
            undoStack_.pop_back();
            if (!graph_.deserialize(snapshot, restoreErr)) {
                statusMessage_ = "Paste failed: " + restoreErr;
                return false;
            }
        }
        if (!err.empty()) {
            statusMessage_ = "Paste failed: " + err;
        } else {
            statusMessage_ = "Paste failed.";
        }
        return false;
    }

    nodeClipboardPayload_ = payload;
    multiSelection_.clear();
    for (int nodeId : newNodeIds) {
        multiSelection_.insert(nodeId);
    }
    selectedNodeId_ = newNodeIds.empty() ? -1 : newNodeIds.front();
    selectedLinkId_ = -1;
    graphDirty_ = true;
    statusMessage_ = "Pasted " + std::to_string(newNodeIds.size()) + " node(s).";
    return true;
}

bool ShaderNodeEditor::duplicateSelectedNodes() {
    const std::vector<int> nodeIds = selectedNodeIdsForClipboard();
    if (nodeIds.empty()) {
        statusMessage_ = "No nodes selected to duplicate.";
        return false;
    }

    const glm::vec2 anchor = selectedNodeBoundsMin(nodeIds) + glm::vec2(32.0f, 32.0f);
    const std::string payload = serializeNodeClipboard(graph_, nodeIds);
    if (payload.empty()) {
        statusMessage_ = "No nodes selected to duplicate.";
        return false;
    }

    std::string validationErr;
    if (!validateNodeClipboardPayload(payload, validationErr)) {
        if (!validationErr.empty()) {
            statusMessage_ = "Duplicate failed: " + validationErr;
        } else {
            statusMessage_ = "Duplicate failed.";
        }
        return false;
    }

    pushUndoSnapshot();

    std::vector<int> newNodeIds;
    std::string err;
    if (!importNodeClipboardPayload(graph_, payload, anchor, newNodeIds, err)) {
        std::string restoreErr;
        if (!undoStack_.empty()) {
            const std::string snapshot = undoStack_.back();
            undoStack_.pop_back();
            if (!graph_.deserialize(snapshot, restoreErr)) {
                statusMessage_ = "Duplicate failed: " + restoreErr;
                return false;
            }
        }
        if (!err.empty()) {
            statusMessage_ = "Duplicate failed: " + err;
        } else {
            statusMessage_ = "Duplicate failed.";
        }
        return false;
    }

    multiSelection_.clear();
    for (int nodeId : newNodeIds) {
        multiSelection_.insert(nodeId);
    }
    selectedNodeId_ = newNodeIds.empty() ? -1 : newNodeIds.front();
    selectedLinkId_ = -1;
    graphDirty_ = true;
    statusMessage_ = "Duplicated " + std::to_string(newNodeIds.size()) + " node(s).";
    return true;
}

void ShaderNodeEditor::undo() {
    if (undoStack_.empty()) return;
    redoStack_.push_back(graph_.serialize());
    while (redoStack_.size() > kMaxHistory) {
        redoStack_.pop_front();
    }
    std::string err;
    const std::string snapshot = undoStack_.back();
    undoStack_.pop_back();
    if (!graph_.deserialize(snapshot, err)) {
        statusMessage_ = "Undo failed: " + err;
        return;
    }
    selectedNodeId_ = -1;
    selectedLinkId_ = -1;
    multiSelection_.clear();
    graphDirty_ = true;
    statusMessage_ = "Undo.";
}

void ShaderNodeEditor::redo() {
    if (redoStack_.empty()) return;
    undoStack_.push_back(graph_.serialize());
    while (undoStack_.size() > kMaxHistory) {
        undoStack_.pop_front();
    }
    std::string err;
    const std::string snapshot = redoStack_.back();
    redoStack_.pop_back();
    if (!graph_.deserialize(snapshot, err)) {
        statusMessage_ = "Redo failed: " + err;
        return;
    }
    selectedNodeId_ = -1;
    selectedLinkId_ = -1;
    multiSelection_.clear();
    graphDirty_ = true;
    statusMessage_ = "Redo.";
}

#pragma endregion

#pragma region 4. Top-level Window

void ShaderNodeEditor::draw(const ShaderNodeEditorCallbacks& callbacks) {
    if (!ImGui::Begin("Shader Node Editor")) {
        ImGui::End();
        return;
    }

    // Swap the graph in when the previewed block changes.
    if (callbacks.currentBlockShaderPath) {
        const std::filesystem::path requested = callbacks.currentBlockShaderPath();
        if (requested != currentBlockShaderPath_) {
            loadForBlock(requested);
        }
    }

    // Global undo/redo shortcuts (only when this window is focused so we don't
    // steal Ctrl+Z from other panels that might add it later).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyAlt) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                if (io.KeyShift) redo(); else undo();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                redo();
            } else if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                applyToBlock(callbacks, false);
            }
        }
        if (!io.KeyCtrl && !io.KeyAlt && !io.KeyShift && !io.WantTextInput &&
            !ImGui::IsAnyItemActive()) {
            if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
                showInspector_ = !showInspector_;
            }
        }
    }

    drawToolbar(callbacks);

    ImGui::BeginChild("##nodeEditorMain", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float totalWidth = ImGui::GetContentRegionAvail().x;
    const float inspectorWidth = showInspector_
        ? std::max(totalWidth * 0.28f, 240.0f)
        : 0.0f;
    const float canvasWidth = showInspector_
        ? std::max(totalWidth - inspectorWidth - 6.0f, 200.0f)
        : totalWidth;

    ImGui::BeginChild("##nodeCanvas", ImVec2(canvasWidth, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas(callbacks);
    ImGui::EndChild();

    if (showInspector_) {
        ImGui::SameLine();
        ImGui::BeginChild("##nodeInspector", ImVec2(0, 0), true);
        drawInspector();
        ImGui::EndChild();
    }

    ImGui::EndChild();

    if (showGeneratedPreview_) {
        drawGeneratedPreview();
    }

    ImGui::End();
}

void ShaderNodeEditor::drawToolbar(const ShaderNodeEditorCallbacks& callbacks) {
    const std::string currentBlock = callbacks.currentBlockDisplayName
        ? callbacks.currentBlockDisplayName() : std::string();
    if (!currentBlock.empty()) {
        ImGui::TextDisabled("Target Block:");
        ImGui::SameLine();
        ImGui::TextUnformatted(currentBlock.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    if (ImGui::Button("Apply to Block")) {
        applyToBlock(callbacks, false);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Apply to Block (Ctrl+F)");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-Apply", &autoApplyEnabled_);
    ImGui::SameLine();
    if (ImGui::Button("Show GLSL")) {
        const auto result = graph_.buildGlsl();
        lastGeneratedGlsl_ = result.ok ? result.glsl : std::string("// ") + result.error;
        showGeneratedPreview_ = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canUndo());
    if (ImGui::Button("Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRedo());
    if (ImGui::Button("Redo")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset Graph")) {
        pushUndoSnapshot();
        graph_.resetToDefault();
        selectedNodeId_ = -1;
        selectedLinkId_ = -1;
        multiSelection_.clear();
        graphDirty_ = true;
        statusMessage_ = "Graph reset.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        panOffset_ = glm::vec2(0.0f);
        zoom_ = 1.0f;
    }
    ImGui::SameLine();
    {
        // "N" toggles the right-hand inspector panel so the canvas can claim the
        // full editor width when the user wants more room for node work.
        const bool active = showInspector_;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 160, 60, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(220, 180, 80, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(240, 200, 100, 255));
        }
        if (ImGui::Button("N")) {
            showInspector_ = !showInspector_;
        }
        if (active) {
            ImGui::PopStyleColor(3);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle Inspector panel (N)");
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Zoom %.2fx  |  Right-click: menu, Del: remove, Ctrl+Z: undo, Ctrl+C/V/D: copy/paste/duplicate",
                        zoom_);

    if (!statusMessage_.empty()) {
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    }
}

#pragma endregion

#pragma region 5. Canvas

void ShaderNodeEditor::drawCanvas(const ShaderNodeEditorCallbacks& callbacks) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();

    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const ImVec2 bottomRight(topLeft.x + canvasSize.x, topLeft.y + canvasSize.y);

    canvasOrigin_ = glm::vec2(topLeft.x, topLeft.y);

    // Background.
    draw->AddRectFilled(topLeft, bottomRight, IM_COL32(26, 27, 32, 255));

    const float gridSpacing = 32.0f * zoom_;
    const ImU32 gridColor = IM_COL32(45, 47, 55, 255);
    for (float x = std::fmod(panOffset_.x, gridSpacing); x < canvasSize.x; x += gridSpacing) {
        draw->AddLine(ImVec2(topLeft.x + x, topLeft.y),
                      ImVec2(topLeft.x + x, bottomRight.y), gridColor);
    }
    for (float y = std::fmod(panOffset_.y, gridSpacing); y < canvasSize.y; y += gridSpacing) {
        draw->AddLine(ImVec2(topLeft.x, topLeft.y + y),
                      ImVec2(bottomRight.x, topLeft.y + y), gridColor);
    }

    // Invisible button captures input over the full canvas area.
    ImGui::InvisibleButton("##canvas_surface", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonMiddle |
                           ImGuiButtonFlags_MouseButtonRight);
    const bool canvasHovered = ImGui::IsItemHovered();
    const bool canvasActive = ImGui::IsItemActive();

    // Panning via middle-mouse drag anywhere on the canvas.
    if (canvasActive && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        panOffset_.x += io.MouseDelta.x;
        panOffset_.y += io.MouseDelta.y;
    }

    // Mouse-wheel zoom anchored on the mouse cursor so the point under the mouse
    // stays put across zoom level changes.
    if (canvasHovered && io.MouseWheel != 0.0f) {
        const glm::vec2 mouseInCanvas(io.MousePos.x - canvasOrigin_.x,
                                      io.MousePos.y - canvasOrigin_.y);
        const glm::vec2 worldPoint(
            (mouseInCanvas.x - panOffset_.x) / zoom_,
            (mouseInCanvas.y - panOffset_.y) / zoom_);
        const float zoomStep = std::pow(1.12f, io.MouseWheel);
        zoom_ = std::clamp(zoom_ * zoomStep, kMinZoom, kMaxZoom);
        panOffset_.x = mouseInCanvas.x - worldPoint.x * zoom_;
        panOffset_.y = mouseInCanvas.y - worldPoint.y * zoom_;
    }

    hoveredNodeId_ = -1;

    draw->PushClipRect(topLeft, bottomRight, true);

    // Draw links first so nodes sit on top.
    for (const Link& link : graph_.links()) {
        const Node* fromNode = graph_.findNode(link.from.nodeId);
        const Node* toNode = graph_.findNode(link.to.nodeId);
        if (!fromNode || !toNode) continue;
        if (link.from.pinIndex >= static_cast<int>(fromNode->outputs.size())) continue;
        if (link.to.pinIndex >= static_cast<int>(toNode->inputs.size())) continue;

        const glm::vec2 fromNodePos = nodeScreenPos(*fromNode);
        const glm::vec2 toNodePos = nodeScreenPos(*toNode);
        const glm::vec2 a = outputPinPos(*fromNode, link.from.pinIndex, fromNodePos);
        const glm::vec2 b = inputPinPos(*toNode, link.to.pinIndex, toNodePos);

        const ImU32 color = (link.id == selectedLinkId_)
            ? IM_COL32(255, 220, 120, 255)
            : colorForPinType(fromNode->outputs[link.from.pinIndex].type);
        drawBezier(draw, ImVec2(a.x, a.y), ImVec2(b.x, b.y), color, linkThickness());
    }

    // In-progress drag link preview.
    if (linkDragActive_) {
        const Node* node = graph_.findNode(linkDragFrom_.nodeId);
        if (node) {
            const glm::vec2 nodePos = nodeScreenPos(*node);
            glm::vec2 anchor = linkDragIsOutput_
                ? outputPinPos(*node, linkDragFrom_.pinIndex, nodePos)
                : inputPinPos(*node, linkDragFrom_.pinIndex, nodePos);
            const ImVec2 mouse = io.MousePos;
            const ImVec2 a = linkDragIsOutput_
                ? ImVec2(anchor.x, anchor.y)
                : mouse;
            const ImVec2 b = linkDragIsOutput_
                ? mouse
                : ImVec2(anchor.x, anchor.y);
            drawBezier(draw, a, b, IM_COL32(255, 255, 255, 200), linkThickness());
        }
    }

    // Nodes.
    for (Node& node : graph_.nodes()) {
        const glm::vec2 nodePos = nodeScreenPos(node);
        const float height = nodeHeight(node);
        const ImVec2 nodeMin(nodePos.x, nodePos.y);
        const ImVec2 nodeMax(nodePos.x + nodeWidth(), nodePos.y + height);

        const bool hovered = canvasHovered &&
            io.MousePos.x >= nodeMin.x && io.MousePos.x <= nodeMax.x &&
            io.MousePos.y >= nodeMin.y && io.MousePos.y <= nodeMax.y;
        if (hovered) {
            hoveredNodeId_ = node.id;
        }

        const ImU32 bodyColor = IM_COL32(38, 40, 48, 235);
        const ImU32 titleColor = colorForCategory(node.kind);
        const bool inMultiSelection = multiSelection_.count(node.id) > 0;
        const ImU32 borderColor = (node.id == selectedNodeId_)
            ? IM_COL32(250, 220, 120, 255)
            : inMultiSelection ? IM_COL32(250, 180, 60, 220)
                               : IM_COL32(10, 11, 14, 255);

        draw->AddRectFilled(nodeMin, nodeMax, bodyColor, nodeRounding());
        draw->AddRectFilled(nodeMin,
                            ImVec2(nodeMax.x, nodeMin.y + titleHeight()),
                            titleColor, nodeRounding(),
                            ImDrawFlags_RoundCornersTop);
        draw->AddRect(nodeMin, nodeMax, borderColor, nodeRounding(), 0,
                      1.5f * zoom_);

        draw->AddText(font, fontSize(),
                      ImVec2(nodeMin.x + nodePadding(), nodeMin.y + 4.0f * zoom_),
                      IM_COL32(235, 235, 235, 255),
                      ShaderNodeGraph::nodeDisplayName(node.kind));

        if (node.kind == NodeKind::APPEND) {
            const std::string typeLabel = std::string("Type [") +
                ShaderNodeGraph::appendOutputTypeName(node.enumOption) + "]";
            draw->AddText(font, fontSize() * 0.82f,
                          ImVec2(nodeMin.x + nodePadding(),
                                 nodeMin.y + titleHeight() + 2.0f * zoom_),
                          IM_COL32(225, 225, 230, 255),
                          typeLabel.c_str());
        }

        if (node.kind == NodeKind::FN_VORONOI) {
            const std::string detail = voronoiSubtitle(node);
            draw->AddText(font, fontSize() * 0.76f,
                          ImVec2(nodeMin.x + nodePadding(),
                                 nodeMin.y + titleHeight() + 2.0f * zoom_),
                          IM_COL32(225, 225, 230, 255),
                          detail.c_str());
        }

        if (node.kind == NodeKind::BLEND_OPERATIONS) {
            const bool saturate = node.constValue.y >= 0.5f;
            const std::string modeLabel = std::string("Mode [") +
                ShaderNodeGraph::blendOperationName(node.enumOption) + "]";
            const std::string settingsLabel = std::string("Opacity [") +
                formatCompactFloat(std::clamp(node.constValue.x, 0.0f, 1.0f), 2) +
                "]  Saturate [" + (saturate ? "On" : "Off") + "]";

            draw->AddText(font, fontSize() * 0.80f,
                          ImVec2(nodeMin.x + nodePadding(),
                                 nodeMin.y + titleHeight() + 2.0f * zoom_),
                          IM_COL32(225, 225, 230, 255),
                          modeLabel.c_str());
            draw->AddText(font, fontSize() * 0.73f,
                          ImVec2(nodeMin.x + nodePadding(),
                                 nodeMin.y + titleHeight() + 14.0f * zoom_),
                          IM_COL32(205, 208, 214, 255),
                          settingsLabel.c_str());
        }

        // Input pins.
        for (std::size_t i = 0; i < node.inputs.size(); ++i) {
            const glm::vec2 pos = inputPinPos(node, static_cast<int>(i), nodePos);
            const ImU32 pinColor = colorForPinType(node.inputs[i].type);
            draw->AddCircleFilled(ImVec2(pos.x, pos.y), pinRadius(), pinColor);
            draw->AddCircle(ImVec2(pos.x, pos.y), pinRadius(),
                            IM_COL32(20, 20, 22, 255), 0, 1.5f * zoom_);

            draw->AddText(font, fontSize(),
                          ImVec2(pos.x + 10.0f * zoom_, pos.y - 8.0f * zoom_),
                          IM_COL32(210, 210, 210, 255),
                          node.inputs[i].name.c_str());
        }

        // Output pins.
        for (std::size_t i = 0; i < node.outputs.size(); ++i) {
            const glm::vec2 pos = outputPinPos(node, static_cast<int>(i), nodePos);
            const ImU32 pinColor = colorForPinType(node.outputs[i].type);
            draw->AddCircleFilled(ImVec2(pos.x, pos.y), pinRadius(), pinColor);
            draw->AddCircle(ImVec2(pos.x, pos.y), pinRadius(),
                            IM_COL32(20, 20, 22, 255), 0, 1.5f * zoom_);

            const char* label = node.outputs[i].name.c_str();
            const ImVec2 textSize = font->CalcTextSizeA(
                fontSize(), FLT_MAX, 0.0f, label);
            draw->AddText(font, fontSize(),
                          ImVec2(pos.x - 10.0f * zoom_ - textSize.x,
                                 pos.y - 8.0f * zoom_),
                          IM_COL32(210, 210, 210, 255), label);
        }
    }

    // Per-node read-only property summaries (drawn after all node bodies so
    // later nodes cannot paint over earlier summaries).
    for (Node& node : graph_.nodes()) {
        drawNodePropertyEditors(node);
    }

    // Rubber-band selection overlay (drawn above nodes so it stays visible).
    if (selectionRectActive_) {
        const ImVec2 a(canvasOrigin_.x + panOffset_.x + selectionRectStart_.x * zoom_,
                       canvasOrigin_.y + panOffset_.y + selectionRectStart_.y * zoom_);
        const ImVec2 b(canvasOrigin_.x + panOffset_.x + selectionRectEnd_.x * zoom_,
                       canvasOrigin_.y + panOffset_.y + selectionRectEnd_.y * zoom_);
        draw->AddRectFilled(a, b, IM_COL32(100, 150, 230, 40));
        draw->AddRect(a, b, IM_COL32(160, 200, 255, 200));
    }

    // Per-node previews draw on top of node bodies; do them in a second pass so
    // later nodes cannot paint over earlier node previews.
    for (const Node& node : graph_.nodes()) {
        if (node.previewEnabled) {
            drawNodePreview(node);
        }
    }

    draw->PopClipRect();

    // --- Pin hit-testing for starting a link drag or for pin context menus ---
    const float pinHitRadius = pinRadius() + 3.0f * zoom_;
    auto pinHitTest = [&](glm::vec2& outHit, PinRef& outPin, bool& outIsOutput) -> bool {
        if (!canvasHovered) return false;
        for (const Node& node : graph_.nodes()) {
            const glm::vec2 nodePos = nodeScreenPos(node);
            for (std::size_t i = 0; i < node.inputs.size(); ++i) {
                const glm::vec2 pos = inputPinPos(node, static_cast<int>(i), nodePos);
                const float ddx = io.MousePos.x - pos.x;
                const float ddy = io.MousePos.y - pos.y;
                if (ddx * ddx + ddy * ddy <= pinHitRadius * pinHitRadius) {
                    outHit = pos;
                    outPin = PinRef{ node.id, static_cast<int>(i) };
                    outIsOutput = false;
                    return true;
                }
            }
            for (std::size_t i = 0; i < node.outputs.size(); ++i) {
                const glm::vec2 pos = outputPinPos(node, static_cast<int>(i), nodePos);
                const float ddx = io.MousePos.x - pos.x;
                const float ddy = io.MousePos.y - pos.y;
                if (ddx * ddx + ddy * ddy <= pinHitRadius * pinHitRadius) {
                    outHit = pos;
                    outPin = PinRef{ node.id, static_cast<int>(i) };
                    outIsOutput = true;
                    return true;
                }
            }
        }
        return false;
    };

    auto linkHitTest = [&](int& outLinkId) -> bool {
        for (const Link& link : graph_.links()) {
            const Node* fromNode = graph_.findNode(link.from.nodeId);
            const Node* toNode = graph_.findNode(link.to.nodeId);
            if (!fromNode || !toNode) continue;
            const glm::vec2 fromNodePos = nodeScreenPos(*fromNode);
            const glm::vec2 toNodePos = nodeScreenPos(*toNode);
            const glm::vec2 a = outputPinPos(*fromNode, link.from.pinIndex, fromNodePos);
            const glm::vec2 b = inputPinPos(*toNode, link.to.pinIndex, toNodePos);
            if (pointNearBezier(io.MousePos, ImVec2(a.x, a.y), ImVec2(b.x, b.y),
                                6.0f * zoom_)) {
                outLinkId = link.id;
                return true;
            }
        }
        return false;
    };

    // Start a link drag on left-mouse press over a pin; otherwise start node drag
    // (single or group), start a selection rectangle on empty canvas, or pick a
    // link.
    if (canvasHovered && !linkDragActive_ && draggedNodeId_ < 0 &&
        !selectionRectActive_ &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        glm::vec2 hitPos;
        PinRef hitPin;
        bool hitIsOutput = false;
        if (pinHitTest(hitPos, hitPin, hitIsOutput)) {
            linkDragActive_ = true;
            linkDragFrom_ = hitPin;
            linkDragIsOutput_ = hitIsOutput;
            selectedLinkId_ = -1;
        } else if (hoveredNodeId_ > 0) {
            if (io.KeyShift) {
                // Shift-click toggles the node in the multi-selection.
                if (multiSelection_.count(hoveredNodeId_) > 0) {
                    multiSelection_.erase(hoveredNodeId_);
                    if (selectedNodeId_ == hoveredNodeId_) {
                        selectedNodeId_ = multiSelection_.empty()
                            ? -1 : *multiSelection_.begin();
                    }
                } else {
                    multiSelection_.insert(hoveredNodeId_);
                    selectedNodeId_ = hoveredNodeId_;
                }
            } else {
                // Plain click on a node outside the group replaces the group;
                // clicking a node already in the group keeps the group intact so
                // the drag moves everyone.
                if (multiSelection_.count(hoveredNodeId_) == 0) {
                    multiSelection_.clear();
                    multiSelection_.insert(hoveredNodeId_);
                }
                selectedNodeId_ = hoveredNodeId_;
            }
            selectedLinkId_ = -1;
            draggedNodeId_ = hoveredNodeId_;
            snapshotPendingForDrag_ = true;  // push history once the drag starts.
            const Node* node = graph_.findNode(hoveredNodeId_);
            if (node) {
                const glm::vec2 nodePos = nodeScreenPos(*node);
                dragGrabOffset_ = glm::vec2(io.MousePos.x - nodePos.x,
                                            io.MousePos.y - nodePos.y);
            }
        } else {
            int pickedLink = -1;
            if (linkHitTest(pickedLink)) {
                if (!io.KeyShift) {
                    multiSelection_.clear();
                    selectedNodeId_ = -1;
                }
                selectedLinkId_ = pickedLink;
            } else {
                // Empty canvas: start rubber-band selection. Shift keeps the
                // existing selection so additions are possible.
                if (!io.KeyShift) {
                    multiSelection_.clear();
                    selectedNodeId_ = -1;
                }
                selectedLinkId_ = -1;
                selectionRectActive_ = true;
                const glm::vec2 worldPt(
                    (io.MousePos.x - canvasOrigin_.x - panOffset_.x) / zoom_,
                    (io.MousePos.y - canvasOrigin_.y - panOffset_.y) / zoom_);
                selectionRectStart_ = worldPt;
                selectionRectEnd_ = worldPt;
            }
        }
    }

    // Double-click on a connected input pin disconnects it.
    if (canvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        glm::vec2 hitPos;
        PinRef hitPin;
        bool hitIsOutput = false;
        if (pinHitTest(hitPos, hitPin, hitIsOutput) && !hitIsOutput) {
            const Node* node = graph_.findNode(hitPin.nodeId);
            if (node && hitPin.pinIndex < static_cast<int>(node->inputs.size()) &&
                node->inputs[hitPin.pinIndex].connectedLinkId > 0) {
                pushUndoSnapshot();
                graph_.disconnectInput(hitPin.nodeId, hitPin.pinIndex);
                graphDirty_ = true;
                statusMessage_ = "Disconnected input.";
            }
        }
    }

    // Node drag. When the primary drag node is part of a multi-selection the
    // delta is applied to every selected node so the group moves rigidly.
    if (draggedNodeId_ > 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            Node* primary = graph_.findNode(draggedNodeId_);
            if (primary) {
                // Snapshot exactly once at the first frame of actual movement.
                const float moveSq = io.MouseDelta.x * io.MouseDelta.x +
                                     io.MouseDelta.y * io.MouseDelta.y;
                if (snapshotPendingForDrag_ && moveSq > 0.01f) {
                    pushUndoSnapshot();
                    snapshotPendingForDrag_ = false;
                }
                const float newX = (io.MousePos.x - canvasOrigin_.x - panOffset_.x
                                    - dragGrabOffset_.x) / zoom_;
                const float newY = (io.MousePos.y - canvasOrigin_.y - panOffset_.y
                                    - dragGrabOffset_.y) / zoom_;
                const float dx = newX - primary->position.x;
                const float dy = newY - primary->position.y;

                const bool dragGroup = multiSelection_.size() > 1 &&
                                       multiSelection_.count(draggedNodeId_) > 0;
                if (dragGroup) {
                    for (Node& n : graph_.nodes()) {
                        if (multiSelection_.count(n.id) > 0) {
                            n.position.x += dx;
                            n.position.y += dy;
                        }
                    }
                } else {
                    primary->position.x = newX;
                    primary->position.y = newY;
                }
                if (moveSq > 0.0f) {
                    graphDirty_ = true;
                }
            }
        } else {
            draggedNodeId_ = -1;
            snapshotPendingForDrag_ = false;
        }
    }

    // Selection rectangle. Active across the whole left-drag; finalize on
    // release by inserting every node whose AABB overlaps the rect into
    // multiSelection_.
    if (selectionRectActive_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            selectionRectEnd_ = glm::vec2(
                (io.MousePos.x - canvasOrigin_.x - panOffset_.x) / zoom_,
                (io.MousePos.y - canvasOrigin_.y - panOffset_.y) / zoom_);
        } else {
            const float minX = std::min(selectionRectStart_.x, selectionRectEnd_.x);
            const float minY = std::min(selectionRectStart_.y, selectionRectEnd_.y);
            const float maxX = std::max(selectionRectStart_.x, selectionRectEnd_.x);
            const float maxY = std::max(selectionRectStart_.y, selectionRectEnd_.y);
            for (const Node& n : graph_.nodes()) {
                // Node bounds in world (un-zoomed) coordinates.
                const float nx0 = n.position.x;
                const float ny0 = n.position.y;
                const float nx1 = nx0 + kBaseNodeWidth;
                const float ny1 = ny0 + nodeHeight(n) / zoom_;
                if (nx1 < minX || nx0 > maxX || ny1 < minY || ny0 > maxY) continue;
                multiSelection_.insert(n.id);
            }
            if (!multiSelection_.empty() && selectedNodeId_ < 0) {
                selectedNodeId_ = *multiSelection_.begin();
            }
            selectionRectActive_ = false;
        }
    }

    // Finish link drag on release.
    if (linkDragActive_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        glm::vec2 hitPos;
        PinRef hitPin;
        bool hitIsOutput = false;
        if (pinHitTest(hitPos, hitPin, hitIsOutput) && hitIsOutput != linkDragIsOutput_) {
            const PinRef fromRef = linkDragIsOutput_ ? linkDragFrom_ : hitPin;
            const PinRef toRef = linkDragIsOutput_ ? hitPin : linkDragFrom_;
            pushUndoSnapshot();
            if (graph_.connect(fromRef, toRef)) {
                graphDirty_ = true;
            } else {
                // Connect refused (same node / out-of-range); roll back the
                // snapshot we preemptively pushed so undo stays meaningful.
                if (!undoStack_.empty()) undoStack_.pop_back();
            }
        }
        linkDragActive_ = false;
    }

    // Right-click routes to the appropriate context menu.
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        !linkDragActive_) {
        int pickedLink = -1;
        if (hoveredNodeId_ > 0) {
            contextMenuKind_ = ContextMenuKind::NODE;
            contextMenuTargetNode_ = hoveredNodeId_;
            selectedNodeId_ = hoveredNodeId_;
            contextMenuRequested_ = true;
        } else if (linkHitTest(pickedLink)) {
            contextMenuKind_ = ContextMenuKind::LINK;
            contextMenuTargetLink_ = pickedLink;
            selectedLinkId_ = pickedLink;
            contextMenuRequested_ = true;
        } else {
            contextMenuKind_ = ContextMenuKind::ADD;
            contextMenuCanvasPos_.x =
                (io.MousePos.x - canvasOrigin_.x - panOffset_.x) / zoom_;
            contextMenuCanvasPos_.y =
                (io.MousePos.y - canvasOrigin_.y - panOffset_.y) / zoom_;
            contextMenuRequested_ = true;
        }
    }

    drawContextMenu();

    // Delete selection on Delete key. Multi-selection takes precedence over a
    // single primary node so group removal is one action.
    if (canvasHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (selectedLinkId_ > 0) {
                pushUndoSnapshot();
                graph_.disconnect(selectedLinkId_);
                selectedLinkId_ = -1;
                graphDirty_ = true;
            } else if (!multiSelection_.empty()) {
                pushUndoSnapshot();
                const std::vector<int> toRemove(multiSelection_.begin(),
                                                multiSelection_.end());
                for (int id : toRemove) {
                    graph_.removeNode(id);
                }
                multiSelection_.clear();
                selectedNodeId_ = -1;
                graphDirty_ = true;
            } else if (selectedNodeId_ > 0) {
                pushUndoSnapshot();
                graph_.removeNode(selectedNodeId_);
                selectedNodeId_ = -1;
                graphDirty_ = true;
            }
        }
    }

    // Clipboard shortcuts live on the canvas so they can act on the active
    // graph without interfering with text entry in the inspector.
    if (canvasHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyAlt && !io.WantTextInput && !ImGui::IsAnyItemActive()) {
            const glm::vec2 mouseWorld(
                (io.MousePos.x - canvasOrigin_.x - panOffset_.x) / zoom_,
                (io.MousePos.y - canvasOrigin_.y - panOffset_.y) / zoom_);

            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                copySelectedNodes();
            } else if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                pasteNodesFromClipboard(mouseWorld);
            } else if (ImGui::IsKeyPressed(ImGuiKey_D, false)) {
                duplicateSelectedNodes();
            }
        }
    }

    // Persistence + auto-apply: only commit once the user stops interacting so we
    // don't rewrite shader.glsl every frame during a drag.
    const bool interacting = draggedNodeId_ > 0 || linkDragActive_ ||
        ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (graphDirty_ && !interacting) {
        if (autoApplyEnabled_) {
            applyToBlock(callbacks, true);
        } else if (!currentBlockShaderPath_.empty()) {
            std::string err;
            saveSidecar(err);  // persist even without re-apply.
        }
        graphDirty_ = false;
    }
}

#pragma endregion

#pragma region 5b. In-Node Editors

// Read-only summary text rendered inside the node body. Editing happens only in
// the Inspector — clicking the node body is reserved for drag/selection.
void ShaderNodeEditor::drawNodePropertyEditors(Node& node) {
    const glm::vec2 nodePos = nodeScreenPos(node);
    const int pinRows = static_cast<int>(
        std::max(node.inputs.size(), node.outputs.size()));
    const float lineH = kInlineRowHeight * zoom_;
    const float editorTop = nodePos.y + titleHeight() + nodeHeaderDetailHeight(node) +
                            pinGap() * 0.75f +
                            pinGap() * static_cast<float>(pinRows) + 2.0f;
    const float editorLeft = nodePos.x + nodePadding();
    const float editorRight = nodePos.x + nodeWidth() - nodePadding();
    const float rowTextSize = fontSize() * 0.82f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const ImU32 labelColor = IM_COL32(205, 208, 214, 255);
    const ImU32 valueColor = IM_COL32(235, 235, 240, 255);

    auto drawRow = [&](int rowIndex, const std::string& text, ImU32 color) {
        const float y = editorTop + static_cast<float>(rowIndex) * lineH;
        draw->AddText(font, rowTextSize, ImVec2(editorLeft, y), color, text.c_str());
    };

    auto drawSwatchRow = [&](int rowIndex, const std::string& label,
                             const glm::vec4& color, bool showAlpha) {
        const float y = editorTop + static_cast<float>(rowIndex) * lineH;
        draw->AddText(font, rowTextSize, ImVec2(editorLeft, y), labelColor,
                      label.c_str());
        const float swatchSize = lineH - 2.0f;
        const float swatchRight = editorRight;
        const float swatchLeft = swatchRight - swatchSize * 2.0f;
        const ImVec2 sMin(swatchLeft, y);
        const ImVec2 sMax(swatchRight, y + swatchSize);
        const ImU32 rgb = ImGui::ColorConvertFloat4ToU32(ImVec4(
            std::clamp(color.x, 0.0f, 1.0f),
            std::clamp(color.y, 0.0f, 1.0f),
            std::clamp(color.z, 0.0f, 1.0f),
            1.0f));
        draw->AddRectFilled(sMin, sMax, rgb);
        if (showAlpha) {
            const float aW = (sMax.x - sMin.x) *
                std::clamp(color.w, 0.0f, 1.0f);
            draw->AddLine(ImVec2(sMin.x, sMax.y - 2.0f),
                          ImVec2(sMin.x + aW, sMax.y - 2.0f),
                          IM_COL32(255, 255, 255, 220), 2.0f);
        }
        draw->AddRect(sMin, sMax, IM_COL32(20, 20, 24, 255));
    };

    int row = 0;

    switch (node.kind) {
    case NodeKind::CONST_FLOAT: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "value: %.3f", node.constValue.x);
        drawRow(row++, buf, valueColor);
        break;
    }
    case NodeKind::CONST_VEC2: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "value: %.2f, %.2f",
                      node.constValue.x, node.constValue.y);
        drawRow(row++, buf, valueColor);
        break;
    }
    case NodeKind::CONST_VEC3: {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "value: %.2f, %.2f, %.2f",
                      node.constValue.x, node.constValue.y, node.constValue.z);
        drawRow(row++, buf, valueColor);
        break;
    }
    case NodeKind::CONST_COLOR:
        drawSwatchRow(row++, "color", node.constValue, false);
        break;
    case NodeKind::APPEND: {
        std::string line = std::string("Type: ") +
            ShaderNodeGraph::appendOutputTypeName(node.enumOption);
        drawRow(row++, line, valueColor);
        break;
    }
    case NodeKind::FN_VORONOI: {
        drawRow(row++, voronoiSubtitle(node), valueColor);
        drawRow(row++, voronoiSettingsSummary(node), valueColor);

        const int qualityIndex = std::clamp(static_cast<int>(std::round(node.constValue.z)), 0, 3);
        const int octaves = std::clamp(static_cast<int>(std::round(node.constValue.w)), 1, 8);
        const bool tileable = voronoiTileableFromOption(node.enumOption);
        const bool smooth = voronoiSmoothFromOption(node.enumOption);

        drawRow(row++, std::string("Search: ") +
                       ShaderNodeGraph::voronoiSearchQualityName(qualityIndex), valueColor);
        drawRow(row++, std::string("Octaves: ") + std::to_string(octaves), valueColor);
        drawRow(row++, std::string("Tileable: ") + (tileable ? "On" : "Off"), valueColor);
        drawRow(row++, std::string("Smooth: ") + (smooth ? "On" : "Off"), valueColor);

        char scaleBuf[64];
        std::snprintf(scaleBuf, sizeof(scaleBuf), "Scale: %.2f", node.constValue.x);
        drawRow(row++, scaleBuf, valueColor);

        char angleBuf[64];
        std::snprintf(angleBuf, sizeof(angleBuf), "Angle: %.1f", node.constValue.y);
        drawRow(row++, angleBuf, valueColor);
        break;
    }
    case NodeKind::BLEND_OPERATIONS: {
        drawRow(row++, std::string("Mode: ") +
                         ShaderNodeGraph::blendOperationName(node.enumOption),
                valueColor);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Opacity: %.2f",
                      std::clamp(node.constValue.x, 0.0f, 1.0f));
        drawRow(row++, buf, valueColor);
        drawRow(row++,
                node.constValue.y >= 0.5f ? "Saturate: On" : "Saturate: Off",
                valueColor);
        break;
    }
    default:
        break;
    }

    // One read-only summary per unlinked input pin so current defaults are
    // visible directly on the node even though editing is inspector-only.
    for (std::size_t i = 0; i < node.inputs.size(); ++i) {
        const InputPin& pin = node.inputs[i];
        if (pin.connectedLinkId > 0) continue;
        switch (pin.type) {
        case PinDataType::FLOAT: {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s: %.3f",
                          pin.name.c_str(), pin.defaultValue.x);
            drawRow(row++, buf, labelColor);
            break;
        }
        case PinDataType::VEC2: {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s: %.2f, %.2f",
                          pin.name.c_str(),
                          pin.defaultValue.x, pin.defaultValue.y);
            drawRow(row++, buf, labelColor);
            break;
        }
        case PinDataType::VEC3:
            drawSwatchRow(row++, pin.name, pin.defaultValue, false);
            break;
        case PinDataType::VEC4:
            drawSwatchRow(row++, pin.name, pin.defaultValue, true);
            break;
        }
    }
}

void ShaderNodeEditor::drawNodePreview(const Node& node) const {
    const glm::vec2 nodePos = nodeScreenPos(node);
    const int pinRows = static_cast<int>(
        std::max(node.inputs.size(), node.outputs.size()));
    int rows = 0;
    switch (node.kind) {
    case NodeKind::CONST_FLOAT:
    case NodeKind::CONST_VEC2:
    case NodeKind::CONST_VEC3:
    case NodeKind::CONST_COLOR:
    case NodeKind::APPEND:
        rows = 1; break;
    case NodeKind::BLEND_OPERATIONS:
        rows = 3; break;
    default: break;
    }
    for (const InputPin& pin : node.inputs) {
        if (pin.connectedLinkId <= 0) ++rows;
    }

    const float editorTop = nodePos.y + titleHeight() + nodeHeaderDetailHeight(node) +
                            pinGap() * 0.75f +
                            pinGap() * static_cast<float>(pinRows) + 2.0f;
    const float previewTop = editorTop +
        static_cast<float>(rows) * (kInlineRowHeight * zoom_) +
        (rows > 0 ? 4.0f : 0.0f);
    const float previewLeft = nodePos.x + nodePadding();
    const float previewRight = nodePos.x + nodeWidth() - nodePadding();
    const float previewBottom = previewTop + 44.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pMin(previewLeft, previewTop);
    const ImVec2 pMax(previewRight, previewBottom);

    draw->AddRectFilled(pMin, pMax, IM_COL32(22, 24, 30, 255), 3.0f);

    auto colorFromVec4 = [](const glm::vec4& c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            std::clamp(c.x, 0.0f, 1.0f),
            std::clamp(c.y, 0.0f, 1.0f),
            std::clamp(c.z, 0.0f, 1.0f),
            1.0f));
    };

    switch (node.kind) {
    case NodeKind::CONST_FLOAT: {
        const float v = std::clamp(node.constValue.x, 0.0f, 1.0f);
        const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(v, v, v, 1.0f));
        draw->AddRectFilled(pMin, pMax, col, 3.0f);
        break;
    }
    case NodeKind::CONST_VEC2: {
        glm::vec4 c(std::clamp(node.constValue.x, 0.0f, 1.0f),
                    std::clamp(node.constValue.y, 0.0f, 1.0f), 0.0f, 1.0f);
        draw->AddRectFilled(pMin, pMax, colorFromVec4(c), 3.0f);
        break;
    }
    case NodeKind::CONST_VEC3:
    case NodeKind::CONST_COLOR:
        draw->AddRectFilled(pMin, pMax, colorFromVec4(node.constValue), 3.0f);
        break;
    case NodeKind::OP_GRADIENT: {
        if (node.gradientStops.empty()) {
            draw->AddRectFilled(pMin, pMax, IM_COL32(200, 200, 200, 255), 3.0f);
        } else {
            const std::vector<GradientStop> sorted = sortedGradientStops(node.gradientStops);
            const float span = std::max(pMax.x - pMin.x, 1.0f);
            if (sorted.size() == 1) {
                draw->AddRectFilled(pMin, pMax, colorToImU32(sorted.front().color), 3.0f);
            } else {
                for (std::size_t i = 0; i + 1 < sorted.size(); ++i) {
                    const GradientStop& a = sorted[i];
                    const GradientStop& b = sorted[i + 1];
                    const float x0 = pMin.x + span * std::clamp(a.position, 0.0f, 1.0f);
                    const float x1 = pMin.x + span * std::clamp(b.position, 0.0f, 1.0f);
                    if (x1 > x0 + 0.5f) {
                        draw->AddRectFilledMultiColor(
                            ImVec2(x0, pMin.y), ImVec2(x1, pMax.y),
                            colorToImU32(a.color), colorToImU32(b.color),
                            colorToImU32(b.color), colorToImU32(a.color));
                    }
                }
            }
        }
        break;
    }
    case NodeKind::FN_VORONOI: {
        const int cols = 18;
        const int rowsGrid = 5;
        const float cellW = std::max((pMax.x - pMin.x) / static_cast<float>(cols), 1.0f);
        const float cellH = std::max((pMax.y - pMin.y) / static_cast<float>(rowsGrid), 1.0f);

        for (int y = 0; y < rowsGrid; ++y) {
            for (int x = 0; x < cols; ++x) {
                const glm::vec2 uv(
                    (static_cast<float>(x) + 0.5f) / static_cast<float>(cols),
                    (static_cast<float>(y) + 0.5f) / static_cast<float>(rowsGrid));
                const ImU32 col = voronoiPreviewColor(node, uv);
                const ImVec2 a(pMin.x + cellW * static_cast<float>(x),
                               pMin.y + cellH * static_cast<float>(y));
                const ImVec2 b(pMin.x + cellW * static_cast<float>(x + 1),
                               pMin.y + cellH * static_cast<float>(y + 1));
                draw->AddRectFilled(a, b, col);
            }
        }

        break;
    }
    default: {
        // Placeholder: diagonal stripes signaling "preview unavailable".
        const ImU32 a = IM_COL32(48, 50, 58, 255);
        const ImU32 b = IM_COL32(70, 74, 86, 255);
        draw->AddRectFilled(pMin, pMax, a, 3.0f);
        const float stripeW = 8.0f;
        draw->PushClipRect(pMin, pMax, true);
        for (float x = pMin.x - (pMax.y - pMin.y); x < pMax.x; x += stripeW * 2.0f) {
            draw->AddQuadFilled(
                ImVec2(x, pMin.y),
                ImVec2(x + stripeW, pMin.y),
                ImVec2(x + stripeW + (pMax.y - pMin.y), pMax.y),
                ImVec2(x + (pMax.y - pMin.y), pMax.y),
                b);
        }
        draw->PopClipRect();
        break;
    }
    }

    draw->AddRect(pMin, pMax, IM_COL32(20, 20, 24, 255), 3.0f);
}

#pragma endregion

#pragma region 6. Inspector

void ShaderNodeEditor::drawInspector() {
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    Node* node = graph_.findNode(selectedNodeId_);
    if (!node) {
        ImGui::TextDisabled("Select a node to edit its parameters.");
        ImGui::Separator();
        ImGui::TextDisabled("Graph: %zu nodes, %zu links.",
                            graph_.nodes().size(), graph_.links().size());
        ImGui::TextDisabled("Undo: %zu  |  Redo: %zu",
                            undoStack_.size(), redoStack_.size());
        return;
    }

    ImGui::Text("Kind: %s", ShaderNodeGraph::nodeDisplayName(node->kind));
    ImGui::Text("Category: %s", ShaderNodeGraph::nodeCategory(node->kind));
    ImGui::Text("Id: %d", node->id);
    ImGui::Separator();

    auto wrapConstEdit = [&](bool edited) {
        if (edited) {
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }
    };

    switch (node->kind) {
    case NodeKind::CONST_FLOAT:
        wrapConstEdit(ImGui::DragFloat("Value", &node->constValue.x, 0.01f, -100.0f, 100.0f));
        break;
    case NodeKind::CONST_VEC2:
        wrapConstEdit(ImGui::DragFloat2("Value", &node->constValue.x, 0.01f));
        break;
    case NodeKind::CONST_VEC3:
        wrapConstEdit(ImGui::DragFloat3("Value", &node->constValue.x, 0.01f));
        break;
    case NodeKind::CONST_COLOR:
        wrapConstEdit(ImGui::ColorEdit3("Color", &node->constValue.x));
        break;
    default:
        break;
    }

    if (node->kind == NodeKind::BLEND_OPERATIONS) {
        ImGui::Separator();
        ImGui::TextWrapped(
            "Common layer blending modes. It blends two inputs with the selected blend mode.");
        ImGui::TextWrapped(
            "Opacity and opacity mask are multiplied before mixing the source and the result.");
        ImGui::Separator();

        const auto& blendModes = ShaderNodeGraph::blendOperationNames();
        int blendModeIndex = std::clamp(
            node->enumOption, 0, static_cast<int>(blendModes.size()) - 1);
        if (blendModeIndex != node->enumOption) {
            node->enumOption = blendModeIndex;
        }

        if (ImGui::Combo("Blend Operation", &blendModeIndex,
                         blendModes.data(), static_cast<int>(blendModes.size()))) {
            node->enumOption = blendModeIndex;
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }

        bool saturate = node->constValue.y >= 0.5f;
        if (ImGui::Checkbox("Saturate", &saturate)) {
            node->constValue.y = saturate ? 1.0f : 0.0f;
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }

        node->constValue.x = std::clamp(node->constValue.x, 0.0f, 1.0f);
        if (ImGui::SliderFloat("Opacity", &node->constValue.x, 0.0f, 1.0f, "%.2f")) {
            node->constValue.x = std::clamp(node->constValue.x, 0.0f, 1.0f);
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }
    }

    if (node->kind == NodeKind::APPEND) {
        ImGui::Separator();
        ImGui::TextUnformatted("Append");

        const char* appendTypes[] = { "Vector2", "Vector3", "Vector4", "Color" };
        int appendTypeIndex = std::clamp(node->enumOption, 0, 3);
        if (appendTypeIndex != node->enumOption) {
            node->enumOption = appendTypeIndex;
        }

        if (ImGui::Combo("Type", &appendTypeIndex, appendTypes, 4)) {
            node->enumOption = appendTypeIndex;
            if (!node->outputs.empty()) {
                node->outputs.front().type = ShaderNodeGraph::appendOutputType(node->enumOption);
            }
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }
        else if (!node->outputs.empty()) {
            node->outputs.front().type = ShaderNodeGraph::appendOutputType(node->enumOption);
        }
    }

    if (node->kind == NodeKind::FN_VORONOI) {
        ImGui::Separator();
        ImGui::TextWrapped("Voronoi noise generator.");
        ImGui::Separator();

        const auto& methodNames = ShaderNodeGraph::voronoiMethodNames();
        int method = voronoiMethodFromOption(node->enumOption);
        if (ImGui::Combo("Method", &method, methodNames.data(),
                         static_cast<int>(methodNames.size()))) {
            node->enumOption = (node->enumOption & ~kVoronoiMethodMask) | (method & kVoronoiMethodMask);
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }

        const auto& distanceNames = ShaderNodeGraph::voronoiDistanceNames();
        int distance = voronoiDistanceFromOption(node->enumOption);
        if (ImGui::Combo("Distance Function", &distance, distanceNames.data(),
                         static_cast<int>(distanceNames.size()))) {
            node->enumOption =
                (node->enumOption & ~kVoronoiDistanceMask) |
                ((distance & 0x3) << 2);
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }

        const auto& searchQualityNames = ShaderNodeGraph::voronoiSearchQualityNames();
        int searchQuality = std::clamp(static_cast<int>(std::round(node->constValue.z)), 0, 3);
        if (ImGui::Combo("Search Quality", &searchQuality,
                         searchQualityNames.data(),
                         static_cast<int>(searchQualityNames.size()))) {
            node->constValue.z = static_cast<float>(searchQuality);
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }

        int octaves = std::clamp(static_cast<int>(std::round(node->constValue.w)), 1, 8);
        if (ImGui::SliderInt("Octaves", &octaves, 1, 8)) {
            node->constValue.w = static_cast<float>(octaves);
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }

        bool tileable = voronoiTileableFromOption(node->enumOption);
        if (ImGui::Checkbox("Tileable", &tileable)) {
            if (tileable) {
                node->enumOption |= kVoronoiTileableMask;
            } else {
                node->enumOption &= ~kVoronoiTileableMask;
            }
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }

        bool smooth = voronoiSmoothFromOption(node->enumOption);
        if (ImGui::Checkbox("Smooth", &smooth)) {
            if (smooth) {
                node->enumOption |= kVoronoiSmoothMask;
            } else {
                node->enumOption &= ~kVoronoiSmoothMask;
            }
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }

        node->constValue.y = std::clamp(node->constValue.y, -180.0f, 180.0f);
        if (ImGui::DragFloat("Angle", &node->constValue.y, 0.1f, -180.0f, 180.0f, "%.1f")) {
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }

        node->constValue.x = std::max(node->constValue.x, 0.01f);
        if (ImGui::DragFloat("Scale", &node->constValue.x, 0.05f, 0.01f, 1000.0f, "%.2f")) {
            node->constValue.x = std::max(node->constValue.x, 0.01f);
            graphDirty_ = true;
            if (ImGui::IsItemActivated()) {
                pushUndoSnapshot();
            }
        }
    }

    if (node->kind == NodeKind::OP_GRADIENT) {
        ImGui::Separator();
        ImGui::TextUnformatted("Color Ramp");

        const std::vector<GradientStop> sortedStops = sortedGradientStops(node->gradientStops);
        const float previewWidth = ImGui::GetContentRegionAvail().x;
        const ImVec2 previewSize(std::max(previewWidth, 1.0f), 32.0f);
        ImGui::InvisibleButton("##gradientPreview", previewSize);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 previewMin = ImGui::GetItemRectMin();
        const ImVec2 previewMax = ImGui::GetItemRectMax();
        const ImU32 previewBg = IM_COL32(34, 34, 38, 255);
        const ImU32 previewBorder = IM_COL32(90, 90, 100, 255);
        draw->AddRectFilled(previewMin, previewMax, previewBg, 4.0f);

        if (sortedStops.empty()) {
            draw->AddRectFilled(previewMin, previewMax, IM_COL32(255, 255, 255, 255), 4.0f);
        } else if (sortedStops.size() == 1) {
            draw->AddRectFilled(previewMin, previewMax,
                                colorToImU32(sortedStops.front().color), 4.0f);
        } else {
            const float previewSpan = std::max(previewMax.x - previewMin.x, 1.0f);
            for (std::size_t i = 0; i + 1 < sortedStops.size(); ++i) {
                const GradientStop& a = sortedStops[i];
                const GradientStop& b = sortedStops[i + 1];
                const float x0 = previewMin.x + previewSpan * std::clamp(a.position, 0.0f, 1.0f);
                const float x1 = previewMin.x + previewSpan * std::clamp(b.position, 0.0f, 1.0f);
                if (x1 <= x0 + 0.5f) {
                    draw->AddLine(ImVec2(x0, previewMin.y), ImVec2(x0, previewMax.y),
                                  colorToImU32(b.color), 2.0f);
                } else {
                    draw->AddRectFilledMultiColor(
                        ImVec2(x0, previewMin.y),
                        ImVec2(x1, previewMax.y),
                        colorToImU32(a.color),
                        colorToImU32(b.color),
                        colorToImU32(b.color),
                        colorToImU32(a.color));
                }
            }
        }

        for (const GradientStop& stop : sortedStops) {
            const float t = std::clamp(stop.position, 0.0f, 1.0f);
            const float x = previewMin.x + (previewMax.x - previewMin.x) * t;
            draw->AddLine(ImVec2(x, previewMin.y), ImVec2(x, previewMax.y),
                          IM_COL32(255, 255, 255, 100), 1.0f);
            draw->AddCircleFilled(ImVec2(x, previewMin.y + 2.0f), 3.0f,
                                  IM_COL32(255, 255, 255, 220));
        }

        draw->AddRect(previewMin, previewMax, previewBorder, 4.0f);

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::Button("Add Stop")) {
            pushUndoSnapshot();
            GradientStop stop;
            stop.position = 0.5f;
            stop.color = sampleGradientColor(node->gradientStops, stop.position);
            node->gradientStops.push_back(stop);
            graphDirty_ = true;
        }

        ImGui::Separator();

        int removeIndex = -1;
        for (std::size_t i = 0; i < node->gradientStops.size(); ++i) {
            GradientStop& stop = node->gradientStops[i];
            ImGui::PushID(static_cast<int>(i));

            ImGui::Text("Stop %zu", i + 1);
            ImGui::SetNextItemWidth(-1.0f);
            wrapConstEdit(ImGui::SliderFloat("Position", &stop.position, 0.0f, 1.0f, "%.3f"));
            ImGui::SetNextItemWidth(-1.0f);
            wrapConstEdit(ImGui::ColorEdit4("Color", &stop.color.x));

            const bool canRemove = node->gradientStops.size() > 2;
            if (!canRemove) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Remove Stop")) {
                removeIndex = static_cast<int>(i);
            }
            if (!canRemove) {
                ImGui::EndDisabled();
            }

            if (i + 1 < node->gradientStops.size()) {
                ImGui::Separator();
            }

            ImGui::PopID();
        }

        if (removeIndex >= 0) {
            pushUndoSnapshot();
            node->gradientStops.erase(node->gradientStops.begin() + removeIndex);
            graphDirty_ = true;
        }
    }

    if (!node->inputs.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Inputs");
        for (std::size_t i = 0; i < node->inputs.size(); ++i) {
            InputPin& pin = node->inputs[i];
            const std::string label = pin.name + " (" +
                ShaderNodeGraph::pinTypeName(pin.type) + ")";
            ImGui::PushID(static_cast<int>(i));
            if (pin.connectedLinkId > 0) {
                ImGui::Text("%s — connected", label.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Disconnect")) {
                    pushUndoSnapshot();
                    graph_.disconnectInput(node->id, static_cast<int>(i));
                    graphDirty_ = true;
                }
            } else {
                bool edited = false;
                switch (pin.type) {
                case PinDataType::FLOAT:
                    edited = ImGui::DragFloat(label.c_str(), &pin.defaultValue.x, 0.01f);
                    break;
                case PinDataType::VEC2:
                    edited = ImGui::DragFloat2(label.c_str(), &pin.defaultValue.x, 0.01f);
                    break;
                case PinDataType::VEC3:
                    edited = ImGui::DragFloat3(label.c_str(), &pin.defaultValue.x, 0.01f);
                    break;
                case PinDataType::VEC4:
                    edited = ImGui::DragFloat4(label.c_str(), &pin.defaultValue.x, 0.01f);
                    break;
                }
                if (edited) {
                    graphDirty_ = true;
                    if (ImGui::IsItemActivated()) {
                        pushUndoSnapshot();
                    }
                }
            }
            ImGui::PopID();
        }
    }

    if (node->kind != NodeKind::OUTPUT_MATERIAL) {
        ImGui::Separator();
        if (ImGui::Button("Delete Node")) {
            pushUndoSnapshot();
            const int removedId = node->id;
            graph_.removeNode(removedId);
            multiSelection_.erase(removedId);
            selectedNodeId_ = -1;
            graphDirty_ = true;
        }
    }
}

#pragma endregion

#pragma region 7. Context Menu

void ShaderNodeEditor::drawContextMenu() {
    if (contextMenuRequested_) {
        const char* popupId =
            contextMenuKind_ == ContextMenuKind::ADD  ? "##shaderNodeAddMenu" :
            contextMenuKind_ == ContextMenuKind::NODE ? "##shaderNodeNodeMenu" :
                                                        "##shaderNodeLinkMenu";
        ImGui::OpenPopup(popupId);
        if (contextMenuKind_ == ContextMenuKind::ADD) {
            std::memset(addMenuSearchBuf_, 0, sizeof(addMenuSearchBuf_));
            addMenuSearchJustOpened_ = true;
        }
        contextMenuRequested_ = false;
    }

    if (contextMenuKind_ == ContextMenuKind::ADD) {
        if (ImGui::BeginPopup("##shaderNodeAddMenu")) {
            ImGui::TextDisabled("Add Node");
            ImGui::Separator();

            const char* clipboardText = ImGui::GetClipboardText();
            const bool hasClipboardNodes = !nodeClipboardPayload_.empty() ||
                (clipboardText && std::strstr(clipboardText, kNodeClipboardFormat) != nullptr);
            ImGui::BeginDisabled(!hasClipboardNodes);
            if (ImGui::MenuItem("Paste Nodes", "Ctrl+V")) {
                pasteNodesFromClipboard(contextMenuCanvasPos_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::Separator();

            // Auto-focus the search field when the menu first opens so the user
            // can start typing immediately without clicking first.
            if (addMenuSearchJustOpened_) {
                ImGui::SetKeyboardFocusHere();
                addMenuSearchJustOpened_ = false;
            }
            ImGui::SetNextItemWidth(-1.0f);
            const bool enterPressed = ImGui::InputText(
                "##nodeSearch", addMenuSearchBuf_, sizeof(addMenuSearchBuf_),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Separator();

            const bool hasSearch = addMenuSearchBuf_[0] != '\0';

            if (hasSearch) {
                // Build a lowercase copy of the query for case-insensitive matching.
                std::string query = addMenuSearchBuf_;
                for (char& c : query) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }

                // Single pass: collect matches and prefer exact matches when available.
                NodeKind firstMatch = NodeKind::COUNT_;
                NodeKind exactMatch = NodeKind::COUNT_;
                bool anyMatch = false;
                for (int i = 0; i < static_cast<int>(NodeKind::COUNT_); ++i) {
                    const NodeKind kind = static_cast<NodeKind>(i);
                    std::string nameLower = ShaderNodeGraph::nodeDisplayName(kind);
                    for (char& c : nameLower) {
                        c = static_cast<char>(
                            std::tolower(static_cast<unsigned char>(c)));
                    }
                    if (nameLower.find(query) == std::string::npos) continue;
                    if (!anyMatch) firstMatch = kind;
                    if (nameLower == query && exactMatch == NodeKind::COUNT_) {
                        exactMatch = kind;
                    }
                    anyMatch = true;
                }

                const NodeKind chosenMatch =
                    exactMatch != NodeKind::COUNT_ ? exactMatch : firstMatch;

                // Enter confirms the best match without going through the list.
                if (enterPressed && anyMatch) {
                    pushUndoSnapshot();
                    graph_.addNode(chosenMatch, contextMenuCanvasPos_);
                    graphDirty_ = true;
                    ImGui::CloseCurrentPopup();
                } else if (anyMatch) {
                    for (int i = 0; i < static_cast<int>(NodeKind::COUNT_); ++i) {
                        const NodeKind kind = static_cast<NodeKind>(i);
                        const char* name = ShaderNodeGraph::nodeDisplayName(kind);
                        std::string nameLower = name;
                        for (char& c : nameLower) {
                            c = static_cast<char>(
                                std::tolower(static_cast<unsigned char>(c)));
                        }
                        if (nameLower.find(query) == std::string::npos) continue;
                        // Highlight the Enter result so the user knows which
                        // node will be added on Enter.
                        const bool isFirst = (kind == chosenMatch);
                        if (isFirst) ImGui::PushStyleColor(ImGuiCol_Text,
                                                           IM_COL32(255, 220, 100, 255));
                        if (ImGui::MenuItem(name)) {
                            pushUndoSnapshot();
                            graph_.addNode(kind, contextMenuCanvasPos_);
                            graphDirty_ = true;
                            ImGui::CloseCurrentPopup();
                        }
                        if (isFirst) ImGui::PopStyleColor();
                    }
                } else {
                    ImGui::TextDisabled("No nodes match \"%s\".", addMenuSearchBuf_);
                }
            } else {
                // No query: show the regular category submenus.
                for (const char* category : kCategoryOrder) {
                    if (ImGui::BeginMenu(category)) {
                        for (int i = 0; i < static_cast<int>(NodeKind::COUNT_); ++i) {
                            const NodeKind kind = static_cast<NodeKind>(i);
                            if (std::strcmp(ShaderNodeGraph::nodeCategory(kind),
                                            category) != 0) {
                                continue;
                            }
                            if (ImGui::MenuItem(
                                    ShaderNodeGraph::nodeDisplayName(kind))) {
                                pushUndoSnapshot();
                                graph_.addNode(kind, contextMenuCanvasPos_);
                                graphDirty_ = true;
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
            }

            ImGui::EndPopup();
        }
        return;
    }

    if (contextMenuKind_ == ContextMenuKind::NODE) {
        if (ImGui::BeginPopup("##shaderNodeNodeMenu")) {
            Node* node = graph_.findNode(contextMenuTargetNode_);
            if (!node) {
                ImGui::EndPopup();
                return;
            }
            ImGui::TextDisabled("%s", ShaderNodeGraph::nodeDisplayName(node->kind));
            ImGui::Separator();

            if (ImGui::MenuItem("Copy Nodes", "Ctrl+C")) {
                copySelectedNodes();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Duplicate Nodes", "Ctrl+D")) {
                duplicateSelectedNodes();
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(node->previewEnabled
                                    ? "Hide Preview"
                                    : "Show Preview")) {
                pushUndoSnapshot();
                node->previewEnabled = !node->previewEnabled;
                graphDirty_ = true;
            }
            ImGui::Separator();

            // Count connections so entries can be disabled sensibly.
            int connectedInputs = 0;
            for (const InputPin& pin : node->inputs) {
                if (pin.connectedLinkId > 0) ++connectedInputs;
            }
            int connectedOutputs = 0;
            for (const Link& link : graph_.links()) {
                if (link.from.nodeId == node->id) ++connectedOutputs;
            }

            ImGui::BeginDisabled(connectedInputs == 0);
            if (ImGui::MenuItem("Disconnect All Inputs")) {
                pushUndoSnapshot();
                for (std::size_t i = 0; i < node->inputs.size(); ++i) {
                    graph_.disconnectInput(node->id, static_cast<int>(i));
                }
                graphDirty_ = true;
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(connectedOutputs == 0);
            if (ImGui::MenuItem("Disconnect All Outputs")) {
                pushUndoSnapshot();
                // Collect target ids first because disconnect() mutates links_.
                std::vector<int> toDrop;
                toDrop.reserve(graph_.links().size());
                for (const Link& link : graph_.links()) {
                    if (link.from.nodeId == node->id) toDrop.push_back(link.id);
                }
                for (int linkId : toDrop) {
                    graph_.disconnect(linkId);
                }
                graphDirty_ = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::BeginDisabled(node->kind == NodeKind::OUTPUT_MATERIAL);
            if (ImGui::MenuItem("Delete Node")) {
                pushUndoSnapshot();
                const int removedId = node->id;
                graph_.removeNode(removedId);
                if (selectedNodeId_ == removedId) selectedNodeId_ = -1;
                multiSelection_.erase(removedId);
                graphDirty_ = true;
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        return;
    }

    if (contextMenuKind_ == ContextMenuKind::LINK) {
        if (ImGui::BeginPopup("##shaderNodeLinkMenu")) {
            ImGui::TextDisabled("Link %d", contextMenuTargetLink_);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Link")) {
                pushUndoSnapshot();
                graph_.disconnect(contextMenuTargetLink_);
                if (selectedLinkId_ == contextMenuTargetLink_) selectedLinkId_ = -1;
                graphDirty_ = true;
            }
            ImGui::EndPopup();
        }
        return;
    }
}

#pragma endregion

#pragma region 8. Generated Preview

void ShaderNodeEditor::drawGeneratedPreview() {
    if (ImGui::Begin("Generated GLSL", &showGeneratedPreview_)) {
        ImGui::TextDisabled("Read-only preview of what is written to shader.glsl.");
        ImGui::Separator();
        if (lastGeneratedGlsl_.empty()) {
            ImGui::TextDisabled("(empty)");
        } else {
            ImGui::BeginChild("##glslScroll", ImVec2(0, 0), true);
            ImGui::TextUnformatted(lastGeneratedGlsl_.c_str());
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

#pragma endregion

#pragma region 9. Apply

void ShaderNodeEditor::applyToBlock(const ShaderNodeEditorCallbacks& callbacks,
                                    bool triggeredByAutoApply) {
    if (!callbacks.onApplyToBlock || !callbacks.currentBlockShaderPath) {
        statusMessage_ = "Apply callback not wired.";
        return;
    }

    const std::filesystem::path target = callbacks.currentBlockShaderPath();
    if (target.empty()) {
        statusMessage_ = "No target block shader path.";
        return;
    }
    currentBlockShaderPath_ = target;

    const ShaderNodeGraph::CodegenResult result = graph_.buildGlsl();
    if (!result.ok) {
        statusMessage_ = "Codegen failed: " + result.error;
        return;
    }

    lastGeneratedGlsl_ = result.glsl;

    std::string err;
    if (!callbacks.onApplyToBlock(target, result.glsl, err)) {
        statusMessage_ = "Apply failed: " + err;
        return;
    }

    // Persist the graph sidecar so the block reloads the same graph next time.
    std::string sidecarErr;
    if (!saveSidecar(sidecarErr)) {
        statusMessage_ = "Shader applied, but sidecar save failed: " + sidecarErr;
        return;
    }

    statusMessage_ = triggeredByAutoApply
        ? std::string("Auto-applied to ") + target.filename().string()
        : std::string("Applied to ") + target.string();
}

#pragma endregion

} // namespace ShaderEditor
