float veins = fbm21(vec2(uv.x * 5.0, uv.y * 1.0 - uTime * 0.45));
float ridge = abs(sin(uv.x * 18.0 + veins * 6.0 + uTime * 2.1));
float pulseField = fbm21(uv * 0.35 + vec2(3.7, 8.1));
float pulse = 0.5 + 0.5 * sin(uTime * 5.8 + pulseField * 6.2831853);
float pores = noise21(localUv * 18.0 + cellHash * 13.0);
vec3 albedo = mix(base * 0.72, base * 1.18, smoothstep(0.36, 0.92, ridge));
albedo *= 0.95 + pores * 0.08;
albedo += vec3(0.06, 0.10, 0.05) * pulse * 0.18;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.66, 0.12,
                  0.10 + ridge * 0.10 * pulse);
