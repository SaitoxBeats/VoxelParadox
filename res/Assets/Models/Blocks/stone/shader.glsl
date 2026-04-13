float strata = fbm21(vec2(uv.x * 3.4 + cellHash * 4.0, worldPos.y * 0.28 + uv.y * 1.4));
float grain = noise21(localUv * 11.0 + cellHash * 9.0);
float cracks = smoothstep(0.63, 0.82,
                          fbm21(uv * 8.0 + vec2(cellHash * 17.0, -cellHash * 13.0)));
vec3 albedo = base * mix(0.72, 1.15, strata);
albedo *= mix(0.96, 0.82, cracks * 0.55);
albedo += vec3((grain - 0.5) * 0.06);
return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.92, 0.05, 0.0);
