// renderer_internal.hpp
// Small shared helpers used by the split renderer .cpp files.

#pragma once

#include <cstdint>
#include <string>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/engine.hpp"
#include "render/config/hud_3d_preview.hpp"

namespace RendererInternal {

template <typename DrawFn>
void renderHUD3DPreviewInRect(const glm::ivec4& rect, const HUD3DPreviewRequest& request,
                              DrawFn&& drawFn) {
    if (rect.z <= 0 || rect.w <= 0) {
        return;
    }

    const glm::vec2 viewportSize = ENGINE::GETVIEWPORTSIZE();
    const int screenWidth = static_cast<int>(viewportSize.x);
    const int screenHeight = static_cast<int>(viewportSize.y);
    if (screenWidth <= 0 || screenHeight <= 0) {
        return;
    }

    GLint previousViewport[4] = {0, 0, screenWidth, screenHeight};
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    GLint previousScissorBox[4] = {0, 0, screenWidth, screenHeight};
    glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    GLfloat previousClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    GLint previousDepthFunc = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    GLint previousBlendSrcRGB = GL_ONE;
    GLint previousBlendDstRGB = GL_ZERO;
    GLint previousBlendSrcAlpha = GL_ONE;
    GLint previousBlendDstAlpha = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDstAlpha);
    GLboolean previousDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);

    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y, rect.z, rect.w);
    glViewport(rect.x, rect.y, rect.z, rect.w);

    if (request.style.clearBackground) {
        glClearColor(request.style.backgroundColor.r, request.style.backgroundColor.g,
                     request.style.backgroundColor.b, request.style.backgroundColor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2],
                     previousClearColor[3]);
    } else {
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    const float previewAspect =
        static_cast<float>(rect.z) / static_cast<float>(glm::max(rect.w, 1));
    const glm::mat4 previewView =
        glm::lookAt(request.camera.position, request.camera.target, request.camera.up);
    const glm::mat4 previewProj =
        glm::perspective(glm::radians(request.camera.fovDegrees), previewAspect,
                         request.camera.nearPlane, request.camera.farPlane);

    drawFn(previewProj * previewView, request.camera.position, request.style);

    glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
               previousViewport[3]);
    glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2],
              previousScissorBox[3]);
    glDepthFunc(previousDepthFunc);
    glBlendFuncSeparate(previousBlendSrcRGB, previousBlendDstRGB, previousBlendSrcAlpha,
                        previousBlendDstAlpha);
    glDepthMask(previousDepthMask);

    if (depthWasEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (blendWasEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (cullWasEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (!scissorWasEnabled) {
        glDisable(GL_SCISSOR_TEST);
    }
}

inline void deleteVertexArrayAndBuffer(GLuint& vao, GLuint& vbo) {
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
}

inline std::uint32_t hashStringFNV1a(const std::string& value) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char ch : value) {
        hash ^= static_cast<std::uint32_t>(ch);
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace RendererInternal
