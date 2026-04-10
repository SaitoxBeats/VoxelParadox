float wave = 0.5 + 0.5 * sin((localUv.x * 12.0) + (localUv.y * 10.0) + uTime * 2.0 + cellHash * 6.0);
float flecks = noise21(localUv * 14.0 + cellHash * 6.0);

vec3 albedo = mix(
    atlasColor * 0.78,
    atlasColor * 1.22 + vec3(0.08, 0.04, 0.00),
    wave
);

albedo += vec3((flecks - 0.5) * 0.08);

return makeSample(
    clamp(albedo, vec3(0.0), vec3(1.6)),
    0.58,
    0.18,
    0.0
);
