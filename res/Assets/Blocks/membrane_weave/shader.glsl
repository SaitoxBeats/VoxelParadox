float weaveA = smoothstep(0.5, 0.58, abs(sin(localUv.x * 16.0 + 1.5)));
float weaveB = smoothstep(0.5, 0.58, abs(sin(localUv.y * 16.0 + 1.5)));
float knots = fbm21(localUv * 16.0 + cellHash * 5.0);
float weave = max(weaveA, weaveB);
vec3 albedo = mix(base * 0.72, base * 1.10, weave);
albedo *= 0.90 + knots * 0.12;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.52, 0.22, 0.08 * weave);
