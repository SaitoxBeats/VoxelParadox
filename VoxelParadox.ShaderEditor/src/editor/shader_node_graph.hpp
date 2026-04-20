#pragma once

// 1. Standard Library
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <glm/glm.hpp>

namespace ShaderEditor {

enum class PinDataType : std::uint8_t {
    FLOAT = 0,
    VEC2 = 1,
    VEC3 = 2,
    VEC4 = 3,
};

enum class NodeKind : std::uint16_t {
    // Inputs bound to shader locals/globals declared in block.frag dispatch site.
    IN_BASE_COLOR = 0,
    IN_UV,
    IN_LOCAL_UV,
    IN_CENTERED_UV,
    IN_WORLD_POS,
    IN_WORLD_NORMAL,
    IN_FACE_NORMAL,
    IN_VIEW_DIR,
    IN_BLOCK_COLOR,
    IN_BLOCK_ALPHA,
    IN_BLOCK_TEXEL,
    IN_CELL,
    IN_CELL_HASH,
    IN_TIME,
    IN_BIOME_TINT,

    // Constants (user editable).
    CONST_FLOAT,
    CONST_VEC2,
    CONST_VEC3,
    CONST_COLOR,

    // Math / vector ops.
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MIX,
    OP_CLAMP,
    OP_SATURATE,
    OP_SMOOTHSTEP,
    OP_POWER,
    OP_SIN,
    OP_COS,
    OP_FRACT,
    OP_FLOOR,
    OP_ABS,
    OP_STEP,
    OP_DOT,
    OP_NORMALIZE,
    OP_LENGTH,
    OP_FRESNEL,

    // Noise helpers provided by block.frag.
    FN_HASH21,
    FN_HASH31,
    FN_NOISE21,
    FN_FBM21,

    // Channel packing / swizzle.
    COMBINE_V2,
    COMBINE_V3,
    SPLIT_V2,
    SPLIT_V3,
    SWIZZLE_TO_VEC3_RGB,

    // Final output.
    OUTPUT_MATERIAL,

    // Appended to preserve int values of saved graphs.
    // Procedural noise generators (scale + optional [0,1] remap).
    FN_NOISE_SIMPLEX_2D,
    FN_NOISE_SIMPLEX_3D,
    FN_NOISE_GRADIENT,
    FN_NOISE_SIMPLE,

    // Truchet pattern generators (scale + resolution).
    FN_TRUCHET_MAZE,
    FN_TRUCHET_CIRCLES,
    FN_TRUCHET_TRIANGLES,

    // Distortions / UV effects.
    FX_DISTORT_UV,
    FX_SWIRL,
    FX_RIPPLE,

    // Compound presets: single node emitting a stone-like albedo + roughness.
    PRESET_STONE_ROUGH,
    PRESET_STONE_CRACKED,

    // World-space seamless UV projection using faceNormal + worldPos.
    SEAMLESS_UV,

    // Editable color ramp sampled from a projected 2D segment with ramp shaping.
    OP_GRADIENT,

    // Radial UV shear around a configurable center point with an offset bias.
    FX_RADIAL_SHEAR,

    // UV panner driven by a speed vector and time scalar.
    FX_PANNER,

    // Extra appended nodes.
    TEXTURE_COORDINATES,
    APPEND,
    BLEND_OPERATIONS,

    // Quantize UVs into a pixel grid for a stepped / pixelated look.
    FX_PIXELATE_UV,

    // Rotate UVs (or any vec2) around an anchor point by an angle (radians) —
    // the "time" input is the rotation amount so plugging uTime spins the UVs.
    FX_ROTATOR,

    // Voronoi noise generator.
    FN_VORONOI,

    COUNT_,
};

struct PinRef {
    int nodeId = -1;  // owning node id
    int pinIndex = -1;  // index within the node's input or output list

    bool valid() const { return nodeId >= 0 && pinIndex >= 0; }
};

struct Link {
    int id = -1;
    PinRef from{};  // output pin
    PinRef to{};    // input pin
};

// A single input slot on a node. If unlinked, falls back to inline defaultValue.
struct InputPin {
    std::string name;
    PinDataType type = PinDataType::FLOAT;
    glm::vec4 defaultValue{ 0.0f };
    int connectedLinkId = -1;
};

struct OutputPin {
    std::string name;
    PinDataType type = PinDataType::FLOAT;
};

// A single stop in a gradient ramp.
struct GradientStop {
    float position = 0.0f;  // Normalized [0,1] ramp coordinate.
    glm::vec4 color{ 1.0f };
};

struct Node {
    int id = -1;
    NodeKind kind = NodeKind::CONST_FLOAT;
    glm::vec2 position{ 0.0f };
    std::vector<InputPin> inputs{};
    std::vector<OutputPin> outputs{};

    // Node-local editable data: color pickers, floats, enum indices, gradient ramps, etc.
    glm::vec4 constValue{ 1.0f };
    int enumOption = 0;  // swizzle component, etc.
    std::vector<GradientStop> gradientStops{};

    // Per-node optional preview swatch rendered below the pins in the canvas.
    bool previewEnabled = false;
};

class ShaderNodeGraph {
public:
    ShaderNodeGraph();

    // Graph mutation.
    int addNode(NodeKind kind, const glm::vec2& position);
    void removeNode(int nodeId);
    bool connect(const PinRef& from, const PinRef& to);
    void disconnect(int linkId);
    void disconnectInput(int nodeId, int inputIndex);
    void clear();
    void resetToDefault();

    // Accessors.
    const std::vector<Node>& nodes() const { return nodes_; }
    std::vector<Node>& nodes() { return nodes_; }
    const std::vector<Link>& links() const { return links_; }
    Node* findNode(int nodeId);
    const Node* findNode(int nodeId) const;
    const Link* findLink(int linkId) const;

    int outputNodeId() const;

    // Codegen: build a GLSL snippet suitable for the block shader body.
    // Signature matches what block_registry.cpp wraps around definition.shaderSource,
    // so the result must end with `return makeSample(...);`.
    struct CodegenResult {
        bool ok = false;
        std::string glsl;
        std::string error;
    };
    CodegenResult buildGlsl() const;

    // JSON persistence.
    bool saveToFile(const std::filesystem::path& path, std::string& outError) const;
    bool loadFromFile(const std::filesystem::path& path, std::string& outError);
    std::string serialize() const;
    bool deserialize(const std::string& json, std::string& outError);

    // Factory for node default configuration (pin names, types, defaults).
    static void configureNode(Node& node);
    static const char* nodeDisplayName(NodeKind kind);
    static const char* nodeCategory(NodeKind kind);
    static const char* pinTypeName(PinDataType type);
    static PinDataType appendOutputType(int mode);
    static const char* appendOutputTypeName(int mode);
    static const std::array<const char*, 16>& blendOperationNames();
    static const char* blendOperationName(int mode);
    static const std::array<const char*, 4>& voronoiMethodNames();
    static const char* voronoiMethodName(int mode);
    static const std::array<const char*, 4>& voronoiDistanceNames();
    static const char* voronoiDistanceName(int mode);
    static const std::array<const char*, 4>& voronoiSearchQualityNames();
    static const char* voronoiSearchQualityName(int mode);

private:
    std::vector<Node> nodes_{};
    std::vector<Link> links_{};
    int nextNodeId_ = 1;
    int nextLinkId_ = 1;
};

} // namespace ShaderEditor
