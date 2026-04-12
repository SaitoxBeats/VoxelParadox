#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "engine/shader.hpp"
#include "world/block/block.hpp"

namespace ShaderEditor {

struct ShaderDiagnostic {
  std::string stage;
  std::filesystem::path filePath{};
  int line = -1;
  std::string message;
};

class BlockShaderSession {
public:
  bool initialize(double nowSeconds);
  void update(double nowSeconds);
  bool requestReload(double nowSeconds, const char* trigger = "Manual reload");
  bool setPreviewBlock(BlockId blockId, double nowSeconds);

  void setHotReloadEnabled(bool enabled) { hotReloadEnabled_ = enabled; }
  bool hotReloadEnabled() const { return hotReloadEnabled_; }

  const std::filesystem::path& fragmentPath() const { return fragmentTemplatePath_; }
  const std::filesystem::path& vertexPath() const { return vertexPath_; }
  const std::filesystem::path& blockShaderPath() const { return blockShaderPath_; }
  BlockId previewBlockId() const { return previewBlockId_; }
  const Shader* activeShader() const { return valid_ ? &shader_ : nullptr; }
  bool hasValidShader() const { return valid_; }
  bool sourceDirty() const { return sourceDirty_; }

  const std::string& statusMessage() const { return statusMessage_; }
  const std::string& lastAttemptText() const { return lastAttemptText_; }
  const std::string& lastSuccessText() const { return lastSuccessText_; }
  const std::vector<ShaderDiagnostic>& diagnostics() const { return diagnostics_; }
  const std::string& rawLog() const { return rawLog_; }

private:
  struct SourceFingerprint {
    bool registryExists = false;
    bool vertexExists = false;
    bool fragmentExists = false;
    bool blockShaderExists = false;
    bool blockTextureExists = false;
    std::filesystem::file_time_type registryWrite{};
    std::filesystem::file_time_type vertexWrite{};
    std::filesystem::file_time_type fragmentWrite{};
    std::filesystem::file_time_type blockShaderWrite{};
    std::filesystem::file_time_type blockTextureWrite{};

    bool operator==(const SourceFingerprint& other) const {
      return registryExists == other.registryExists &&
             vertexExists == other.vertexExists &&
             fragmentExists == other.fragmentExists &&
             blockShaderExists == other.blockShaderExists &&
             blockTextureExists == other.blockTextureExists &&
             (!registryExists || registryWrite == other.registryWrite) &&
             (!vertexExists || vertexWrite == other.vertexWrite) &&
             (!fragmentExists || fragmentWrite == other.fragmentWrite) &&
             (!blockShaderExists || blockShaderWrite == other.blockShaderWrite) &&
             (!blockTextureExists || blockTextureWrite == other.blockTextureWrite);
    }

    bool operator!=(const SourceFingerprint& other) const {
      return !(*this == other);
    }
  };

  static constexpr double kDebounceSeconds = 0.20;
  static constexpr double kRetrySeconds = 0.75;

  std::filesystem::path registryPath_{"Assets/Blocks/registry.json"};
  std::filesystem::path fragmentTemplatePath_{"Assets/Shaders/block.frag"};
  std::filesystem::path vertexPath_{"Assets/Shaders/block.vert"};
  std::filesystem::path blockShaderPath_{};
  std::filesystem::path blockTexturePath_{};
  BlockId previewBlockId_ = BlockIds::STONE;

  Shader shader_{};
  std::vector<ShaderDiagnostic> diagnostics_{};
  std::string rawLog_{};
  std::string statusMessage_{"Waiting for the initial shader compile."};
  std::string lastAttemptText_{"Never"};
  std::string lastSuccessText_{"Never"};

  bool valid_ = false;
  bool hotReloadEnabled_ = true;
  bool sourceDirty_ = false;
  bool fingerprintInitialized_ = false;
  double lastChangeDetectedSeconds_ = 0.0;
  double lastAttemptSeconds_ = -1000.0;
  SourceFingerprint observedFingerprint_{};
  SourceFingerprint appliedFingerprint_{};

  SourceFingerprint readFingerprint() const;
  bool compileCurrentSelection(double nowSeconds, const char* trigger);
  static std::string formatCurrentTimestamp();
  static bool readTextFile(const std::filesystem::path& path, std::string& outText,
                           std::string& outError);
  static std::string readShaderLog(GLuint shader);
  static std::string readProgramLog(GLuint program);
  static bool compileStage(GLenum shaderType, const std::string& source,
                           const std::filesystem::path& filePath,
                           const char* stageName, GLuint& outShader,
                           std::string& outLog,
                           std::vector<ShaderDiagnostic>& outDiagnostics);
  static void appendDiagnosticsFromLog(
      std::vector<ShaderDiagnostic>& outDiagnostics, const std::string& log,
      const std::filesystem::path& filePath, const char* stageName);
};

} // namespace ShaderEditor
