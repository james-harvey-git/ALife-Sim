#version 410

flat in int vInstanceID;
in vec2 vFragPos;

uniform samplerBuffer uCreatureData;
uniform int uLodTier;    // 0 = full, 1 = simple
uniform int uDataOffset; // TBO creature index offset for this tier

out vec4 fragColor;

// ── Data fetch helpers ──
// Each creature is 64 floats = 16 vec4 texels in the TBO.

vec4 fetchTexel(int creatureIdx, int texelIdx) {
    return texelFetch(uCreatureData, creatureIdx * 16 + texelIdx);
}

vec2 fetchSegPos(int creatureIdx, int segIdx) {
    // Offsets 0-11: 6 segment positions as vec2
    // Texels 0-2: positions (each texel holds 2 segments as xy,zw)
    int texel = segIdx / 2;
    vec4 data = fetchTexel(creatureIdx, texel);
    return (segIdx % 2 == 0) ? data.xy : data.zw;
}

float fetchSegRadius(int creatureIdx, int segIdx) {
    // Offsets 12-17: 6 radii
    // Texel 3: radii[0..3] as xyzw
    // Texel 4: radii[4..5] as xy (zw = headPos)
    if (segIdx < 4) {
        vec4 data = fetchTexel(creatureIdx, 3);
        if (segIdx == 0) return data.x;
        if (segIdx == 1) return data.y;
        if (segIdx == 2) return data.z;
        return data.w;
    } else {
        vec4 data = fetchTexel(creatureIdx, 4);
        return (segIdx == 4) ? data.x : data.y;
    }
}

vec2 fetchHeadPos(int creatureIdx) {
    vec4 data = fetchTexel(creatureIdx, 4);
    return data.zw;  // offsets 18-19
}

vec2 fetchForwardAxis(int creatureIdx) {
    vec4 data = fetchTexel(creatureIdx, 5);
    return data.xy;  // offsets 20-21
}

float fetchFloat(int creatureIdx, int floatIdx) {
    int texel = floatIdx / 4;
    int component = floatIdx % 4;
    vec4 data = fetchTexel(creatureIdx, texel);
    if (component == 0) return data.x;
    if (component == 1) return data.y;
    if (component == 2) return data.z;
    return data.w;
}

// ── New data fetch helpers for visual quality floats ──
float fetchBlendStiffness(int ci) { return fetchFloat(ci, 52); }
float fetchTailWaveSpeed(int ci)  { return fetchFloat(ci, 53); }
float fetchTailWaveLength(int ci) { return fetchFloat(ci, 54); }
float fetchTailTaper(int ci)      { return fetchFloat(ci, 55); }
float fetchFinShape(int ci)       { return fetchFloat(ci, 56); }
float fetchTaperCurve(int ci)     { return fetchFloat(ci, 57); }

// ── SDF primitives ──

float sdCircle(vec2 p, float r) {
    return length(p) - r;
}

float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float sdCapsule(vec2 p, vec2 a, vec2 b, float r) {
    vec2 pa = p - a, ba = b - a;
    float baba = dot(ba, ba);
    if (baba < 1e-6) return length(pa) - r;
    float h = clamp(dot(pa, ba) / baba, 0.0, 1.0);
    return length(pa - ba * h) - r;
}

float sdTaperedCapsule(vec2 p, vec2 a, vec2 b, float rA, float rB) {
    vec2 pa = p - a, ba = b - a;
    float baba = dot(ba, ba);
    if (baba < 1e-6) return length(pa) - rA;
    float h = clamp(dot(pa, ba) / baba, 0.0, 1.0);
    float r = mix(rA, rB, h);
    return length(pa - ba * h) - r;
}

// ── HSL to RGB ──

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

    // ── Stage 1: Body core — 6-segment smooth blend ──

    float dBody = 1e9;

    int segCount = (uLodTier == 0) ? 6 : 3;
    int segIndices[6] = int[6](0, 1, 2, 3, 4, 5);
    // Simple LOD uses segments 0, 3, 5 (head, mid, tail)
    if (uLodTier == 1) {
        segIndices[0] = 0;
        segIndices[1] = 3;
        segIndices[2] = 5;
    }

    // Compute average radius for reference scale
    float avgRadius = 0.0;
    for (int i = 0; i < segCount; i++) {
        avgRadius += fetchSegRadius(ci, segIndices[i]);
    }
    avgRadius /= float(segCount);

    // Per-creature blend parameters
    float taperCurve = fetchTaperCurve(ci);
    float blendStiffness = fetchBlendStiffness(ci);

    for (int i = 0; i < segCount; i++) {
        int si = segIndices[i];
        vec2 segPos = fetchSegPos(ci, si);
        float segR = fetchSegRadius(ci, si);

        // Variable blend kernel: generous at head, tight at tail
        float segT = float(i) / max(float(segCount - 1), 1.0);
        float k = avgRadius
                * mix(0.55, 0.18, segT * segT * taperCurve)
                * mix(1.2, 0.5, blendStiffness);

        float d = sdCircle(vFragPos - segPos, segR);
        dBody = smin(dBody, d, k);
    }

    // Asymmetric head: slight forward-axis elongation driven by diet
    if (uLodTier == 0) {
        float diet = fetchFloat(ci, 36);
        vec2 headPos = fetchSegPos(ci, 0);
        float headR = fetchSegRadius(ci, 0);
        vec2 fwd = fetchForwardAxis(ci);

        // Elongation factor: 1.0 (herbivore) to 1.15 (carnivore)
        float headElong = 1.0 + diet * 0.15;
        // Compress distance along forward axis to create ellipse
        vec2 toFrag = vFragPos - headPos;
        float fwdDist = dot(toFrag, fwd);
        vec2 squeezed = toFrag - fwd * fwdDist * (1.0 - 1.0 / headElong);
        float dHeadEllipse = length(squeezed) - headR;

        // Blend elongated head into body with generous kernel
        float headK = avgRadius * 0.5 * mix(1.2, 0.5, blendStiffness);
        dBody = smin(dBody, dHeadEllipse, headK);
    }

    // ── Prepare feature SDFs (populated in full LOD only) ──
    float dOrganism = dBody;
    float dMouth = 1e9;
    float dEyes = 1e9;
    float dPupils = 1e9;
    float dHighlights = 1e9;
    float eyeR = 0.0;

    if (uLodTier == 0) {
        // ── Stage 2: Appendages ──
        float gaitPhase = fetchFloat(ci, 23);
        float thrustDrive = fetchFloat(ci, 24);
        float tailLength = fetchFloat(ci, 40);
        float tailFlex = fetchFloat(ci, 41);
        float tailFork = fetchFloat(ci, 42);
        float finArea = fetchFloat(ci, 38);
        float finPlacement = fetchFloat(ci, 39);
        float spikes = fetchFloat(ci, 47);
        float sensorExpr = fetchFloat(ci, 46);

        // ── Tail filaments — 3-segment Bezier with spatial wave ──
        int tailCount = 1 + int(tailFork * 2.0 + tailFlex * 0.5);
        tailCount = clamp(tailCount, 1, 4);

        vec2 tailSeg = fetchSegPos(ci, 5);
        float tailR = fetchSegRadius(ci, 5);
        vec2 tailDir = tailSeg - fetchSegPos(ci, 4);
        float tailDirLen = length(tailDir);
        vec2 tailAxis = (tailDirLen > 0.001) ? tailDir / tailDirLen : vec2(1.0, 0.0);
        vec2 tailPerp = vec2(-tailAxis.y, tailAxis.x);

        float waveSpeed = fetchTailWaveSpeed(ci);
        float waveLen = fetchTailWaveLength(ci);
        float tailTaperGene = fetchTailTaper(ci);

        // Map genome values to usable ranges
        float waveSpeedMul = 0.6 + waveSpeed * 1.9;   // [0.6, 2.5]
        float waveLenMul = 1.2 + waveLen * 3.0;        // [1.2, 4.2] phase offset per segment
        float amplitude = (0.14 + tailFlex * 0.42) * (0.28 + thrustDrive * 0.72);

        float dTails = 1e9;
        for (int t = 0; t < 4; t++) {
            if (t >= tailCount) break;
            float spread = (float(t) - float(tailCount - 1) * 0.5) * 0.35;
            vec2 root = tailSeg + tailAxis * tailR * 0.6
                      + tailPerp * spread * tailR;

            float fLen = tailLength * avgRadius * 2.5;

            // Spatial wave — phase progresses along length
            float basePhase = gaitPhase * waveSpeedMul * 1.18 + float(t) * 1.3;
            float w1 = sin(basePhase) * amplitude;
            float w2 = sin(basePhase + waveLenMul * 0.33) * amplitude * 0.8;
            float w3 = sin(basePhase + waveLenMul * 0.66) * amplitude * 0.5;

            // 4 control points for S-curve
            vec2 P0 = root;
            vec2 P1 = root + tailAxis * fLen * 0.33 + tailPerp * w1 * fLen * 0.35;
            vec2 P2 = root + tailAxis * fLen * 0.66 + tailPerp * w2 * fLen * 0.25;
            vec2 P3 = root + tailAxis * fLen         + tailPerp * w3 * fLen * 0.10;

            // Tapered widths — root to tip narrowing
            float rootW = tailR * 0.28;
            float tipW = tailR * mix(0.10, 0.03, tailTaperGene);

            // 3 tapered capsule segments blended smoothly
            float w01 = mix(rootW, rootW * 0.6, 0.5);
            float w12 = mix(rootW * 0.6, tipW * 2.0, 0.5);
            float s1 = sdTaperedCapsule(vFragPos, P0, P1, rootW, w01);
            float s2 = sdTaperedCapsule(vFragPos, P1, P2, w01, w12);
            float s3 = sdTaperedCapsule(vFragPos, P2, P3, w12, tipW);
            float dFil = smin(s1, s2, rootW * 0.6);
            dFil = smin(dFil, s3, rootW * 0.4);
            dTails = min(dTails, dFil);
        }
        dOrganism = smin(dOrganism, dTails, tailR * 0.4);

        // ── Dorsal fin — curved paddles with shape gene ──
        if (finArea > 0.1) {
            int finSeg = 1 + int(finPlacement * 3.0);
            finSeg = clamp(finSeg, 1, 4);
            vec2 finPos = fetchSegPos(ci, finSeg);
            float finR = fetchSegRadius(ci, finSeg);
            vec2 finDir = fetchSegPos(ci, finSeg - 1) - finPos;
            float finDirLen = length(finDir);
            vec2 finAxis = (finDirLen > 0.001) ? finDir / finDirLen : vec2(1.0, 0.0);
            vec2 finPerp = vec2(-finAxis.y, finAxis.x);

            float finShapeGene = fetchFinShape(ci);
            float flap = sin(gaitPhase * 1.36) * (0.12 + finArea * 0.48)
                       * (0.25 + thrustDrive * 0.75);

            for (int side = 0; side < 2; side++) {
                float sSign = (side == 0) ? -1.0 : 1.0;
                vec2 finBase = finPos + finPerp * sSign * finR * 0.7;

                // Fin length: larger than before
                float fLength = finR * (0.8 + finArea * 1.4);
                // Midpoint bows backward under thrust for paddle shape
                vec2 finMid = finBase
                    + finPerp * sSign * fLength * 0.55
                    + finAxis * (flap * finR * sSign - fLength * 0.12);
                vec2 finTip = finBase
                    + finPerp * sSign * fLength
                    + finAxis * flap * finR * sSign * 0.6;

                // Width varies by finShape gene: blade (narrow) to fan (broad)
                float baseW = finR * 0.15 * finArea;
                float midW = baseW * (0.6 + finShapeGene * 1.4);  // paddle bulge at mid
                float tipW = baseW * 0.25;

                float d1 = sdTaperedCapsule(vFragPos, finBase, finMid, baseW, midW);
                float d2 = sdTaperedCapsule(vFragPos, finMid, finTip, midW, tipW);
                float dFin = smin(d1, d2, midW * 0.8);
                // Generous blend where fin meets body
                dOrganism = smin(dOrganism, dFin, baseW * 2.4);
            }
        }

        // ── Whiskers ──
        int whiskerCount = (sensorExpr > 0.4) ? ((sensorExpr > 0.7) ? 2 : 1) : 0;
        if (whiskerCount > 0) {
            vec2 headPos2 = fetchSegPos(ci, 0);
            float headR = fetchSegRadius(ci, 0);
            vec2 headAxis = fetchForwardAxis(ci);
            vec2 headPerp = vec2(-headAxis.y, headAxis.x);

            for (int w = 0; w < 2; w++) {
                if (w >= whiskerCount) break;
                float wSide = (w == 0) ? -1.0 : 1.0;
                vec2 wBase = headPos2 + headPerp * wSide * headR * 0.6
                           - headAxis * headR * 0.3;
                float wiggle = sin(gaitPhase * 2.6 + float(w) * 1.4) * 0.15;
                vec2 wTip = wBase + headPerp * wSide * headR * 1.2
                          + headAxis * (wiggle * headR);
                float ww = headR * 0.06;
                dOrganism = smin(dOrganism, sdTaperedCapsule(vFragPos, wBase, wTip, ww, ww * 0.2), ww * 1.5);
            }
        }

        // ── Spikes — smooth dorsal ridges ──
        if (spikes > 0.28) {
            vec2 headFwd = fetchForwardAxis(ci);
            // Determine dorsal side consistently (perpendicular to forward, pick one side)
            vec2 dorsalDir = vec2(-headFwd.y, headFwd.x);

            for (int s = 1; s <= 4; s++) {
                vec2 sPos = fetchSegPos(ci, s);
                float sR = fetchSegRadius(ci, s);
                vec2 sDir = fetchSegPos(ci, s - 1) - sPos;
                float sDirLen = length(sDir);
                vec2 sAxis = (sDirLen > 0.001) ? sDir / sDirLen : vec2(1.0, 0.0);

                // Ridge protrudes from dorsal side, angled slightly backward
                float ridgeLen = sR * (0.05 + spikes * 0.15);
                float ridgeW = sR * 0.14;

                vec2 ridgeBase = sPos + dorsalDir * sR * 0.8;
                vec2 ridgeTip = ridgeBase + dorsalDir * ridgeLen
                              - sAxis * ridgeLen * 0.35;  // backward curve

                // Organic blend into body
                dOrganism = smin(dOrganism,
                    sdTaperedCapsule(vFragPos, ridgeBase, ridgeTip, ridgeW, ridgeW * 0.4),
                    ridgeW * 2.0);
            }
        }

        // ── Stage 3: Mouth carving ──
        float diet = fetchFloat(ci, 36);
        float jawLength = fetchFloat(ci, 43);
        float jawArc = fetchFloat(ci, 44);
        float biteDrive = fetchFloat(ci, 25);

        vec2 headPos3 = fetchSegPos(ci, 0);
        float headR3 = fetchSegRadius(ci, 0);
        vec2 fwdAxis = fetchForwardAxis(ci);

        vec2 mouthCentre = headPos3 + fwdAxis * headR3 * 0.88;

        float baseW = headR3 * mix(0.32, 0.22, diet) * (0.5 + jawArc * 0.7);
        float baseH = headR3 * mix(0.10, 0.06, diet) * (0.3 + jawLength * 0.8);

        float gape = baseH * (1.0 + biteDrive * mix(2.0, 5.0, diet));

        vec2 mp = vFragPos - mouthCentre;
        vec2 mLocal = vec2(
            mp.x * fwdAxis.y - mp.y * fwdAxis.x,
            mp.x * fwdAxis.x + mp.y * fwdAxis.y
        );

        // Herbivore: ellipse mouth
        float dMouthHerb = (length(mLocal / vec2(baseW, gape)) - 1.0) * min(baseW, gape);

        // Carnivore: lens shape (two intersecting circles)
        float lensR = baseW * mix(1.8, 0.9, diet);
        float lensOff = sqrt(max(0.0, lensR * lensR - baseW * baseW));
        float dLensTop = length(mLocal - vec2(0.0, -lensOff)) - lensR;
        float dLensBot = length(mLocal - vec2(0.0, lensOff)) - lensR;
        float dMouthCarn = max(dLensTop, dLensBot) - gape;

        dMouth = mix(dMouthHerb, dMouthCarn, diet);

        float carveDepth = smoothstep(0.0, -headR3 * 0.15, sdCircle(vFragPos - headPos3, headR3));
        float dMouthCarve = mix(0.05, dMouth, carveDepth);

        dOrganism = max(dOrganism, -dMouthCarve);

        // ── Stage 4: Eyes ──
        float sensorRange = fetchFloat(ci, 45);

        int eyeCount = (sensorRange > 0.6) ? 3 : ((sensorRange > 0.3) ? 2 : 1);
        eyeR = headR3 * 0.12;

        vec2 headPerp2 = vec2(-fwdAxis.y, fwdAxis.x);

        for (int e = 0; e < 3; e++) {
            if (e >= eyeCount) break;
            float ea;
            if (eyeCount == 1) ea = 0.0;
            else if (eyeCount == 2) ea = (e == 0) ? -0.32 : 0.32;
            else ea = (e == 0) ? -0.52 : ((e == 1) ? 0.0 : 0.52);

            vec2 eyePos = headPos3
                        + fwdAxis * headR3 * 0.55
                        + headPerp2 * ea * headR3;

            dEyes = min(dEyes, sdCircle(vFragPos - eyePos, eyeR));
            dPupils = min(dPupils, sdCircle(vFragPos - eyePos, eyeR * 0.42));
            dHighlights = min(dHighlights, sdCircle(
                vFragPos - eyePos - fwdAxis * eyeR * 0.2 + headPerp2 * eyeR * 0.25,
                eyeR * 0.18));
        }
    }

    // ── Stage 5: Colour composition ──
    float hue = fetchFloat(ci, 32);
    float sat = fetchFloat(ci, 33);
    float lit = fetchFloat(ci, 34);
    float energy = fetchFloat(ci, 35);

    vec3 baseCol = hsl2rgb(hue, sat, lit);
    vec3 rimCol = hsl2rgb(hue - 0.03, sat * 1.1, lit - 0.18);
    vec3 innerCol = hsl2rgb(hue + 0.04, sat * 0.7, lit + 0.16);
    vec3 energyCol = hsl2rgb(hue + 0.1, 1.0, 0.72);

    float bodyAlpha = smoothstep(1.0, -1.0, dOrganism);
    float rim = smoothstep(0.0, -avgRadius * 0.15, dOrganism)
              * (1.0 - smoothstep(-avgRadius * 0.15, -avgRadius * 0.45, dOrganism));
    float inner = smoothstep(-avgRadius * 0.05, -avgRadius * 0.5, dOrganism);
    float outline = smoothstep(avgRadius * 0.06, avgRadius * 0.01, abs(dOrganism));

    vec3 col = mix(baseCol, innerCol, inner * 0.55);
    col = mix(col, rimCol, rim * 0.5);
    col += energyCol * energy * 0.12 * inner;

    // Per-segment damage visualization
    if (uLodTier == 0) {
        for (int s = 0; s < 6; s++) {
            float integrity = fetchFloat(ci, 26 + s);
            if (integrity < 0.95) {
                vec2 segP = fetchSegPos(ci, s);
                float segR = fetchSegRadius(ci, s);
                float segDist = sdCircle(vFragPos - segP, segR);
                float segInfluence = smoothstep(0.0, -segR * 0.8, segDist);

                float dmg = 1.0 - integrity;
                vec3 damageCol = hsl2rgb(hue - 0.05, sat * 0.3, lit * 0.4);
                col = mix(col, damageCol, segInfluence * dmg * 0.6);
            }
        }
    }

    // Body markings
    if (uLodTier == 0) {
        float pattern = fetchFloat(ci, 48);
        if (pattern < 0.5) {
            int markCount = (pattern < 0.25) ? 3 : 2;
            for (int m = 0; m < 3; m++) {
                if (m >= markCount) break;
                int markSeg = 1 + m;
                vec2 mPos = fetchSegPos(ci, markSeg);
                float mR = fetchSegRadius(ci, markSeg);
                vec2 mDir = fetchSegPos(ci, markSeg - 1) - mPos;
                float mDirLen = length(mDir);
                vec2 mAxis = (mDirLen > 0.001) ? mDir / mDirLen : vec2(1.0, 0.0);
                vec2 mPerp = vec2(-mAxis.y, mAxis.x);

                vec2 markCenter = mPos + mPerp * mR * 0.4;
                float markW = mR * 0.5;
                float markH = mR * 0.25;
                float markDist = sdCapsule(vFragPos, markCenter - mAxis * markH, markCenter + mAxis * markH, markW * 0.3);
                float markMask = smoothstep(1.0, -1.0, markDist) * smoothstep(1.0, -1.0, dBody);
                vec3 markCol = hsl2rgb(hue + 0.12, sat * 0.8, lit + 0.1);
                col = mix(col, markCol, markMask * 0.35);
            }
        }
    }

    // Subsurface scatter near head
    if (uLodTier == 0) {
        vec2 hp = fetchSegPos(ci, 0);
        float headD = sdCircle(vFragPos - hp, fetchSegRadius(ci, 0));
        float sss = smoothstep(0.0, -avgRadius * 0.6, headD)
                  * smoothstep(-avgRadius * 0.6, 0.0, headD + avgRadius * 0.3);
        col += hsl2rgb(hue + 0.08, 0.5, 0.9) * sss * 0.18 * (0.5 + energy * 0.5);
    }

    // Outline
    col = mix(col, rimCol * 0.6, outline * 0.85);

    // Mouth interior (full LOD only)
    if (uLodTier == 0) {
        float diet2 = fetchFloat(ci, 36);
        float mouthMask = smoothstep(0.5, -0.5, dMouth) * smoothstep(1.0, -1.0, dBody);
        vec3 mouthCol = mix(
            hsl2rgb(hue + 0.94, 0.5, 0.38),
            hsl2rgb(hue - 0.06, 0.4, 0.12),
            diet2
        );
        col = mix(col, mouthCol, mouthMask * 0.92);

        float lipEdge = smoothstep(1.0, 0.2, abs(dMouth)) * smoothstep(1.0, -1.0, dBody);
        col = mix(col, rimCol * 0.75, lipEdge * 0.5);
    }

    // Eyes (full LOD only)
    if (uLodTier == 0) {
        float aggro2 = fetchFloat(ci, 37);
        float eyeAlpha = smoothstep(0.5, -0.5, dEyes);
        float eyeInner2 = smoothstep(0.0, -eyeR, dEyes);
        float pupilA = smoothstep(0.5, -0.5, dPupils);
        float hlA = smoothstep(0.3, -0.3, dHighlights);

        // Sclera
        col = mix(col, vec3(0.95, 0.96, 0.97), eyeAlpha * 0.97);
        // Iris
        vec3 irisCol = hsl2rgb(hue * (1.0 - aggro2 * 0.5) + aggro2 * 0.04, 0.7, 0.45);
        col = mix(col, irisCol, eyeAlpha * eyeInner2 * 0.85);
        // Pupil
        col = mix(col, vec3(0.04, 0.05, 0.07), pupilA * 0.95);
        // Catchlight
        col = mix(col, vec3(1.0), hlA * 0.92);
        // Eye outline
        float eyeOutline = smoothstep(0.5, 0.1, abs(dEyes));
        col = mix(col, rimCol * 0.5, eyeOutline * 0.7);
    }

    if (bodyAlpha < 0.001) discard;
    fragColor = vec4(col, bodyAlpha * 0.97);
}
