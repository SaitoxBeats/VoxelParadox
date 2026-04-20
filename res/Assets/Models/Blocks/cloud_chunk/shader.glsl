float quality = float(max(uCloudQuality, 0));
float billow = fbm21(uv * 1.65 + vec2(cellHash * 6.0, uTime * 0.010));
float wisp = uCloudQuality <= 0
    ? 0.5
    : fbm21(uv * (5.0 + quality * 1.4) + vec2(-uTime * 0.018, cellHash * 9.0));
float verticalLift = smoothstep(-0.45, 0.85, worldNormal.y);
float rim = pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 2.5);
vec3 warmTop = vec3(1.05, 1.06, 1.02);
vec3 coolBase = vec3(0.72, 0.76, 0.82);
vec3 albedo = mix(coolBase, warmTop, verticalLift);
albedo = mix(albedo * 0.86, vec3(1.0), billow * 0.30 + wisp * 0.12);
albedo += vec3(0.11, 0.12, 0.13) * rim * (0.55 + quality * 0.16);
return makeSample(clamp(albedo, vec3(0.0), vec3(1.35)), 0.97, 0.015, 0.0);
