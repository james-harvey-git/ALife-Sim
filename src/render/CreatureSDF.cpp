#include "render/CreatureSDF.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#include <SDL2/SDL.h>

namespace alife {

namespace {

inline float wrapDelta(float delta, float worldSize) {
    if (delta > worldSize * 0.5f) return delta - worldSize;
    if (delta < -worldSize * 0.5f) return delta + worldSize;
    return delta;
}

inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

}  // namespace

CreatureSDF::~CreatureSDF() { shutdown(); }

std::string CreatureSDF::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_Log("CreatureSDF: failed to open file: %s", path.c_str());
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint CreatureSDF::loadShaderProgram(const std::string& vertPath, const std::string& fragPath) {
    std::string vertSource = readFile(vertPath);
    std::string fragSource = readFile(fragPath);
    if (vertSource.empty() || fragSource.empty()) {
        SDL_Log("CreatureSDF: shader source empty for %s / %s", vertPath.c_str(), fragPath.c_str());
        return 0;
    }

    auto compileShader = [](GLenum type, const std::string& source) -> GLuint {
        GLuint shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            GLint logLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(static_cast<std::size_t>(std::max(0, logLength)), '\0');
            if (logLength > 0) {
                glGetShaderInfoLog(shader, logLength, nullptr, log.data());
            }
            SDL_Log("CreatureSDF: shader compile failed: %s", log.c_str());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSource);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSource);
    if (vert == 0 || frag == 0) {
        if (vert != 0) glDeleteShader(vert);
        if (frag != 0) glDeleteShader(frag);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<std::size_t>(std::max(0, logLength)), '\0');
        if (logLength > 0) {
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
        }
        SDL_Log("CreatureSDF: program link failed: %s", log.c_str());
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

bool CreatureSDF::initialize(const std::string& shaderDir) {
    sdfProgram_ = loadShaderProgram(
        shaderDir + "/creature_sdf.vert",
        shaderDir + "/creature_sdf.frag");
    dotProgram_ = loadShaderProgram(
        shaderDir + "/creature_dot.vert",
        shaderDir + "/creature_dot.frag");

    if (sdfProgram_ == 0 || dotProgram_ == 0) {
        SDL_Log("CreatureSDF: one or more shader programs failed to load");
        return false;
    }

    // Create unit quad VAO: triangle strip covering [0,1] range
    float quadVerts[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    glGenVertexArrays(1, &quadVao_);
    glBindVertexArray(quadVao_);

    // Quad vertex buffer (attribute 0)
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    // Instance VBO (attribute 1): per-instance AABB as vec4
    glGenBuffers(1, &instanceVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), nullptr);
    glVertexAttribDivisor(1, 1);

    glBindVertexArray(0);

    // Create texture buffer object (TBO)
    glGenBuffers(1, &dataBuffer_);
    glGenTextures(1, &dataTexture_);
    textureCapacity_ = 0;

    SDL_Log("CreatureSDF: initialized");
    return true;
}

void CreatureSDF::shutdown() {
    if (sdfProgram_ != 0) {
        glDeleteProgram(sdfProgram_);
        sdfProgram_ = 0;
    }
    if (dotProgram_ != 0) {
        glDeleteProgram(dotProgram_);
        dotProgram_ = 0;
    }
    if (quadVao_ != 0) {
        glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
    if (quadVbo_ != 0) {
        glDeleteBuffers(1, &quadVbo_);
        quadVbo_ = 0;
    }
    if (instanceVbo_ != 0) {
        glDeleteBuffers(1, &instanceVbo_);
        instanceVbo_ = 0;
    }
    if (dataTexture_ != 0) {
        glDeleteTextures(1, &dataTexture_);
        dataTexture_ = 0;
    }
    if (dataBuffer_ != 0) {
        glDeleteBuffers(1, &dataBuffer_);
        dataBuffer_ = 0;
    }
    textureCapacity_ = 0;

    SDL_Log("CreatureSDF: shut down");
}

void CreatureSDF::ensureTextureCapacity(int creatureCount) {
    if (creatureCount <= textureCapacity_) {
        return;
    }

    // Round up to next power-of-two-ish multiple for fewer reallocations
    int newCapacity = std::max(creatureCount, 1024);
    if (newCapacity < textureCapacity_ * 2) {
        newCapacity = textureCapacity_ * 2;
    }

    GLsizeiptr bufferBytes = static_cast<GLsizeiptr>(newCapacity) * kDataStride * sizeof(float);
    glBindBuffer(GL_TEXTURE_BUFFER, dataBuffer_);
    glBufferData(GL_TEXTURE_BUFFER, bufferBytes, nullptr, GL_STREAM_DRAW);

    glBindTexture(GL_TEXTURE_BUFFER, dataTexture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, dataBuffer_);

    textureCapacity_ = newCapacity;
}

void CreatureSDF::render(const Simulation& sim,
                         const SDL_FRect& worldViewport,
                         float cameraZoom,
                         const Vec2& cameraCenter,
                         int drawableWidth,
                         int drawableHeight) {
    // Pack creature data and classify into LOD tiers
    packAndClassify(sim, worldViewport, cameraZoom, cameraCenter, drawableWidth, drawableHeight);

    int fullCount = static_cast<int>(fullInstances_.size());
    int simpleCount = static_cast<int>(simpleInstances_.size());
    int dotCount = static_cast<int>(dotInstances_.size());
    int totalVisible = fullCount + simpleCount + dotCount;

    // Early return if no visible creatures
    if (totalVisible == 0) return;

    // Ensure TBO has enough capacity and upload packed data
    ensureTextureCapacity(totalVisible);
    GLsizeiptr dataBytes = static_cast<GLsizeiptr>(totalVisible) * kDataStride * sizeof(float);
    glBindBuffer(GL_TEXTURE_BUFFER, dataBuffer_);
    glBufferSubData(GL_TEXTURE_BUFFER, 0, dataBytes, packedData_.data());

    // Bind TBO as texture unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, dataTexture_);

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Build combined instance buffer [full...][simple...][dot...]
    allInstances_.clear();
    allInstances_.reserve(static_cast<std::size_t>(totalVisible));
    allInstances_.insert(allInstances_.end(), fullInstances_.begin(), fullInstances_.end());
    allInstances_.insert(allInstances_.end(), simpleInstances_.begin(), simpleInstances_.end());
    allInstances_.insert(allInstances_.end(), dotInstances_.begin(), dotInstances_.end());

    // Upload instance buffer
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    GLsizeiptr instanceBytes = static_cast<GLsizeiptr>(totalVisible) * static_cast<GLsizeiptr>(sizeof(InstanceData));
    glBufferData(GL_ARRAY_BUFFER, instanceBytes, allInstances_.data(), GL_STREAM_DRAW);

    float viewport[2] = {static_cast<float>(drawableWidth), static_cast<float>(drawableHeight)};

    // Helper: issue an instanced draw call for one tier.
    // Since macOS GL 4.1 lacks glDrawArraysInstancedBaseInstance,
    // we rebind the instance attribute with an offset for each tier.
    auto drawTier = [&](GLuint program, int lodTier, int baseInstance, int instanceCount) {
        if (instanceCount == 0) return;

        glUseProgram(program);
        glUniform2f(glGetUniformLocation(program, "uViewport"), viewport[0], viewport[1]);
        glUniform1i(glGetUniformLocation(program, "uCreatureData"), 0);
        glUniform1i(glGetUniformLocation(program, "uLodTier"), lodTier);
        // Tell the shader where this tier's data starts in the TBO.
        // The shader can use: uDataOffset + gl_InstanceID to index into the TBO.
        glUniform1i(glGetUniformLocation(program, "uDataOffset"), baseInstance);

        // Rebind instance attribute with byte offset for this tier
        glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
        auto byteOffset = static_cast<GLsizeiptr>(baseInstance) * static_cast<GLsizeiptr>(sizeof(InstanceData));
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                              reinterpret_cast<const void*>(byteOffset));

        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, instanceCount);
    };

    // Draw each tier
    int baseInstance = 0;

    // Full SDF tier
    drawTier(sdfProgram_, 0, baseInstance, fullCount);
    baseInstance += fullCount;

    // Simple SDF tier
    drawTier(sdfProgram_, 1, baseInstance, simpleCount);
    baseInstance += simpleCount;

    // Dot tier
    drawTier(dotProgram_, 2, baseInstance, dotCount);

    // Clean up — leave GL_BLEND enabled (caller expects it on for the rest of the frame)
    glBindVertexArray(0);
    glUseProgram(0);
}

void CreatureSDF::packAndClassify(const Simulation& sim,
                                  const SDL_FRect& worldViewport,
                                  float cameraZoom,
                                  const Vec2& cameraCenter,
                                  int drawableWidth,
                                  int drawableHeight) {
    const auto& creatures = sim.creatures();
    const float worldW = sim.worldWidth();
    const float worldH = sim.worldHeight();

    // Compute scale factors.
    // worldScale matches Renderer::worldScale(): min(vpW/worldW, vpH/worldH) * zoom
    const float worldScale = std::min(worldViewport.w / std::max(worldW, 1.0f),
                                      worldViewport.h / std::max(worldH, 1.0f))
                           * cameraZoom;
    const float retinaScale = (worldViewport.w > 0.0f)
        ? static_cast<float>(drawableWidth) / worldViewport.w
        : 1.0f;

    const float screenW = static_cast<float>(drawableWidth);
    const float screenH = static_cast<float>(drawableHeight);

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

    // Clear previous frame data
    fullInstances_.clear();
    simpleInstances_.clear();
    dotInstances_.clear();

    // Count alive creatures for buffer sizing
    int aliveCount = 0;
    for (const auto& c : creatures) {
        if (c.alive) ++aliveCount;
    }

    // Temporary storage for classification and cached screen positions
    struct ClassifiedCreature {
        int tier;          // 0=full, 1=simple, 2=dot
        int creatureIdx;
        InstanceData aabb;
        Vec2 segScreen[kBodySegments];
        float segRadiiScreen[kBodySegments];
    };
    std::vector<ClassifiedCreature> classified;
    classified.reserve(static_cast<std::size_t>(aliveCount));

    for (int ci = 0; ci < static_cast<int>(creatures.size()); ++ci) {
        const Creature& c = creatures[static_cast<std::size_t>(ci)];
        if (!c.alive) continue;

        ClassifiedCreature cc;
        cc.creatureIdx = ci;

        // Compute screen positions for all segments
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;

        for (int s = 0; s < kBodySegments; ++s) {
            cc.segScreen[s] = worldToScreen(c.bodyPoints[static_cast<std::size_t>(s)]);
            cc.segRadiiScreen[s] = c.bodyRadii[static_cast<std::size_t>(s)] * worldScale * retinaScale;

            float r = cc.segRadiiScreen[s];
            minX = std::min(minX, cc.segScreen[s].x - r);
            minY = std::min(minY, cc.segScreen[s].y - r);
            maxX = std::max(maxX, cc.segScreen[s].x + r);
            maxY = std::max(maxY, cc.segScreen[s].y + r);
        }

        // Classify into LOD tier by screen-space head diameter
        float headScreenDiameter = 2.0f * cc.segRadiiScreen[0];
        if (headScreenDiameter > kFullSdfThreshold) {
            cc.tier = 0;  // Full SDF
        } else if (headScreenDiameter > kSimpleSdfThreshold) {
            cc.tier = 1;  // Simple SDF
        } else {
            cc.tier = 2;  // Dot
        }

        // Generous padding for appendages (fins, tail, jaw)
        float appendagePad = headScreenDiameter * 1.5f;
        cc.aabb = {minX - appendagePad, minY - appendagePad,
                   maxX + appendagePad, maxY + appendagePad};

        // Cull off-screen creatures
        if (cc.aabb.maxX < 0.0f || cc.aabb.minX > screenW ||
            cc.aabb.maxY < 0.0f || cc.aabb.minY > screenH) {
            continue;
        }

        classified.push_back(cc);
    }

    // Sort by tier so packing is contiguous: [full...][simple...][dot...]
    std::sort(classified.begin(), classified.end(),
              [](const ClassifiedCreature& a, const ClassifiedCreature& b) {
                  return a.tier < b.tier;
              });

    // Resize and zero packed data for actual visible count
    int visibleCount = static_cast<int>(classified.size());
    packedData_.resize(static_cast<std::size_t>(visibleCount) * kDataStride);
    if (visibleCount > 0) {
        std::memset(packedData_.data(), 0, packedData_.size() * sizeof(float));
    }

    // Pack each creature's data and push instance AABBs
    for (int i = 0; i < visibleCount; ++i) {
        const ClassifiedCreature& cc = classified[static_cast<std::size_t>(i)];
        const Creature& c = creatures[static_cast<std::size_t>(cc.creatureIdx)];
        float* d = packedData_.data() + static_cast<std::size_t>(i) * kDataStride;

        // [0-11] Segment screen positions (6 x vec2) — from cached values
        for (int s = 0; s < kBodySegments; ++s) {
            d[s * 2 + 0] = cc.segScreen[s].x;
            d[s * 2 + 1] = cc.segScreen[s].y;
        }

        // [12-17] Segment radii in screen pixels — from cached values
        for (int s = 0; s < kBodySegments; ++s) {
            d[12 + s] = cc.segRadiiScreen[s];
        }

        // [18-19] Head screen position (copy of offsets 0-1)
        d[18] = d[0];
        d[19] = d[1];

        // [20-21] Forward axis (cos(angle), sin(angle))
        d[20] = std::cos(c.angle);
        d[21] = std::sin(c.angle);

        // [22] Heading angle
        d[22] = c.angle;

        // [23] Gait phase
        d[23] = c.gaitPhase;

        // [24] Thrust drive (outputs[0] clamped 0-1)
        d[24] = clamp01(c.outputs[0]);

        // [25] Bite drive (outputs[4] clamped 0-1)
        d[25] = clamp01(c.outputs[4]);

        // [26-31] Segment integrity (1 - segmentDamage/segmentDurability, clamped 0-1)
        for (int s = 0; s < kBodySegments; ++s) {
            float durability = c.traits.segmentDurability[static_cast<std::size_t>(s)];
            float damage = c.segmentDamage[static_cast<std::size_t>(s)];
            float integrity = (durability > 0.0f) ? (1.0f - damage / durability) : 1.0f;
            d[26 + s] = clamp01(integrity);
        }

        // Compute derived traits first (needed for saturation/lightness)
        float plantAffinity = c.genome.ecology.plantAffinity;
        float meatAffinity = c.genome.ecology.meatAffinity;
        float diet = clamp01(meatAffinity / std::max(0.18f, plantAffinity + meatAffinity));
        float repThresh = std::max(56.0f, c.traits.reproductionThreshold);
        float energyLevel = clamp01(c.energy / repThresh);
        float aggression = clamp01(c.genome.ecology.aggression * 0.68f + diet * 0.32f);

        // [32] Hue
        d[32] = c.genome.morphology.hue;

        // [33] Saturation: 0.46 + 0.32 * (armor*0.72 + aggression*0.28)
        float armor = c.genome.morphology.armor;
        d[33] = 0.46f + (0.78f - 0.46f) * (armor * 0.72f + aggression * 0.28f);

        // [34] Lightness: 0.56 + 0.20 * (plantAffinity*0.55 + energyLevel*0.45)
        d[34] = 0.56f + (0.76f - 0.56f) * (plantAffinity * 0.55f + energyLevel * 0.45f);

        // [35] Energy level
        d[35] = energyLevel;

        // [36] Diet
        d[36] = diet;

        // [37] Aggression (blended)
        d[37] = aggression;

        // [38-51] Genome morphology traits (14 values)
        const auto& morph = c.genome.morphology;
        d[38] = morph.finArea;
        d[39] = morph.finPlacement;
        d[40] = morph.tailLength;
        d[41] = morph.tailFlex;
        d[42] = morph.tailFork;
        d[43] = morph.jawLength;
        d[44] = morph.jawArc;
        d[45] = morph.sensorRange;
        d[46] = morph.sensorSpan;   // used for expressiveness
        d[47] = morph.spikes;
        d[48] = morph.pattern;
        d[49] = morph.bodyTaper;
        d[50] = morph.armor;
        d[51] = morph.coreSize;

        // [52-55] Reserved (already zeroed by memset)

        // Push instance AABB into the correct tier vector
        switch (cc.tier) {
            case 0: fullInstances_.push_back(cc.aabb); break;
            case 1: simpleInstances_.push_back(cc.aabb); break;
            case 2: dotInstances_.push_back(cc.aabb); break;
            default: break;
        }
    }
}

}  // namespace alife
