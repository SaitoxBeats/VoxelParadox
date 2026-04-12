// Arquivo: VoxelParadox.Client/src/Renderer/render/config/hud_3d_preview.hpp
// Papel: declara "hud 3d preview" dentro do subsistema "render" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#include <algorithm>

#include <glm/glm.hpp>


enum class HUD3DPreviewAnchor {
    TOP_LEFT = 0,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT,
    BOTTOM_CENTER,
    CENTER,
};

struct HUD3DPreviewLayout {
    HUD3DPreviewAnchor anchor = HUD3DPreviewAnchor::BOTTOM_CENTER;
    glm::ivec2 size{140, 140};
    glm::ivec2 offset{0, 18};
};

struct HUD3DPreviewCamera {
    glm::vec3 position{0.0f, 0.15f, 3.1f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovDegrees = 18.0f;
    float nearPlane = 0.1f;
    float farPlane = 32.0f;
};

struct HUD3DPreviewStyle {
    bool clearBackground = true;
    glm::vec4 backgroundColor{0.03f, 0.03f, 0.05f, 1.0f};
};

struct HUD3DPreviewRequest {
    HUD3DPreviewLayout layout{};
    HUD3DPreviewCamera camera{};
    HUD3DPreviewStyle style{};
};

// Funcao: resolve 'resolveHUD3DPreviewRect' neste modulo do projeto VoxelParadox.Client.
// Detalhe: usa 'request', 'screenWidth', 'screenHeight' para traduzir o estado atual para uma resposta concreta usada pelo restante do sistema.
// Retorno: devolve 'glm::ivec4' com o resultado composto por esta chamada.
inline glm::ivec4 resolveHUD3DPreviewRect(const HUD3DPreviewRequest& request,
                                          int screenWidth,
                                          int screenHeight) {
    const int width = std::max(1, request.layout.size.x);
    const int height = std::max(1, request.layout.size.y);
    int x = 0;
    int y = 0;

    switch (request.layout.anchor) {
    case HUD3DPreviewAnchor::TOP_LEFT:
        x = request.layout.offset.x;
        y = screenHeight - height - request.layout.offset.y;
        break;
    case HUD3DPreviewAnchor::TOP_RIGHT:
        x = screenWidth - width - request.layout.offset.x;
        y = screenHeight - height - request.layout.offset.y;
        break;
    case HUD3DPreviewAnchor::BOTTOM_LEFT:
        x = request.layout.offset.x;
        y = request.layout.offset.y;
        break;
    case HUD3DPreviewAnchor::BOTTOM_RIGHT:
        x = screenWidth - width - request.layout.offset.x;
        y = request.layout.offset.y;
        break;
    case HUD3DPreviewAnchor::CENTER:
        x = (screenWidth - width) / 2 + request.layout.offset.x;
        y = (screenHeight - height) / 2 + request.layout.offset.y;
        break;
    case HUD3DPreviewAnchor::BOTTOM_CENTER:
    default:
        x = (screenWidth - width) / 2 + request.layout.offset.x;
        y = request.layout.offset.y;
        break;
    }

    const int clampedX = std::clamp(x, 0, std::max(0, screenWidth));
    const int clampedY = std::clamp(y, 0, std::max(0, screenHeight));
    const int clippedWidth = std::max(0, std::min(x + width, screenWidth) - clampedX);
    const int clippedHeight = std::max(0, std::min(y + height, screenHeight) - clampedY);

    return glm::ivec4(clampedX, clampedY, clippedWidth, clippedHeight);
}

