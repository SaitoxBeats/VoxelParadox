#pragma once

// 1. Standard Library
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "shader_node_graph.hpp"

namespace ShaderEditor {

// Callbacks the host app plugs in to react to user actions in the editor.
struct ShaderNodeEditorCallbacks {
    // Fired when the user clicks "Apply to Block" and the graph produced valid GLSL.
    // The target path is the block's shader.glsl (already resolved). Implementer
    // is expected to write the GLSL and trigger a shader reload.
    std::function<bool(const std::filesystem::path& targetPath,
                       const std::string& glslBody,
                       std::string& outError)> onApplyToBlock;

    // The currently previewed block's shader.glsl path (may be empty until a
    // block is selected). Used as the default "Apply" destination and the
    // sidecar-graph auto-load source.
    std::function<std::filesystem::path()> currentBlockShaderPath;

    // Display name of the currently previewed block, for header display.
    std::function<std::string()> currentBlockDisplayName;
};

class ShaderNodeEditor {
public:
    void init();
    void draw(const ShaderNodeEditorCallbacks& callbacks);

    // Switch the editor to the block at `blockShaderPath`, loading its sidecar
    // graph (`<block>/shader.nodegraph.json`) if present or falling back to the
    // default passthrough graph. Idempotent if the path hasn't changed.
    void loadForBlock(const std::filesystem::path& blockShaderPath);

    // Undo/redo history commands.
    void undo();
    void redo();
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    ShaderNodeGraph& graph() { return graph_; }
    const ShaderNodeGraph& graph() const { return graph_; }

    const std::string& lastStatus() const { return statusMessage_; }

private:
    // Canvas state.
    glm::vec2 canvasOrigin_{ 0.0f, 0.0f };
    glm::vec2 panOffset_{ 0.0f, 0.0f };
    float zoom_ = 1.0f;

    // Interaction state.
    int draggedNodeId_ = -1;
    glm::vec2 dragGrabOffset_{ 0.0f, 0.0f };
    int hoveredNodeId_ = -1;
    int selectedNodeId_ = -1;
    int selectedLinkId_ = -1;

    // Multi-selection: nodes that participate in group drag + group delete.
    // selectedNodeId_ stays the "primary" for the inspector; this set is the
    // full group and always contains selectedNodeId_ when it is valid.
    std::unordered_set<int> multiSelection_{};

    // Drag-to-select rectangle on empty canvas.
    bool selectionRectActive_ = false;
    glm::vec2 selectionRectStart_{ 0.0f, 0.0f };  // canvas-world coords
    glm::vec2 selectionRectEnd_{ 0.0f, 0.0f };

    // Pending connection.
    bool linkDragActive_ = false;
    PinRef linkDragFrom_{};
    bool linkDragIsOutput_ = true;  // true => user grabbed an output pin

    // Context menu state. One of addMenu / nodeMenu / linkMenu opens based on
    // what the right-click landed on.
    enum class ContextMenuKind { NONE, ADD, NODE, LINK };
    ContextMenuKind contextMenuKind_ = ContextMenuKind::NONE;
    bool contextMenuRequested_ = false;
    glm::vec2 contextMenuCanvasPos_{ 0.0f };
    int contextMenuTargetNode_ = -1;
    int contextMenuTargetLink_ = -1;

    // Graph + persistence.
    ShaderNodeGraph graph_{};
    std::filesystem::path currentBlockShaderPath_{};
    bool graphDirty_ = false;
    std::string statusMessage_{};
    std::string lastGeneratedGlsl_{};
    bool showGeneratedPreview_ = false;
    bool autoApplyEnabled_ = false;

    // Undo/redo. Serialized JSON snapshots; cap with kMaxHistory to stay bounded.
    std::deque<std::string> undoStack_{};
    std::deque<std::string> redoStack_{};
    bool snapshotPendingForDrag_ = false;
    static constexpr std::size_t kMaxHistory = 64;

    // Add-node search bar.
    char addMenuSearchBuf_[128]{};
    bool addMenuSearchJustOpened_ = false;

    // Drawing helpers.
    void drawToolbar(const ShaderNodeEditorCallbacks& callbacks);
    void drawCanvas(const ShaderNodeEditorCallbacks& callbacks);
    void drawInspector();
    void drawContextMenu();
    void drawGeneratedPreview();

    void applyToBlock(const ShaderNodeEditorCallbacks& callbacks,
                      bool triggeredByAutoApply);
    std::filesystem::path sidecarPathFor(
        const std::filesystem::path& shaderPath) const;
    bool saveSidecar(std::string& outError) const;

    // Undo helpers.
    void pushUndoSnapshot();
    void resetHistory();

    // Scaled metrics (zoom-aware). Pure functions of zoom_.
    float nodeWidth() const;
    float pinGap() const;
    float pinRadius() const;
    float titleHeight() const;
    float nodePadding() const;
    float nodeRounding() const;
    float linkThickness() const;
    float fontSize() const;
    float nodeHeight(const Node& node) const;
    glm::vec2 nodeScreenPos(const Node& node) const;
    glm::vec2 inputPinPos(const Node& node, int inputIndex,
                          const glm::vec2& nodePos) const;
    glm::vec2 outputPinPos(const Node& node, int outputIndex,
                           const glm::vec2& nodePos) const;
};

} // namespace ShaderEditor
