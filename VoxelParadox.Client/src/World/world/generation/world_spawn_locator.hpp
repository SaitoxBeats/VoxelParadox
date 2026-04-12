#pragma once

#include <glm/glm.hpp>

class FractalWorld;

struct WorldSpawnLocatorConfig {
    int primeRadiusChunks = 2;
    int searchRadiusXZ = 12;
    int searchRadiusY = 20;
    float groundProbeDistance = 0.08f;
    float supportInset = 0.05f;
};

glm::vec3 findNearestGroundSpawnPosition(FractalWorld& world,
                                         const glm::vec3& spawnAnchor,
                                         float playerRadius,
                                         float bodyHeight,
                                         float eyeHeight,
                                         const WorldSpawnLocatorConfig& config = {});
