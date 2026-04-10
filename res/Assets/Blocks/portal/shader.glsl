float dist = length(centeredUv);
float ring = 0.5 + 0.5 * sin(dist * 26.0 - uTime * 2.0);
float vortex = fbm21(uv * 2.25 + vec2(uTime * 0.15, -uTime * 0.12) + vec2(cellHash * 9.0));
vec3 albedo = mix(vec3(0.06, 0.02, 0.10),
                  base * 1.20 + vec3(0.10, 0.0, 0.16), vortex);
albedo += vec3(0.18, 0.03, 0.22) * ring * 0.25;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.8)), 0.05, 0.35,
                  0.55 + ring * 2.30 + vortex * 0.20);
