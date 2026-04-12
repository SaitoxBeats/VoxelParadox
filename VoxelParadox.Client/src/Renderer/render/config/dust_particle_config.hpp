// Arquivo: VoxelParadox.Client/src/Renderer/render/config/dust_particle_config.hpp
// Papel: declara "dust particle config" dentro do subsistema "render" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#include <cstddef>

#include <glm/glm.hpp>

struct DustParticleConfig {
    std::size_t initialBufferCapacity = 2048;

    float cellSize = 6.5f;
    float radius = 18.0f;
    int particlesPerCell = 3;
    int cellRadiusY = 3;

    float minSway = 0.10f;
    float maxSway = 0.50f;

    float minSpeed = 0.3f;
    float maxSpeed = 1.6f;
    float verticalSpeedMultiplier = 0.70f;
    float depthSpeedMultiplier = 0.85f;

    float verticalSwayMultiplier = 0.45f;

    float baseAlpha = 0.18f;
    float distanceAlphaBoost = 0.30f;

    float minSize = 1.2f;
    float maxSize = 4.0f;

    glm::vec3 colorTint = glm::vec3(0.92f, 0.88f, 0.82f);
    float fogColorBlend = 0.45f;

    float fadeOutSpeed = 1.5f;
    float fadeInSpeed = 0.3f;
    int readyChunkRadius = 1;
};

namespace DustParticles {
inline DustParticleConfig config{};
}
