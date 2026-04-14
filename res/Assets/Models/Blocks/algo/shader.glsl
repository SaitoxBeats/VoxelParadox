vec4 texel = sampleBlockTexture(materialId, localUv);
return makeSample(texel.rgb, 0.9, 0.1, 0.0);
