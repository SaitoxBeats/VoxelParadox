// renderer_portals.cpp
// Unity mental model: this is the "portal and debug camera" slice of the renderer.
// It owns stencil setup, nested world preview rendering, and frustum debug drawing.

#include "render/core/renderer.hpp"

#include <array>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/camera.hpp"
#include "engine/engine.hpp"
#include "world/generation/fractal_world.hpp"
#include "world/persistence/world_stack.hpp"

void Renderer::setupStencilFaceQuad() {
    glGenVertexArrays(1, &stencilFaceVAO);
    glGenBuffers(1, &stencilFaceVBO);
    glBindVertexArray(stencilFaceVAO);
    glBindBuffer(GL_ARRAY_BUFFER, stencilFaceVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 30, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void Renderer::setupPortalFrameGeometry() {
    glGenVertexArrays(1, &portalFrameVAO);
    glGenBuffers(1, &portalFrameVBO);
    glBindVertexArray(portalFrameVAO);
    glBindBuffer(GL_ARRAY_BUFFER, portalFrameVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 144, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Renderer::setupScreenQuad() {
    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f,  1.0f, 1.0f,  0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f, 1.0f,  0.0f, -1.0f, 1.0f,  0.0f,
    };

    glGenVertexArrays(1, &screenQuadVAO);
    glGenBuffers(1, &screenQuadVBO);
    glBindVertexArray(screenQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, screenQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Renderer::buildFaceBasis(glm::ivec3 faceNormal, glm::vec3& normal,
                              glm::vec3& tangent, glm::vec3& bitangent) {
    normal = glm::normalize(glm::vec3(faceNormal));
    const glm::vec3 helper = std::abs(normal.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                        : glm::vec3(0.0f, 1.0f, 0.0f);
    tangent = glm::normalize(glm::cross(helper, normal));
    bitangent = glm::normalize(glm::cross(normal, tangent));
}

void Renderer::updatePortalFaceQuad(glm::ivec3 blockPos, glm::ivec3 faceNormal,
                                    float halfSize, float planeOffset) {
    glm::vec3 normal(0.0f);
    glm::vec3 tangent(0.0f);
    glm::vec3 bitangent(0.0f);
    buildFaceBasis(faceNormal, normal, tangent, bitangent);

    const glm::vec3 center =
        glm::vec3(blockPos) + glm::vec3(0.5f) + normal * planeOffset;
    const glm::vec3 v0 = center - tangent * halfSize - bitangent * halfSize;
    const glm::vec3 v1 = center + tangent * halfSize - bitangent * halfSize;
    const glm::vec3 v2 = center + tangent * halfSize + bitangent * halfSize;
    const glm::vec3 v3 = center - tangent * halfSize + bitangent * halfSize;

    const float vertices[] = {
        v0.x, v0.y, v0.z, 0.0f, 0.0f, v1.x, v1.y, v1.z, 1.0f, 0.0f,
        v2.x, v2.y, v2.z, 1.0f, 1.0f, v0.x, v0.y, v0.z, 0.0f, 0.0f,
        v2.x, v2.y, v2.z, 1.0f, 1.0f, v3.x, v3.y, v3.z, 0.0f, 1.0f,
    };

    glBindBuffer(GL_ARRAY_BUFFER, stencilFaceVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
}

int Renderer::updatePortalFrameGeometry(glm::ivec3 blockPos, glm::ivec3 faceNormal,
                                        bool innerWalls) {
    glm::vec3 normal(0.0f);
    glm::vec3 tangent(0.0f);
    glm::vec3 bitangent(0.0f);
    buildFaceBasis(faceNormal, normal, tangent, bitangent);

    const glm::vec3 blockCenter = glm::vec3(blockPos) + glm::vec3(0.5f);
    const float outerHalf = 0.43f;
    const float frontOpeningHalf = 0.36f;
    const float innerOpeningHalf = 0.31f;
    const float frontPlane = 0.5035f;
    const float innerPlane = 0.4000f;

    const glm::vec3 frontCenter = blockCenter + normal * frontPlane;
    const glm::vec3 innerCenter = blockCenter + normal * innerPlane;

    const auto corner = [&](const glm::vec3& center, float sx, float sy, float half) {
        return center + tangent * (sx * half) + bitangent * (sy * half);
    };

    const glm::vec3 outerBL = corner(frontCenter, -1.0f, -1.0f, outerHalf);
    const glm::vec3 outerBR = corner(frontCenter, 1.0f, -1.0f, outerHalf);
    const glm::vec3 outerTR = corner(frontCenter, 1.0f, 1.0f, outerHalf);
    const glm::vec3 outerTL = corner(frontCenter, -1.0f, 1.0f, outerHalf);

    const glm::vec3 frontBL = corner(frontCenter, -1.0f, -1.0f, frontOpeningHalf);
    const glm::vec3 frontBR = corner(frontCenter, 1.0f, -1.0f, frontOpeningHalf);
    const glm::vec3 frontTR = corner(frontCenter, 1.0f, 1.0f, frontOpeningHalf);
    const glm::vec3 frontTL = corner(frontCenter, -1.0f, 1.0f, frontOpeningHalf);

    const glm::vec3 innerBL = corner(innerCenter, -1.0f, -1.0f, innerOpeningHalf);
    const glm::vec3 innerBR = corner(innerCenter, 1.0f, -1.0f, innerOpeningHalf);
    const glm::vec3 innerTR = corner(innerCenter, 1.0f, 1.0f, innerOpeningHalf);
    const glm::vec3 innerTL = corner(innerCenter, -1.0f, 1.0f, innerOpeningHalf);

    float vertices[72] = {};
    int vertexIndex = 0;
    const auto addQuad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                             const glm::vec3& d) {
        const glm::vec3 quadVertices[] = {a, b, c, a, c, d};
        for (const glm::vec3& vertex : quadVertices) {
            vertices[vertexIndex++] = vertex.x;
            vertices[vertexIndex++] = vertex.y;
            vertices[vertexIndex++] = vertex.z;
        }
    };

    if (!innerWalls) {
        addQuad(outerTL, outerTR, frontTR, frontTL);
        addQuad(frontBL, frontBR, outerBR, outerBL);
        addQuad(outerBL, frontBL, frontTL, outerTL);
        addQuad(frontBR, outerBR, outerTR, frontTR);
    } else {
        addQuad(frontTL, frontTR, innerTR, innerTL);
        addQuad(innerBL, innerBR, frontBR, frontBL);
        addQuad(frontBL, frontTL, innerTL, innerBL);
        addQuad(innerBR, innerTR, frontTR, frontBR);
    }

    glBindBuffer(GL_ARRAY_BUFFER, portalFrameVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float) * vertexIndex, vertices);
    return vertexIndex / 3;
}

Camera Renderer::buildThirdPersonDebugCamera(const Camera& source) const {
    Camera debugCamera = source;
    const glm::vec3 forward = glm::normalize(source.getForward());
    const glm::vec3 up = glm::normalize(source.getUp());

    debugCamera.position = source.position - forward * 7.0f + up * 2.5f;
    debugCamera.baseFov = glm::clamp(source.baseFov * 0.95f, 60.0f, 95.0f);
    debugCamera.lookAt(source.position + forward * 1.5f, up);
    return debugCamera;
}

void Renderer::renderCameraFrustumDebug(const Camera& source,
                                        const glm::mat4& debugViewProjection) {
    const glm::vec3 forward = glm::normalize(source.getForward());
    const glm::vec3 right = glm::normalize(source.getRight());
    const glm::vec3 up = glm::normalize(source.getUp());

    const float nearDistance = source.nearPlane;
    const float farDistance = glm::min(source.farPlane, 18.0f);
    const float tanHalfFov = std::tan(glm::radians(source.getCurrentFov()) * 0.5f);
    const float aspect = glm::max(ENGINE::GETVIEWPORTSIZE().x, 1.0f) /
                         glm::max(ENGINE::GETVIEWPORTSIZE().y, 1.0f);

    const float nearHalfHeight = tanHalfFov * nearDistance;
    const float nearHalfWidth = nearHalfHeight * aspect;
    const float farHalfHeight = tanHalfFov * farDistance;
    const float farHalfWidth = farHalfHeight * aspect;

    const glm::vec3 nearCenter = source.position + forward * nearDistance;
    const glm::vec3 farCenter = source.position + forward * farDistance;

    const std::array<glm::vec3, 8> corners = {
        nearCenter - right * nearHalfWidth - up * nearHalfHeight,
        nearCenter + right * nearHalfWidth - up * nearHalfHeight,
        nearCenter + right * nearHalfWidth + up * nearHalfHeight,
        nearCenter - right * nearHalfWidth + up * nearHalfHeight,
        farCenter - right * farHalfWidth - up * farHalfHeight,
        farCenter + right * farHalfWidth - up * farHalfHeight,
        farCenter + right * farHalfWidth + up * farHalfHeight,
        farCenter - right * farHalfWidth + up * farHalfHeight,
    };

    const int edgeIndices[][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    std::array<float, 72> vertices{};
    std::size_t vertexIndex = 0;
    for (const auto& edge : edgeIndices) {
        const glm::vec3& start = corners[static_cast<std::size_t>(edge[0])];
        const glm::vec3& end = corners[static_cast<std::size_t>(edge[1])];
        vertices[vertexIndex++] = start.x;
        vertices[vertexIndex++] = start.y;
        vertices[vertexIndex++] = start.z;
        vertices[vertexIndex++] = end.x;
        vertices[vertexIndex++] = end.y;
        vertices[vertexIndex++] = end.z;
    }

    lineShader.use();
    lineShader.setMat4("uMVP", debugViewProjection);
    lineShader.setVec4("uColor", glm::vec4(0.25f, 0.95f, 1.0f, 0.95f));

    glBindVertexArray(portalFrameVAO);
    glBindBuffer(GL_ARRAY_BUFFER, portalFrameVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.75f);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertexIndex / 3));
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderStencilMask(const glm::mat4& vp, glm::ivec3 blockPos,
                                 glm::ivec3 faceNormal) {
    updatePortalFaceQuad(blockPos, faceNormal, 0.31f, 0.5035f);

    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    lineShader.use();
    lineShader.setMat4("uMVP", vp);
    lineShader.setVec4("uColor", glm::vec4(1.0f));
    glBindVertexArray(stencilFaceVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
}

void Renderer::prepareStencilDepthWindow() {
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_ALWAYS);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    depthWindowShader.use();
    glBindVertexArray(screenQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_LESS);
}

void Renderer::renderNestedPreviewWorld(WorldStack& worldStack, FractalWorld& nestedWorld,
                                        const Player::NestedPreviewPortal& portal,
                                        const Camera& playerCamera, float aspect, float time) {
    worldStack.applyRenderDistancePreset(nestedWorld);

    const Camera previewCamera = Player::buildNestedPreviewCamera(playerCamera, portal);
    nestedWorld.update(previewCamera.position, previewCamera.getForward());

    const int childDepth = nestedWorld.depth;
    const glm::vec4 previewFog = getFogColor(childDepth);
    const glm::mat4 previewView = previewCamera.getViewMatrix();
    const glm::mat4 previewProj = previewCamera.getProjectionMatrix(aspect);
    const glm::mat4 previewVP = previewProj * previewView;

    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    lineShader.use();
    lineShader.setMat4("uMVP", glm::mat4(1.0f));
    lineShader.setVec4("uColor",
                       glm::vec4(previewFog.r, previewFog.g, previewFog.b, portal.fade));
    glBindVertexArray(screenQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);

    blockShader.use();
    blockShader.setMat4("uVP", previewVP);
    blockShader.setMat4("uModel", glm::mat4(1.0f));
    blockShader.setVec3("uCameraPos", previewCamera.position);
    blockShader.setVec4("uFogColor", previewFog);
    blockShader.setFloat("uFogDensity",
                         computeFogDensity(childDepth, nestedWorld.renderDistance));
    blockShader.setFloat("uTime", time);
    blockShader.setFloat("uAlpha", portal.fade);
    blockShader.setFloat("uAoStrength", 1.0f);
    blockShader.setVec4("uBiomeTint", getBiomeMaterialTint(&nestedWorld, childDepth));
    setBreakEffectUniforms(glm::vec3(0.0f), 0.0f);
    setHighlightEffectUniforms(glm::vec3(0.0f), 0.0f);

    nestedWorld.render(previewCamera.position, previewVP);
    renderWorldBlockModels(nestedWorld, previewVP, previewCamera.position, previewFog,
                           childDepth, nestedWorld.renderDistance, time, portal.fade);
    renderEntities(nestedWorld, previewVP, previewCamera.position, previewFog, childDepth,
                   nestedWorld.renderDistance, time, portal.fade);
    renderDroppedItems(nestedWorld, previewVP, previewCamera.position, previewFog, childDepth,
                       nestedWorld.renderDistance, time, portal.fade);
}

void Renderer::renderPortalFrame(const glm::mat4& vp, glm::ivec3 blockPos,
                                 glm::ivec3 faceNormal, float fade) {
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    lineShader.use();
    lineShader.setMat4("uMVP", vp);
    glBindVertexArray(portalFrameVAO);

    const int rimVertices = updatePortalFrameGeometry(blockPos, faceNormal, false);
    lineShader.setVec4("uColor", glm::vec4(0.08f, 0.10f, 0.14f, fade));
    glDrawArrays(GL_TRIANGLES, 0, rimVertices);

    const int wallVertices = updatePortalFrameGeometry(blockPos, faceNormal, true);
    lineShader.setVec4("uColor", glm::vec4(0.03f, 0.04f, 0.06f, fade));
    glDrawArrays(GL_TRIANGLES, 0, wallVertices);

    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glStencilMask(0xFF);
    glDisable(GL_STENCIL_TEST);
}
