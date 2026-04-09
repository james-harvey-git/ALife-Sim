#version 410

layout(location = 0) in vec2 aQuadPos;
layout(location = 1) in vec4 aInstanceAABB;  // minX, minY, maxX, maxY

uniform vec2 uViewport;

flat out int vInstanceID;
out vec2 vFragPos;

void main() {
    // Expand unit quad [0,1] to world-space AABB
    vec2 worldPos = mix(aInstanceAABB.xy, aInstanceAABB.zw, aQuadPos);

    // Convert world-space position to NDC
    vec2 ndc = vec2(
        (worldPos.x / uViewport.x) * 2.0 - 1.0,
        1.0 - (worldPos.y / uViewport.y) * 2.0
    );

    gl_Position = vec4(ndc, 0.0, 1.0);
    vInstanceID = gl_InstanceID;
    vFragPos = worldPos;
}
