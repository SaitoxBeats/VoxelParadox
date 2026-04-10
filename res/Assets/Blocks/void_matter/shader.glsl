float voidNoise = fbm21(uv * 6.0 + vec2(uTime * 0.20, -uTime * 0.20) + cellHash * 5.0);
float wisps = 0.5 + 0.5 * sin((uv.x - uv.y) * 14.0 - uTime * 3.0 + cellHash * 8.0);
float rim = pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 2.4);
vec3 albedo = mix(base * 0.22, base * 0.85 + vec3(0.08, 0.0, 0.12), voidNoise);
albedo += vec3(0.06, 0.01, 0.08) * wisps * 0.25;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.2)), 0.52, 0.18,
                  0.12 + rim * 0.18 + wisps * 0.08);
