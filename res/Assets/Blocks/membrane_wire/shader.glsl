float strand = smoothstep(0.08, 0.28, 1.0 - abs(centeredUv.x)) *
               smoothstep(0.08, 0.28, 1.0 - abs(centeredUv.y));
float pulse = 0.5 + 0.5 * sin(uTime * 7.0 + uv.y * 9.0 + cellHash * 5.0);
vec3 albedo = mix(base * 0.75, base * 1.20, strand);
albedo += vec3(0.08, 0.12, 0.08) * pulse * 0.18;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.5)), 0.18, 0.32,
                  0.24 + strand * 0.22 + pulse * 0.10);
