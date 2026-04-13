// shader_editor_app.cpp
// Unity mental model: Main application loop and UI manager for the Shader Editor.
// Orchestrates the live shader reloading, preview viewport, and diagnostics tools.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <chrono>

// 2. Third-party Libraries
#include <glad/glad.h>
#include <imgui.h>

// 3. Internal Engine & Core Modules
#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "path/app_paths.hpp"

// 4. Local Project Modules
#include "shader_editor_app.hpp"
#include "ui/imgui_layer.hpp"

#pragma endregion

namespace ShaderEditor {

#pragma region 1. Constants & Helpers
    // --- 1. Constants & Helpers ---
    namespace {

        constexpr const char* kWindowTitle = "VoxelParadox Shader Editor";
        constexpr const char* kDefaultFontPath = "Assets/Fonts/zpix.ttf";

        constexpr BlockCategory kEditorBlockCategoryOrder[] = {
            BlockCategory::TERRAIN,
            BlockCategory::DECORATION,
            BlockCategory::PORTAL,
            BlockCategory::PLANT,
            BlockCategory::ORGANIC,
        };

        void clearMainFramebuffer(GLFWwindow* window) {
            if (!window) {
                return;
            }

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

            if (framebufferWidth <= 0 || framebufferHeight <= 0) {
                return;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.04f, 0.045f, 0.055f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        }

        BlockCategory primaryEditorCategory(BlockId blockId) {
            const std::uint32_t categories = getBlockCategories(blockId);

            for (const BlockCategory category : kEditorBlockCategoryOrder) {
                const std::uint32_t mask = static_cast<std::uint32_t>(category);
                if ((categories & mask) != 0u) {
                    return category;
                }
            }

            return BlockCategory::NONE;
        }

    } // namespace
#pragma endregion

#pragma region 2. App Lifecycle
// --- 2. App Lifecycle ---

    bool ShaderEditorApp::run() {
        if (!initialize()) {
            shutdown();
            return false;
        }

        double lastTimeSeconds = glfwGetTime();
        ENGINE::INIT(lastTimeSeconds);

        while (!glfwWindowShouldClose(window_)) {
            const double frameTimeSeconds = glfwGetTime();
            float dtSeconds = static_cast<float>(frameTimeSeconds - lastTimeSeconds);
            lastTimeSeconds = frameTimeSeconds;
            dtSeconds = glm::min(dtSeconds, 0.05f);

            ENGINE::UPDATE(frameTimeSeconds, dtSeconds);
            ENGINE::BEGINPERFFRAME();
            const auto cpuFrameStart = std::chrono::steady_clock::now();

            Input::update();
            shaderSession_.update(frameTimeSeconds);

            ImGuiLayer::beginFrame();
            drawUi(dtSeconds, static_cast<float>(frameTimeSeconds));
            clearMainFramebuffer(window_);
            ImGuiLayer::render();

            const auto cpuFrameEnd = std::chrono::steady_clock::now();
            const float cpuFrameMs = std::chrono::duration<float, std::milli>(cpuFrameEnd - cpuFrameStart).count();
            const float fpsInstant = dtSeconds > 0.0f ? (1.0f / dtSeconds) : 0.0f;

            ENGINE::ENDPERFFRAME(cpuFrameMs, fpsInstant);

            glfwSwapBuffers(window_);
        }

        shutdown();
        return true;
    }

    bool ShaderEditorApp::initialize() {
        bootstrapConfig_.windowTitle = kWindowTitle;
        bootstrapConfig_.windowSize = glm::ivec2(1600, 900);
        bootstrapConfig_.viewportMode = ENGINE::VIEWPORTMODE::WINDOWMODE;
        bootstrapConfig_.defaultFontPath = kDefaultFontPath;
        bootstrapConfig_.saveDirectory = "Saves/ShaderEditor";

        if (!Bootstrap::initialize(bootstrapConfig_, window_)) {
            return false;
        }

        glfwMaximizeWindow(window_);

        if (!renderer_.init()) {
            return false;
        }

        ImGuiLayer::Config imguiConfig;
        imguiConfig.dockingEnabled = true;
        imguiConfig.iniFilename = editorIniPath().string();

        const bool imguiInitialized = ImGuiLayer::initialize(window_, imguiConfig);
        Bootstrap::reportImGuiStatus(imguiInitialized);

        if (!imguiInitialized) {
            return false;
        }

        Input::setCursorVisible(true);
        orbitCamera_.reset(camera_);
        shaderSession_.initialize(0.0);

        return true;
    }

    void ShaderEditorApp::shutdown() {
        renderer_.cleanup();
        ImGuiLayer::shutdown();
        ENGINE::CLEANUPPERF();
        Bootstrap::shutdownWindow(window_);
        window_ = nullptr;
    }

    std::filesystem::path ShaderEditorApp::editorIniPath() const {
        return AppPaths::workspaceFile("imgui_shader_editor.ini");
    }

#pragma endregion

#pragma region 3. UI Drawing & Layout
    // --- 3. UI Drawing & Layout ---

    void ShaderEditorApp::drawUi(float dtSeconds, float timeSeconds) {
        currentTimeSeconds_ = timeSeconds;

        drawDockspace();
        drawStatusPanel();
        drawPreviewWindow(dtSeconds, timeSeconds);
        drawControlsWindow();
        drawDiagnosticsWindow();
    }

    void ShaderEditorApp::drawDockspace() {
#ifdef IMGUI_HAS_DOCK
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
#endif
    }

    void ShaderEditorApp::drawStatusPanel() {
        if (!ImGui::Begin("Shader Status")) {
            ImGui::End();
            return;
        }

        const bool validShader = shaderSession_.hasValidShader();

        ImGui::Text("Shader Root");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", AppPaths::shadersRoot().string().c_str());

        ImGui::Text("Loaded Fragment");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", shaderSession_.fragmentPath().generic_string().c_str());

        ImGui::Text("Loaded Vertex");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", shaderSession_.vertexPath().generic_string().c_str());

        ImGui::Text("Block Shader");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", shaderSession_.blockShaderPath().generic_string().c_str());

        ImGui::Separator();

        ImGui::Text("Compilation Status");
        ImGui::SameLine();
        ImGui::TextColored(
            validShader ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
            "%s", validShader ? "Valid" : "Fallback Active"
        );

        bool hotReloadEnabled = shaderSession_.hotReloadEnabled();
        if (ImGui::Checkbox("Hot Reload Enabled", &hotReloadEnabled)) {
            shaderSession_.setHotReloadEnabled(hotReloadEnabled);
        }

        if (ImGui::Button("Reload Now")) {
            shaderSession_.requestReload(currentTimeSeconds_, "Manual reload");
        }

        ImGui::Text("Pending Changes");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", shaderSession_.sourceDirty() ? "Yes" : "No");

        ImGui::TextWrapped("%s", shaderSession_.statusMessage().c_str());
        ImGui::Text("Last Attempt: %s", shaderSession_.lastAttemptText().c_str());
        ImGui::Text("Last Success: %s", shaderSession_.lastSuccessText().c_str());

        ImGui::End();
    }

    void ShaderEditorApp::drawPreviewWindow(float dtSeconds, float timeSeconds) {
        if (!ImGui::Begin("Preview")) {
            ImGui::End();
            return;
        }

        ImVec2 available = ImGui::GetContentRegionAvail();
        available.x = std::max(available.x, 1.0f);
        available.y = std::max(available.y, 1.0f);

        const ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
        const ImVec2 mousePos = ImGui::GetIO().MousePos;

        const bool previewHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
            mousePos.x >= cursorScreenPos.x &&
            mousePos.x <= cursorScreenPos.x + available.x &&
            mousePos.y >= cursorScreenPos.y &&
            mousePos.y <= cursorScreenPos.y + available.y;

        orbitCamera_.update(camera_, dtSeconds, previewHovered);

        const glm::ivec2 viewportSize(
            std::max(1, static_cast<int>(available.x)),
            std::max(1, static_cast<int>(available.y))
        );

        ENGINE::BEGINGPUFRAMEQUERY();
        renderer_.render(
            shaderSession_.activeShader(), camera_, viewportSize,
            previewSettings_, timeSeconds
        );
        ENGINE::ENDGPUFRAMEQUERY();

        if (renderer_.colorTexture() != 0) {
            ImGui::Image((ImTextureID)(intptr_t)renderer_.colorTexture(), available,
                ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
        else {
            ImGui::TextDisabled("Preview render target unavailable.");
        }

        if (!shaderSession_.hasValidShader()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "Fallback magenta shader active due to compile/link failure."
            );
        }

        ImGui::End();
    }

    void ShaderEditorApp::drawControlsWindow() {
        if (!ImGui::Begin("Preview Controls")) {
            ImGui::End();
            return;
        }

        if (drawBlockIdCombo("Preview Block", previewSettings_.blockType)) {
            shaderSession_.setPreviewBlock(previewSettings_.blockType, currentTimeSeconds_);
        }

        bool autoRotateEnabled = orbitCamera_.autoRotateEnabled();
        if (ImGui::Checkbox("Auto Rotate", &autoRotateEnabled)) {
            orbitCamera_.setAutoRotateEnabled(autoRotateEnabled);
        }

        float autoRotateSpeed = orbitCamera_.autoRotateSpeed();
        if (ImGui::SliderFloat("Rotation Speed", &autoRotateSpeed, 0.0f, 3.5f, "%.2f")) {
            orbitCamera_.setAutoRotateSpeed(autoRotateSpeed);
        }

        bool manualControlEnabled = orbitCamera_.manualControlEnabled();
        if (ImGui::Checkbox("Manual Mouse Orbit", &manualControlEnabled)) {
            orbitCamera_.setManualControlEnabled(manualControlEnabled);
        }

        float distance = orbitCamera_.distance();
        if (ImGui::SliderFloat("Zoom Distance", &distance, 1.6f, 12.0f, "%.2f")) {
            orbitCamera_.setDistance(distance);
            orbitCamera_.update(camera_, 0.0f, false);
        }

        ImGui::Checkbox("Highlight", &previewSettings_.highlightEnabled);
        ImGui::SliderFloat("Break State", &previewSettings_.breakState, 0.0f, 1.0f, "%.2f");

        if (ImGui::Button("Reset Camera")) {
            orbitCamera_.reset(camera_);
        }

        ImGui::Checkbox("Wireframe", &previewSettings_.wireframe);
        ImGui::ColorEdit3("Background Color", &previewSettings_.backgroundColor.x);

        ImGui::Separator();
        ImGui::Text("Performance");
        ImGui::Text("FPS: %.1f", ENGINE::GETFPS());
        ImGui::Text("CPU: %.2f ms", ENGINE::GETCPUFRAMETIMEMS());
        ImGui::Text("GPU: %.2f ms", ENGINE::GETGPUFRAMETIMEMS());

        ImGui::End();
    }

    void ShaderEditorApp::drawDiagnosticsWindow() {
        if (!ImGui::Begin("Diagnostics")) {
            ImGui::End();
            return;
        }

        const auto& diagnostics = shaderSession_.diagnostics();

        if (diagnostics.empty()) {
            ImGui::TextDisabled("No shader diagnostics right now.");
        }
        else {
            for (const ShaderDiagnostic& diagnostic : diagnostics) {
                if (diagnostic.line > 0) {
                    ImGui::TextWrapped(
                        "%s | %s:%d | %s", diagnostic.stage.c_str(),
                        diagnostic.filePath.string().c_str(), diagnostic.line,
                        diagnostic.message.c_str()
                    );
                }
                else {
                    ImGui::TextWrapped(
                        "%s | %s | %s", diagnostic.stage.c_str(),
                        diagnostic.filePath.string().c_str(),
                        diagnostic.message.c_str()
                    );
                }
                ImGui::Separator();
            }
        }

        if (ImGui::CollapsingHeader("Raw Compiler Log", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("RawCompilerLog", ImVec2(0.0f, 180.0f), true);
            if (shaderSession_.rawLog().empty()) {
                ImGui::TextDisabled("No raw log captured.");
            }
            else {
                ImGui::TextUnformatted(shaderSession_.rawLog().c_str());
            }
            ImGui::EndChild();
        }

        ImGui::End();
    }

#pragma endregion

#pragma region 4. UI Data Helpers
    // --- 4. UI Data Helpers ---

    bool ShaderEditorApp::drawBlockIdCombo(const char* label, BlockId& blockId) {
        bool changed = false;

        if (ImGui::BeginCombo(label, getBlockDisplayName(blockId))) {
            const auto& definitions = BlockRegistry::instance().definitions();

            auto drawCategory = [&](BlockCategory category) {
                bool drewAny = false;

                for (const BlockDefinition& definition : definitions) {
                    const BlockId candidate = definition.idValue;

                    if (candidate == static_cast<BlockId>(BlockIds::AIR) || !isSolid(candidate) ||
                        primaryEditorCategory(candidate) != category) {
                        continue;
                    }

                    if (!drewAny) {
                        ImGui::SeparatorText(getBlockCategoryDisplayName(category));
                        drewAny = true;
                    }

                    const bool selected = candidate == blockId;
                    if (ImGui::Selectable(definition.displayName.c_str(), selected)) {
                        blockId = candidate;
                        changed = true;
                    }

                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                };

            for (const BlockCategory category : kEditorBlockCategoryOrder) {
                drawCategory(category);
            }

            bool drewMisc = false;

            for (const BlockDefinition& definition : definitions) {
                const BlockId candidate = definition.idValue;

                if (candidate == static_cast<BlockId>(BlockIds::AIR) || !isSolid(candidate) ||
                    primaryEditorCategory(candidate) != BlockCategory::NONE) {
                    continue;
                }

                if (!drewMisc) {
                    ImGui::SeparatorText("Other");
                    drewMisc = true;
                }

                const bool selected = candidate == blockId;
                if (ImGui::Selectable(definition.displayName.c_str(), selected)) {
                    blockId = candidate;
                    changed = true;
                }

                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        return changed;
    }

#pragma endregion

} // namespace ShaderEditor