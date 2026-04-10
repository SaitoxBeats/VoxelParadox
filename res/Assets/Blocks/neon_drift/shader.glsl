float time = uTime;
float clock = time / 5.0;
float ttime = floor(time) + pow(fract(time), sin(uTime * 1.333) * 0.5);

vec2 driftUv = centeredUv * 2.0;
vec2 sourceUv = driftUv;
driftUv.x += tan(abs(driftUv.x) * 5.0) * 0.08;

float stripe = fract(10.0 * driftUv.x + clock);
stripe = smoothstep(0.20, 0.10, stripe);

float pulse = tan(driftUv.x + time);
float flashGate = step(0.10, abs(cos(clock * 2.0)) * abs(driftUv.x));
float accentGate = step(1.0, sin(clock) * sin(clock) * abs(driftUv.x) * 4.0);

vec3 flashColor = vec3(
    pulse * 0.10 + flashGate,
    pulse * 0.90,
    0.10 + accentGate
);

vec3 albedo = mix(vec3(0.08, 0.08, 0.10), flashColor, stripe);
albedo = mix(base * 0.45, albedo, 0.88);

float cellPulse = sin(ttime + 1.0 - floor(length(floor(sourceUv * 10.0))));
float emissive = max(0.12, stripe * max(cellPulse, 0.0) * 1.45);

return makeSample(clamp(albedo, vec3(0.0), vec3(1.8)), 0.18, 0.42, emissive);
