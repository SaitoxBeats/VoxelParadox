#version 460 core
// block.frag
// Shared block surface shader template. The block registry injects block base colors,
// per-block texture bindings, material functions, and the dispatch table below.

in vec3 vWorldPos;
in vec3 vLocalPos;
in vec3 vNormal;
in vec3 vFaceNormal;
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
/*__BLOCK_TEXTURE_DECLARATIONS__*/
uniform vec3 uBreakBlockCenter;
uniform float uBreakProgress;
uniform vec3 uHighlightBlockCenter;
uniform float uHighlightActive;

struct PointLightData {
    vec3 position;
    vec3 color;
    float radius;
};

const int MAX_POINT_LIGHTS = 32;
uniform int uPointLightCount;
uniform PointLightData uPointLights[MAX_POINT_LIGHTS];

out vec4 FragColor;

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

// --- Procedural noise + pattern helpers used by the ShaderEditor node graph.
// Outputs are in signed range [-1, 1] for the simplex/gradient family; node
// codegen remaps to [0, 1] when the "0-1 range" flag is on.

vec3 permuteSn(vec3 x) {
    return mod(((x * 34.0) + 1.0) * x, 289.0);
}

vec4 permuteSn4(vec4 x) {
    return mod(((x * 34.0) + 1.0) * x, 289.0);
}

vec4 taylorInvSqrtSn(vec4 r) {
    return 1.79284291400159 - 0.85373472095314 * r;
}

float snoise2(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439,
                        -0.577350269189626, 0.024390243902439);
    vec2 i  = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permuteSn(permuteSn(i.y + vec3(0.0, i1.y, 1.0)) +
                       i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)),
                 0.0);
    m = m * m;
    m = m * m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    vec3 g;
    g.x  = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

float snoise3(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);
    vec3 i  = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + 2.0 * C.xxx;
    vec3 x3 = x0 - 1.0 + 3.0 * C.xxx;
    i = mod(i, 289.0);
    vec4 p = permuteSn4(permuteSn4(permuteSn4(
                 i.z + vec4(0.0, i1.z, i2.z, 1.0))
               + i.y + vec4(0.0, i1.y, i2.y, 1.0))
               + i.x + vec4(0.0, i1.x, i2.x, 1.0));
    float n_ = 1.0 / 7.0;
    vec3 ns = n_ * D.wyz - D.xzx;
    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    vec4 norm = taylorInvSqrtSn(vec4(dot(p0, p0), dot(p1, p1),
                                     dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)),
                 0.0);
    m = m * m;
    return 42.0 * dot(m * m,
                      vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

vec2 gradDir2(vec2 p) {
    float a = hash21(p) * 6.2831853;
    return vec2(cos(a), sin(a));
}

float gnoise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = dot(gradDir2(i),                    f);
    float b = dot(gradDir2(i + vec2(1.0, 0.0)),   f - vec2(1.0, 0.0));
    float c = dot(gradDir2(i + vec2(0.0, 1.0)),   f - vec2(0.0, 1.0));
    float d = dot(gradDir2(i + vec2(1.0, 1.0)),   f - vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float simpleNoise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float truchetMaze(vec2 p, float resolution) {
    p *= max(resolution, 0.0001);
    vec2 cell = floor(p);
    vec2 f = fract(p);
    float h = hash21(cell);
    if (h < 0.5) f.x = 1.0 - f.x;
    float d = min(length(f), length(f - vec2(1.0)));
    float w = 0.12;
    return 1.0 - smoothstep(0.5 - w, 0.5 + w, abs(d - 0.5));
}

float truchetCircles(vec2 p, float resolution) {
    p *= max(resolution, 0.0001);
    vec2 cell = floor(p);
    vec2 f = fract(p);
    float h = hash21(cell);
    if (h < 0.5) f.x = 1.0 - f.x;
    float d = min(length(f), length(f - vec2(1.0)));
    float w = 0.18;
    float band = 1.0 - smoothstep(w, w + 0.04, abs(d - 0.5));
    return clamp(band, 0.0, 1.0);
}

float truchetTriangles(vec2 p, float resolution) {
    p *= max(resolution, 0.0001);
    vec2 cell = floor(p);
    vec2 f = fract(p);
    float h = hash21(cell);
    if (h < 0.25)      f = f;
    else if (h < 0.5)  f = vec2(1.0 - f.x, f.y);
    else if (h < 0.75) f = vec2(f.x, 1.0 - f.y);
    else               f = vec2(1.0 - f.x, 1.0 - f.y);
    return step(f.x + f.y, 1.0);
}

vec2 distortUvFbm(vec2 uv, float strength, float timeOffset) {
    vec2 offset = vec2(
        fbm21(uv * 2.0 + vec2(timeOffset, 0.0)),
        fbm21(uv * 2.0 + vec2(0.0, timeOffset))
    );
    return uv + (offset - 0.5) * strength;
}

vec2 swirlUv(vec2 uv, float strength) {
    vec2 c = uv - vec2(0.5);
    float d = length(c);
    float a = strength * d;
    float s = sin(a);
    float co = cos(a);
    return vec2(co * c.x - s * c.y, s * c.x + co * c.y) + vec2(0.5);
}

vec2 rippleUv(vec2 uv, float frequency, float amplitude, float timeOffset) {
    vec2 c = uv - vec2(0.5);
    float d = length(c);
    vec2 dir = c / max(d, 1e-4);
    float w = sin(d * frequency - timeOffset) * amplitude;
    return uv + dir * w;
}

const float BREAK_PHASE_POWER = 2.0;
const float BREAK_PIXEL_GRID = 16.0;
const float BREAK_STAR_GRID = 25.0;
const float BREAK_CORNER_MAX_DISTANCE = 0.70710678;
const float HIGHLIGHT_EDGE_INNER = 0.018;
const float HIGHLIGHT_EDGE_MID = 0.015;
const float HIGHLIGHT_EDGE_OUTER = 0.010;
const float HIGHLIGHT_ANTS_SCALE = 0.08;
const float HIGHLIGHT_ANTS_SPEED = 1.6;

vec2 breakHash2(vec2 p) {
    return vec2(
        hash21(p),
        hash21(p + vec2(37.1, 91.7))
    );
}

float breakFaceId(vec3 normal) {
    vec3 absNormal = abs(normal);
    if (absNormal.x > absNormal.y && absNormal.x > absNormal.z) {
        return normal.x > 0.0 ? 0.0 : 1.0;
    }
    if (absNormal.z > absNormal.y) {
        return normal.z > 0.0 ? 2.0 : 3.0;
    }
    return normal.y > 0.0 ? 4.0 : 5.0;
}

vec3 breakVoronoi2D(vec2 x) {
    vec2 n = floor(x);
    vec2 f = fract(x);
    vec2 mg = vec2(0.0);
    vec2 mr = vec2(0.0);

    float md = 1e9;

    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 g = vec2(float(i), float(j));
            vec2 o = breakHash2(n + g);
            vec2 r = g + o - f;
            float d = dot(r, r);

            if (d < md) {
                md = d;
                mr = r;
                mg = g;
            }
        }
    }

    md = 1e9;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 g = mg + vec2(float(i), float(j));
            vec2 o = breakHash2(n + g);
            vec2 r = g + o - f;
            vec2 delta = r - mr;
            float deltaLengthSq = dot(delta, delta);

            if (deltaLengthSq > 0.00001) {
                md = min(md, dot(0.5 * (mr + r), delta * inversesqrt(deltaLengthSq)));
            }
        }
    }

    return vec3(md, n + mg);
}

vec2 faceUv(vec3 worldPos, vec3 normal) {
    vec3 absNormal = abs(normal);

    if (absNormal.x > absNormal.y && absNormal.x > absNormal.z) {
        return vec2(worldPos.z, worldPos.y);
    }

    if (absNormal.z > absNormal.y) {
        return vec2(worldPos.x, worldPos.y);
    }

    return vec2(worldPos.x, worldPos.z);
}

float blockMask(vec3 worldPos, vec3 blockCenter) {
    vec3 blockMin = blockCenter - vec3(0.5);
    vec3 blockMax = blockCenter + vec3(0.5);
    vec3 inside =
        step(blockMin - vec3(0.0015), worldPos) *
        step(worldPos, blockMax + vec3(0.0015));
    return inside.x * inside.y * inside.z;
}

vec3 applySelectionHighlight(vec3 baseColor, vec3 worldPos, vec3 normal) {
    float selectionMask = uHighlightActive * blockMask(worldPos, uHighlightBlockCenter);
    if (selectionMask <= 0.0) {
        return baseColor;
    }

    vec3 viewDir = normalize(uCameraPos - worldPos);
    vec3 local = clamp(worldPos - (uHighlightBlockCenter - vec3(0.5)), 0.0, 1.0);
    vec2 localUv = clamp(faceUv(local, normal), 0.0, 1.0);
    float aa = max(max(fwidth(localUv.x), fwidth(localUv.y)) * 1.5, 0.0005);

    vec3 absNormal = abs(normal);
    vec3 edgeNormalMinU = vec3(0.0);
    vec3 edgeNormalMaxU = vec3(0.0);
    vec3 edgeNormalMinV = vec3(0.0);
    vec3 edgeNormalMaxV = vec3(0.0);

    if (absNormal.x > absNormal.y && absNormal.x > absNormal.z) {
        edgeNormalMinU = vec3(0.0, 0.0, -1.0);
        edgeNormalMaxU = vec3(0.0, 0.0, 1.0);
        edgeNormalMinV = vec3(0.0, -1.0, 0.0);
        edgeNormalMaxV = vec3(0.0, 1.0, 0.0);
    } else if (absNormal.z > absNormal.y) {
        edgeNormalMinU = vec3(-1.0, 0.0, 0.0);
        edgeNormalMaxU = vec3(1.0, 0.0, 0.0);
        edgeNormalMinV = vec3(0.0, -1.0, 0.0);
        edgeNormalMaxV = vec3(0.0, 1.0, 0.0);
    } else {
        edgeNormalMinU = vec3(-1.0, 0.0, 0.0);
        edgeNormalMaxU = vec3(1.0, 0.0, 0.0);
        edgeNormalMinV = vec3(0.0, 0.0, -1.0);
        edgeNormalMaxV = vec3(0.0, 0.0, 1.0);
    }

    float silhouetteMinU = 1.0 - smoothstep(-0.02, 0.04, dot(edgeNormalMinU, viewDir));
    float silhouetteMaxU = 1.0 - smoothstep(-0.02, 0.04, dot(edgeNormalMaxU, viewDir));
    float silhouetteMinV = 1.0 - smoothstep(-0.02, 0.04, dot(edgeNormalMinV, viewDir));
    float silhouetteMaxV = 1.0 - smoothstep(-0.02, 0.04, dot(edgeNormalMaxV, viewDir));

    float outerMinU =
        (1.0 - smoothstep(HIGHLIGHT_EDGE_MID - aa, HIGHLIGHT_EDGE_OUTER + aa, localUv.x)) *
        silhouetteMinU;
    float outerMaxU =
        (1.0 - smoothstep(HIGHLIGHT_EDGE_MID - aa, HIGHLIGHT_EDGE_OUTER + aa,
                          1.0 - localUv.x)) * silhouetteMaxU;
    float outerMinV =
        (1.0 - smoothstep(HIGHLIGHT_EDGE_MID - aa, HIGHLIGHT_EDGE_OUTER + aa, localUv.y)) *
        silhouetteMinV;
    float outerMaxV =
        (1.0 - smoothstep(HIGHLIGHT_EDGE_MID - aa, HIGHLIGHT_EDGE_OUTER + aa,
                          1.0 - localUv.y)) * silhouetteMaxV;

    float innerMinU =
        (1.0 - smoothstep(HIGHLIGHT_EDGE_INNER - aa, HIGHLIGHT_EDGE_MID + aa, localUv.x)) *
        silhouetteMinU;
    float innerMaxU =
        (1.0 - smoothstep(HIGHLIGHT_EDGE_INNER - aa, HIGHLIGHT_EDGE_MID + aa,
                          1.0 - localUv.x)) * silhouetteMaxU;
    float innerMinV =
        (1.0 - smoothstep(HIGHLIGHT_EDGE_INNER - aa, HIGHLIGHT_EDGE_MID + aa, localUv.y)) *
        silhouetteMinV;
    float innerMaxV =
        (1.0 - smoothstep(HIGHLIGHT_EDGE_INNER - aa, HIGHLIGHT_EDGE_MID + aa,
                          1.0 - localUv.y)) * silhouetteMaxV;

    float outerBand = max(max(outerMinU, outerMaxU), max(outerMinV, outerMaxV));
    float innerBand = max(max(innerMinU, innerMaxU), max(innerMinV, innerMaxV));

    float pattern = fract((gl_FragCoord.x + gl_FragCoord.y) * HIGHLIGHT_ANTS_SCALE -
                          uTime * HIGHLIGHT_ANTS_SPEED);
    float ants = step(0.5, pattern);
    vec3 antsColor = mix(vec3(0.02), vec3(1.0), ants);

    vec3 color = mix(baseColor, vec3(0.0), clamp(outerBand * 0.95 * selectionMask, 0.0, 1.0));
    color = mix(color, antsColor, clamp(innerBand * selectionMask, 0.0, 1.0));
    return color;
}

vec3 blockBaseColor(int materialId) {
/*__BLOCK_BASE_COLOR__*/    return vec3(1.0, 0.0, 1.0);
}

vec4 sampleBlockTexture(int materialId, vec2 localUv) {
/*__BLOCK_TEXTURE_SAMPLE__*/
    return vec4(1.0);
}

MaterialSample makeSample(vec3 albedo, float roughness, float specular, float emissive) {
    MaterialSample result;
    result.albedo = albedo;
    result.roughness = roughness;
    result.specular = specular;
    result.emissive = emissive;
    return result;
}

/*__BLOCK_SHADER_DECLARATIONS__*/

MaterialSample sampleBlockMaterial(int materialId, vec3 worldPos, vec3 worldNormal,
                                   vec3 faceNormal, vec3 viewDir) {
/*__BLOCK_SHADER_DISPATCH__*/    return makeSample(vec3(1.0, 0.0, 1.0), 0.80, 0.10, 0.0);
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
    vec3 worldNormal = normalize(vNormal);
    vec3 faceNormal = normalize(vFaceNormal);
    vec3 materialPos = (uUseLocalMaterialSpace != 0) ? vLocalPos : vWorldPos;
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    MaterialSample material =
        sampleBlockMaterial(vMaterialId, materialPos, worldNormal, faceNormal, viewDir);

    vec3 tint = clamp(vTint.rgb * uBiomeTint.rgb, vec3(0.45), vec3(1.85));
    material.albedo *= tint;

    vec3 lightDir = normalize(vec3(0.4, 1.0, 0.3));
    float diffuse = max(dot(worldNormal, lightDir), 0.0);
    float ambient = 0.38;
    float ao = mix(0.35, 1.0, vAO);
    float light = (ambient + diffuse * 0.55) * mix(1.0, ao, uAoStrength);

    vec3 halfDir = normalize(lightDir + viewDir);
    float specPower = mix(14.0, 96.0, 1.0 - material.roughness);
    float specular =
        pow(max(dot(worldNormal, halfDir), 0.0), specPower) * material.specular;

    vec3 color = material.albedo * light +
                 mix(vec3(specular), material.albedo * specular, 0.25);

    // Point light accumulation (emissive blocks + player torch)
    vec3 pointLightContrib = vec3(0.0);
    for (int i = 0; i < uPointLightCount; ++i) {
        vec3 toLight = uPointLights[i].position - vWorldPos;
        float dist = length(toLight);
        float r = uPointLights[i].radius;
        if (dist >= r) continue;

        vec3 lDir = toLight / max(dist, 0.001);
        float attenuation = 1.0 - dist / r;
        attenuation = attenuation * attenuation;

        float nDotL = max(dot(worldNormal, lDir), 0.0);
        vec3 plDiffuse = uPointLights[i].color * nDotL * attenuation;

        vec3 plHalf = normalize(lDir + viewDir);
        float plSpec = pow(max(dot(worldNormal, plHalf), 0.0), specPower) * material.specular;
        vec3 plSpecular = uPointLights[i].color * plSpec * attenuation;

        pointLightContrib += material.albedo * plDiffuse * 0.8 +
                             mix(plSpecular, material.albedo * plSpecular, 0.25) * 0.5;
    }
    color += pointLightContrib * ao;

    float pulse = 0.5 + 0.5 * sin(uTime * 3.0 + vWorldPos.x + vWorldPos.z);
    vec2 ditherCoord = faceUv(materialPos, faceNormal) * 16.0;
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
            vec3 pixelLocal =
                (floor(local * BREAK_PIXEL_GRID) + vec3(0.5)) / BREAK_PIXEL_GRID;

            vec2 breakUv = faceUv(pixelLocal, faceNormal);
            vec3 blockSeed = floor(uBreakBlockCenter);
            float faceId = breakFaceId(faceNormal);
            vec2 faceSeed = vec2(
                hash31(blockSeed + vec3(11.3, 7.1, 3.7 + faceId)),
                hash31(blockSeed + vec3(23.8, 19.4, 17.2 + faceId * 1.6180339))
            );

            vec2 p = (breakUv - 0.5) * 12.0 + faceSeed * 8.0;
            vec3 c = breakVoronoi2D(p);
            float edgeDistance = c.x;
            vec2 nearestCell = c.yz;

            vec2 cornerDelta = min(breakUv, vec2(1.0) - breakUv);
            float cornerDistance = length(cornerDelta);
            float propagation =
                pow(clamp(cornerDistance / BREAK_CORNER_MAX_DISTANCE, 0.0, 1.0),
                    BREAK_PHASE_POWER);

            float cellOffset =
                (hash21(nearestCell + faceSeed * 13.0 +
                        vec2(faceId * 0.37, faceId * 0.71)) - 0.5) * 0.12;

            float crackLine = 1.0 - smoothstep(0.018, 0.055, edgeDistance);
            float edgeLead = crackLine * 0.05;
            float revealCoord = propagation + cellOffset - edgeLead;

            float filledMask =
                smoothstep(revealCoord - 0.05, revealCoord + 0.05,
                           uBreakProgress);

            float crackFront =
                smoothstep(revealCoord - 0.10, revealCoord + 0.01,
                           uBreakProgress);
            float crackMask = crackLine * crackFront;

            float fractureMask = max(filledMask, crackMask * 0.92);
            fractureMask = max(fractureMask,
                               smoothstep(0.985, 1.0, uBreakProgress));
            fractureMask = clamp(fractureMask, 0.0, 1.0);

            color = mix(color, vec3(0.0), fractureMask);

            vec2 starCoord = breakUv * BREAK_STAR_GRID;
            vec2 starCell = floor(starCoord);
            vec2 starLocal = fract(starCoord) - vec2(0.5);
            vec2 starCenter =
                (breakHash2(starCell + faceSeed * 5.0 +
                            vec2(5.0 + faceId, 11.0 + faceId * 1.73)) - 0.5) * 0.55;
            float starPresence =
                step(0.0015,
                     hash21(starCell + faceSeed * 17.0 +
                            vec2(3.7 + faceId, 9.1 + faceId * 0.41)));
            float starShape =
                1.0 - smoothstep(0.10, 0.24, length(starLocal - starCenter));
            float starBlink =
                0.35 + 0.65 * (0.5 + 0.5 * sin(
                    uTime * 3.85 +
                    hash21(starCell + faceSeed * 3.0 +
                           vec2(7.3 + faceId, 2.1 + faceId * 0.23)) *
                        6.2831853));
            float starMask =
                starPresence * 0.0 * starShape * starBlink *
                smoothstep(0.55, 0.9, filledMask);

            color = mix(color, vec3(1.0), clamp(starMask, 0.0, 1.0));
        }
    }

    float dist = length(vWorldPos - uCameraPos);
    float fog = 1.0 - exp(-dist * uFogDensity);
    color = mix(color, uFogColor.rgb, fog);
    color = applySelectionHighlight(color, vWorldPos, faceNormal);

    FragColor = vec4(color, uAlpha);
}
