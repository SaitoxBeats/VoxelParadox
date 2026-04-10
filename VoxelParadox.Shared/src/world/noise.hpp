// Arquivo: VoxelParadox.Client/src/World/world/noise.hpp
// Papel: declara "noise" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes
#include <algorithm>
#include <cmath>
#include <cstdint>
#pragma endregion

#pragma region NoiseApi
namespace noise {

// Standard Perlin permutation table, doubled
static const int perm[512] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
    35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
    134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
    55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
    18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
    250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
    189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
    172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
    228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
    107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
    35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
    134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
    55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
    18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
    250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
    189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
    172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
    228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
    107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

// Funcao: suaviza 'fade' nos utilitarios de ruido procedural.
// Detalhe: usa 't' para aplicar a curva de transicao usada pelos calculos procedurais.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
// Funcao: interpola 'lerp' nos utilitarios de ruido procedural.
// Detalhe: usa 't', 'a', 'b' para combinar os valores recebidos ao longo de um fator continuo.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
inline float lerp(float t, float a, float b) { return a + t * (b - a); }

// Funcao: calcula 'grad' nos utilitarios de ruido procedural.
// Detalhe: usa 'hash', 'x', 'y', 'z' para avaliar o gradiente usado pela etapa procedural correspondente.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
inline float grad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

// Funcao: avalia 'perlin' nos utilitarios de ruido procedural.
// Detalhe: usa 'x', 'y', 'z' para produzir o valor de ruido usado pelas camadas procedurais do jogo.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
inline float perlin(float x, float y, float z) {
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;
    int Z = (int)std::floor(z) & 255;
    x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
    float u = fade(x), v = fade(y), w = fade(z);
    int A = perm[X] + Y, AA = perm[A] + Z, AB = perm[A + 1] + Z;
    int B = perm[X + 1] + Y, BA = perm[B] + Z, BB = perm[B + 1] + Z;
    return lerp(w,
        lerp(v, lerp(u, grad(perm[AA], x, y, z), grad(perm[BA], x - 1, y, z)),
                lerp(u, grad(perm[AB], x, y - 1, z), grad(perm[BB], x - 1, y - 1, z))),
        lerp(v, lerp(u, grad(perm[AA + 1], x, y, z - 1), grad(perm[BA + 1], x - 1, y, z - 1)),
                lerp(u, grad(perm[AB + 1], x, y - 1, z - 1), grad(perm[BB + 1], x - 1, y - 1, z - 1))));
}

inline std::uint32_t hash3i(int x, int y, int z, std::uint32_t seed) {
    std::uint32_t hash = seed;
    hash ^= static_cast<std::uint32_t>(x) * 0x9e3779b1u;
    hash ^= static_cast<std::uint32_t>(y) * 0x85ebca77u;
    hash ^= static_cast<std::uint32_t>(z) * 0xc2b2ae3du;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash;
}

inline float hash01(std::uint32_t value) {
    return static_cast<float>(value & 0x00FFFFFFu) / 16777216.0f;
}

inline float hashSigned(std::uint32_t value) {
    return hash01(value) * 2.0f - 1.0f;
}

// Funcao: avalia 'fbm' nos utilitarios de ruido procedural.
// Detalhe: usa 'x', 'y', 'z', 'octaves', 'persistence' para produzir o valor de ruido usado pelas camadas procedurais do jogo.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
inline float fbm(float x, float y, float z, int octaves, float persistence = 0.5f,
                 float lacunarity = 2.0f) {
    float total = 0, amplitude = 1, frequency = 1, maxVal = 0;
    for (int i = 0; i < octaves; i++) {
        total += perlin(x * frequency, y * frequency, z * frequency) * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return maxVal > 0.0f ? (total / maxVal) : 0.0f;
}

inline float ridgedFbm(float x, float y, float z, int octaves,
                       float persistence = 0.5f, float lacunarity = 2.0f,
                       float sharpness = 1.0f) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxVal = 0.0f;
    const float clampedSharpness = sharpness < 0.001f ? 0.001f : sharpness;

    for (int i = 0; i < octaves; i++) {
        float signal = 1.0f - std::abs(perlin(x * frequency, y * frequency, z * frequency));
        if (std::abs(clampedSharpness - 1.0f) > 0.0001f) {
            signal = std::pow(signal, clampedSharpness);
        }
        total += signal * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    const float normalized = maxVal > 0.0f ? (total / maxVal) : 0.0f;
    return normalized * 2.0f - 1.0f;
}

inline float cellular(float x, float y, float z, std::uint32_t seed,
                      float jitter = 1.0f, float distanceBlend = 0.0f) {
    const int baseX = static_cast<int>(std::floor(x));
    const int baseY = static_cast<int>(std::floor(y));
    const int baseZ = static_cast<int>(std::floor(z));
    const float clampedJitter = std::clamp(jitter, 0.0f, 1.0f);
    const float clampedDistanceBlend = std::clamp(distanceBlend, 0.0f, 1.0f);

    float nearestSq = 1.0e30f;
    float secondSq = 1.0e30f;

    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                const int cellX = baseX + dx;
                const int cellY = baseY + dy;
                const int cellZ = baseZ + dz;
                const std::uint32_t hash = hash3i(cellX, cellY, cellZ, seed);

                const float featureX = static_cast<float>(cellX) + 0.5f +
                    hashSigned(hash ^ 0x68bc21ebu) * 0.5f * clampedJitter;
                const float featureY = static_cast<float>(cellY) + 0.5f +
                    hashSigned(hash ^ 0x02e5be93u) * 0.5f * clampedJitter;
                const float featureZ = static_cast<float>(cellZ) + 0.5f +
                    hashSigned(hash ^ 0x7f4a7c15u) * 0.5f * clampedJitter;

                const float deltaX = featureX - x;
                const float deltaY = featureY - y;
                const float deltaZ = featureZ - z;
                const float distanceSq =
                    deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;

                if (distanceSq < nearestSq) {
                    secondSq = nearestSq;
                    nearestSq = distanceSq;
                } else if (distanceSq < secondSq) {
                    secondSq = distanceSq;
                }
            }
        }
    }

    constexpr float kMaxDistance = 1.73205080757f;
    const float nearest = std::sqrt(nearestSq);
    const float second = std::sqrt(secondSq);
    const float centerValue = 1.0f - std::clamp(nearest / kMaxDistance, 0.0f, 1.0f);
    const float edgeValue =
        std::clamp((second - nearest) / kMaxDistance, 0.0f, 1.0f);
    return lerp(clampedDistanceBlend, centerValue, edgeValue) * 2.0f - 1.0f;
}

inline float cellularFbm(float x, float y, float z, std::uint32_t seed, int octaves,
                         float persistence = 0.5f, float lacunarity = 2.0f,
                         float jitter = 1.0f, float distanceBlend = 0.0f) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxVal = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        const std::uint32_t octaveSeed =
            seed ^ (0x9e3779b9u * static_cast<std::uint32_t>(i + 1));
        total += cellular(x * frequency, y * frequency, z * frequency, octaveSeed,
                          jitter, distanceBlend) * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return maxVal > 0.0f ? (total / maxVal) : 0.0f;
}

// Seeded variant - offsets coordinates by seed
// Funcao: avalia 'seededFbm' nos utilitarios de ruido procedural.
// Detalhe: usa 'x', 'y', 'z', 'seed', 'octaves', 'persistence' para produzir o valor de ruido usado pelas camadas procedurais do jogo.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
inline float seededFbm(float x, float y, float z, uint32_t seed, int octaves,
                       float persistence = 0.5f, float lacunarity = 2.0f) {
    float sx = (seed & 0xFF) * 0.3713f;
    float sy = ((seed >> 8) & 0xFF) * 0.2918f;
    float sz = ((seed >> 16) & 0xFF) * 0.4517f;
    return fbm(x + sx, y + sy, z + sz, octaves, persistence, lacunarity);
}

} // namespace noise
#pragma endregion
