#version 410

flat in int vInstanceID;
in vec2 vFragPos;

uniform samplerBuffer uCreatureData;
uniform int uLodTier;

out vec4 fragColor;

void main() {
    // Placeholder: output red semi-transparent
    fragColor = vec4(1.0, 0.0, 0.0, 0.5);
}
