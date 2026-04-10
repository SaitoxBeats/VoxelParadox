float bands = 0.5 + 0.5 * sin((uv.x + uv.y) * 18.0 + uTime * 2.4);
float sparkle = noise21(localUv * 14.0 + cellHash * 9.0);
float fresnel = pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 4.0);
vec3 albedo = mix(base * 0.78, vec3(1.0), bands * 0.32 + fresnel * 0.22);
albedo *= 0.96 + sparkle * 0.08;
albedo += vec3(0.08, 0.14, 0.18) * fresnel;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.6)), 0.14, 0.65,
                  0.42 + bands * 0.28 + fresnel * 0.30 + sparkle * 0.04);
