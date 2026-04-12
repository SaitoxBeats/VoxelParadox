vec2 warpedUv = localUv;
warpedUv.x += sin((localUv.y * 12.0) + uTime * 2.0 + cellHash * 6.0) * 0.03;
warpedUv.y += cos((localUv.x * 10.0) - uTime * 1.6 + cellHash * 4.0) * 0.03;
warpedUv = clamp(warpedUv, vec2(0.001), vec2(0.999));

vec4 warpedTexel = sampleBlockAtlas(materialId, warpedUv);
vec3 warpedColor = warpedTexel.rgb;

float bands = 0.5 + 0.5 * sin((warpedUv.x * 18.0) + uTime * 1.5 + cellHash * 4.0);
float flecks = noise21(warpedUv * 14.0 + cellHash * 6.0);

vec3 albedo = mix(
    warpedColor * 0.82,
    warpedColor * 1.18 + vec3(0.08, 0.04, 0.00),
    bands
);

albedo += vec3(flecks * 0.05);

return makeSample(
    clamp(albedo, vec3(0.0), vec3(1.6)),
    0.58,
    0.18,
    0.0
);