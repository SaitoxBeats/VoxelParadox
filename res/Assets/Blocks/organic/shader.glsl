float fiber = fbm21(vec2(uv.x * 9.0 + cellHash * 4.0, uv.y * 3.0 - cellHash * 3.0));
float pores = noise21(localUv * 18.0 + cellHash * 13.0);
vec3 albedo = mix(base * 0.78, base * 1.06, fiber);
albedo *= 0.92 + pores * 0.10;
albedo += vec3(0.05, 0.02, 0.01) * smoothstep(0.72, 1.0, pores);
return makeSample(clamp(albedo, vec3(0.0), vec3(1.25)), 0.78, 0.08, 0.0);
