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
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <imgui.h>
#include <imgui_internal.h>

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
    const bool hasConst =
        node.kind == NodeKind::CONST_FLOAT || node.kind == NodeKind::CONST_VEC2 ||
        node.kind == NodeKind::CONST_VEC3 || node.kind == NodeKind::CONST_COLOR;
    const float constExtra = hasConst ? 28.0f * zoom_ : 0.0f;
    return titleHeight() + baseBody + constExtra + nodePadding();
}

glm::vec2 ShaderNodeEditor::nodeScreenPos(const Node& node) const {
    return glm::vec2(canvasOrigin_.x + panOffset_.x + node.position.x * zoom_,
                     canvasOrigin_.y + panOffset_.y + node.position.y * zoom_);
}

glm::vec2 ShaderNodeEditor::inputPinPos(const Node& node, int inputIndex,
                                        const glm::vec2& nodePos) const {
    return glm::vec2(nodePos.x,
                     nodePos.y + titleHeight() + pinGap() * 0.75f +
                         pinGap() * static_cast<float>(inputIndex));
}

glm::vec2 ShaderNodeEditor::outputPinPos(const Node& node, int outputIndex,
                                         const glm::vec2& nodePos) const {
    return glm::vec2(nodePos.x + nodeWidth(),
                     nodePos.y + titleHeight() + pinGap() * 0.75f +
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
            }
        }
    }

    drawToolbar(callbacks);

    ImGui::BeginChild("##nodeEditorMain", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float totalWidth = ImGui::GetContentRegionAvail().x;
    const float inspectorWidth = std::max(totalWidth * 0.28f, 240.0f);
    const float canvasWidth = std::max(totalWidth - inspectorWidth - 6.0f, 200.0f);

    ImGui::BeginChild("##nodeCanvas", ImVec2(canvasWidth, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas(callbacks);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##nodeInspector", ImVec2(0, 0), true);
    drawInspector();
    ImGui::EndChild();

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
    ImGui::TextDisabled("Zoom %.2fx  |  Right-click: menu, Del: remove, Ctrl+Z: undo",
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

    // Rubber-band selection overlay (drawn above nodes so it stays visible).
    if (selectionRectActive_) {
        const ImVec2 a(canvasOrigin_.x + panOffset_.x + selectionRectStart_.x * zoom_,
                       canvasOrigin_.y + panOffset_.y + selectionRectStart_.y * zoom_);
        const ImVec2 b(canvasOrigin_.x + panOffset_.x + selectionRectEnd_.x * zoom_,
                       canvasOrigin_.y + panOffset_.y + selectionRectEnd_.y * zoom_);
        draw->AddRectFilled(a, b, IM_COL32(100, 150, 230, 40));
        draw->AddRect(a, b, IM_COL32(160, 200, 255, 200));
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

                // Single pass: find the first match and collect all matches.
                NodeKind firstMatch = NodeKind::COUNT_;
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
                    anyMatch = true;
                }

                // Enter confirms the first match without going through the list.
                if (enterPressed && anyMatch) {
                    pushUndoSnapshot();
                    graph_.addNode(firstMatch, contextMenuCanvasPos_);
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
                        // Highlight the first (Enter) result so the user knows
                        // which node will be added on Enter.
                        const bool isFirst = (kind == firstMatch);
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
