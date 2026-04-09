#pragma once

#include <string>
#include <vector>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <SDL2/SDL_opengl.h>
#endif

#include "sim/Simulation.hpp"

#include <SDL2/SDL.h>

namespace alife {

class CreatureSDF {
public:
    CreatureSDF() = default;
    ~CreatureSDF();

    bool initialize(const std::string& shaderDir);
    void shutdown();
    void render(const Simulation& sim,
                const SDL_FRect& worldViewport,
                float cameraZoom,
                const Vec2& cameraCenter,
                int drawableWidth,
                int drawableHeight);

private:
    struct InstanceData {
        float minX;
        float minY;
        float maxX;
        float maxY;
    };

    static constexpr int kDataStride = 56;
    static constexpr float kFullSdfThreshold = 30.0f;
    static constexpr float kSimpleSdfThreshold = 8.0f;
    static constexpr float kHysteresis = 2.0f;

    GLuint sdfProgram_ = 0;
    GLuint dotProgram_ = 0;
    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLuint instanceVbo_ = 0;
    GLuint dataBuffer_ = 0;
    GLuint dataTexture_ = 0;
    int textureCapacity_ = 0;

    std::vector<float> packedData_;
    std::vector<InstanceData> fullInstances_;
    std::vector<InstanceData> simpleInstances_;
    std::vector<InstanceData> dotInstances_;

    GLuint loadShaderProgram(const std::string& vertPath, const std::string& fragPath);
    std::string readFile(const std::string& path);
    void ensureTextureCapacity(int creatureCount);
    void packAndClassify(const Simulation& sim,
                         const SDL_FRect& worldViewport,
                         float cameraZoom,
                         const Vec2& cameraCenter,
                         int drawableWidth,
                         int drawableHeight);
};

}  // namespace alife
