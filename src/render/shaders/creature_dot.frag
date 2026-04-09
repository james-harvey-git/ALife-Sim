#version 410

flat in int vInstanceID;
in vec2 vFragPos;

uniform samplerBuffer uCreatureData;
uniform int uDataOffset;

out vec4 fragColor;

vec4 fetchTexel(int creatureIdx, int texelIdx) {
    return texelFetch(uCreatureData, creatureIdx * 14 + texelIdx);
}

vec3 hsl2rgb(float h, float s, float l) {
    h = fract(h) * 6.0;
    float c = (1.0 - abs(2.0 * l - 1.0)) * s;
    float x = c * (1.0 - abs(mod(h, 2.0) - 1.0));
    vec3 rgb;
    if      (h < 1.0) rgb = vec3(c, x, 0);
    else if (h < 2.0) rgb = vec3(x, c, 0);
    else if (h < 3.0) rgb = vec3(0, c, x);
    else if (h < 4.0) rgb = vec3(0, x, c);
    else if (h < 5.0) rgb = vec3(x, 0, c);
    else              rgb = vec3(c, 0, x);
    return rgb + (l - c * 0.5);
}

void main() {
    int ci = uDataOffset + vInstanceID;

    // Read head position (offsets 0-1) and head radius (offset 12)
    vec4 t0 = fetchTexel(ci, 0);
    vec2 headPos = t0.xy;
    vec4 t3 = fetchTexel(ci, 3);
    float radius = t3.x;  // head segment radius

    // Read colour
    vec4 t8 = fetchTexel(ci, 8);  // offsets 32-35: hue, sat, lit, energy
    float hue = t8.x;
    float energy = t8.w;

    // Soft circle
    float d = length(vFragPos - headPos);
    float alpha = smoothstep(radius, radius - 1.5, d);

    vec3 col = hsl2rgb(hue, 0.6, 0.3 + energy * 0.3);

    if (alpha < 0.001) discard;
    fragColor = vec4(col, alpha * 0.85);
}
