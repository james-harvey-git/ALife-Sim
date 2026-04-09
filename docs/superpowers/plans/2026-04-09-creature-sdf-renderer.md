# Creature SDF Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the geometry-based creature rendering with a per-creature SDF (Signed Distance Field) renderer that produces smooth organic visuals, faithful to the physics body chain, scaling to 5,000+ creatures via instanced rendering with 3-tier LOD.

**Architecture:** A new `CreatureSDF` module (`src/render/CreatureSDF.hpp/.cpp`) with standalone GLSL shaders owns the entire creature draw pipeline. Each frame it packs creature data into a float TBO, classifies creatures into 3 LOD tiers, and issues 3 instanced draw calls. The fragment shader evaluates SDF distance functions for the 6-segment body chain, appendages, mouth, and eyes, then composites layered shading. `Renderer.cpp` delegates creature rendering to this module and retains environment, HUD, and debug overlays.

**Tech Stack:** C++20, OpenGL 4.1 Core, GLSL 410, SDL2, Apple M5 Pro GPU

**Spec:** `docs/superpowers/specs/2026-04-09-creature-sdf-renderer-design.md`

---

## File Structure

| File | Role |
|------|------|
| `src/render/CreatureSDF.hpp` | Public interface: `initialize()`, `shutdown()`, `render()` |
| `src/render/CreatureSDF.cpp` | GL setup, data packing, LOD classification, instanced draw calls, shader file loading |
| `src/render/shaders/creature_sdf.vert` | Vertex shader: quad expansion from per-instance AABB, TBO data fetch setup |
| `src/render/shaders/creature_sdf.frag` | Fragment shader: full SDF pipeline (body, appendages, mouth, eyes, shading) |
| `src/render/shaders/creature_dot.vert` | Dot-tier vertex shader: point-sized quad from AABB |
| `src/render/shaders/creature_dot.frag` | Dot-tier fragment shader: soft coloured circle |
| `src/render/Renderer.hpp` | Modified: add `CreatureSDF` member |
| `src/render/Renderer.cpp` | Modified: bump GL to 4.1, bump shader versions, wire in `CreatureSDF`, remove old creature drawing code |
| `CMakeLists.txt` | Modified: add `CreatureSDF.cpp`, define shader directory path |

---

## Task 1: Scaffold CreatureSDF module and GL 4.1 bump

**Files:**
- Create: `src/render/CreatureSDF.hpp`
- Create: `src/render/CreatureSDF.cpp`
- Modify: `src/render/Renderer.hpp`
- Modify: `src/render/Renderer.cpp:798-800` (GL version), `Renderer.cpp:303-360` (shader versions)
- Modify: `CMakeLists.txt:23-27` (add source), add shader path define

- [ ] **Step 1: Create CreatureSDF.hpp with the public interface**

```cpp
// src/render/CreatureSDF.hpp
#pragma once

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <SDL2/SDL_opengl.h>
#endif

#include <cstdint>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "sim/Simulation.hpp"

namespace alife {

class CreatureSDF {
public:
    CreatureSDF() = default;
    ~CreatureSDF();

    bool initialize(const std::string& shaderDir);
    void shutdown();

    void render(
        const Simulation& sim,
        const SDL_FRect& worldViewport,
        float cameraZoom,
        const Vec2& cameraCenter,
        int drawableWidth,
        int drawableHeight
    );

private:
    static constexpr int kDataStride = 56;  // floats per creature
    static constexpr float kFullSdfThreshold = 30.0f;   // screen px diameter
    static constexpr float kSimpleSdfThreshold = 8.0f;
    static constexpr float kHysteresis = 2.0f;

    GLuint sdfProgram_ = 0;
    GLuint dotProgram_ = 0;

    // Quad geometry (shared by all instances)
    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;

    // Per-instance data: screen-space AABB (4 floats) + creature index
    GLuint instanceVbo_ = 0;

    // Creature data TBO
    GLuint dataBuffer_ = 0;
    GLuint dataTexture_ = 0;

    int textureCapacity_ = 0;

    // CPU-side packing buffers
    std::vector<float> packedData_;

    struct InstanceData {
        float minX, minY, maxX, maxY;
    };
    std::vector<InstanceData> fullInstances_;
    std::vector<InstanceData> simpleInstances_;
    std::vector<InstanceData> dotInstances_;

    bool loadShaderProgram(const std::string& vertPath, const std::string& fragPath, GLuint& programOut);
    std::string readFile(const std::string& path);
    void ensureTextureCapacity(int creatureCount);
    void packAndClassify(
        const Simulation& sim,
        const SDL_FRect& worldViewport,
        float cameraZoom,
        const Vec2& cameraCenter,
        int drawableWidth,
        int drawableHeight
    );
};

}  // namespace alife
```

- [ ] **Step 2: Create minimal CreatureSDF.cpp with init/shutdown stubs**

```cpp
// src/render/CreatureSDF.cpp
#include "render/CreatureSDF.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace alife {

CreatureSDF::~CreatureSDF() {
    shutdown();
}

std::string CreatureSDF::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_Log("CreatureSDF: failed to open shader file: %s", path.c_str());
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool CreatureSDF::loadShaderProgram(const std::string& vertPath, const std::string& fragPath, GLuint& programOut) {
    const std::string vertSrc = readFile(vertPath);
    const std::string fragSrc = readFile(fragPath);
    if (vertSrc.empty() || fragSrc.empty()) {
        return false;
    }

    auto compileShader = [](GLenum type, const std::string& source) -> GLuint {
        GLuint shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint success = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE) {
            GLint logLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
            std::string log(static_cast<std::size_t>(std::max(0, logLen)), '\0');
            if (logLen > 0) {
                glGetShaderInfoLog(shader, logLen, nullptr, log.data());
            }
            SDL_Log("Shader compile error: %s", log.c_str());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (vert == 0 || frag == 0) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<std::size_t>(std::max(0, logLen)), '\0');
        if (logLen > 0) {
            glGetProgramInfoLog(program, logLen, nullptr, log.data());
        }
        SDL_Log("Program link error: %s", log.c_str());
        glDeleteProgram(program);
        return false;
    }

    programOut = program;
    return true;
}

bool CreatureSDF::initialize(const std::string& shaderDir) {
    // Load shader programs
    if (!loadShaderProgram(shaderDir + "/creature_sdf.vert", shaderDir + "/creature_sdf.frag", sdfProgram_)) {
        SDL_Log("CreatureSDF: failed to load SDF shader program");
        return false;
    }
    if (!loadShaderProgram(shaderDir + "/creature_dot.vert", shaderDir + "/creature_dot.frag", dotProgram_)) {
        SDL_Log("CreatureSDF: failed to load dot shader program");
        return false;
    }

    // Create unit quad VAO: 4 vertices forming a triangle strip
    // Vertex positions are [0,1] range — the vertex shader maps to the instance AABB
    float quadVerts[] = {
        0.0f, 0.0f,   // bottom-left
        1.0f, 0.0f,   // bottom-right
        0.0f, 1.0f,   // top-left
        1.0f, 1.0f    // top-right
    };

    glGenVertexArrays(1, &quadVao_);
    glBindVertexArray(quadVao_);

    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);

    // Attribute 0: unit quad position (vec2)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    // Instance VBO for per-creature AABB (attribute 1: vec4 = minX, minY, maxX, maxY)
    glGenBuffers(1, &instanceVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), nullptr);
    glVertexAttribDivisor(1, 1);  // advance once per instance

    glBindVertexArray(0);

    // Create TBO for creature data
    glGenBuffers(1, &dataBuffer_);
    glGenTextures(1, &dataTexture_);

    SDL_Log("CreatureSDF: initialized successfully");
    return true;
}

void CreatureSDF::shutdown() {
    if (sdfProgram_) { glDeleteProgram(sdfProgram_); sdfProgram_ = 0; }
    if (dotProgram_) { glDeleteProgram(dotProgram_); dotProgram_ = 0; }
    if (quadVao_) { glDeleteVertexArrays(1, &quadVao_); quadVao_ = 0; }
    if (quadVbo_) { glDeleteBuffers(1, &quadVbo_); quadVbo_ = 0; }
    if (instanceVbo_) { glDeleteBuffers(1, &instanceVbo_); instanceVbo_ = 0; }
    if (dataTexture_) { glDeleteTextures(1, &dataTexture_); dataTexture_ = 0; }
    if (dataBuffer_) { glDeleteBuffers(1, &dataBuffer_); dataBuffer_ = 0; }
    textureCapacity_ = 0;
}

void CreatureSDF::ensureTextureCapacity(int creatureCount) {
    if (creatureCount <= textureCapacity_) return;

    int newCapacity = std::max(256, creatureCount + creatureCount / 4);
    std::size_t bytes = static_cast<std::size_t>(newCapacity) * kDataStride * sizeof(float);

    glBindBuffer(GL_TEXTURE_BUFFER, dataBuffer_);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_DRAW);

    glBindTexture(GL_TEXTURE_BUFFER, dataTexture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, dataBuffer_);

    textureCapacity_ = newCapacity;
}

void CreatureSDF::render(
    const Simulation& /*sim*/,
    const SDL_FRect& /*worldViewport*/,
    float /*cameraZoom*/,
    const Vec2& /*cameraCenter*/,
    int /*drawableWidth*/,
    int /*drawableHeight*/
) {
    // Stub — will be implemented in Task 3
}

}  // namespace alife
```

- [ ] **Step 3: Bump GL context to 4.1 in Renderer.cpp**

In `src/render/Renderer.cpp`, change the GL context version attributes at line ~798-800:

```cpp
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
```

- [ ] **Step 4: Bump existing shader versions from #version 150 to #version 410**

In `src/render/Renderer.cpp`, update all four embedded shader strings (at lines ~304, ~321, ~331, ~351). Change each `#version 150` to `#version 410`. No other shader code changes needed — GLSL 410 is backwards compatible with 150 for these simple shaders.

- [ ] **Step 5: Add CreatureSDF member to Renderer and wire init/shutdown**

In `src/render/Renderer.hpp`, add include and member:

```cpp
#include "render/CreatureSDF.hpp"
```

Add as private member of class `Renderer`:

```cpp
    CreatureSDF creatureSdf_;
```

In `src/render/Renderer.cpp`, at the end of `Renderer::initialize()` (after the font loading block around line ~838), add:

```cpp
    if (!creatureSdf_.initialize(ALIFE_SHADER_DIR)) {
        SDL_Log("CreatureSDF initialization failed");
        shutdown();
        return false;
    }
```

In `Renderer::shutdown()`, add before existing cleanup:

```cpp
    creatureSdf_.shutdown();
```

- [ ] **Step 6: Add CreatureSDF.cpp to CMakeLists.txt and define shader path**

In `CMakeLists.txt`, update the `add_executable` block:

```cmake
add_executable(alife_sim
    src/main.cpp
    src/sim/Simulation.cpp
    src/render/Renderer.cpp
    src/render/CreatureSDF.cpp
)
```

After the `target_include_directories` line, add the shader directory define:

```cmake
target_compile_definitions(alife_sim PRIVATE
    ALIFE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders"
)
```

Keep the existing `GL_SILENCE_DEPRECATION` define block as-is (it's conditional on APPLE).

- [ ] **Step 7: Create minimal placeholder shader files**

Create `src/render/shaders/creature_sdf.vert`:

```glsl
#version 410

layout(location = 0) in vec2 aQuadPos;     // unit quad [0,1]
layout(location = 1) in vec4 aInstanceAABB; // screen-space AABB: minX, minY, maxX, maxY

uniform vec2 uViewport;  // drawable size in pixels

flat out int vInstanceID;
out vec2 vFragPos;  // screen-pixel position of this fragment

void main() {
    vInstanceID = gl_InstanceID;

    // Expand unit quad to instance AABB
    vec2 screenPos = mix(aInstanceAABB.xy, aInstanceAABB.zw, aQuadPos);
    vFragPos = screenPos;

    // Convert screen pixels to NDC
    vec2 ndc = vec2(
        (screenPos.x / uViewport.x) * 2.0 - 1.0,
        1.0 - (screenPos.y / uViewport.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
```

Create `src/render/shaders/creature_sdf.frag`:

```glsl
#version 410

flat in int vInstanceID;
in vec2 vFragPos;

uniform samplerBuffer uCreatureData;
uniform int uLodTier;  // 0 = full, 1 = simple

out vec4 fragColor;

void main() {
    // Placeholder: render a red quad to prove the pipeline works
    fragColor = vec4(1.0, 0.0, 0.0, 0.5);
}
```

Create `src/render/shaders/creature_dot.vert`:

```glsl
#version 410

layout(location = 0) in vec2 aQuadPos;
layout(location = 1) in vec4 aInstanceAABB;

uniform vec2 uViewport;

flat out int vInstanceID;
out vec2 vFragPos;

void main() {
    vInstanceID = gl_InstanceID;
    vec2 screenPos = mix(aInstanceAABB.xy, aInstanceAABB.zw, aQuadPos);
    vFragPos = screenPos;
    vec2 ndc = vec2(
        (screenPos.x / uViewport.x) * 2.0 - 1.0,
        1.0 - (screenPos.y / uViewport.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
```

Create `src/render/shaders/creature_dot.frag`:

```glsl
#version 410

flat in int vInstanceID;
in vec2 vFragPos;

uniform samplerBuffer uCreatureData;

out vec4 fragColor;

void main() {
    // Placeholder: blue dot
    fragColor = vec4(0.0, 0.0, 1.0, 0.5);
}
```

- [ ] **Step 8: Build and verify**

Run:
```bash
cmake -S . -B build && cmake --build build 2>&1 | tail -20
```

Expected: compiles cleanly with no errors. The app should still launch and render creatures via the old geometry path (since we haven't wired `render()` into the draw loop yet).

- [ ] **Step 9: Run smoke test**

Run:
```bash
./build/alife_sim --smoke-test --seed 1 --smoke-steps 600
```

Expected: passes — no functional changes yet.

- [ ] **Step 10: Commit**

```bash
git add src/render/CreatureSDF.hpp src/render/CreatureSDF.cpp \
       src/render/shaders/creature_sdf.vert src/render/shaders/creature_sdf.frag \
       src/render/shaders/creature_dot.vert src/render/shaders/creature_dot.frag \
       src/render/Renderer.hpp src/render/Renderer.cpp CMakeLists.txt
git commit -m "Scaffold CreatureSDF module and bump GL to 4.1

Add CreatureSDF with shader loading, TBO setup, and instanced quad
infrastructure. Wire into Renderer init/shutdown. Bump GL context
from 3.2 to 4.1 Core and shader versions from 150 to 410. Creature
rendering still uses old geometry path — SDF render() is a stub."
```

---

## Task 2: Data packing and LOD classification

**Files:**
- Modify: `src/render/CreatureSDF.cpp` (implement `packAndClassify` and `render`)

- [ ] **Step 1: Implement the pack-and-classify loop**

Add the following to `CreatureSDF.cpp`, replacing the stub `render()` and implementing `packAndClassify`:

```cpp
namespace {

float wrapDelta(float delta, float worldSize) {
    if (delta > worldSize * 0.5f) return delta - worldSize;
    if (delta < -worldSize * 0.5f) return delta + worldSize;
    return delta;
}

}  // namespace

void CreatureSDF::packAndClassify(
    const Simulation& sim,
    const SDL_FRect& worldViewport,
    float cameraZoom,
    const Vec2& cameraCenter,
    int drawableWidth,
    int drawableHeight
) {
    const float worldW = sim.worldWidth();
    const float worldH = sim.worldHeight();
    const float worldScale = std::min(worldViewport.w, worldViewport.h)
                           / std::max(worldW, worldH) * cameraZoom;

    // Pixel scale factor for Retina: drawable size vs viewport size
    const float retinaScale = static_cast<float>(drawableWidth)
                            / static_cast<float>(worldViewport.w + worldViewport.x * 2.0f);

    fullInstances_.clear();
    simpleInstances_.clear();
    dotInstances_.clear();

    const auto& creatures = sim.creatures();
    int visibleCount = 0;

    // First pass: count visible creatures for TBO sizing
    for (const auto& c : creatures) {
        if (!c.alive) continue;
        ++visibleCount;
    }

    packedData_.resize(static_cast<std::size_t>(visibleCount) * kDataStride);
    ensureTextureCapacity(visibleCount);

    int fullCount = 0;
    int simpleCount = 0;
    int dotCount = 0;

    // We pack in three passes so each LOD tier is contiguous in the TBO:
    // [full SDF creatures...][simple SDF creatures...][dot creatures...]
    // This allows glDrawArraysInstancedBaseInstance per tier.

    // Helper to transform world pos to screen pos
    auto worldToScreen = [&](const Vec2& point) -> Vec2 {
        float dx = wrapDelta(point.x - cameraCenter.x, worldW);
        float dy = wrapDelta(point.y - cameraCenter.y, worldH);
        float vpCenterX = worldViewport.x + worldViewport.w * 0.5f;
        float vpCenterY = worldViewport.y + worldViewport.h * 0.5f;
        return {
            (vpCenterX + dx * worldScale) * retinaScale,
            (vpCenterY + dy * worldScale) * retinaScale
        };
    };

    // Temporary per-creature classification
    struct CreatureClassification {
        const Creature* creature;
        int lodTier;  // 0=full, 1=simple, 2=dot
        float screenDiameter;
    };
    std::vector<CreatureClassification> classified;
    classified.reserve(static_cast<std::size_t>(visibleCount));

    const float vpLeft = worldViewport.x * retinaScale;
    const float vpTop = worldViewport.y * retinaScale;
    const float vpRight = (worldViewport.x + worldViewport.w) * retinaScale;
    const float vpBottom = (worldViewport.y + worldViewport.h) * retinaScale;

    for (const auto& c : creatures) {
        if (!c.alive) continue;

        // Quick screen-space estimate using head segment
        Vec2 headScreen = worldToScreen(c.bodyPoints[0]);
        float headRadiusScreen = c.bodyRadii[0] * worldScale * retinaScale;
        float screenDiam = headRadiusScreen * 2.0f;

        // Rough viewport culling with generous padding
        float pad = screenDiam * 3.0f;  // padding for body chain + appendages
        if (headScreen.x + pad < vpLeft || headScreen.x - pad > vpRight ||
            headScreen.y + pad < vpTop || headScreen.y - pad > vpBottom) {
            continue;
        }

        int tier;
        if (screenDiam > kFullSdfThreshold) {
            tier = 0;
        } else if (screenDiam > kSimpleSdfThreshold) {
            tier = 1;
        } else {
            tier = 2;
        }

        classified.push_back({&c, tier, screenDiam});
    }

    // Sort by tier so packing is contiguous: full(0), simple(1), dot(2)
    std::sort(classified.begin(), classified.end(),
        [](const CreatureClassification& a, const CreatureClassification& b) {
            return a.lodTier < b.lodTier;
        }
    );

    // Pack data and build instance AABBs
    packedData_.resize(classified.size() * kDataStride);

    for (std::size_t idx = 0; idx < classified.size(); ++idx) {
        const Creature& c = *classified[idx].creature;
        float* dest = packedData_.data() + idx * kDataStride;

        // Offsets 0-11: segment screen positions (6 x vec2)
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        float maxRadiusScreen = 0.0f;
        for (int s = 0; s < kBodySegments; ++s) {
            Vec2 sp = worldToScreen(c.bodyPoints[s]);
            dest[s * 2 + 0] = sp.x;
            dest[s * 2 + 1] = sp.y;

            float rScreen = c.bodyRadii[s] * worldScale * retinaScale;
            dest[12 + s] = rScreen;  // Offsets 12-17: segment radii

            if (rScreen > maxRadiusScreen) maxRadiusScreen = rScreen;
            if (sp.x - rScreen < minX) minX = sp.x - rScreen;
            if (sp.x + rScreen > maxX) maxX = sp.x + rScreen;
            if (sp.y - rScreen < minY) minY = sp.y - rScreen;
            if (sp.y + rScreen > maxY) maxY = sp.y + rScreen;
        }

        // Offsets 18-19: head screen position
        dest[18] = dest[0];
        dest[19] = dest[1];

        // Offsets 20-21: forward axis
        float cosA = std::cos(c.angle);
        float sinA = std::sin(c.angle);
        dest[20] = cosA;
        dest[21] = sinA;

        // Offset 22: heading angle
        dest[22] = c.angle;

        // Offset 23: gait phase
        dest[23] = c.gaitPhase;

        // Offset 24: thrust drive (output 0 clamped)
        dest[24] = std::clamp(c.outputs[0], 0.0f, 1.0f);

        // Offset 25: bite drive (output 4 clamped)
        dest[25] = std::clamp(c.outputs[4], 0.0f, 1.0f);

        // Offsets 26-31: segment integrity
        for (int s = 0; s < kBodySegments; ++s) {
            float durability = std::max(c.traits.segmentDurability[s], 1.0f);
            dest[26 + s] = std::clamp(1.0f - c.segmentDamage[s] / durability, 0.0f, 1.0f);
        }

        // Genome-derived appearance traits (offsets 32-51)
        dest[32] = c.genome.morphology.hue;
        // Compute saturation and lightness the same way Renderer.cpp does
        float diet = std::clamp(
            c.genome.ecology.meatAffinity
            / std::max(0.18f, c.genome.ecology.plantAffinity + c.genome.ecology.meatAffinity),
            0.0f, 1.0f);
        float energyLevel = std::clamp(c.energy / std::max(56.0f, c.traits.reproductionThreshold), 0.0f, 1.0f);
        float aggression = std::clamp(c.genome.ecology.aggression * 0.68f + diet * 0.32f, 0.0f, 1.0f);
        float sat = 0.46f + (0.78f - 0.46f) * (c.genome.morphology.armor * 0.72f + aggression * 0.28f);
        float lit = 0.56f + (0.76f - 0.56f) * (c.genome.ecology.plantAffinity * 0.55f + energyLevel * 0.45f);
        dest[33] = sat;
        dest[34] = lit;
        dest[35] = energyLevel;
        dest[36] = diet;
        dest[37] = aggression;
        dest[38] = c.genome.morphology.finArea;
        dest[39] = c.genome.morphology.finPlacement;
        dest[40] = c.genome.morphology.tailLength;
        dest[41] = c.genome.morphology.tailFlex;
        dest[42] = c.genome.morphology.tailFork;
        dest[43] = c.genome.morphology.jawLength;
        dest[44] = c.genome.morphology.jawArc;
        dest[45] = c.genome.morphology.sensorRange;
        dest[46] = c.genome.morphology.sensorSpan;  // used for whisker/eye expressiveness
        dest[47] = c.genome.morphology.spikes;
        dest[48] = c.genome.morphology.pattern;
        dest[49] = c.genome.morphology.bodyTaper;
        dest[50] = c.genome.morphology.armor;
        dest[51] = c.genome.morphology.coreSize;
        dest[52] = 0.0f;  // reserved
        dest[53] = 0.0f;
        dest[54] = 0.0f;
        dest[55] = 0.0f;

        // Pad AABB for appendages
        float appendagePad = maxRadiusScreen * 1.5f;
        float tailPad = c.genome.morphology.tailLength * maxRadiusScreen * 2.0f;
        float totalPad = std::max(appendagePad, tailPad) + 4.0f;  // +4px safety

        InstanceData inst {
            minX - totalPad, minY - totalPad,
            maxX + totalPad, maxY + totalPad
        };

        switch (classified[idx].lodTier) {
            case 0: fullInstances_.push_back(inst); ++fullCount; break;
            case 1: simpleInstances_.push_back(inst); ++simpleCount; break;
            default: dotInstances_.push_back(inst); ++dotCount; break;
        }
    }
}
```

- [ ] **Step 2: Implement render() to upload data and issue instanced draws**

Replace the stub `render()` in `CreatureSDF.cpp`:

```cpp
void CreatureSDF::render(
    const Simulation& sim,
    const SDL_FRect& worldViewport,
    float cameraZoom,
    const Vec2& cameraCenter,
    int drawableWidth,
    int drawableHeight
) {
    packAndClassify(sim, worldViewport, cameraZoom, cameraCenter, drawableWidth, drawableHeight);

    int totalCreatures = static_cast<int>(fullInstances_.size() + simpleInstances_.size() + dotInstances_.size());
    if (totalCreatures == 0) return;

    // Upload creature data to TBO
    std::size_t dataBytes = packedData_.size() * sizeof(float);
    glBindBuffer(GL_TEXTURE_BUFFER, dataBuffer_);
    glBufferSubData(GL_TEXTURE_BUFFER, 0, static_cast<GLsizeiptr>(dataBytes), packedData_.data());

    // Bind TBO as texture unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, dataTexture_);

    // Enable blending for SDF alpha compositing
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(quadVao_);

    float viewportVec[2] = {
        static_cast<float>(drawableWidth),
        static_cast<float>(drawableHeight)
    };

    // Build combined instance buffer: [full...][simple...][dot...]
    std::vector<InstanceData> allInstances;
    allInstances.reserve(static_cast<std::size_t>(totalCreatures));
    allInstances.insert(allInstances.end(), fullInstances_.begin(), fullInstances_.end());
    allInstances.insert(allInstances.end(), simpleInstances_.begin(), simpleInstances_.end());
    allInstances.insert(allInstances.end(), dotInstances_.begin(), dotInstances_.end());

    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(allInstances.size() * sizeof(InstanceData)),
        allInstances.data(), GL_STREAM_DRAW);

    int fullCount = static_cast<int>(fullInstances_.size());
    int simpleCount = static_cast<int>(simpleInstances_.size());
    int dotCount = static_cast<int>(dotInstances_.size());

    // Draw full SDF tier
    if (fullCount > 0) {
        glUseProgram(sdfProgram_);
        glUniform2fv(glGetUniformLocation(sdfProgram_, "uViewport"), 1, viewportVec);
        glUniform1i(glGetUniformLocation(sdfProgram_, "uCreatureData"), 0);
        glUniform1i(glGetUniformLocation(sdfProgram_, "uLodTier"), 0);
        glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4,
            fullCount, 0);
    }

    // Draw simple SDF tier
    if (simpleCount > 0) {
        glUseProgram(sdfProgram_);
        glUniform2fv(glGetUniformLocation(sdfProgram_, "uViewport"), 1, viewportVec);
        glUniform1i(glGetUniformLocation(sdfProgram_, "uCreatureData"), 0);
        glUniform1i(glGetUniformLocation(sdfProgram_, "uLodTier"), 1);
        glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4,
            simpleCount, static_cast<GLuint>(fullCount));
    }

    // Draw dot tier
    if (dotCount > 0) {
        glUseProgram(dotProgram_);
        glUniform2fv(glGetUniformLocation(dotProgram_, "uViewport"), 1, viewportVec);
        glUniform1i(glGetUniformLocation(dotProgram_, "uCreatureData"), 0);
        glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4,
            dotCount, static_cast<GLuint>(fullCount + simpleCount));
    }

    glBindVertexArray(0);
    glUseProgram(0);
}
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build 2>&1 | tail -20
```

Expected: compiles cleanly.

- [ ] **Step 4: Commit**

```bash
git add src/render/CreatureSDF.cpp
git commit -m "Implement data packing, LOD classification, and instanced draw

Pack creature data (segment positions, radii, genome traits) into a
float TBO each frame. Classify creatures into full/simple/dot tiers
by screen-pixel diameter. Issue 3 instanced draw calls with
glDrawArraysInstancedBaseInstance. Shader still renders placeholder
quads."
```

---

## Task 3: Wire SDF rendering into the draw loop

**Files:**
- Modify: `src/render/Renderer.cpp` (call `creatureSdf_.render()`, keep old code behind a flag temporarily)

- [ ] **Step 1: Add the CreatureSDF render call alongside the old path**

In `Renderer::draw()`, find the creature rendering loop (around line ~1901-1911). Add the SDF render call *before* the old creature loop, and gate the old loop behind a `constexpr bool`:

```cpp
    // --- SDF creature rendering ---
    constexpr bool kUseSdfRenderer = true;
    if (kUseSdfRenderer) {
        creatureSdf_.render(
            simulation,
            worldViewport_,
            cameraZoom_,
            cameraCenter_,
            renderer_->drawableWidth,
            renderer_->drawableHeight
        );
    }

    // --- Old geometry creature rendering (temporary fallback) ---
    if (!kUseSdfRenderer) {
        const std::uint64_t selectedId = simulation.selectedCreature();
        for (const Creature& creature : simulation.creatures()) {
            if (creature.id != selectedId) {
                drawCreature(creature, false);
            }
        }
        for (const Creature& creature : simulation.creatures()) {
            if (creature.id == selectedId) {
                drawCreature(creature, true);
            }
        }
    }
```

Note: the `drawCreature` lambda and all its code stays intact for now — we need the fallback toggle during development. It will be removed in Task 8.

- [ ] **Step 2: Flush the batch geometry before SDF rendering**

The existing renderer accumulates geometry in `renderer_->colorVertices` / `colorIndices` and draws it via `flushColored(renderer_)`. The SDF renderer uses its own VAO and shader program, so we must flush the accumulated batch before switching GL state. Add a `flushColored(renderer_)` call immediately before the SDF render call:

```cpp
    // Flush any accumulated geometry before switching to SDF pipeline
    flushColored(renderer_);

    // --- SDF creature rendering ---
    creatureSdf_.render( ... );
```

After the SDF rendering completes, the existing renderer will continue accumulating geometry for subsequent elements (selection overlays, HUD). The existing `colorVao` must be re-bound after the SDF call returns. Add after the SDF render call:

```cpp
    // Restore batched geometry VAO after SDF rendering
    glBindVertexArray(renderer_->colorVao);
    glUseProgram(renderer_->colorProgram);
```

- [ ] **Step 3: Build and launch visually**

```bash
cmake --build build && ./build/alife_sim
```

Expected: the app launches. Creatures should appear as red semi-transparent rectangles (the placeholder SDF shader output) instead of the old detailed geometry. The environment, HUD, and all other rendering should look normal.

If creatures don't appear at all, check:
- Is the shader directory path correct? (check console for "failed to open shader file" messages)
- Are the quads being sized correctly? (the AABB computation in packAndClassify)

- [ ] **Step 4: Run smoke test**

```bash
./build/alife_sim --smoke-test --seed 1 --smoke-steps 600
```

Expected: passes — smoke test is headless and doesn't validate rendering.

- [ ] **Step 5: Commit**

```bash
git add src/render/Renderer.cpp
git commit -m "Wire CreatureSDF into draw loop with toggle

SDF renderer now called during creature rendering phase. Old geometry
path preserved behind kUseSdfRenderer toggle for fallback during
development. Creatures render as red placeholder quads confirming
pipeline works end-to-end."
```

---

## Task 4: SDF body core — 6-segment smooth blend

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag`

- [ ] **Step 1: Implement the data fetch helpers and body SDF**

Replace the placeholder `creature_sdf.frag` with the body core implementation:

```glsl
#version 410

flat in int vInstanceID;
in vec2 vFragPos;

uniform samplerBuffer uCreatureData;
uniform int uLodTier;  // 0 = full, 1 = simple

out vec4 fragColor;

// ── Data fetch helpers ──
// Each creature is 56 floats = 14 vec4 texels in the TBO.
// gl_InstanceID (passed as vInstanceID) gives the creature index.

vec4 fetchTexel(int creatureIdx, int texelIdx) {
    return texelFetch(uCreatureData, creatureIdx * 14 + texelIdx);
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

// ── SDF primitives ──

float sdCircle(vec2 p, float r) {
    return length(p) - r;
}

float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// ── HSL to RGB ──

vec3 hsl2rgb(float h, float s, float l) {
    // h is 0-1 (genome hue is 0-1)
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
    int ci = vInstanceID;  // creature index

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

    // Compute average radius for blend kernel
    float avgRadius = 0.0;
    for (int i = 0; i < segCount; i++) {
        avgRadius += fetchSegRadius(ci, segIndices[i]);
    }
    avgRadius /= float(segCount);
    float blendK = avgRadius * 0.55;

    for (int i = 0; i < segCount; i++) {
        int si = segIndices[i];
        vec2 segPos = fetchSegPos(ci, si);
        float segR = fetchSegRadius(ci, si);
        float d = sdCircle(vFragPos - segPos, segR);
        dBody = smin(dBody, d, blendK);
    }

    // ── Stage 5 (partial): Basic colour composition ──

    float hue = fetchFloat(ci, 32);
    float sat = fetchFloat(ci, 33);
    float lit = fetchFloat(ci, 34);
    float energy = fetchFloat(ci, 35);

    vec3 baseCol = hsl2rgb(hue, sat, lit);
    vec3 rimCol = hsl2rgb(hue - 0.03, sat * 1.1, lit - 0.18);
    vec3 innerCol = hsl2rgb(hue + 0.04, sat * 0.7, lit + 0.16);

    // Distance-based shading
    float bodyAlpha = smoothstep(1.0, -1.0, dBody);
    float rim = smoothstep(0.0, -avgRadius * 0.15, dBody)
              * (1.0 - smoothstep(-avgRadius * 0.15, -avgRadius * 0.45, dBody));
    float inner = smoothstep(-avgRadius * 0.05, -avgRadius * 0.5, dBody);

    // Outline
    float outline = smoothstep(avgRadius * 0.06, avgRadius * 0.01, abs(dBody));

    vec3 col = mix(baseCol, innerCol, inner * 0.55);
    col = mix(col, rimCol, rim * 0.5);

    // Energy glow
    vec3 energyCol = hsl2rgb(hue + 0.1, 1.0, 0.72);
    col += energyCol * energy * 0.12 * inner;

    // Outline darkening
    col = mix(col, rimCol * 0.6, outline * 0.85);

    if (bodyAlpha < 0.001) discard;

    fragColor = vec4(col, bodyAlpha * 0.97);
}
```

- [ ] **Step 2: Build and launch visually**

```bash
cmake --build build && ./build/alife_sim
```

Expected: creatures render as smooth, coloured organic blobs that follow their articulated body chains. The shapes should blend smoothly between segments — no hard edges. Colour should reflect genome hue. At this stage there are no appendages, eyes, or mouth yet — just smooth body silhouettes with basic rim/inner shading.

- [ ] **Step 3: Verify LOD tiers by zooming in and out**

Zoom in (`+` key or scroll wheel): nearby creatures should appear as smooth 6-segment blobs.
Zoom out (`-` key or scroll wheel): distant creatures should still be smooth but simpler (3-segment blend for simple tier).
Very distant: dot-tier creatures won't be visible yet (dot shader is still placeholder).

- [ ] **Step 4: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Implement SDF body core with 6-segment smooth blend

Fragment shader evaluates sdCircle for each body segment and blends
with smin for organic silhouettes. Basic HSL colour composition with
rim, inner glow, and energy highlights. LOD tier 1 uses 3 segments
(head/mid/tail) for reduced cost at medium distance."
```

---

## Task 5: Dot tier shader

**Files:**
- Modify: `src/render/shaders/creature_dot.frag`

- [ ] **Step 1: Implement the dot fragment shader**

Replace the placeholder `creature_dot.frag`:

```glsl
#version 410

flat in int vInstanceID;
in vec2 vFragPos;

uniform samplerBuffer uCreatureData;

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
    int ci = vInstanceID;

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
```

- [ ] **Step 2: Build and verify zoomed out**

```bash
cmake --build build && ./build/alife_sim
```

Zoom all the way out. Very small creatures should now appear as soft coloured dots rather than disappearing.

- [ ] **Step 3: Commit**

```bash
git add src/render/shaders/creature_dot.frag
git commit -m "Implement dot-tier shader for distant creatures

Soft coloured circle with genome hue and energy-modulated brightness.
Used for creatures under 8px screen diameter."
```

---

## Task 6: SDF appendages — tails, fins, whiskers, spikes

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag`

- [ ] **Step 1: Add SDF capsule primitive and tail filament evaluation**

Add the capsule SDF and tail evaluation functions to `creature_sdf.frag`, after the existing SDF primitives and before `main()`:

```glsl
float sdCapsule(vec2 p, vec2 a, vec2 b, float r) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

// Tapered capsule — radius varies from rA at point a to rB at point b
float sdTaperedCapsule(vec2 p, vec2 a, vec2 b, float rA, float rB) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    float r = mix(rA, rB, h);
    return length(pa - ba * h) - r;
}

float sdTriangle(vec2 p, vec2 a, vec2 b, vec2 c) {
    vec2 e0 = b - a, e1 = c - b, e2 = a - c;
    vec2 v0 = p - a, v1 = p - b, v2 = p - c;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(
        vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
        vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
        vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}
```

- [ ] **Step 2: Add Stage 2 appendage evaluation in main()**

In `main()`, after the Stage 1 body core block and before Stage 5 colour composition, add Stage 2 (only for full LOD tier):

```glsl
    float dOrganism = dBody;

    if (uLodTier == 0) {
        // Fetch appendage traits
        float gaitPhase = fetchFloat(ci, 23);
        float thrustDrive = fetchFloat(ci, 24);
        float tailLength = fetchFloat(ci, 40);
        float tailFlex = fetchFloat(ci, 41);
        float tailFork = fetchFloat(ci, 42);
        float finArea = fetchFloat(ci, 38);
        float finPlacement = fetchFloat(ci, 39);
        float spikes = fetchFloat(ci, 47);
        float sensorExpr = fetchFloat(ci, 46);

        // ── Tail filaments ──
        int tailCount = 1 + int(tailFork * 2.0 + tailFlex * 0.5);
        tailCount = clamp(tailCount, 1, 4);

        vec2 tailSeg = fetchSegPos(ci, 5);
        float tailR = fetchSegRadius(ci, 5);
        vec2 tailAxis = normalize(tailSeg - fetchSegPos(ci, 4));

        float dTails = 1e9;
        for (int t = 0; t < 4; t++) {
            if (t >= tailCount) break;
            float spread = (float(t) - float(tailCount - 1) * 0.5) * 0.35;
            vec2 root = tailSeg + tailAxis * tailR * 0.6
                      + vec2(-tailAxis.y, tailAxis.x) * spread * tailR;

            float wave = sin(gaitPhase * 1.18 + float(t) * 1.3)
                       * (0.14 + tailFlex * 0.42) * (0.28 + thrustDrive * 0.72);
            vec2 perpAxis = vec2(-tailAxis.y, tailAxis.x);

            float fLen = tailLength * avgRadius * 2.5;
            vec2 mid = root + tailAxis * fLen * 0.5 + perpAxis * wave * fLen * 0.3;
            vec2 tip = root + tailAxis * fLen + perpAxis * wave * fLen * 0.15;

            float rootW = tailR * 0.28;
            float tipW = tailR * 0.06;
            float s1 = sdTaperedCapsule(vFragPos, root, mid, rootW, rootW * 0.5);
            float s2 = sdTaperedCapsule(vFragPos, mid, tip, rootW * 0.5, tipW);
            dTails = min(dTails, smin(s1, s2, rootW * 0.6));
        }
        dOrganism = smin(dOrganism, dTails, tailR * 0.4);

        // ── Dorsal fin ──
        if (finArea > 0.1) {
            int finSeg = 1 + int(finPlacement * 3.0);
            finSeg = clamp(finSeg, 1, 4);
            vec2 finPos = fetchSegPos(ci, finSeg);
            float finR = fetchSegRadius(ci, finSeg);
            vec2 finAxis = vec2(0.0);
            if (finSeg > 0) {
                finAxis = normalize(fetchSegPos(ci, finSeg - 1) - finPos);
            }
            vec2 finPerp = vec2(-finAxis.y, finAxis.x);

            float finExpr = finArea;
            float flap = sin(gaitPhase * 1.36) * (0.08 + finExpr * 0.38) * (0.25 + thrustDrive * 0.75);

            for (int side = 0; side < 2; side++) {
                float sSign = (side == 0) ? -1.0 : 1.0;
                vec2 finBase = finPos + finPerp * sSign * finR * 0.7;
                vec2 finTip = finBase + finPerp * sSign * finR * (0.6 + finArea * 0.8)
                            + finAxis * flap * finR * sSign;
                float fw = finR * 0.15 * finArea;
                dOrganism = smin(dOrganism, sdTaperedCapsule(vFragPos, finBase, finTip, fw, fw * 0.3), fw * 1.8);
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

        // ── Spikes ──
        if (spikes > 0.28) {
            for (int s = 1; s <= 4; s++) {
                vec2 sPos = fetchSegPos(ci, s);
                float sR = fetchSegRadius(ci, s);
                vec2 sAxis = vec2(0.0);
                if (s > 0) {
                    sAxis = normalize(fetchSegPos(ci, s - 1) - sPos);
                }
                vec2 sPerp = vec2(-sAxis.y, sAxis.x);

                float spikeLen = sR * (0.05 + spikes * 0.22);
                vec2 sBase = sPos + sPerp * sR * 0.85;
                vec2 sTip = sBase + sPerp * spikeLen;
                vec2 sBaseR = sPos - sPerp * sR * 0.85;
                vec2 sTipR = sBaseR - sPerp * spikeLen;

                float sw = sR * 0.06;
                dOrganism = min(dOrganism, sdTaperedCapsule(vFragPos, sBase, sTip, sw, sw * 0.15));
                dOrganism = min(dOrganism, sdTaperedCapsule(vFragPos, sBaseR, sTipR, sw, sw * 0.15));
            }
        }
    }
```

Update the colour composition to use `dOrganism` instead of `dBody`:

Change `float bodyAlpha = smoothstep(1.0, -1.0, dBody);` to:
```glsl
    float bodyAlpha = smoothstep(1.0, -1.0, dOrganism);
```

And similarly update the `rim`, `inner`, and `outline` calculations to use `dOrganism` for the overall silhouette, but keep `dBody` available for body-only shading effects (like subsurface scatter, which should be body-centric).

- [ ] **Step 2: Build and verify visually**

```bash
cmake --build build && ./build/alife_sim
```

Expected: creatures now have visible tail filaments that wave with gait phase, dorsal fins that flap, whiskers on sensor-heavy creatures, and spikes on armored creatures. All appendages should smoothly blend into the body — no hard attachment seams.

- [ ] **Step 3: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Add SDF appendages: tails, fins, whiskers, spikes

Tail filaments wave with gait phase and thrust drive. Dorsal fins
flap at the genome-specified placement segment. Whiskers emerge on
sensor-expressive creatures. Spikes protrude from armored segments.
All blended into body via smin for seamless organic attachment."
```

---

## Task 7: SDF mouth carving and eyes

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag`

- [ ] **Step 1: Add Stage 3 mouth carving after appendages**

In `main()`, after the Stage 2 appendage block (still inside the `uLodTier == 0` guard), add the mouth carving:

```glsl
        // ── Stage 3: Mouth carving ──
        float diet = fetchFloat(ci, 36);
        float jawLength = fetchFloat(ci, 43);
        float jawArc = fetchFloat(ci, 44);
        float biteDrive = fetchFloat(ci, 25);

        vec2 headPos3 = fetchSegPos(ci, 0);
        float headR3 = fetchSegRadius(ci, 0);
        vec2 fwdAxis = fetchForwardAxis(ci);

        vec2 mouthCentre = headPos3 + fwdAxis * headR3 * 0.88;

        // Base mouth dimensions
        float baseW = headR3 * mix(0.32, 0.22, diet) * (0.5 + jawArc * 0.7);
        float baseH = headR3 * mix(0.10, 0.06, diet) * (0.3 + jawLength * 0.8);

        // Bite gape
        float gape = baseH * (1.0 + biteDrive * mix(2.0, 5.0, diet));

        // Rotate mouth coordinates into head-local space
        vec2 mp = vFragPos - mouthCentre;
        // Rotate so forward axis = +y
        vec2 mLocal = vec2(
            mp.x * fwdAxis.y - mp.y * fwdAxis.x,   // x in head-local
            mp.x * fwdAxis.x + mp.y * fwdAxis.y     // y in head-local (forward)
        );

        // Herbivore: ellipse mouth
        float dMouthHerb = (length(mLocal / vec2(baseW, gape)) - 1.0) * min(baseW, gape);

        // Carnivore: lens shape (two intersecting circles)
        float lensR = baseW * mix(1.8, 0.9, diet);
        float lensOff = sqrt(max(0.0, lensR * lensR - baseW * baseW));
        float dLensTop = length(mLocal - vec2(0.0, -lensOff)) - lensR;
        float dLensBot = length(mLocal - vec2(0.0, lensOff)) - lensR;
        float dMouthCarn = max(dLensTop, dLensBot) - gape;

        float dMouth = mix(dMouthHerb, dMouthCarn, diet);

        // Only carve where inside head region
        float carveDepth = smoothstep(0.0, -headR3 * 0.15, sdCircle(vFragPos - headPos3, headR3));
        float dMouthCarve = mix(0.05, dMouth, carveDepth);

        dOrganism = max(dOrganism, -dMouthCarve);
```

- [ ] **Step 2: Add Stage 4 eye SDFs**

After the mouth carving, still inside `uLodTier == 0`:

```glsl
        // ── Stage 4: Eyes ──
        float sensorRange = fetchFloat(ci, 45);
        float aggro = fetchFloat(ci, 37);

        int eyeCount = (sensorRange > 0.6) ? 3 : ((sensorRange > 0.3) ? 2 : 1);
        float eyeR = headR3 * 0.12;

        float dEyes = 1e9;
        float dPupils = 1e9;
        float dHighlights = 1e9;

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
```

- [ ] **Step 3: Update Stage 5 colour composition to include mouth and eyes**

In the colour composition section, add mouth interior and eye rendering. Replace the existing composition block with the full version:

```glsl
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
            hsl2rgb(hue + 0.94, 0.5, 0.38),   // herbivore: pale fleshy
            hsl2rgb(hue - 0.06, 0.4, 0.12),    // carnivore: dark throat
            diet2
        );
        col = mix(col, mouthCol, mouthMask * 0.92);

        // Lip edge highlight
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
```

Note: `dMouth`, `dBody`, `dEyes`, `dPupils`, `dHighlights`, and `eyeR` are only defined inside the `uLodTier == 0` block. The variables need to be declared before the LOD branching and given default values for the simple tier. Move the declarations of `dMouth`, `dEyes`, `dPupils`, `dHighlights`, and `eyeR` to before the `if (uLodTier == 0)` block:

```glsl
    float dOrganism = dBody;
    float dMouth = 1e9;
    float dEyes = 1e9;
    float dPupils = 1e9;
    float dHighlights = 1e9;
    float eyeR = 0.0;
```

Then the `if (uLodTier == 0)` block populates them. The colour composition checks `uLodTier` before using them.

- [ ] **Step 4: Build and verify visually**

```bash
cmake --build build && ./build/alife_sim
```

Expected: creatures now have:
- Carved mouth openings that vary by diet (wide rounded for herbivores, narrow slit for carnivores)
- Bite animation when creatures attack
- 1-3 eyes with visible sclera, coloured iris, dark pupil, and bright catchlight
- Subsurface scatter glow near the head

- [ ] **Step 5: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Add SDF mouth carving and multi-layer eye rendering

Mouth shape morphs by diet: herbivore ellipse to carnivore lens slit.
Bite drive animates vertical gape. Eyes have sclera, aggression-tinted
iris, pupil, and catchlight. Subsurface scatter hint near head region.
All features gated to full LOD tier only."
```

---

## Task 8: Remove old creature rendering code

**Files:**
- Modify: `src/render/Renderer.cpp` (delete drawCreature lambda and old creature loop)

- [ ] **Step 1: Remove the kUseSdfRenderer toggle and old fallback**

In `Renderer::draw()`, remove the `constexpr bool kUseSdfRenderer` line, the `if (!kUseSdfRenderer)` block, and any remaining references. The creature rendering section should now just be:

```cpp
    // --- SDF creature rendering ---
    creatureSdf_.render(
        simulation,
        worldViewport_,
        cameraZoom_,
        cameraCenter_,
        renderer_->drawableWidth,
        renderer_->drawableHeight
    );
```

- [ ] **Step 2: Remove the drawCreature lambda**

Delete the entire `auto drawCreature = [&](const Creature& creature, bool selected) {` lambda from approximately line 1324 to line 1888 (the closing `};` of the lambda). This is the ~560 lines of geometry-based creature rendering code.

Keep the selection debug overlay code that comes after (sensor arc, bite arc, collision ring, joint strain, velocity trail) — those are separate from the `drawCreature` lambda and still useful.

- [ ] **Step 3: Clean up any now-unused helper functions**

If any helper lambdas or functions (like `drawBodyRibbon`, colour mixing helpers used only by drawCreature, etc.) are now unreferenced, remove them. The compiler will tell you via warnings.

- [ ] **Step 4: Build and verify**

```bash
cmake --build build 2>&1 | tail -30
```

Expected: compiles cleanly with no warnings about unused variables/functions (or at most existing warnings unrelated to this change).

- [ ] **Step 5: Launch and do full visual QA**

```bash
./build/alife_sim
```

Verify:
- Creatures render with smooth SDF bodies at all zoom levels
- LOD transitions are smooth (no popping)
- Selection debug overlays (sensor/bite arcs, collision ring) still render correctly
- HUD, graphs, inspector all work as before
- Environment (blooms, carrion, reefs, nutrient grid) renders normally

- [ ] **Step 6: Run regression suite**

```bash
cd build && ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/render/Renderer.cpp
git commit -m "Remove old geometry-based creature rendering

Delete the ~560-line drawCreature lambda and all supporting geometry
code. Creature rendering now fully handled by CreatureSDF module.
Selection debug overlays retained in Renderer.cpp."
```

---

## Task 9: Polish and damage visualization

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag`

- [ ] **Step 1: Add per-segment damage visualization**

In the colour composition section (Stage 5), after the base colour mixing and before the outline, add damage modulation:

```glsl
    // Per-segment damage visualization
    if (uLodTier == 0) {
        for (int s = 0; s < 6; s++) {
            float integrity = fetchFloat(ci, 26 + s);
            if (integrity < 0.95) {
                vec2 segP = fetchSegPos(ci, s);
                float segR = fetchSegRadius(ci, s);
                float segDist = sdCircle(vFragPos - segP, segR);
                float segInfluence = smoothstep(0.0, -segR * 0.8, segDist);

                // Darken and desaturate damaged regions
                float dmg = 1.0 - integrity;
                vec3 damageCol = hsl2rgb(hue - 0.05, sat * 0.3, lit * 0.4);
                col = mix(col, damageCol, segInfluence * dmg * 0.6);
            }
        }
    }
```

- [ ] **Step 2: Add body markings/pattern**

After damage visualization, add markings for creatures with expressed patterns:

```glsl
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
                vec2 mAxis = vec2(0.0);
                if (markSeg > 0) {
                    mAxis = normalize(fetchSegPos(ci, markSeg - 1) - mPos);
                }
                vec2 mPerp = vec2(-mAxis.y, mAxis.x);

                // Mark as a colour band on one side
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
```

- [ ] **Step 3: Build and verify visually**

```bash
cmake --build build && ./build/alife_sim
```

Expected: damaged creatures show darkened, desaturated segments. Creatures with low `pattern` values show subtle colour markings along their body.

- [ ] **Step 4: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Add damage visualization and body markings

Damaged segments show darkened, desaturated regions proportional to
injury severity. Body markings appear as subtle colour bands on
pattern-expressing creatures. Both gated to full LOD tier."
```

---

## Task 10: Final regression and cleanup

**Files:**
- Modify: `src/render/Renderer.cpp` (any remaining cleanup)

- [ ] **Step 1: Run full regression suite**

```bash
cd build && ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 2: Run multi-seed headless probe**

```bash
./build/alife_sim --batch-run --seed 1 --batch-count 3 --smoke-steps 3600
```

Expected: all 3 seeds complete without crashes or errors.

- [ ] **Step 3: Visual QA at multiple zoom levels**

Launch `./build/alife_sim` and verify:
- Zoomed in: full SDF creatures with smooth bodies, appendages, eyes, mouth, markings, damage
- Medium zoom: simplified but still smooth silhouettes
- Zoomed out: coloured dots, performance stays smooth
- Zoom transitions: no popping or sudden visual changes
- Selection: debug overlays (arcs, collision ring) render correctly over SDF creatures
- HUD: all panels, graphs, inspector functional

- [ ] **Step 4: Verify .gitignore for brainstorm files**

```bash
echo ".superpowers/" >> .gitignore
```

(Only if `.superpowers/` is not already in `.gitignore`.)

- [ ] **Step 5: Commit any cleanup**

```bash
git add -A
git commit -m "Final cleanup and regression verification

All tests pass. Multi-seed headless probes complete. Visual QA
confirmed at all zoom levels and LOD tiers."
```
