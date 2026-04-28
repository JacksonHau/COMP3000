#version 330 core

in vec2 vUV;
in vec3 vWorldPos;

out vec4 FragColor;

uniform sampler2D uTex;

uniform int uTorchCount;
uniform vec3 uTorchPos[8];
uniform vec3 uTorchColor[8];
uniform float uTorchRadius[8];
uniform float uAmbient;

void main() {
    vec4 tex = texture(uTex, vUV);

    vec3 lightAccum = vec3(uAmbient);

    for (int i = 0; i < uTorchCount; i++) {
        float distToTorch = distance(vWorldPos, uTorchPos[i]);

        float intensity = clamp(1.0 - (distToTorch / uTorchRadius[i]), 0.0, 1.0);
        intensity = intensity * intensity;

        lightAccum += uTorchColor[i] * intensity;
    }

    lightAccum = clamp(lightAccum, vec3(0.0), vec3(1.25));

    FragColor = vec4(tex.rgb * lightAccum, tex.a);
}