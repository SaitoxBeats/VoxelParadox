float billow = fbm21(uv * 2.2 + vec2(cellHash * 7.0, uTime * 0.015));
float wisp = fbm21(uv * 7.5 + vec2(-uTime * 0.025, cellHash * 11.0));
float rim = pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 2.0);
vec3 albedo = mix(base * 0.84, vec3(1.0), billow * 0.38 + wisp * 0.12);
albedo += vec3(0.10) * rim;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.3)), 0.94, 0.02, 0.0);
