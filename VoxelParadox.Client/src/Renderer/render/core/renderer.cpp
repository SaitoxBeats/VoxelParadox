// renderer.cpp
// Unity mental model: this is the RenderManager frame coordinator.
// It owns shader setup, frame orchestration, and global visual transitions.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <utility>

// 2. Third-party Libraries
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

// 3. Internal Engine/Core Modules
#include "engine/camera.hpp"
#include "engine/engine.hpp"

// 4. Local Project Headers
#include "client_assets.hpp"
#include "client_defaults.hpp"
#include "render/config/dust_particle_config.hpp"
#include "render/hud/hud.hpp"
#include "render/cache/block_texture_cache.hpp"
#include "render/cache/item_texture_cache.hpp"
#include "render/core/renderer.hpp"
#include "render/core/renderer_internal.hpp"
#include "world/block/block_registry.hpp"
#include "world/generation/fractal_world.hpp"
#include "world/persistence/world_stack.hpp"

#pragma endregion

namespace {

#pragma region 1. Embedded Shaders
    // --- 1. Embedded Shaders ---
    // Renderer owns the 3D frame. Most of the file is shader code plus the glue
    // that feeds world, player, and HUD preview data into the GPU.

    static const char* BLOCK_VERT = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTint;
layout(location = 3) in float aAO;
layout(location = 4) in float aMaterial;
uniform mat4 uVP;
uniform mat4 uModel;
out vec3 vWorldPos;
out vec3 vLocalPos;
out vec3 vNormal;
out vec4 vTint;
out float vAO;
flat out int vMaterialId;
void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vLocalPos = aPos;
    vNormal = mat3(uModel) * aNormal;
    vTint = aTint;
    vAO = aAO;
    vMaterialId = int(aMaterial + 0.5);
    gl_Position = uVP * worldPos;
}
)";

    static const char* BLOCK_FRAG = R"(
#version 460 core
in vec3 vWorldPos;
in vec3 vLocalPos;
in vec3 vNormal;
in vec4 vTint;
in float vAO;
flat in int vMaterialId;
uniform vec3 uCameraPos;
uniform vec4 uFogColor;
uniform float uFogDensity;
uniform float uTime;
uniform float uAlpha;
uniform float uAoStrength;
uniform vec4 uBiomeTint;
uniform int uUseLocalMaterialSpace;
uniform vec3 uBreakBlockCenter;
uniform float uBreakProgress;

struct PointLightData {
    vec3 position;
    vec3 color;
    float radius;
};
const int MAX_POINT_LIGHTS = 32;
uniform int uPointLightCount;
uniform PointLightData uPointLights[MAX_POINT_LIGHTS];

out vec4 FragColor;

const int MATERIAL_STONE = 1;
const int MATERIAL_CRYSTAL = 2;
const int MATERIAL_VOID_MATTER = 3;
const int MATERIAL_MEMBRANE = 4;
const int MATERIAL_ORGANIC = 5;
const int MATERIAL_METAL = 6;
const int MATERIAL_PORTAL = 7;
const int MATERIAL_MEMBRANE_WEAVE = 8;

struct MaterialSample {
    vec3 albedo;
    float roughness;
    float specular;
    float emissive;
};

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float hash31(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);
}

float noise21(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm21(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 4; octave++) {
        value += noise21(p) * amplitude;
        p = p * 2.03 + vec2(19.7, 11.3);
        amplitude *= 0.5;
    }
    return value;
}

vec2 faceUv(vec3 worldPos, vec3 normal) {
    vec3 absNormal = abs(normal);
    if (absNormal.x > absNormal.y && absNormal.x > absNormal.z) {
        return worldPos.yz;
    }
    if (absNormal.y > absNormal.z) {
        return worldPos.xz;
    }
    return worldPos.xy;
}

vec3 blockBaseColor(int materialId) {
    if (materialId == MATERIAL_STONE) {
        return vec3(0.45, 0.42, 0.50);
    }
    if (materialId == MATERIAL_CRYSTAL) {
        return vec3(0.15, 0.85, 0.95);
    }
    if (materialId == MATERIAL_VOID_MATTER) {
        return vec3(0.20, 0.08, 0.30);
    }
    if (materialId == MATERIAL_MEMBRANE) {
        return vec3(0.25, 0.90, 0.55);
    }
    if (materialId == MATERIAL_ORGANIC) {
        return vec3(0.75, 0.35, 0.28);
    }
    if (materialId == MATERIAL_METAL) {
        return vec3(0.68, 0.70, 0.75);
    }
    if (materialId == MATERIAL_PORTAL) {
        return vec3(0.95, 0.20, 0.85);
    }
    if (materialId == MATERIAL_MEMBRANE_WEAVE) {
        return vec3(0.58, 0.92, 0.78);
    }
    return vec3(1.0, 0.0, 1.0);
}

MaterialSample makeSample(vec3 albedo, float roughness, float specular, float emissive) {
    MaterialSample result;
    result.albedo = albedo;
    result.roughness = roughness;
    result.specular = specular;
    result.emissive = emissive;
    return result;
}

MaterialSample sampleBlockMaterial(int materialId, vec3 worldPos, vec3 normal, vec3 viewDir) {
    vec3 base = blockBaseColor(materialId);
    vec2 uv = faceUv(worldPos, normal);
    vec2 localUv = fract(uv + vec2(0.001));
    vec2 centeredUv = localUv - 0.5;
    vec3 cell = floor(worldPos - normal * 0.5 + vec3(0.001));
    float cellHash = hash31(cell + vec3(float(materialId) * 1.6180339, 2.13, 4.37));

    if (materialId == MATERIAL_STONE) {
        float strata = fbm21(vec2(uv.x * 3.4 + cellHash * 4.0, worldPos.y * 0.28 + uv.y * 1.4));
        float grain = noise21(localUv * 11.0 + cellHash * 9.0);
        float cracks = smoothstep(0.63, 0.82,
                                  fbm21(uv * 8.0 + vec2(cellHash * 17.0, -cellHash * 13.0)));
        vec3 albedo = base * mix(0.72, 1.15, strata);
        albedo *= mix(0.96, 0.82, cracks * 0.55);
        albedo += vec3((grain - 0.5) * 0.06);
        return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.92, 0.05, 0.0);
    }

    if (materialId == MATERIAL_CRYSTAL) {
        float bands = 0.5 + 0.5 * sin((localUv.x + localUv.y) * 18.0 + uTime * 2.4 +
                                      cellHash * 6.2831853);
        float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 4.0);
        vec3 albedo = mix(base * 0.78, vec3(1.0), bands * 0.32 + fresnel * 0.22);
        albedo += vec3(0.08, 0.14, 0.18) * fresnel;
        return makeSample(clamp(albedo, vec3(0.0), vec3(1.6)), 0.14, 0.65,
                          0.42 + bands * 0.28 + fresnel * 0.30);
    }

    if (materialId == MATERIAL_VOID_MATTER) {
        float voidNoise = fbm21(uv * 6.0 + vec2(uTime * 0.12, -uTime * 0.18) + cellHash * 5.0);
        float wisps = 0.5 + 0.5 * sin((uv.x - uv.y) * 14.0 - uTime * 3.0 +
                                      cellHash * 8.0);
        float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.4);
        vec3 albedo = mix(base * 0.22, base * 0.85 + vec3(0.08, 0.0, 0.12), voidNoise);
        albedo += vec3(0.06, 0.01, 0.08) * wisps * 0.25;
        return makeSample(clamp(albedo, vec3(0.0), vec3(1.2)), 0.52, 0.18,
                          0.12 + rim * 0.18 + wisps * 0.08);
    }

    if (materialId == MATERIAL_MEMBRANE) {
        float veins = fbm21(vec2(localUv.x * 4.0 + cellHash * 8.0,
                                 localUv.y * 10.0 - uTime * 0.45));
        float ridge = abs(sin(localUv.x * 18.0 + veins * 6.0 + uTime * 2.1 +
                              cellHash * 6.2831853));
        float pulse = 0.5 + 0.5 * sin(uTime * 2.8 + cellHash * 7.0 + worldPos.y * 0.35);
        vec3 albedo = mix(base * 0.72, base * 1.18, smoothstep(0.36, 0.92, ridge));
        albedo += vec3(0.06, 0.10, 0.05) * pulse * 0.18;
        return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.66, 0.12,
                          0.10 + ridge * 0.10 * pulse);
    }

    if (materialId == MATERIAL_ORGANIC) {
        float fiber = fbm21(vec2(uv.x * 9.0 + cellHash * 4.0, uv.y * 3.0 - cellHash * 3.0));
        float pores = noise21(localUv * 18.0 + cellHash * 13.0);
        vec3 albedo = mix(base * 0.78, base * 1.06, fiber);
        albedo *= 0.92 + pores * 0.10;
        albedo += vec3(0.05, 0.02, 0.01) * smoothstep(0.72, 1.0, pores);
        return makeSample(clamp(albedo, vec3(0.0), vec3(1.25)), 0.78, 0.08, 0.0);
    }

    if (materialId == MATERIAL_METAL) {
        float brushed = 0.5 + 0.5 * sin(localUv.y * 96.0 + cellHash * 10.0);
        float scratches = fbm21(vec2(localUv.x * 18.0 + cellHash * 7.0, localUv.y * 72.0));
        float edge = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.5);
        vec3 albedo = mix(base * 0.72, base * 1.12, brushed * 0.22 + scratches * 0.18);
        albedo += vec3(edge) * 0.08;
        return makeSample(clamp(albedo, vec3(0.0), vec3(1.6)), 0.18, 0.85, 0.0);
    }

    if (materialId == MATERIAL_PORTAL) {
        float dist = length(centeredUv);
        float ring = 0.5 + 0.5 * sin(dist * 26.0 - uTime * 8.0);
        float vortex = fbm21(uv * 5.0 + vec2(uTime * 0.45, -uTime * 0.32) +
                             vec2(cellHash * 9.0));
        vec3 albedo = mix(vec3(0.06, 0.02, 0.10),
                          base * 1.20 + vec3(0.10, 0.0, 0.16), vortex);
        albedo += vec3(0.18, 0.03, 0.22) * ring * 0.25;
        return makeSample(clamp(albedo, vec3(0.0), vec3(1.8)), 0.05, 0.35,
                          0.55 + ring * 0.30 + vortex * 0.20);
    }

    if (materialId == MATERIAL_MEMBRANE_WEAVE) {
        float weaveA = smoothstep(0.42, 0.58, abs(sin(localUv.x * 18.0)));
        float weaveB = smoothstep(0.42, 0.58, abs(sin(localUv.y * 18.0 + 1.5707963)));
        float knots = fbm21(localUv * 10.0 + cellHash * 5.0);
        float weave = max(weaveA, weaveB);
        vec3 albedo = mix(base * 0.72, base * 1.10, weave);
        albedo *= 0.90 + knots * 0.12;
        return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.52, 0.22,
                          0.08 * weave);
    }

    return makeSample(base, 0.80, 0.10, 0.0);
}

float bayerDither4x4(vec2 pix, float brightness) {
    int x = int(mod(pix.x, 4.0));
    int y = int(mod(pix.y, 4.0));
    float matrix[16] = float[16](
        0.0,  8.0,  2.0,  10.0,
        12.0, 4.0,  14.0, 6.0,
        3.0,  11.0, 1.0,  9.0,
        15.0, 7.0,  13.0, 5.0
    );
    float threshold = (matrix[y * 4 + x] + 0.5) / 16.0;
    return brightness > threshold ? 1.0 : 0.0;
}

void main() {
    vec3 materialPos = (uUseLocalMaterialSpace != 0) ? vLocalPos : vWorldPos;
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    MaterialSample material = sampleBlockMaterial(vMaterialId, materialPos, normal, viewDir);

    vec3 tint = clamp(vTint.rgb * uBiomeTint.rgb, vec3(0.45), vec3(1.85));
    material.albedo *= tint;

    vec3 lightDir = normalize(vec3(0.4, 1.0, 0.3));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float ambient = 0.38;
    float ao = mix(0.35, 1.0, vAO);
    float light = (ambient + diffuse * 0.55) * mix(1.0, ao, uAoStrength);

    vec3 halfDir = normalize(lightDir + viewDir);
    float specPower = mix(14.0, 96.0, 1.0 - material.roughness);
    float specular = pow(max(dot(normal, halfDir), 0.0), specPower) * material.specular;

    vec3 color = material.albedo * light +
                 mix(vec3(specular), material.albedo * specular, 0.25);

    vec3 pointLightContrib = vec3(0.0);
    for (int i = 0; i < uPointLightCount; ++i) {
        vec3 toLight = uPointLights[i].position - vWorldPos;
        float plDist = length(toLight);
        float r = uPointLights[i].radius;
        if (plDist >= r) continue;
        vec3 lDir = toLight / max(plDist, 0.001);
        float att = 1.0 - plDist / r;
        att = att * att;
        float nDotL = max(dot(normal, lDir), 0.0);
        vec3 plDiffuse = uPointLights[i].color * nDotL * att;
        vec3 plHalf = normalize(lDir + viewDir);
        float plSpec = pow(max(dot(normal, plHalf), 0.0), specPower) * material.specular;
        vec3 plSpecular = uPointLights[i].color * plSpec * att;
        pointLightContrib += material.albedo * plDiffuse * 0.8 +
                             mix(plSpecular, material.albedo * plSpecular, 0.25) * 0.5;
    }
    color += pointLightContrib * ao;

    float pulse = 0.5 + 0.5 * sin(uTime * 3.0 + vWorldPos.x + vWorldPos.z);
    vec2 ditherCoord = faceUv(materialPos, normal) * 8.0;
    float ditherMask = bayerDither4x4(floor(ditherCoord), pulse);
    float ditheredPulse = mix(pulse * 0.45, pulse, ditherMask);
    float emissive =
        (material.emissive + vTint.a * (0.4 + 0.6 * ditheredPulse)) * uBiomeTint.a;
    color += material.albedo * emissive;

    if (uBreakProgress > 0.001) {
        vec3 blockMin = uBreakBlockCenter - vec3(0.5);
        vec3 blockMax = uBreakBlockCenter + vec3(0.5);
        vec3 inside =
            step(blockMin - vec3(0.001), vWorldPos) *
            step(vWorldPos, blockMax + vec3(0.001));
        float breakMask = inside.x * inside.y * inside.z;

        if (breakMask > 0.5) {
            vec3 local = clamp(vWorldPos - blockMin, 0.0, 1.0);
            vec3 noisePos = floor(local * 12.0 + vec3(0.0, uTime * 3.5, uTime * 2.1));
            float noiseA = hash31(noisePos + floor(abs(normal) * 11.0));
            float noiseB = hash31(noisePos.zxy + 19.37);
            float streaks =
                0.5 + 0.5 * sin(local.x * 19.0 + local.y * 27.0 + local.z * 23.0 + uTime * 11.0);
            float breakNoise = mix(noiseA, noiseB, 0.5) * 0.7 + streaks * 0.3;
            float fracture =
                uBreakProgress *
                smoothstep(0.16 - uBreakProgress * 0.08, 0.92, breakNoise);
            float shimmer =
                (0.5 + 0.5 * sin(uTime * 18.0 + breakNoise * 15.0)) * fracture;
            float voidMask =
                smoothstep(0.58, 1.0, breakNoise + uBreakProgress * 0.35) * uBreakProgress;

            color = mix(color, vec3(0.03, 0.01, 0.04), fracture * 0.88);
            color += vec3(0.24, 0.06, 0.30) * shimmer;
            color *= 1.0 - voidMask * 0.38;
        }
    }

    float dist = length(vWorldPos - uCameraPos);
    float fog = 1.0 - exp(-dist * uFogDensity);
    color = mix(color, uFogColor.rgb, fog);

    FragColor = vec4(color, uAlpha);
}
)";

    static const char* LINE_VERT = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

    static const char* LINE_FRAG = R"(
#version 460 core
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    FragColor = uColor;
}
)";

    static const char* DEPTH_WINDOW_VERT = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
void main() {
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
)";

    static const char* DEPTH_WINDOW_FRAG = R"(
#version 460 core
void main() {
    gl_FragDepth = 1.0;
}
)";

    static const char* DUST_PARTICLE_VERT = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aAlpha;
layout(location = 2) in float aSize;
uniform mat4 uVP;
uniform vec3 uCameraPos;
out float vAlpha;
void main() {
    gl_Position = uVP * vec4(aPos, 1.0);
    float dist = length(aPos - uCameraPos);
    gl_PointSize = clamp(aSize / (1.0 + dist * 0.05), 1.5, 3.5);
    vAlpha = aAlpha;
}
)";

    static const char* DUST_PARTICLE_FRAG = R"(
#version 460 core
in float vAlpha;
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(uColor.rgb, uColor.a * vAlpha);
}
)";

    static const char* ITEM_SPRITE_VERT = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uVP;
uniform mat4 uModel;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uVP * uModel * vec4(aPos, 1.0);
}
)";

    static const char* ITEM_SPRITE_FRAG = R"(
#version 460 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform float uAlpha;
out vec4 FragColor;
void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) discard;
    FragColor = vec4(texColor.rgb, texColor.a * uAlpha);
}
)";

    static const char* DEATH_SCREEN_GLSL = R"(
// DARK DREAM — Optimized + Vinheta controlável
// Altere este valor entre 0.0 e 1.0:
#define VIGNETTE_EXTRA 0.0

float hash(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}

float noise2D(vec2 p){
    vec2 i=floor(p),f=fract(p);
    vec2 u=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),u.x),
               mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),u.x),u.y);
}

float fbm(vec2 p){
    float v=0.0,a=0.5;
    for(int i=0;i<4;i++){v+=a*noise2D(p);p*=2.0;a*=0.5;}
    return v;
}

mat2 rot2(float a){float s=sin(a),c=cos(a);return mat2(c,-s,s,c);}

vec2 warp(vec2 p){
    float t=iTime*0.12;
    vec2 q=vec2(fbm(p*1.6+vec2(1.7,9.2)+t),
                fbm(p*1.4+vec2(8.3,2.8)-t));
    p+=0.45*(q-0.5);
    p*=rot2(iTime*0.04);
    return p;
}

vec3 palette(float t){
    vec3 a=vec3(0.42,0.38,0.45),b=vec3(0.55,0.45,0.60);
    vec3 c=vec3(1.0),d=vec3(0.08,0.42,0.32);
    return a+b*cos(6.28318*(c*t+d));
}

float hash3(vec3 p){return fract(sin(dot(p,vec3(12.9898,78.233,45.164)))*43758.5453);}

float smoothNoise3D(vec3 p){
    vec3 i=floor(p),f=fract(p);
    f=f*f*(3.0-2.0*f);
    return mix(
        mix(mix(hash3(i),hash3(i+vec3(1,0,0)),f.x),
            mix(hash3(i+vec3(0,1,0)),hash3(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash3(i+vec3(0,0,1)),hash3(i+vec3(1,0,1)),f.x),
            mix(hash3(i+vec3(0,1,1)),hash3(i+vec3(1,1,1)),f.x),f.y),
        f.z);
}

float dither(vec2 pix,float b){
    int x=int(mod(pix.x,4.0)),y=int(mod(pix.y,4.0));
    float m[16]=float[](0.0,8.0,2.0,10.0,12.0,4.0,14.0,6.0,
                        3.0,11.0,1.0,9.0,15.0,7.0,13.0,5.0);
    return b>(m[y*4+x]+0.5)/16.0?1.0:0.0;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2 coord=floor(fragCoord*0.5)*2.0;
    vec2 uv=(coord-0.5*iResolution.xy)/iResolution.y;
    float r=dot(uv,uv);

    vec2 warped=warp(uv*2.4);
    float wl=max(dot(warped,warped),0.0001);
    warped*=1.0/(0.5+wl*0.8);
    warped*=2.0+1.0/(0.2*iTime+0.5);

    float angle=atan(warped.y,warped.x);
    float dist=inversesqrt(max(dot(warped,warped),0.0001));
    vec3 p3=vec3(cos(angle)*5.5,sin(angle)*5.5,dist+iTime*1.5);

    float fog=smoothNoise3D(p3*0.8)+hash3(p3*2.0-iTime*0.5)*0.2;
    fog*=smoothstep(0.01,0.3,sqrt(r));
    fog=smoothstep(0.025,1.25,fog);

    float n=mix(fbm(warped),fbm(warped*1.7+4.0),0.9);
    vec3 col=palette(n+fog*0.3+0.15*sqrt(r));
    col*=(dither(coord,fog)*0.72+0.28);

    float light=smoothstep(0.2,0.75,fog);
    float g=smoothstep(0.55,1.0,fog);
    float glow=g*g*g;
    float v=1.0-r*0.75;

    vec3 result=(col*light+col*glow*1.2)*v*v;
    result*=result*(3.0-2.0*result);

    // Vinheta extra — aplica por cima de tudo
    result*=1.0-VIGNETTE_EXTRA;

    fragColor=vec4(result,1.0);
}

)";
#pragma endregion

} // namespace

#pragma region 2. Renderer Lifecycle
// --- 2. Renderer Lifecycle ---

// Function: initializes 'init' in the main renderer.
// Detail: centralizes the logic needed to prepare resources and the initial state before use.
// Return: returns 'bool' to indicate success, presence, validation, or any other relevant condition produced by the call.
bool Renderer::init() {
    // --- 1. Shader Programs ---
    // The block shader is assembled from per-block material snippets and only
    // falls back to the embedded emergency shader when the data-driven path fails.
    const BlockShaderSources blockShaderSources =
        BlockRegistry::instance().buildShaderSources();
    const bool assembledBlockShaderCompiled =
        blockShaderSources.valid() &&
        blockShader.compile(blockShaderSources.vertexSource.c_str(),
                            blockShaderSources.fragmentSource.c_str());

    if (!assembledBlockShaderCompiled) {
        if (!blockShaderSources.error.empty()) {
            std::printf("[Blocks] %s\n", blockShaderSources.error.c_str());
        } else {
            std::printf("[Blocks] Failed to compile assembled block shader. Using fallback.\n");
        }

        if (!blockShaderSources.vertexSource.empty() &&
            !blockShaderSources.fallbackFragmentSource.empty() &&
            blockShader.compile(blockShaderSources.vertexSource.c_str(),
                                blockShaderSources.fallbackFragmentSource.c_str())) {
            std::printf("[Blocks] Compiled atlas-aware fallback block shader.\n");
        } else if (!blockShader.compile(BLOCK_VERT, BLOCK_FRAG)) {
            return false;
        }
    }

    if (!lineShader.compile(LINE_VERT, LINE_FRAG)) {
        return false;
    }

    if (!depthWindowShader.compile(DEPTH_WINDOW_VERT, DEPTH_WINDOW_FRAG)) {
        return false;
    }

    if (!dustParticleShader.compile(DUST_PARTICLE_VERT, DUST_PARTICLE_FRAG)) {
        return false;
    }

    if (!itemSpriteShader.compile(ITEM_SPRITE_VERT, ITEM_SPRITE_FRAG)) {
        return false;
    }

    const std::unordered_map<std::string, std::string> deathScreenMacroOverrides = {
        {"VIGNETTE_EXTRA", "uVignetteExtra"},
    };
    if (!deathScreenShader.compileFullscreenShadertoyFromFile(
            ClientAssets::kDeathScreenShaderToy,
            deathScreenMacroOverrides) &&
        !deathScreenShader.compileFullscreenShadertoy(
            DEATH_SCREEN_GLSL,
            deathScreenMacroOverrides)) {
        return false;
    }

    // --- 2. Core Geometry ---
    setupBreakBlockCube();
    setupStencilFaceQuad();
    setupPortalFrameGeometry();
    setupScreenQuad();
    setupItemSpriteQuad();
    setupDustParticles();

    // --- 3. Render Assets ---
    if (!setupEntityAssets()) {
        return false;
    }

    if (!setupBlockModelAssets()) {
        return false;
    }

    if (!setupBlockTextures()) {
        return false;
    }

    return true;
}

void Renderer::setRenderScale(float scale) {
    renderScale_ = glm::clamp(
        scale,
        ClientDefaults::kMinRenderScale,
        ClientDefaults::kMaxRenderScale
    );

    if (renderScale_ >= ClientDefaults::kMaxRenderScale) {
        releaseSceneRenderTarget();
    }
}

// Function: executes 'cleanup' in the main renderer.
// Detail: centralizes the logic needed to end the stage and discard associated resources.
void Renderer::cleanup() {
    // --- 1. Core Geometry ---
    RendererInternal::deleteVertexArrayAndBuffer(crosshairVAO, crosshairVBO);
    RendererInternal::deleteVertexArrayAndBuffer(breakBlockVAO, breakBlockVBO);
    RendererInternal::deleteVertexArrayAndBuffer(stencilFaceVAO, stencilFaceVBO);
    RendererInternal::deleteVertexArrayAndBuffer(portalFrameVAO, portalFrameVBO);
    RendererInternal::deleteVertexArrayAndBuffer(screenQuadVAO, screenQuadVBO);
    RendererInternal::deleteVertexArrayAndBuffer(itemSpriteVAO, itemSpriteVBO);
    RendererInternal::deleteVertexArrayAndBuffer(dustParticleVAO, dustParticleVBO);
    cleanupBlockTextures();
    cleanupEntityAssets();
    cleanupBlockModelAssets();

    // --- 2. Asset Caches ---
    for (auto& [key, texture] : itemTextureCache) {
        (void)key;
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
    }

    itemTextureCache.clear();
    dustParticleScratch.clear();
    dustParticleCapacity = 0;
    releaseSceneRenderTarget();

    // --- 3. Shader Programs ---
    blockShader.release();
    lineShader.release();
    depthWindowShader.release();
    dustParticleShader.release();
    itemSpriteShader.release();
    entityShader.release();
    deathScreenShader.release();

    dustTransition = {};
    heldItemTransition = {};
}

bool Renderer::setupBlockTextures() {
    cleanupBlockTextures();
    blockTextures_ =
        BlockTextureCache::loadBlockTextures(BlockRegistry::instance().definitions());
    return true;
}

void Renderer::cleanupBlockTextures() {
    BlockTextureCache::destroyBlockTextures(blockTextures_);
}

void Renderer::bindBlockTextures() {
    BlockTextureCache::bindBlockTextures(blockTextures_);
}
#pragma endregion

#pragma region 2b. Point Light Collection

void Renderer::collectPointLights(const FractalWorld* world, const glm::vec3& cameraPos,
    const glm::vec3& playerPos, float time) {
    pointLightCount_ = 0;

    // Slot 0: player-carried torch light
    //PointLight& playerLight = pointLights_[pointLightCount_++];
    //playerLight.position = playerPos + glm::vec3(0.0f, 0.3f, 0.0f);
    //playerLight.color = glm::vec3(1.0f, 0.9f, 0.75f);
    //playerLight.radius = 0.0f;

    if (!world) {
        return;
    }

    // Collect emissive blocks from loaded chunks near the camera.
    // We gather candidates then keep the closest ones to fit our budget.
    struct LightCandidate {
        glm::vec3 position;
        glm::vec3 color;
        float radius;
        float distSq;
    };

    std::vector<LightCandidate> candidates;
    candidates.reserve(128);

    const glm::ivec3 centerChunk =
        FractalWorld::worldToChunkPos(glm::ivec3(glm::floor(cameraPos)));
    const int scanRadius = glm::min(world->renderDistance, 4);

    for (const auto& [chunkPos, chunk] : world->chunks) {
        if (!chunk || !chunk->generated) {
            continue;
        }

        const glm::ivec3 d = chunkPos - centerChunk;
        if (std::abs(d.x) > scanRadius || std::abs(d.y) > scanRadius || std::abs(d.z) > scanRadius) {
            continue;
        }

        for (const auto& light : chunk->lightBlocks()) {
            const float distSq = glm::dot(light.worldPos - cameraPos, light.worldPos - cameraPos);
            const float lightRadius = getBlockLightRadius(light.type);
            // Skip lights too far to contribute
            if (distSq > (lightRadius + 8.0f) * (lightRadius + 8.0f)) {
                continue;
            }

            candidates.push_back({
                light.worldPos,
                getBlockLightColor(light.type),
                lightRadius,
                distSq
            });
        }
    }

    // Sort by distance, keep closest
    std::sort(candidates.begin(), candidates.end(),
        [](const LightCandidate& a, const LightCandidate& b) {
            return a.distSq < b.distSq;
        });

    const int maxBlockLights = kMaxPointLights - pointLightCount_;
    const int count = glm::min(static_cast<int>(candidates.size()), maxBlockLights);
    for (int i = 0; i < count; ++i) {
        PointLight& pl = pointLights_[pointLightCount_++];
        pl.position = candidates[i].position;
        pl.color = candidates[i].color;
        pl.radius = candidates[i].radius;
    }
}

void Renderer::uploadPointLights() {
    blockShader.setInt("uPointLightCount", pointLightCount_);

    for (int i = 0; i < pointLightCount_; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "uPointLights[%d].position", i);
        blockShader.setVec3(buf, pointLights_[i].position);
        std::snprintf(buf, sizeof(buf), "uPointLights[%d].color", i);
        blockShader.setVec3(buf, pointLights_[i].color);
        std::snprintf(buf, sizeof(buf), "uPointLights[%d].radius", i);
        blockShader.setFloat(buf, pointLights_[i].radius);
    }
}

#pragma endregion

#pragma region 3. Frame Rendering
// --- 3. Frame Rendering ---

// Function: renders 'render' in the main renderer.
// Detail: uses 'worldStack', 'player', 'aspect', 'time', 'debugThirdPersonView' to draw the corresponding visual output using the current state.
void Renderer::render(WorldStack& worldStack, Player& player, float aspect, float time,
    bool wireframeMode, bool debugThirdPersonView) {
    const glm::vec2 viewportSize = ENGINE::GETVIEWPORTSIZE();
    const glm::ivec2 outputSize(
        static_cast<int>(viewportSize.x),
        static_cast<int>(viewportSize.y)
    );

    if (outputSize.x <= 0 || outputSize.y <= 0) {
        return;
    }

    if (shouldUseScaledSceneTarget(outputSize)) {
        ensureSceneRenderTarget(outputSize);
    }

    const bool usingScaledSceneTarget =
        shouldUseScaledSceneTarget(outputSize) &&
        sceneRenderTarget_.framebuffer != 0;

    const glm::ivec2 sceneSize = usingScaledSceneTarget ? sceneRenderTarget_.size : outputSize;

    if (usingScaledSceneTarget) {
        glBindFramebuffer(GL_FRAMEBUFFER, sceneRenderTarget_.framebuffer);
    }
    else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glViewport(0, 0, sceneSize.x, sceneSize.y);

    const float sceneAspect =
        static_cast<float>(outputSize.x) / static_cast<float>(glm::max(outputSize.y, 1));

    renderScene(worldStack, player, sceneAspect, time, wireframeMode, debugThirdPersonView);

    if (!usingScaledSceneTarget) {
        return;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneRenderTarget_.framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, sceneSize.x, sceneSize.y, 0, 0, outputSize.x,
        outputSize.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputSize.x, outputSize.y);
}

void Renderer::renderScene(WorldStack& worldStack, Player& player, float aspect, float time,
    bool wireframeMode, bool debugThirdPersonView) {
    // This is the coordination point of the 3D frame: active world, nested preview, items, particles, and overlays.

    // --- 1. Frame Atmosphere & Transitions ---
    const int depth = worldStack.currentDepth();
    glm::vec4 fog = getFogColor(depth);

    if (player.transition != PlayerTransition::NONE && player.transitionDuration > 0.0f) {
        const float t = glm::clamp(
            player.transitionTimer / player.transitionDuration,
            0.0f, 1.0f
        );

        const auto smooth01 = [](float x) {
            const float clamped = glm::clamp(x, 0.0f, 1.0f);
            return clamped * clamped * (3.0f - 2.0f * clamped);
            };

        float darkAmount = 0.0f;
        if (player.transition == PlayerTransition::DIVING_IN) {
            darkAmount = 0.92f * smooth01(glm::clamp(t * 1.25f, 0.0f, 1.0f));
        }
        else if (player.transition == PlayerTransition::RISING_OUT) {
            darkAmount = 0.60f * smooth01(1.0f - t);
        }
        fog = glm::mix(fog, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), darkAmount);
    }

    glClearColor(fog.r, fog.g, fog.b, 1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    FractalWorld* world = worldStack.currentWorld();
    const float dt = glm::max(ENGINE::GETDELTATIME(), 0.0f);
    updateDustTransition(world, player, depth, dt);
    updateHeldItemTransition(world, player, depth, dt);

    // --- 2. Camera & Target State ---
    Camera sceneCamera = player.camera;
    if (debugThirdPersonView) {
        sceneCamera = buildThirdPersonDebugCamera(player.camera);
    }

    const glm::mat4 view = sceneCamera.getViewMatrix();
    const glm::mat4 proj = sceneCamera.getProjectionMatrix(aspect);
    const glm::mat4 vp = proj * view;

    float breakProgress = 0.0f;
    glm::vec3 breakBlockCenter(0.0f);
    float highlightActive = 0.0f;
    glm::vec3 highlightBlockCenter(0.0f);

    if (world && player.hasTargetBlock() && player.isBreakingTargetBlock() &&
        player.getBreakingBlock() == player.getTargetBlock()) {
        const BlockId targetType = world->getBlock(player.getTargetBlock());
        if (canTargetBlock(targetType)) {
            breakProgress = player.getBreakingProgress();
            breakBlockCenter = glm::vec3(player.getTargetBlock()) + glm::vec3(0.5f);
        }
    }

    if (world && player.hasTargetBlock() && HUD::isVisible()) {
        const BlockId targetType = world->getBlock(player.getTargetBlock());
        if (canTargetBlock(targetType)) {
            highlightActive = 1.0f;
            highlightBlockCenter = glm::vec3(player.getTargetBlock()) + glm::vec3(0.5f);
        }
    }

    // --- 3. Main Terrain Pass ---
    blockShader.use();
    blockShader.setMat4("uVP", vp);
    blockShader.setMat4("uModel", glm::mat4(1.0f));
    blockShader.setVec3("uCameraPos", sceneCamera.position);
    blockShader.setVec4("uFogColor", fog);
    blockShader.setFloat("uFogDensity",
        computeFogDensity(depth, world ? world->renderDistance : 5));
    blockShader.setFloat("uTime", time);
    blockShader.setFloat("uAlpha", 1.0f);
    blockShader.setFloat("uAoStrength", 1.0f);
    blockShader.setVec4("uBiomeTint", getBiomeMaterialTint(world, depth));
    blockShader.setInt("uUseLocalMaterialSpace", 0);
    bindBlockTextures();

    if (advancedLightingEnabled_) {
        collectPointLights(world, sceneCamera.position, player.camera.position, time);
        uploadPointLights();
    } else {
        pointLightCount_ = 0;
        blockShader.setInt("uPointLightCount", 0);
    }

    setBreakEffectUniforms(breakBlockCenter, breakProgress);
    setHighlightEffectUniforms(highlightBlockCenter, highlightActive);

    const glm::vec3 cullingCameraPos =
        debugThirdPersonView ? player.camera.position : sceneCamera.position;

    const glm::mat4 cullingViewProjection =
        debugThirdPersonView
        ? player.camera.getProjectionMatrix(aspect) * player.camera.getViewMatrix()
        : vp;

    if (wireframeMode) {
        // Keep the toggle scoped to the terrain pass so HUD and overlays stay filled.
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    worldStack.render(sceneCamera.position, vp, cullingCameraPos, cullingViewProjection);

    if (wireframeMode) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    setBreakEffectUniforms(glm::vec3(0.0f), 0.0f);
    setHighlightEffectUniforms(glm::vec3(0.0f), 0.0f);

    // --- 4. Supplemental World Passes ---
    if (world) {
        renderWorldBlockModels(*world, vp, sceneCamera.position, fog, depth,
            world->renderDistance, time, 1.0f);
    }

    if (world) {
        renderEntities(*world, vp, sceneCamera.position, fog, depth,
            world->renderDistance, time, 1.0f);
    }

    if (world) {
        renderDroppedItems(*world, vp, sceneCamera.position, fog, depth, world->renderDistance,
            time, 1.0f);
    }

    if (world && player.hasTargetBlock() && HUD::isVisible()) {
        renderTargetSelectionWireframe(*world, player, vp);
    }

    // --- 5. Nested Preview & Particles ---
    if ((player.nestedPreview.active || player.nestedPreview.fade > 0.001f) && world &&
        isSolid(world->getBlock(player.nestedPreview.block))) {
        FractalWorld* nestedWorld =
            worldStack.getOrCreateNestedPreviewWorld(player.nestedPreview.block);

        if (nestedWorld) {
            renderStencilMask(vp, player.nestedPreview.block, player.nestedPreview.normal);
            prepareStencilDepthWindow();
            renderNestedPreviewWorld(worldStack, *nestedWorld, player.nestedPreview,
                player.camera, aspect, time);
            renderPortalFrame(vp, player.nestedPreview.block, player.nestedPreview.normal,
                player.nestedPreview.fade);
        }
    }

    if (world && dustTransition.visibility > 0.001f) {
        renderDustParticles(*world, sceneCamera, vp, fog, depth, time,
            dustTransition.visibility);
    }

    // --- 6. Debug & Held Item Overlays ---
    if (debugThirdPersonView) {
        renderCameraFrustumDebug(player.camera, vp);
    }
    else if (!player.isInventoryOpen() && HUD::isVisible()) {
        renderHeldItem(player, world, vp, depth, time, heldItemTransition.visibility);
    }
}

void Renderer::renderDeathScreenBackground(const glm::ivec2& screenSize, float timeSeconds,
    float vignetteExtra) {
    if (screenSize.x <= 0 || screenSize.y <= 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    deathScreenShader.use();
    deathScreenShader.setVec2("uResolution", glm::vec2(screenSize));
    deathScreenShader.setFloat("uTime", timeSeconds);
    deathScreenShader.setVec4("uMouse", glm::vec4(0.0f));
    deathScreenShader.setFloat("uVignetteExtra", glm::clamp(vignetteExtra, 0.0f, 1.0f));

    glBindVertexArray(screenQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
#pragma endregion

#pragma region 4. Scene Render Targets
// --- 4. Scene Render Targets ---

glm::ivec2 Renderer::sceneRenderSizeFor(const glm::ivec2& outputSize) const {
    const float clampedScale = glm::clamp(
        renderScale_,
        ClientDefaults::kMinRenderScale,
        ClientDefaults::kMaxRenderScale
    );

    return glm::ivec2(
        glm::max(1, static_cast<int>(std::lround(outputSize.x * clampedScale))),
        glm::max(1, static_cast<int>(std::lround(outputSize.y * clampedScale)))
    );
}

bool Renderer::shouldUseScaledSceneTarget(const glm::ivec2& outputSize) const {
    return outputSize.x > 0 &&
        outputSize.y > 0 &&
        renderScale_ < (ClientDefaults::kMaxRenderScale - 0.001f);
}

void Renderer::ensureSceneRenderTarget(const glm::ivec2& outputSize) {
    const glm::ivec2 targetSize = sceneRenderSizeFor(outputSize);

    if (sceneRenderTarget_.framebuffer != 0 && sceneRenderTarget_.size == targetSize) {
        return;
    }

    releaseSceneRenderTarget();

    glGenFramebuffers(1, &sceneRenderTarget_.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneRenderTarget_.framebuffer);

    glGenTextures(1, &sceneRenderTarget_.colorTexture);
    glBindTexture(GL_TEXTURE_2D, sceneRenderTarget_.colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, targetSize.x, targetSize.y, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        sceneRenderTarget_.colorTexture, 0);

    glGenRenderbuffers(1, &sceneRenderTarget_.depthStencilRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneRenderTarget_.depthStencilRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, targetSize.x, targetSize.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        sceneRenderTarget_.depthStencilRenderbuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        releaseSceneRenderTarget();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    sceneRenderTarget_.size = targetSize;
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::releaseSceneRenderTarget() {
    if (sceneRenderTarget_.depthStencilRenderbuffer != 0) {
        glDeleteRenderbuffers(1, &sceneRenderTarget_.depthStencilRenderbuffer);
        sceneRenderTarget_.depthStencilRenderbuffer = 0;
    }

    if (sceneRenderTarget_.colorTexture != 0) {
        glDeleteTextures(1, &sceneRenderTarget_.colorTexture);
        sceneRenderTarget_.colorTexture = 0;
    }

    if (sceneRenderTarget_.framebuffer != 0) {
        glDeleteFramebuffers(1, &sceneRenderTarget_.framebuffer);
        sceneRenderTarget_.framebuffer = 0;
    }

    sceneRenderTarget_.size = glm::ivec2(0);
}
#pragma endregion

#pragma region 5. Atmosphere & Transitions
// --- 5. Atmosphere & Transitions ---

// Function: returns 'getFogColor' in the main renderer.
// Detail: uses 'depth' to expose derived data or controlled access to internal state.
// Return: returns 'glm::vec4' with the result composed by this call.
glm::vec4 Renderer::getFogColor(int depth) {
    static const glm::vec4 fogColors[] = {
        {0.02f, 0.02f, 0.08f, 1.0f},
        {0.06f, 0.01f, 0.10f, 1.0f},
        {0.10f, 0.02f, 0.02f, 1.0f},
        {0.01f, 0.07f, 0.05f, 1.0f},
        {0.08f, 0.06f, 0.01f, 1.0f},
        {0.02f, 0.05f, 0.10f, 1.0f},
        {0.58431375f, 0.97647059f, 0.89019608f, 1.0f}, // #95f9e3
        {0.11764706f, 0.65882353f, 0.58823529f, 1.0f}, // #1ea896
        {0.47058824f, 0.52156863f, 0.52156863f, 1.0f}, // #788585
    };

    const int index =
        ((depth % static_cast<int>(std::size(fogColors))) +
            static_cast<int>(std::size(fogColors))) %
        static_cast<int>(std::size(fogColors));

    return fogColors[index];
}

// Function: executes 'computeFogDensity' in the main renderer.
// Detail: uses 'depth', 'renderDistance' to encapsulate this specific stage of the subsystem.
// Return: returns 'float' with the numerical value calculated for the next pipeline decision.
float Renderer::computeFogDensity(int depth, int renderDistance) const {
    const float visibleDistance =
        std::max(28.0f, static_cast<float>(renderDistance * 16 - 10));
    const float depthScale = 1.0f + static_cast<float>(depth) * 0.04f;

    return (4.6f / visibleDistance) * depthScale;
}

// Function: assembles 'makeDustWorldKey' in the main renderer.
// Detail: uses 'world', 'depth' to derive and compose a value ready for the next pipeline stage.
// Return: returns 'std::uint64_t' with the numerical value calculated for the next pipeline decision.
std::uint64_t Renderer::makeDustWorldKey(const FractalWorld* world, int depth) const {
    if (!world) {
        return 0;
    }

    return static_cast<std::uint64_t>(world->seed) ^
        (static_cast<std::uint64_t>(world->depth) << 32) ^
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(depth)) << 48);
}

// Function: updates 'updateDustTransition' in the main renderer.
// Detail: uses 'world', 'player', 'depth', 'dt' to synchronize derived state with the current frame.
void Renderer::updateDustTransition(FractalWorld* world, const Player& player, int depth, float dt) {
    const auto& dust = DustParticles::config;
    const std::uint64_t currentWorldKey = makeDustWorldKey(world, depth);
    const bool transitionActive = player.transition != PlayerTransition::NONE;
    const bool areaReady =
        world && world->isAreaGenerated(player.camera.position, dust.readyChunkRadius);

    if (!dustTransition.initialized) {
        dustTransition.initialized = true;
        dustTransition.worldKey = currentWorldKey;
        dustTransition.waitingForWorldLoad = !areaReady;
        dustTransition.visibility = areaReady ? 1.0f : 0.0f;
    }

    if (currentWorldKey != dustTransition.worldKey) {
        dustTransition.worldKey = currentWorldKey;
        dustTransition.waitingForWorldLoad = true;
    }

    if (transitionActive) {
        dustTransition.waitingForWorldLoad = true;
        dustTransition.visibility =
            std::max(0.0f, dustTransition.visibility - dust.fadeOutSpeed * dt);
        return;
    }

    if (dustTransition.waitingForWorldLoad) {
        if (!areaReady) {
            dustTransition.visibility =
                std::max(0.0f, dustTransition.visibility - dust.fadeOutSpeed * dt);
            return;
        }
        dustTransition.waitingForWorldLoad = false;
    }

    dustTransition.visibility =
        std::min(1.0f, dustTransition.visibility + dust.fadeInSpeed * dt);
}

// Function: updates 'updateHeldItemTransition' in the main renderer.
// Detail: uses 'world', 'player', 'depth', 'dt' to synchronize derived state with the current frame.
void Renderer::updateHeldItemTransition(FractalWorld* world, const Player& player, int depth, float dt) {
    constexpr float hideSpeed = 5.0f;
    constexpr float showSpeed = 5.0f;
    constexpr float swapTravelDistance = 0.34f;
    constexpr float swapPhaseDurationFallback = 0.09f;

    const std::uint64_t currentWorldKey = makeDustWorldKey(world, depth);
    const bool transitionActive = player.transition != PlayerTransition::NONE;
    const bool areaReady =
        world &&
        world->isAreaGenerated(player.camera.position, DustParticles::config.readyChunkRadius);

    if (!heldItemTransition.initialized) {
        heldItemTransition.initialized = true;
        heldItemTransition.worldKey = currentWorldKey;
        heldItemTransition.waitingForWorldLoad = !areaReady;
        heldItemTransition.visibility = areaReady ? 1.0f : 0.0f;
        heldItemTransition.selectedIndex = player.getSelectedHotbarIndex();
        heldItemTransition.currentItem = player.getSelectedHotbarItem();
        heldItemTransition.swapFromItem = heldItemTransition.currentItem;
        heldItemTransition.swapToItem = heldItemTransition.currentItem;
        heldItemTransition.swapTimer = heldItemTransition.swapDuration;
        heldItemTransition.swapVerticalOffset = 0.0f;
        heldItemTransition.swapAnimating = false;
        heldItemTransition.swapPhase = HeldItemTransitionState::SwapPhase::NONE;
    }

    if (currentWorldKey != heldItemTransition.worldKey) {
        heldItemTransition.worldKey = currentWorldKey;
        heldItemTransition.waitingForWorldLoad = true;
        heldItemTransition.swapAnimating = false;
        heldItemTransition.swapTimer = 0.0f;
        heldItemTransition.swapVerticalOffset = 0.0f;
        heldItemTransition.selectedIndex = player.getSelectedHotbarIndex();
        heldItemTransition.currentItem = player.getSelectedHotbarItem();
        heldItemTransition.swapFromItem = heldItemTransition.currentItem;
        heldItemTransition.swapToItem = heldItemTransition.currentItem;
        heldItemTransition.swapPhase = HeldItemTransitionState::SwapPhase::NONE;
    }

    if (transitionActive) {
        heldItemTransition.waitingForWorldLoad = true;
        heldItemTransition.visibility =
            std::max(0.0f, heldItemTransition.visibility - hideSpeed * dt);
        heldItemTransition.swapAnimating = false;
        heldItemTransition.swapTimer = 0.0f;
        heldItemTransition.swapVerticalOffset = 0.0f;
        heldItemTransition.selectedIndex = player.getSelectedHotbarIndex();
        heldItemTransition.currentItem = player.getSelectedHotbarItem();
        heldItemTransition.swapFromItem = heldItemTransition.currentItem;
        heldItemTransition.swapToItem = heldItemTransition.currentItem;
        heldItemTransition.swapPhase = HeldItemTransitionState::SwapPhase::NONE;
        return;
    }

    if (heldItemTransition.waitingForWorldLoad) {
        if (!areaReady) {
            heldItemTransition.visibility =
                std::max(0.0f, heldItemTransition.visibility - hideSpeed * dt);
            heldItemTransition.swapAnimating = false;
            heldItemTransition.swapTimer = 0.0f;
            heldItemTransition.swapVerticalOffset = 0.0f;
            heldItemTransition.selectedIndex = player.getSelectedHotbarIndex();
            heldItemTransition.currentItem = player.getSelectedHotbarItem();
            heldItemTransition.swapFromItem = heldItemTransition.currentItem;
            heldItemTransition.swapToItem = heldItemTransition.currentItem;
            heldItemTransition.swapPhase = HeldItemTransitionState::SwapPhase::NONE;
            return;
        }
        heldItemTransition.waitingForWorldLoad = false;
    }

    const int selectedIndex = player.getSelectedHotbarIndex();
    const InventoryItem selectedItem = player.getSelectedHotbarItem();

    if (heldItemTransition.selectedIndex != selectedIndex) {
        heldItemTransition.swapAnimating = true;
        heldItemTransition.swapPhase = HeldItemTransitionState::SwapPhase::HIDING;
        heldItemTransition.swapTimer = 0.0f;
        heldItemTransition.swapDuration = glm::max(
            heldItemTransition.swapDuration,
            swapPhaseDurationFallback * 2.0f
        );

        heldItemTransition.swapFromItem =
            heldItemTransition.currentItem.empty() ? selectedItem : heldItemTransition.currentItem;

        heldItemTransition.swapToItem = selectedItem;
        heldItemTransition.selectedIndex = selectedIndex;
    }

    if (heldItemTransition.swapAnimating) {
        const float phaseDuration =
            glm::max(heldItemTransition.swapDuration * 0.5f, 0.0001f);

        const auto easeIn01 = [](float x) {
            const float clamped = glm::clamp(x, 0.0f, 1.0f);
            return clamped * clamped * clamped;
            };

        const auto easeOut01 = [](float x) {
            const float clamped = glm::clamp(x, 0.0f, 1.0f);
            const float inv = 1.0f - clamped;
            return 1.0f - inv * inv * inv;
            };

        if (heldItemTransition.swapPhase == HeldItemTransitionState::SwapPhase::HIDING) {
            heldItemTransition.swapTimer =
                glm::min(heldItemTransition.swapTimer + dt, phaseDuration);

            const float t = heldItemTransition.swapTimer / phaseDuration;

            heldItemTransition.swapVerticalOffset = -swapTravelDistance * easeIn01(t);
            heldItemTransition.currentItem = heldItemTransition.swapFromItem;

            if (heldItemTransition.swapTimer >= phaseDuration) {
                heldItemTransition.swapPhase = HeldItemTransitionState::SwapPhase::SHOWING;
                heldItemTransition.swapTimer = 0.0f;
                heldItemTransition.currentItem = heldItemTransition.swapToItem;
                heldItemTransition.swapVerticalOffset = -swapTravelDistance;
            }

        }
        else if (heldItemTransition.swapPhase == HeldItemTransitionState::SwapPhase::SHOWING) {
            heldItemTransition.swapTimer =
                glm::min(heldItemTransition.swapTimer + dt, phaseDuration);

            const float t = heldItemTransition.swapTimer / phaseDuration;

            heldItemTransition.swapVerticalOffset =
                -swapTravelDistance * (1.0f - easeOut01(t));

            heldItemTransition.currentItem = heldItemTransition.swapToItem;

            if (heldItemTransition.swapTimer >= phaseDuration) {
                heldItemTransition.swapAnimating = false;
                heldItemTransition.swapPhase = HeldItemTransitionState::SwapPhase::NONE;
                heldItemTransition.swapTimer = 0.0f;
                heldItemTransition.currentItem = heldItemTransition.swapToItem;
                heldItemTransition.swapVerticalOffset = 0.0f;
            }

        }
        else {
            heldItemTransition.swapAnimating = false;
            heldItemTransition.swapTimer = 0.0f;
            heldItemTransition.swapVerticalOffset = 0.0f;
            heldItemTransition.currentItem = selectedItem;
            heldItemTransition.swapPhase = HeldItemTransitionState::SwapPhase::NONE;
        }
    }
    else {
        heldItemTransition.currentItem = selectedItem;
        heldItemTransition.swapVerticalOffset = 0.0f;
        heldItemTransition.swapPhase = HeldItemTransitionState::SwapPhase::NONE;
    }

    heldItemTransition.visibility =
        std::min(1.0f, heldItemTransition.visibility + showSpeed * dt);
}
#pragma endregion
