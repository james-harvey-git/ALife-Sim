#include "render/CreatureSDF.hpp"

#include <fstream>
#include <sstream>

#include <SDL2/SDL.h>

namespace alife {

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

void CreatureSDF::initialize(const std::string& shaderDir) {
    sdfProgram_ = loadShaderProgram(
        shaderDir + "/creature_sdf.vert",
        shaderDir + "/creature_sdf.frag");
    dotProgram_ = loadShaderProgram(
        shaderDir + "/creature_dot.vert",
        shaderDir + "/creature_dot.frag");

    if (sdfProgram_ == 0 || dotProgram_ == 0) {
        SDL_Log("CreatureSDF: warning - one or more shader programs failed to load");
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

void CreatureSDF::ensureTextureCapacity(int requiredFloats) {
    if (requiredFloats <= textureCapacity_) {
        return;
    }

    // Round up to next power-of-two-ish multiple for fewer reallocations
    int newCapacity = std::max(requiredFloats, 1024);
    if (newCapacity < textureCapacity_ * 2) {
        newCapacity = textureCapacity_ * 2;
    }

    glBindBuffer(GL_TEXTURE_BUFFER, dataBuffer_);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(newCapacity) * sizeof(float),
                 nullptr,
                 GL_STREAM_DRAW);

    glBindTexture(GL_TEXTURE_BUFFER, dataTexture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, dataBuffer_);

    textureCapacity_ = newCapacity;
}

void CreatureSDF::render(const Simulation& /*sim*/,
                         const SDL_FRect& /*worldViewport*/,
                         float /*cameraZoom*/,
                         const Vec2& /*cameraCenter*/,
                         int /*drawableWidth*/,
                         int /*drawableHeight*/) {
    // Stub: will be implemented in Task 2
}

void CreatureSDF::packAndClassify(const Simulation& /*sim*/,
                                  const SDL_FRect& /*worldViewport*/,
                                  float /*cameraZoom*/,
                                  const Vec2& /*cameraCenter*/) {
    // Stub: will be implemented in Task 2
}

}  // namespace alife
