float brushed = 0.5 + 0.5 * sin(uv.y * 96.0);
float scratches = fbm21(vec2(uv.x * 18.0, uv.y * 72.0));
float microScratch = noise21(localUv * 18.0 + cellHash * 7.0);
float edge = pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 3.5);
vec3 albedo = mix(base * 0.72, base * 1.12,
                  brushed * 0.22 + scratches * 0.14 + microScratch * 0.04);
albedo += vec3(edge) * 0.08;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.6)), 0.18, 0.85, 0.0);
