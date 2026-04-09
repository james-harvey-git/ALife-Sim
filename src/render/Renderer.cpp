#include "render/Renderer.hpp"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <SDL2/SDL_opengl.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace alife {

struct RendererState {
    SDL_GLContext glContext = nullptr;
    GLuint colorProgram = 0;
    GLuint colorVao = 0;
    GLuint colorVbo = 0;
    GLuint colorEbo = 0;
    GLint colorViewportLocation = -1;
    GLuint textProgram = 0;
    GLuint textVao = 0;
    GLuint textVbo = 0;
    GLuint textEbo = 0;
    GLint textViewportLocation = -1;
    GLint textSamplerLocation = -1;
    int drawableWidth = 0;
    int drawableHeight = 0;

    struct ColorVertex {
        float x;
        float y;
        float r;
        float g;
        float b;
        float a;
    };

    struct TextVertex {
        float x;
        float y;
        float u;
        float v;
        float r;
        float g;
        float b;
        float a;
    };

    std::vector<ColorVertex> colorVertices;
    std::vector<std::uint32_t> colorIndices;
};

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinCameraZoom = 1.0f;
constexpr float kMaxCameraZoom = 14.0f;
constexpr float kDefaultCameraZoom = 6.5f;
constexpr float kLineWidth = 1.2f;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Vec2 operator+(const Vec2& lhs, const Vec2& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

Vec2 operator-(const Vec2& lhs, const Vec2& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

Vec2 operator*(const Vec2& value, float scale) {
    return {value.x * scale, value.y * scale};
}

Vec2 operator/(const Vec2& value, float scale) {
    return {value.x / scale, value.y / scale};
}

float lengthSquared(const Vec2& value) {
    return value.x * value.x + value.y * value.y;
}

float length(const Vec2& value) {
    return std::sqrt(lengthSquared(value));
}

float wrapAxis(float value, float extent) {
    if (extent <= 0.0f) {
        return value;
    }
    value = std::fmod(value, extent);
    if (value < 0.0f) {
        value += extent;
    }
    return value;
}

Vec2 wrapWorldPoint(const Vec2& point, float worldWidth, float worldHeight) {
    return {wrapAxis(point.x, worldWidth), wrapAxis(point.y, worldHeight)};
}

float unwrapAxisNearReference(float value, float reference, float extent) {
    if (extent <= 0.0f) {
        return value;
    }
    const float wraps = std::round((reference - value) / extent);
    return value + wraps * extent;
}

Vec2 unwrapPointNearReference(const Vec2& point, const Vec2& reference, float worldWidth, float worldHeight) {
    return {
        unwrapAxisNearReference(point.x, reference.x, worldWidth),
        unwrapAxisNearReference(point.y, reference.y, worldHeight)
    };
}

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value * 255.0f, 0.0f, 255.0f));
}

SDL_Color hsv(float hue, float saturation, float value, std::uint8_t alpha = 255) {
    hue -= std::floor(hue);
    saturation = std::clamp(saturation, 0.0f, 1.0f);
    value = std::clamp(value, 0.0f, 1.0f);

    const float scaled = hue * 6.0f;
    const int sector = static_cast<int>(std::floor(scaled));
    const float fraction = scaled - static_cast<float>(sector);
    const float p = value * (1.0f - saturation);
    const float q = value * (1.0f - fraction * saturation);
    const float t = value * (1.0f - (1.0f - fraction) * saturation);

    float red = value;
    float green = t;
    float blue = p;

    switch (sector % 6) {
        case 0:
            red = value;
            green = t;
            blue = p;
            break;
        case 1:
            red = q;
            green = value;
            blue = p;
            break;
        case 2:
            red = p;
            green = value;
            blue = t;
            break;
        case 3:
            red = p;
            green = q;
            blue = value;
            break;
        case 4:
            red = t;
            green = p;
            blue = value;
            break;
        default:
            red = value;
            green = p;
            blue = q;
            break;
    }

    return {toByte(red), toByte(green), toByte(blue), alpha};
}

SDL_Color tint(SDL_Color color, float brightnessScale, std::uint8_t alpha = 255) {
    const auto scale = [brightnessScale](std::uint8_t channel) {
        return static_cast<std::uint8_t>(std::clamp(channel * brightnessScale, 0.0f, 255.0f));
    };

    return {scale(color.r), scale(color.g), scale(color.b), alpha};
}

bool pointInRect(int x, int y, const SDL_FRect& rect) {
    const float px = static_cast<float>(x);
    const float py = static_cast<float>(y);
    return px >= rect.x && px <= rect.x + rect.w && py >= rect.y && py <= rect.y + rect.h;
}

float toFloatChannel(std::uint8_t channel) {
    return static_cast<float>(channel) / 255.0f;
}

RendererState::ColorVertex makeColorVertex(const SDL_FPoint& point, SDL_Color color) {
    return {
        point.x,
        point.y,
        toFloatChannel(color.r),
        toFloatChannel(color.g),
        toFloatChannel(color.b),
        toFloatChannel(color.a)
    };
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(0, logLength)), '\0');
    if (logLength > 0) {
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    }
    SDL_Log("OpenGL shader compile failed: %s", log.c_str());
    glDeleteShader(shader);
    return 0;
}

GLuint linkProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader != 0) {
            glDeleteShader(fragmentShader);
        }
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) {
        return program;
    }

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(0, logLength)), '\0');
    if (logLength > 0) {
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
    }
    SDL_Log("OpenGL program link failed: %s", log.c_str());
    glDeleteProgram(program);
    return 0;
}

bool initializeBackend(RendererState* renderer) {
    constexpr const char* kColorVertexShader = R"glsl(
        #version 410
        in vec2 aPosition;
        in vec4 aColor;
        uniform vec2 uViewport;
        out vec4 vColor;

        void main() {
            vec2 ndc = vec2(
                (aPosition.x / uViewport.x) * 2.0 - 1.0,
                1.0 - (aPosition.y / uViewport.y) * 2.0
            );
            gl_Position = vec4(ndc, 0.0, 1.0);
            vColor = aColor;
        }
    )glsl";

    constexpr const char* kColorFragmentShader = R"glsl(
        #version 410
        in vec4 vColor;
        out vec4 fragColor;

        void main() {
            fragColor = vColor;
        }
    )glsl";

    constexpr const char* kTextVertexShader = R"glsl(
        #version 410
        in vec2 aPosition;
        in vec2 aTexCoord;
        in vec4 aColor;
        uniform vec2 uViewport;
        out vec2 vTexCoord;
        out vec4 vColor;

        void main() {
            vec2 ndc = vec2(
                (aPosition.x / uViewport.x) * 2.0 - 1.0,
                1.0 - (aPosition.y / uViewport.y) * 2.0
            );
            gl_Position = vec4(ndc, 0.0, 1.0);
            vTexCoord = aTexCoord;
            vColor = aColor;
        }
    )glsl";

    constexpr const char* kTextFragmentShader = R"glsl(
        #version 410
        uniform sampler2D uTexture;
        in vec2 vTexCoord;
        in vec4 vColor;
        out vec4 fragColor;

        void main() {
            fragColor = texture(uTexture, vTexCoord) * vColor;
        }
    )glsl";

    renderer->colorProgram = linkProgram(kColorVertexShader, kColorFragmentShader);
    renderer->textProgram = linkProgram(kTextVertexShader, kTextFragmentShader);
    if (renderer->colorProgram == 0 || renderer->textProgram == 0) {
        return false;
    }

    renderer->colorViewportLocation = glGetUniformLocation(renderer->colorProgram, "uViewport");
    renderer->textViewportLocation = glGetUniformLocation(renderer->textProgram, "uViewport");
    renderer->textSamplerLocation = glGetUniformLocation(renderer->textProgram, "uTexture");
    const GLint colorPositionLocation = glGetAttribLocation(renderer->colorProgram, "aPosition");
    const GLint colorVertexColorLocation = glGetAttribLocation(renderer->colorProgram, "aColor");
    const GLint textPositionLocation = glGetAttribLocation(renderer->textProgram, "aPosition");
    const GLint textUvLocation = glGetAttribLocation(renderer->textProgram, "aTexCoord");
    const GLint textVertexColorLocation = glGetAttribLocation(renderer->textProgram, "aColor");
    if (colorPositionLocation < 0 || colorVertexColorLocation < 0 || textPositionLocation < 0 || textUvLocation < 0
        || textVertexColorLocation < 0) {
        SDL_Log("OpenGL attribute lookup failed.");
        return false;
    }

    glGenVertexArrays(1, &renderer->colorVao);
    glGenBuffers(1, &renderer->colorVbo);
    glGenBuffers(1, &renderer->colorEbo);
    glBindVertexArray(renderer->colorVao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->colorVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->colorEbo);
    glEnableVertexAttribArray(static_cast<GLuint>(colorPositionLocation));
    glVertexAttribPointer(
        static_cast<GLuint>(colorPositionLocation),
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RendererState::ColorVertex),
        reinterpret_cast<void*>(offsetof(RendererState::ColorVertex, x))
    );
    glEnableVertexAttribArray(static_cast<GLuint>(colorVertexColorLocation));
    glVertexAttribPointer(
        static_cast<GLuint>(colorVertexColorLocation),
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RendererState::ColorVertex),
        reinterpret_cast<void*>(offsetof(RendererState::ColorVertex, r))
    );

    glGenVertexArrays(1, &renderer->textVao);
    glGenBuffers(1, &renderer->textVbo);
    glGenBuffers(1, &renderer->textEbo);
    glBindVertexArray(renderer->textVao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->textVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->textEbo);
    glEnableVertexAttribArray(static_cast<GLuint>(textPositionLocation));
    glVertexAttribPointer(
        static_cast<GLuint>(textPositionLocation),
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RendererState::TextVertex),
        reinterpret_cast<void*>(offsetof(RendererState::TextVertex, x))
    );
    glEnableVertexAttribArray(static_cast<GLuint>(textUvLocation));
    glVertexAttribPointer(
        static_cast<GLuint>(textUvLocation),
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RendererState::TextVertex),
        reinterpret_cast<void*>(offsetof(RendererState::TextVertex, u))
    );
    glEnableVertexAttribArray(static_cast<GLuint>(textVertexColorLocation));
    glVertexAttribPointer(
        static_cast<GLuint>(textVertexColorLocation),
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RendererState::TextVertex),
        reinterpret_cast<void*>(offsetof(RendererState::TextVertex, r))
    );

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return true;
}

void destroyBackend(RendererState* renderer) {
    if (renderer == nullptr) {
        return;
    }

    if (renderer->colorProgram != 0) {
        glDeleteProgram(renderer->colorProgram);
        renderer->colorProgram = 0;
    }
    if (renderer->textProgram != 0) {
        glDeleteProgram(renderer->textProgram);
        renderer->textProgram = 0;
    }
    if (renderer->colorVbo != 0) {
        glDeleteBuffers(1, &renderer->colorVbo);
        renderer->colorVbo = 0;
    }
    if (renderer->colorEbo != 0) {
        glDeleteBuffers(1, &renderer->colorEbo);
        renderer->colorEbo = 0;
    }
    if (renderer->colorVao != 0) {
        glDeleteVertexArrays(1, &renderer->colorVao);
        renderer->colorVao = 0;
    }
    if (renderer->textVbo != 0) {
        glDeleteBuffers(1, &renderer->textVbo);
        renderer->textVbo = 0;
    }
    if (renderer->textEbo != 0) {
        glDeleteBuffers(1, &renderer->textEbo);
        renderer->textEbo = 0;
    }
    if (renderer->textVao != 0) {
        glDeleteVertexArrays(1, &renderer->textVao);
        renderer->textVao = 0;
    }
}

void beginFrame(RendererState* renderer, SDL_Window* window) {
    SDL_GL_MakeCurrent(window, renderer->glContext);
    SDL_GL_GetDrawableSize(window, &renderer->drawableWidth, &renderer->drawableHeight);
    glViewport(0, 0, renderer->drawableWidth, renderer->drawableHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(11.0f / 255.0f, 14.0f / 255.0f, 20.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    renderer->colorVertices.clear();
    renderer->colorIndices.clear();
}

void flushColored(RendererState* renderer) {
    if (renderer == nullptr || renderer->colorIndices.empty()) {
        return;
    }

    glUseProgram(renderer->colorProgram);
    glUniform2f(renderer->colorViewportLocation, static_cast<float>(renderer->drawableWidth), static_cast<float>(renderer->drawableHeight));
    glBindVertexArray(renderer->colorVao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->colorVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(renderer->colorVertices.size() * sizeof(RendererState::ColorVertex)),
        renderer->colorVertices.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->colorEbo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(renderer->colorIndices.size() * sizeof(std::uint32_t)),
        renderer->colorIndices.data(),
        GL_DYNAMIC_DRAW
    );
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(renderer->colorIndices.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    renderer->colorVertices.clear();
    renderer->colorIndices.clear();
}

void setClipRect(RendererState* renderer, const SDL_Rect* rect) {
    flushColored(renderer);
    if (rect == nullptr) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(rect->x, renderer->drawableHeight - (rect->y + rect->h), rect->w, rect->h);
}

void appendColoredGeometry(
    RendererState* renderer,
    const std::vector<SDL_FPoint>& vertices,
    const std::vector<std::uint32_t>& indices,
    SDL_Color color
) {
    if (renderer == nullptr || vertices.empty() || indices.empty()) {
        return;
    }

    const std::uint32_t baseIndex = static_cast<std::uint32_t>(renderer->colorVertices.size());
    renderer->colorVertices.reserve(renderer->colorVertices.size() + vertices.size());
    renderer->colorIndices.reserve(renderer->colorIndices.size() + indices.size());
    for (const SDL_FPoint& point : vertices) {
        renderer->colorVertices.push_back(makeColorVertex(point, color));
    }
    for (std::uint32_t index : indices) {
        renderer->colorIndices.push_back(baseIndex + index);
    }
}

void fillRect(RendererState* renderer, const SDL_FRect& rect, SDL_Color color) {
    const std::vector<SDL_FPoint> vertices {
        {rect.x, rect.y},
        {rect.x + rect.w, rect.y},
        {rect.x + rect.w, rect.y + rect.h},
        {rect.x, rect.y + rect.h}
    };
    const std::vector<std::uint32_t> indices {0, 1, 2, 0, 2, 3};
    appendColoredGeometry(renderer, vertices, indices, color);
}

void fillEllipse(RendererState* renderer, float cx, float cy, float rx, float ry, SDL_Color color) {
    constexpr int kSegments = 28;
    std::vector<SDL_FPoint> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(kSegments + 2);
    indices.reserve(kSegments * 3);

    vertices.push_back({cx, cy});
    for (int index = 0; index <= kSegments; ++index) {
        const float angle = static_cast<float>(index) / static_cast<float>(kSegments) * kPi * 2.0f;
        vertices.push_back({cx + std::cos(angle) * rx, cy + std::sin(angle) * ry});
        if (index > 0) {
            indices.push_back(0);
            indices.push_back(static_cast<std::uint32_t>(index));
            indices.push_back(static_cast<std::uint32_t>(index + 1));
        }
    }

    appendColoredGeometry(renderer, vertices, indices, color);
}

void drawLine(RendererState* renderer, SDL_FPoint a, SDL_FPoint b, SDL_Color color) {
    Vec2 delta {b.x - a.x, b.y - a.y};
    const float deltaLength = length(delta);
    if (deltaLength < 1e-4f) {
        return;
    }

    const Vec2 tangent = delta / deltaLength;
    const Vec2 normal {-tangent.y, tangent.x};
    const Vec2 offset = normal * (kLineWidth * 0.5f);
    const std::vector<SDL_FPoint> vertices {
        {a.x + offset.x, a.y + offset.y},
        {a.x - offset.x, a.y - offset.y},
        {b.x - offset.x, b.y - offset.y},
        {b.x + offset.x, b.y + offset.y}
    };
    const std::vector<std::uint32_t> indices {0, 1, 2, 0, 2, 3};
    appendColoredGeometry(renderer, vertices, indices, color);
}

void renderLabel(RendererState* renderer, TTF_Font* font, float x, float y, const std::string& text, SDL_Color color) {
    if (renderer == nullptr || font == nullptr || text.empty()) {
        return;
    }

    flushColored(renderer);

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (surface == nullptr) {
        return;
    }

    SDL_Surface* rgbaSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);
    if (rgbaSurface == nullptr) {
        return;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        rgbaSurface->w,
        rgbaSurface->h,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgbaSurface->pixels
    );

    const std::array<RendererState::TextVertex, 4> vertices {{
        {x, y, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {x + static_cast<float>(rgbaSurface->w), y, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {x + static_cast<float>(rgbaSurface->w), y + static_cast<float>(rgbaSurface->h), 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {x, y + static_cast<float>(rgbaSurface->h), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}
    }};
    const std::array<std::uint32_t, 6> indices {0, 1, 2, 0, 2, 3};

    glUseProgram(renderer->textProgram);
    glUniform2f(renderer->textViewportLocation, static_cast<float>(renderer->drawableWidth), static_cast<float>(renderer->drawableHeight));
    glUniform1i(renderer->textSamplerLocation, 0);
    glBindVertexArray(renderer->textVao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->textVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->textEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices.data(), GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glDeleteTextures(1, &texture);
    SDL_FreeSurface(rgbaSurface);
}

void presentFrame(RendererState* renderer, SDL_Window* window) {
    flushColored(renderer);
    SDL_GL_SwapWindow(window);
}

std::string formatFloat(float value, int precision = 1) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::optional<std::string> findFontPath() {
    constexpr std::array<const char*, 5> candidates {{
        "/System/Library/Fonts/Supplemental/Avenir Next.ttc",
        "/System/Library/Fonts/Supplemental/Helvetica.ttc",
        "/System/Library/Fonts/SFNS.ttf",
        "/Library/Fonts/Arial Unicode.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    }};

    for (const char* candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return std::string(candidate);
        }
    }
    return std::nullopt;
}

}  // namespace

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::initialize() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return false;
    }
    if (TTF_Init() != 0) {
        SDL_Quit();
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

    window_ = SDL_CreateWindow(
        "ALife Sim",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        kWindowWidth,
        kWindowHeight,
        SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL
    );
    if (window_ == nullptr) {
        shutdown();
        return false;
    }

    renderer_ = new RendererState {};
    renderer_->glContext = SDL_GL_CreateContext(window_);
    if (renderer_->glContext == nullptr) {
        shutdown();
        return false;
    }

    SDL_GL_MakeCurrent(window_, renderer_->glContext);
    SDL_GL_SetSwapInterval(1);
    if (!initializeBackend(renderer_)) {
        shutdown();
        return false;
    }

    if (const auto fontPath = findFontPath(); fontPath.has_value()) {
        titleFont_ = TTF_OpenFont(fontPath->c_str(), 20);
        font_ = TTF_OpenFont(fontPath->c_str(), 16);
        smallFont_ = TTF_OpenFont(fontPath->c_str(), 13);
    }

    if (!creatureSdf_.initialize(ALIFE_SHADER_DIR)) {
        SDL_Log("CreatureSDF initialization failed");
        shutdown();
        return false;
    }

    return true;
}

void Renderer::shutdown() {
    creatureSdf_.shutdown();
    if (titleFont_ != nullptr) {
        TTF_CloseFont(titleFont_);
        titleFont_ = nullptr;
    }
    if (font_ != nullptr) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
    if (smallFont_ != nullptr) {
        TTF_CloseFont(smallFont_);
        smallFont_ = nullptr;
    }
    if (renderer_ != nullptr) {
        if (renderer_->glContext != nullptr) {
            SDL_GL_MakeCurrent(window_, renderer_->glContext);
            destroyBackend(renderer_);
            SDL_GL_DeleteContext(renderer_->glContext);
            renderer_->glContext = nullptr;
        }
        delete renderer_;
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    if (TTF_WasInit()) {
        TTF_Quit();
    }
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        SDL_Quit();
    }
}

void Renderer::updateViewport(const Simulation& simulation) {
    (void)simulation;
    worldViewport_.x = static_cast<float>(kMargin);
    worldViewport_.y = static_cast<float>(kMargin);
    worldViewport_.w = static_cast<float>(kWindowWidth - kPanelWidth - kMargin * 3);
    worldViewport_.h = static_cast<float>(kWindowHeight - kMargin * 2);
}

void Renderer::resetCamera(const Simulation& simulation) {
    cameraZoom_ = kDefaultCameraZoom;
    cameraInitialized_ = true;
    focusSelection(simulation, true);
}

float Renderer::worldScale(const Simulation& simulation) const {
    return std::min(worldViewport_.w / simulation.worldWidth(), worldViewport_.h / simulation.worldHeight()) * cameraZoom_;
}

Vec2 Renderer::visibleWorldExtents(const Simulation& simulation) const {
    const float scale = worldScale(simulation);
    if (scale <= 1e-5f) {
        return {simulation.worldWidth(), simulation.worldHeight()};
    }
    return {
        worldViewport_.w / scale,
        worldViewport_.h / scale
    };
}

void Renderer::focusSelection(const Simulation& simulation, bool snap) {
    const CreatureSnapshot selected = simulation.selectedCreatureSnapshot();
    if (selected.valid) {
        if (snap || !cameraInitialized_) {
            cameraCenter_ = selected.position;
        } else {
            cameraCenter_ = unwrapPointNearReference(
                selected.position,
                cameraCenter_,
                simulation.worldWidth(),
                simulation.worldHeight()
            );
        }
        cameraInitialized_ = true;
        return;
    }

    if (!cameraInitialized_ || snap) {
        cameraCenter_ = {simulation.worldWidth() * 0.5f, simulation.worldHeight() * 0.5f};
        cameraInitialized_ = true;
    }
}

void Renderer::toggleFollowSelection(const Simulation& simulation) {
    followSelection_ = !followSelection_;
    if (followSelection_) {
        focusSelection(simulation, true);
    }
}

void Renderer::toggleBrainOverlay() {
    showBrainOverlay_ = !showBrainOverlay_;
}

void Renderer::zoomView(float zoomSteps, const Simulation& simulation, std::optional<SDL_Point> anchor) {
    if (!cameraInitialized_) {
        resetCamera(simulation);
    }
    std::optional<Vec2> anchorBefore;
    if (anchor.has_value()) {
        anchorBefore = screenToWorld(anchor->x, anchor->y, simulation);
    }
    const float factor = std::pow(1.18f, zoomSteps);
    cameraZoom_ = std::clamp(cameraZoom_ * factor, kMinCameraZoom, kMaxCameraZoom);

    if (anchorBefore.has_value()) {
        const float scale = worldScale(simulation);
        if (scale > 1e-5f) {
            const Vec2 targetBefore = unwrapPointNearReference(
                *anchorBefore,
                cameraCenter_,
                simulation.worldWidth(),
                simulation.worldHeight()
            );
            const Vec2 anchorAfter {
                cameraCenter_.x + (static_cast<float>(anchor->x) - (worldViewport_.x + worldViewport_.w * 0.5f)) / scale,
                cameraCenter_.y + (static_cast<float>(anchor->y) - (worldViewport_.y + worldViewport_.h * 0.5f)) / scale
            };
            cameraCenter_ = cameraCenter_ + (targetBefore - anchorAfter);
        }
    }
}

void Renderer::updateCamera(const Simulation& simulation) {
    if (!cameraInitialized_) {
        resetCamera(simulation);
    }
    if (followSelection_ && !draggingWorld_) {
        focusSelection(simulation, false);
    }
}

Vec2 Renderer::wrappedPositionNearCamera(const Vec2& point, const Simulation& simulation) const {
    return unwrapPointNearReference(point, cameraCenter_, simulation.worldWidth(), simulation.worldHeight());
}

SDL_FPoint Renderer::worldToScreen(const Vec2& world, const Simulation& simulation) const {
    const float scale = worldScale(simulation);
    const Vec2 wrapped = wrappedPositionNearCamera(world, simulation);
    const Vec2 delta {wrapped.x - cameraCenter_.x, wrapped.y - cameraCenter_.y};
    return {
        worldViewport_.x + worldViewport_.w * 0.5f + delta.x * scale,
        worldViewport_.y + worldViewport_.h * 0.5f + delta.y * scale
    };
}

bool Renderer::screenPointInWorld(int screenX, int screenY) const {
    return screenX >= worldViewport_.x
        && screenX <= worldViewport_.x + worldViewport_.w
        && screenY >= worldViewport_.y
        && screenY <= worldViewport_.y + worldViewport_.h;
}

std::optional<Vec2> Renderer::screenToWorld(int screenX, int screenY, const Simulation& simulation) const {
    if (!screenPointInWorld(screenX, screenY)) {
        return std::nullopt;
    }

    const float scale = worldScale(simulation);
    if (scale <= 1e-5f) {
        return std::nullopt;
    }
    return wrapWorldPoint(
        {
            cameraCenter_.x + (static_cast<float>(screenX) - (worldViewport_.x + worldViewport_.w * 0.5f)) / scale,
            cameraCenter_.y + (static_cast<float>(screenY) - (worldViewport_.y + worldViewport_.h * 0.5f)) / scale
        },
        simulation.worldWidth(),
        simulation.worldHeight()
    );
}

void Renderer::beginWorldDrag(int screenX, int screenY) {
    draggingWorld_ = true;
    dragStartScreenX_ = screenX;
    dragStartScreenY_ = screenY;
    dragStartCamera_ = cameraCenter_;
    followSelection_ = false;
}

void Renderer::updateWorldDrag(int screenX, int screenY, const Simulation& simulation) {
    if (!draggingWorld_) {
        return;
    }
    const float scale = worldScale(simulation);
    if (scale <= 1e-5f) {
        return;
    }
    cameraCenter_ = {
        dragStartCamera_.x - (static_cast<float>(screenX - dragStartScreenX_) / scale),
        dragStartCamera_.y - (static_cast<float>(screenY - dragStartScreenY_) / scale)
    };
    cameraInitialized_ = true;
}

void Renderer::endWorldDrag() {
    draggingWorld_ = false;
}

bool Renderer::isDraggingWorld() const {
    return draggingWorld_;
}

void Renderer::panCameraWorld(const Vec2& delta) {
    if (std::abs(delta.x) <= 1e-5f && std::abs(delta.y) <= 1e-5f) {
        return;
    }
    cameraCenter_ = cameraCenter_ + delta;
    cameraInitialized_ = true;
    followSelection_ = false;
}

float Renderer::zoomLevel() const {
    return cameraZoom_;
}

bool Renderer::screenPointInSelection(int screenX, int screenY) const {
    return pointInRect(screenX, screenY, selectionViewport_);
}

void Renderer::cycleDebugOverlay() {
    switch (debugOverlay_) {
        case DebugOverlay::None:
            debugOverlay_ = DebugOverlay::Nutrient;
            break;
        case DebugOverlay::Nutrient:
            debugOverlay_ = DebugOverlay::Shelter;
            break;
        case DebugOverlay::Shelter:
            debugOverlay_ = DebugOverlay::Shear;
            break;
        default:
            debugOverlay_ = DebugOverlay::None;
            break;
    }
}

void Renderer::scrollSelection(float deltaPixels) {
    selectionScroll_ = std::max(0.0f, selectionScroll_ + deltaPixels);
}

const char* Renderer::debugOverlayLabel() const {
    switch (debugOverlay_) {
        case DebugOverlay::Nutrient:
            return "nutrient";
        case DebugOverlay::Shelter:
            return "lee shelter";
        case DebugOverlay::Shear:
            return "shear";
        default:
            return "off";
    }
}

Renderer::UiAction Renderer::uiActionAt(int screenX, int screenY) const {
    if (pointInRect(screenX, screenY, randomSelectButton_)) {
        return UiAction::SelectRandom;
    }
    if (pointInRect(screenX, screenY, topEnergyButton_)) {
        return UiAction::SelectTopEnergy;
    }
    if (pointInRect(screenX, screenY, dominantLineageButton_)) {
        return UiAction::SelectDominantLineage;
    }
    if (pointInRect(screenX, screenY, newestLineageButton_)) {
        return UiAction::SelectNewestLineage;
    }
    if (pointInRect(screenX, screenY, overlayButton_)) {
        return UiAction::CycleOverlay;
    }
    return UiAction::None;
}

void Renderer::draw(const Simulation& simulation, bool paused, int timeScale) {
    if (renderer_ == nullptr) {
        return;
    }

    beginFrame(renderer_, window_);
    updateViewport(simulation);
    updateCamera(simulation);
    selectionViewport_ = {0.0f, 0.0f, 0.0f, 0.0f};
    randomSelectButton_ = {0.0f, 0.0f, 0.0f, 0.0f};
    topEnergyButton_ = {0.0f, 0.0f, 0.0f, 0.0f};
    dominantLineageButton_ = {0.0f, 0.0f, 0.0f, 0.0f};
    newestLineageButton_ = {0.0f, 0.0f, 0.0f, 0.0f};
    overlayButton_ = {0.0f, 0.0f, 0.0f, 0.0f};

    const float scale = worldScale(simulation);
    const float worldScale = scale;
    const float visibleWorldWidth = worldViewport_.w / scale;
    const float visibleWorldHeight = worldViewport_.h / scale;

    auto drawText = [&](TTF_Font* font, float x, float y, const std::string& text, SDL_Color color) {
        renderLabel(renderer_, font, x, y, text, color);
    };

    const Stats& stats = simulation.stats();
    const auto info = simulation.selectionInfo();
    const auto& history = simulation.history();
    const std::uint64_t currentSelectionId = simulation.selectedCreature();
    if (currentSelectionId != lastSelectedCreatureId_) {
        selectionScroll_ = 0.0f;
        if (followSelection_) {
            focusSelection(simulation, true);
        }
        lastSelectedCreatureId_ = currentSelectionId;
    }

    auto drawCard = [&](const SDL_FRect& rect, const std::string& title) {
        fillRect(renderer_, rect, {22, 28, 37, 255});
        fillRect(renderer_, {rect.x, rect.y, rect.w, 28.0f}, {28, 40, 53, 255});
        drawText(font_, rect.x + 10.0f, rect.y + 5.0f, title, {224, 232, 240, 255});
    };

    auto drawButton = [&](const SDL_FRect& rect, const std::string& label, SDL_Color fill, SDL_Color text) {
        fillRect(renderer_, rect, fill);
        fillRect(renderer_, {rect.x, rect.y + rect.h - 2.0f, rect.w, 2.0f}, tint(fill, 1.18f, 255));
        drawText(smallFont_, rect.x + 9.0f, rect.y + 4.0f, label, text);
    };

    auto drawSeries = [&](const SDL_FRect& rect, auto getter, float maxValue, SDL_Color color) {
        const auto& history = simulation.history();
        if (history.size() < 2 || maxValue <= 0.0f) {
            return;
        }
        for (std::size_t index = 1; index < history.size(); ++index) {
            const float x1 = rect.x + rect.w * (static_cast<float>(index - 1) / static_cast<float>(history.size() - 1));
            const float x2 = rect.x + rect.w * (static_cast<float>(index) / static_cast<float>(history.size() - 1));
            const float y1 = rect.y + rect.h - (getter(history[index - 1]) / maxValue) * rect.h;
            const float y2 = rect.y + rect.h - (getter(history[index]) / maxValue) * rect.h;
            drawLine(renderer_, {x1, y1}, {x2, y2}, color);
        }
    };

    auto drawBucketStrip = [&](float x, float y, float w, float h, const std::array<float, kSensorBuckets>& values, SDL_Color color) {
        const float cellW = (w - 4.0f) / static_cast<float>(kSensorBuckets);
        for (int bucket = 0; bucket < kSensorBuckets; ++bucket) {
            const SDL_FRect bg {x + bucket * cellW, y, cellW - 2.0f, h};
            fillRect(renderer_, bg, {34, 43, 56, 255});
            fillRect(
                renderer_,
                {bg.x, bg.y + (1.0f - clamp01(values[bucket])) * bg.h, bg.w, clamp01(values[bucket]) * bg.h},
                color
            );
        }
    };

    auto drawBrainOverlay = [&](const SelectionInfo& info) {
        if (!info.valid) {
            return;
        }

        const SDL_FRect overlay {
            worldViewport_.x + 16.0f,
            worldViewport_.y + 16.0f,
            std::min(360.0f, worldViewport_.w * 0.42f),
            std::min(250.0f, worldViewport_.h * 0.34f)
        };
        fillRect(renderer_, overlay, {12, 16, 22, 220});
        fillRect(renderer_, {overlay.x, overlay.y, overlay.w, 26.0f}, {23, 33, 45, 232});
        drawText(font_, overlay.x + 10.0f, overlay.y + 4.0f, "Brain Topology", {235, 240, 244, 255});
        drawText(
            smallFont_,
            overlay.x + overlay.w - 112.0f,
            overlay.y + 7.0f,
            "h" + std::to_string(info.brainHiddenCount) + "  c" + std::to_string(info.brainConnectionCount),
            {156, 176, 196, 255}
        );

        const SDL_FRect graph {
            overlay.x + 10.0f,
            overlay.y + 34.0f,
            overlay.w - 20.0f,
            overlay.h - 58.0f
        };
        fillRect(renderer_, graph, {16, 21, 28, 210});

        const float inputX = graph.x + 18.0f;
        const float memoryX = graph.x + graph.w * 0.32f;
        const float hiddenX = graph.x + graph.w * 0.62f;
        const float outputX = graph.x + graph.w - 22.0f;

        const auto nodeY = [&](int index, int count) {
            if (count <= 1) {
                return graph.y + graph.h * 0.5f;
            }
            const float t = static_cast<float>(index) / static_cast<float>(count - 1);
            return graph.y + 8.0f + t * (graph.h - 16.0f);
        };

        const auto valueForNode = [&](BrainGenome::NodeKind kind, int index) {
            switch (kind) {
                case BrainGenome::NodeKind::Input:
                    return info.inputs[index];
                case BrainGenome::NodeKind::Memory:
                    return info.memory[index];
                case BrainGenome::NodeKind::Hidden:
                    return info.hiddenActivations[index];
                default:
                    return info.outputs[index];
            }
        };

        const auto pointForNode = [&](BrainGenome::NodeKind kind, int index) {
            switch (kind) {
                case BrainGenome::NodeKind::Input:
                    return SDL_FPoint {inputX, nodeY(index, kInputCount)};
                case BrainGenome::NodeKind::Memory:
                    return SDL_FPoint {memoryX, nodeY(index, kMemorySize)};
                case BrainGenome::NodeKind::Hidden:
                    return SDL_FPoint {hiddenX, nodeY(index, std::max(1, info.brainHiddenCount))};
                default:
                    return SDL_FPoint {outputX, nodeY(index, kOutputCount)};
            }
        };

        drawText(smallFont_, graph.x + 4.0f, graph.y - 1.0f, "inputs", {112, 142, 170, 255});
        drawText(smallFont_, memoryX - 18.0f, graph.y - 1.0f, "mem", {112, 142, 170, 255});
        drawText(smallFont_, hiddenX - 20.0f, graph.y - 1.0f, "hidden", {112, 142, 170, 255});
        drawText(smallFont_, outputX - 25.0f, graph.y - 1.0f, "out", {112, 142, 170, 255});

        for (int connectionIndex = 0; connectionIndex < info.brainConnectionCount; ++connectionIndex) {
            const auto& connection = info.connections[connectionIndex];
            const SDL_FPoint start = pointForNode(connection.fromKind, connection.fromIndex);
            const SDL_FPoint end = pointForNode(connection.toKind, connection.toIndex);
            const float magnitude = clamp01(std::abs(connection.weight) / 2.0f);
            const float sourceDrive = clamp01(std::abs(valueForNode(connection.fromKind, connection.fromIndex)));
            const std::uint8_t alpha = static_cast<std::uint8_t>(28 + 118.0f * std::max(magnitude, sourceDrive * 0.75f));
            const SDL_Color color = connection.weight >= 0.0f
                ? SDL_Color {99, 211, 166, alpha}
                : SDL_Color {233, 128, 104, alpha};
            drawLine(renderer_, start, end, color);
        }

        const auto drawNode = [&](BrainGenome::NodeKind kind, int index, float radius) {
            const float value = valueForNode(kind, index);
            const SDL_FPoint point = pointForNode(kind, index);
            const float normalized = kind == BrainGenome::NodeKind::Input
                ? clamp01(value)
                : clamp01(value * 0.5f + 0.5f);
            const SDL_Color fill = kind == BrainGenome::NodeKind::Input
                ? hsv(0.55f - normalized * 0.08f, 0.5f, 0.36f + normalized * 0.5f, 240)
                : (value >= 0.0f
                    ? SDL_Color {static_cast<std::uint8_t>(72 + normalized * 88.0f), static_cast<std::uint8_t>(140 + normalized * 80.0f), 173, 248}
                    : SDL_Color {214, static_cast<std::uint8_t>(94 + normalized * 60.0f), static_cast<std::uint8_t>(118 + normalized * 68.0f), 248});
            fillEllipse(renderer_, point.x, point.y, radius, radius, fill);
            fillEllipse(renderer_, point.x, point.y, radius * 0.45f, radius * 0.45f, {239, 244, 247, 56});
        };

        for (int inputIndex = 0; inputIndex < kInputCount; ++inputIndex) {
            drawNode(BrainGenome::NodeKind::Input, inputIndex, 2.2f);
        }
        for (int memoryIndex = 0; memoryIndex < kMemorySize; ++memoryIndex) {
            drawNode(BrainGenome::NodeKind::Memory, memoryIndex, 3.2f);
        }
        for (int hiddenIndex = 0; hiddenIndex < info.brainHiddenCount; ++hiddenIndex) {
            drawNode(BrainGenome::NodeKind::Hidden, hiddenIndex, 3.6f);
        }
        constexpr std::array<const char*, kOutputCount> overlayOutputLabels {"turn", "thrust", "graze", "bite", "sig", "split"};
        for (int outputIndex = 0; outputIndex < kOutputCount; ++outputIndex) {
            drawNode(BrainGenome::NodeKind::Output, outputIndex, 4.2f);
            const SDL_FPoint point = pointForNode(BrainGenome::NodeKind::Output, outputIndex);
            drawText(smallFont_, point.x + 8.0f, point.y - 6.0f, overlayOutputLabels[outputIndex], {210, 219, 226, 255});
        }

        fillRect(renderer_, {overlay.x, overlay.y + overlay.h - 18.0f, overlay.w, 18.0f}, {20, 28, 36, 226});
        drawText(
            smallFont_,
            overlay.x + 8.0f,
            overlay.y + overlay.h - 15.0f,
            "green excite  red inhibit  node brightness = live activation",
            {152, 168, 184, 255}
        );
    };

    fillRect(renderer_, {0.0f, 0.0f, static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)}, {11, 14, 20, 255});
    fillRect(renderer_, worldViewport_, {20, 27, 36, 255});

    const SDL_Rect worldClip {
        static_cast<int>(std::floor(worldViewport_.x)),
        static_cast<int>(std::floor(worldViewport_.y)),
        static_cast<int>(std::ceil(worldViewport_.w)),
        static_cast<int>(std::ceil(worldViewport_.h))
    };
    setClipRect(renderer_, &worldClip);

    constexpr int gridColumns = 26;
    constexpr int gridRows = 18;
    for (int row = 0; row < gridRows; ++row) {
        for (int column = 0; column < gridColumns; ++column) {
            const float tx = (static_cast<float>(column) + 0.5f) / static_cast<float>(gridColumns) - 0.5f;
            const float ty = (static_cast<float>(row) + 0.5f) / static_cast<float>(gridRows) - 0.5f;
            const Vec2 sample = wrapWorldPoint(
                {
                    cameraCenter_.x + tx * visibleWorldWidth,
                    cameraCenter_.y + ty * visibleWorldHeight
                },
                simulation.worldWidth(),
                simulation.worldHeight()
            );
            const float nutrient = simulation.sampleNutrient(sample.x, sample.y);
            const Vec2 current = simulation.sampleCurrent(sample.x, sample.y);
            SDL_Color color = hsv(0.47f + nutrient * 0.1f, 0.42f, 0.16f + nutrient * 0.22f, static_cast<std::uint8_t>(32 + nutrient * 60.0f));
            const SDL_FPoint center = worldToScreen(sample, simulation);

            const SDL_FRect cell {
                center.x - worldViewport_.w / static_cast<float>(gridColumns) * 0.5f,
                center.y - worldViewport_.h / static_cast<float>(gridRows) * 0.5f,
                worldViewport_.w / static_cast<float>(gridColumns) + 1.0f,
                worldViewport_.h / static_cast<float>(gridRows) + 1.0f
            };
            fillRect(renderer_, cell, color);

            const SDL_FPoint arrow {
                center.x + current.x * 0.015f * worldScale,
                center.y + current.y * 0.015f * worldScale
            };
            drawLine(renderer_, center, arrow, {92, 156, 178, 64});
        }
    }

    if (debugOverlay_ != DebugOverlay::None) {
        constexpr int overlayColumns = 32;
        constexpr int overlayRows = 24;
        for (int row = 0; row < overlayRows; ++row) {
            for (int column = 0; column < overlayColumns; ++column) {
                const float tx = (static_cast<float>(column) + 0.5f) / static_cast<float>(overlayColumns) - 0.5f;
                const float ty = (static_cast<float>(row) + 0.5f) / static_cast<float>(overlayRows) - 0.5f;
                const Vec2 sample = wrapWorldPoint(
                    {
                        cameraCenter_.x + tx * visibleWorldWidth,
                        cameraCenter_.y + ty * visibleWorldHeight
                    },
                    simulation.worldWidth(),
                    simulation.worldHeight()
                );
                const EnvironmentProbe probe = simulation.probeEnvironment(
                    sample.x,
                    sample.y
                );

                float signal = 0.0f;
                SDL_Color color {0, 0, 0, 0};
                switch (debugOverlay_) {
                    case DebugOverlay::Nutrient:
                        signal = probe.nutrient;
                        color = {84, 228, 152, static_cast<std::uint8_t>(18 + signal * 80.0f)};
                        break;
                    case DebugOverlay::Shelter:
                        signal = probe.shelter;
                        color = {96, 224, 214, static_cast<std::uint8_t>(18 + signal * 150.0f)};
                        break;
                    case DebugOverlay::Shear:
                        signal = probe.shear;
                        color = {118, 170, 255, static_cast<std::uint8_t>(18 + signal * 150.0f)};
                        break;
                    default:
                        break;
                }

                if (signal < 0.02f) {
                    continue;
                }

                const SDL_FPoint center = worldToScreen(sample, simulation);
                const SDL_FRect cell {
                    center.x - worldViewport_.w / static_cast<float>(overlayColumns) * 0.5f,
                    center.y - worldViewport_.h / static_cast<float>(overlayRows) * 0.5f,
                    worldViewport_.w / static_cast<float>(overlayColumns) + 1.0f,
                    worldViewport_.h / static_cast<float>(overlayRows) + 1.0f
                };
                fillRect(renderer_, cell, color);
            }
        }
    }

    for (const Reef& reef : simulation.reefs()) {
        const float radius = reef.radius * worldScale;
        const SDL_Color halo = {73, 108, 120, static_cast<std::uint8_t>(28 + reef.shear * 22.0f)};
        const SDL_Color body = {58, 74, 84, 214};
        const SDL_Color ridge = {112, 143, 154, 124};
        const SDL_Color moss = {74, 131, 114, static_cast<std::uint8_t>(54 + reef.nutrientBoost * 44.0f)};
        const SDL_FPoint center = worldToScreen(reef.position, simulation);
        if (center.x + radius * 1.35f < worldViewport_.x
            || center.x - radius * 1.35f > worldViewport_.x + worldViewport_.w
            || center.y + radius * 1.35f < worldViewport_.y
            || center.y - radius * 1.35f > worldViewport_.y + worldViewport_.h) {
            continue;
        }

        fillEllipse(renderer_, center.x, center.y, radius * 1.22f, radius * 1.12f, halo);
        fillEllipse(renderer_, center.x, center.y, radius, radius * 0.92f, body);
        fillEllipse(renderer_, center.x - radius * 0.12f, center.y - radius * 0.08f, radius * 0.52f, radius * 0.38f, ridge);
        fillEllipse(renderer_, center.x + radius * 0.16f, center.y + radius * 0.1f, radius * 0.42f, radius * 0.28f, moss);
        drawLine(
            renderer_,
            {center.x - radius * 0.54f, center.y - radius * 0.16f},
            {center.x + radius * 0.44f, center.y + radius * 0.12f},
            {136, 167, 178, 74}
        );
        drawLine(
            renderer_,
            {center.x - radius * 0.28f, center.y + radius * 0.26f},
            {center.x + radius * 0.24f, center.y - radius * 0.3f},
            {136, 167, 178, 62}
        );
    }

    for (const Bloom& bloom : simulation.blooms()) {
        const SDL_FPoint screen = worldToScreen(bloom.position, simulation);
        const float radius = 3.5f + bloom.energy / bloom.maxEnergy * 8.0f;
        fillEllipse(renderer_, screen.x, screen.y, radius, radius * 0.82f, {74, 211, 153, 210});
        fillEllipse(renderer_, screen.x, screen.y, radius * 0.55f, radius * 0.45f, {176, 253, 193, 200});
    }

    for (const Carrion& chunk : simulation.carrion()) {
        const SDL_FPoint screen = worldToScreen(chunk.position, simulation);
        const float radius = 2.8f + std::sqrt(std::max(chunk.energy, 1.0f)) * 0.55f;
        fillEllipse(renderer_, screen.x, screen.y, radius, radius * 0.75f, {171, 77, 54, 190});
        fillEllipse(renderer_, screen.x + radius * 0.3f, screen.y - radius * 0.2f, radius * 0.35f, radius * 0.26f, {229, 155, 118, 160});
    }

    // Flush accumulated batch geometry before switching to SDF pipeline
    flushColored(renderer_);

    creatureSdf_.render(
        simulation,
        worldViewport_,
        cameraZoom_,
        cameraCenter_,
        renderer_->drawableWidth,
        renderer_->drawableHeight
    );

    // Restore batched geometry VAO after SDF rendering
    glBindVertexArray(renderer_->colorVao);
    glUseProgram(renderer_->colorProgram);
    if (showBrainOverlay_) {
        drawBrainOverlay(info);
    }
    setClipRect(renderer_, nullptr);

    const SDL_FRect panel {
        static_cast<float>(kWindowWidth - kPanelWidth - kMargin),
        static_cast<float>(kMargin),
        static_cast<float>(kPanelWidth),
        static_cast<float>(kWindowHeight - kMargin * 2)
    };
    fillRect(renderer_, panel, {15, 18, 24, 238});
    fillRect(renderer_, {panel.x, panel.y, panel.w, 54.0f}, {24, 34, 45, 255});

    float textY = panel.y + 14.0f;
    drawText(titleFont_, panel.x + 18.0f, textY - 2.0f, "ALife Sim", {240, 244, 247, 255});
    textY += 22.0f;
    drawText(smallFont_, panel.x + 18.0f, textY, "segmented phenotype ecology", {129, 151, 176, 255});
    textY += 34.0f;

    const SDL_FRect summaryCard {panel.x + 14.0f, textY, panel.w - 28.0f, 118.0f};
    drawCard(summaryCard, "World");
    drawText(font_, summaryCard.x + 12.0f, summaryCard.y + 36.0f, "time " + formatFloat(simulation.timeSeconds(), 1) + "s", {228, 233, 238, 255});
    drawText(font_, summaryCard.x + 150.0f, summaryCard.y + 36.0f, paused ? "paused" : "running", paused ? SDL_Color{255, 204, 128, 255} : SDL_Color{140, 230, 166, 255});
    drawText(font_, summaryCard.x + 248.0f, summaryCard.y + 36.0f, std::to_string(timeScale) + "x", {197, 215, 229, 255});
    drawText(font_, summaryCard.x + 12.0f, summaryCard.y + 58.0f, "pop " + std::to_string(stats.population), {232, 240, 246, 255});
    drawText(font_, summaryCard.x + 110.0f, summaryCard.y + 58.0f, "blooms " + std::to_string(stats.blooms), {110, 227, 170, 255});
    drawText(font_, summaryCard.x + 235.0f, summaryCard.y + 58.0f, "carrion " + std::to_string(stats.carrion), {222, 141, 112, 255});
    drawText(smallFont_, summaryCard.x + 12.0f, summaryCard.y + 84.0f, "births " + std::to_string(stats.births) + "  deaths " + std::to_string(stats.deaths), {181, 194, 208, 255});
    drawText(smallFont_, summaryCard.x + 208.0f, summaryCard.y + 84.0f, "season " + formatFloat(stats.season, 2) + "  reefs " + std::to_string(stats.reefs), {181, 194, 208, 255});
    drawText(
        smallFont_,
        summaryCard.x + 12.0f,
        summaryCard.y + 100.0f,
        "overlay " + std::string(debugOverlayLabel()) + " (V)  zoom " + formatFloat(cameraZoom_, 1) + "x" + (followSelection_ ? "  track on" : "  track off"),
        {127, 153, 173, 255}
    );
    textY += summaryCard.h + 10.0f;

    const SDL_FRect populationCard {panel.x + 14.0f, textY, panel.w - 28.0f, 136.0f};
    drawCard(populationCard, "Population History");
    const SDL_FRect popGraph {populationCard.x + 10.0f, populationCard.y + 36.0f, populationCard.w - 20.0f, 74.0f};
    fillRect(renderer_, popGraph, {14, 18, 24, 255});
    float populationMax = 1.0f;
    for (const HistorySample& sample : history) {
        populationMax = std::max(populationMax, static_cast<float>(std::max({sample.population, sample.blooms, sample.carrion})));
    }
    drawSeries(popGraph, [](const HistorySample& sample) { return static_cast<float>(sample.blooms); }, populationMax, {104, 226, 174, 255});
    drawSeries(popGraph, [](const HistorySample& sample) { return static_cast<float>(sample.carrion); }, populationMax, {219, 128, 104, 255});
    drawSeries(popGraph, [](const HistorySample& sample) { return static_cast<float>(sample.population); }, populationMax, {238, 242, 247, 255});
    drawText(smallFont_, popGraph.x + 4.0f, popGraph.y + 2.0f, std::to_string(static_cast<int>(populationMax)), {118, 132, 148, 255});
    drawText(smallFont_, populationCard.x + 12.0f, populationCard.y + 114.0f, "white population", {230, 234, 238, 255});
    drawText(smallFont_, populationCard.x + 128.0f, populationCard.y + 114.0f, "green blooms", {104, 226, 174, 255});
    drawText(smallFont_, populationCard.x + 230.0f, populationCard.y + 114.0f, "rust carrion", {219, 128, 104, 255});
    textY += populationCard.h + 10.0f;

    const SDL_FRect ecologyCard {panel.x + 14.0f, textY, panel.w - 28.0f, 136.0f};
    drawCard(ecologyCard, "Ecology Drift");
    const SDL_FRect roleGraph {ecologyCard.x + 10.0f, ecologyCard.y + 36.0f, ecologyCard.w - 20.0f, 54.0f};
    fillRect(renderer_, roleGraph, {14, 18, 24, 255});
    float roleMax = 1.0f;
    for (const HistorySample& sample : history) {
        roleMax = std::max(roleMax, static_cast<float>(std::max({sample.grazers, sample.omnivores, sample.hunters})));
    }
    drawSeries(roleGraph, [](const HistorySample& sample) { return static_cast<float>(sample.grazers); }, roleMax, {125, 218, 139, 255});
    drawSeries(roleGraph, [](const HistorySample& sample) { return static_cast<float>(sample.omnivores); }, roleMax, {125, 173, 236, 255});
    drawSeries(roleGraph, [](const HistorySample& sample) { return static_cast<float>(sample.hunters); }, roleMax, {235, 146, 104, 255});
    drawText(smallFont_, ecologyCard.x + 12.0f, ecologyCard.y + 96.0f, "plant " + formatFloat(stats.avgPlantAffinity, 2), {125, 218, 139, 255});
    drawText(smallFont_, ecologyCard.x + 110.0f, ecologyCard.y + 96.0f, "meat " + formatFloat(stats.avgMeatAffinity, 2), {235, 146, 104, 255});
    drawText(smallFont_, ecologyCard.x + 202.0f, ecologyCard.y + 96.0f, "reef " + formatFloat(stats.avgSubstrateContact, 2), {156, 188, 198, 255});
    drawText(smallFont_, ecologyCard.x + 292.0f, ecologyCard.y + 96.0f, "lee " + formatFloat(stats.avgSubstrateShelter, 2), {142, 214, 198, 255});
    drawText(smallFont_, ecologyCard.x + 12.0f, ecologyCard.y + 114.0f, "lin " + std::to_string(stats.activeLineages), {158, 194, 240, 255});
    drawText(smallFont_, ecologyCard.x + 88.0f, ecologyCard.y + 114.0f, "dom " + formatFloat(stats.dominantLineageShare, 2), {206, 212, 220, 255});
    drawText(smallFont_, ecologyCard.x + 170.0f, ecologyCard.y + 114.0f, "shear " + formatFloat(stats.avgLocalShear, 2), {128, 176, 212, 255});
    drawText(smallFont_, ecologyCard.x + 264.0f, ecologyCard.y + 114.0f, "brain " + formatFloat(stats.avgBrainComplexity, 2), {184, 172, 236, 255});
    textY += ecologyCard.h + 10.0f;

    constexpr float controlsCardHeight = 132.0f;
    const SDL_FRect selectionCard {
        panel.x + 14.0f,
        textY,
        panel.w - 28.0f,
        panel.y + panel.h - textY - (controlsCardHeight + 10.0f)
    };
    drawCard(selectionCard, "Selection");
    selectionViewport_ = {selectionCard.x + 10.0f, selectionCard.y + 32.0f, selectionCard.w - 20.0f, selectionCard.h - 42.0f};

    const float selectionContentHeight = info.valid ? 418.0f : 150.0f;
    const float maxSelectionScroll = std::max(0.0f, selectionContentHeight - selectionViewport_.h);
    selectionScroll_ = std::clamp(selectionScroll_, 0.0f, maxSelectionScroll);

    const SDL_Rect selectionClip {
        static_cast<int>(std::floor(selectionViewport_.x)),
        static_cast<int>(std::floor(selectionViewport_.y)),
        static_cast<int>(std::ceil(selectionViewport_.w)),
        static_cast<int>(std::ceil(selectionViewport_.h))
    };
    setClipRect(renderer_, &selectionClip);

    if (info.valid) {
        float sy = selectionViewport_.y + 4.0f - selectionScroll_;
        drawText(font_, selectionCard.x + 12.0f, sy, "#" + std::to_string(info.id) + "  " + toString(info.dietClass), {235, 239, 244, 255});
        sy += 22.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "lineage " + std::to_string(info.lineageId)
                + (info.lineageParentId > 0 ? " <- " + std::to_string(info.lineageParentId) : "")
                + " depth " + std::to_string(info.lineageDepth)
                + " pop " + std::to_string(info.lineagePopulation)
                + " age " + formatFloat(info.lineageAge, 1),
            {165, 197, 232, 255}
        );
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "energy " + formatFloat(info.energy, 1) + "  health " + formatFloat(info.health, 1) + "  age " + formatFloat(info.age, 1), {219, 226, 233, 255});
        sy += 18.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "move " + formatFloat(info.worldSpeed, 1)
                + "  swim " + formatFloat(info.swimSpeed, 1)
                + "  flow " + formatFloat(info.currentSpeed, 1),
            {194, 210, 223, 255}
        );
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "mass " + formatFloat(info.mass, 1) + "  body " + formatFloat(info.majorRadius, 1) + " x " + formatFloat(info.minorRadius, 1), {219, 226, 233, 255});
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "fin " + formatFloat(info.finSpan, 1) + "  spacing " + formatFloat(info.segmentSpacing, 1) + "  wave " + formatFloat(info.tailWaveAmplitude, 2), {219, 226, 233, 255});
        sy += 18.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "fin pos " + formatFloat(info.finPlacement, 2)
                + "  tail len " + formatFloat(info.tailLengthScale, 2)
                + "  fork " + formatFloat(info.tailFork, 2),
            {219, 226, 233, 255}
        );
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "sensor " + formatFloat(info.sensorRange, 1) + "  bite " + formatFloat(info.biteDamage, 1) + "  graze " + formatFloat(info.grazeRate, 1), {219, 226, 233, 255});
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "plant " + formatFloat(info.plantAffinity, 2) + "  meat " + formatFloat(info.meatAffinity, 2) + "  aggr " + formatFloat(info.aggression, 2), {219, 226, 233, 255});
        sy += 18.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "head integ " + formatFloat(info.headIntegrity, 2)
                + "  tail integ " + formatFloat(info.tailIntegrity, 2),
            {210, 189, 179, 255}
        );
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "upkeep " + formatFloat(info.upkeep, 1) + "  repro " + formatFloat(info.reproductionThreshold, 1), {179, 194, 210, 255});
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "slip " + formatFloat(info.bodySlip, 2) + "  curve " + formatFloat(info.bodyCurvature, 2) + "  flow " + formatFloat(info.flowAlignment, 2), {179, 194, 210, 255});
        sy += 18.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "strain " + formatFloat(info.bodyStrain, 2)
                + "  comp " + formatFloat(info.bodyCompression, 2)
                + "  couple " + formatFloat(info.propulsionCoupling, 2),
            {186, 196, 220, 255}
        );
        sy += 18.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "reef " + formatFloat(info.substrateProximity, 2)
                + "  touch " + formatFloat(info.substrateContact, 2)
                + "  hold " + formatFloat(info.substrateGrip, 2)
                + "  scrape " + formatFloat(info.substrateScrape, 2),
            {160, 194, 208, 255}
        );
        sy += 18.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "lee " + formatFloat(info.substrateShelter, 2)
                + "  shear " + formatFloat(info.localShear, 2),
            {160, 194, 208, 255}
        );
        sy += 18.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "brain h " + std::to_string(info.brainHiddenCount)
                + "  conn " + std::to_string(info.brainConnectionCount)
                + "  load " + formatFloat(info.brainComplexity, 2)
                + "  nov " + formatFloat(info.brainNovelty, 2),
            {163, 196, 224, 255}
        );
        sy += 20.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "act t " + formatFloat(info.outputs[0], 2)
                + "  thrust " + formatFloat(info.outputs[1], 2)
                + "  graze " + formatFloat(info.outputs[2], 2),
            {205, 214, 222, 255}
        );
        sy += 16.0f;
        drawText(
            smallFont_,
            selectionCard.x + 12.0f,
            sy,
            "bite " + formatFloat(info.outputs[3], 2)
                + "  signal " + formatFloat(info.outputs[4], 2)
                + "  split " + formatFloat(info.outputs[5], 2),
            {205, 214, 222, 255}
        );
        sy += 20.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "sensor buckets", {163, 196, 224, 255});
        sy += 14.0f;
        constexpr std::array<const char*, 5> sensorLabels {"plant", "carrion", "prey-op", "threat", "signal"};
        const std::array<SDL_Color, 5> sensorColors {{
            {110, 227, 170, 255},
            {222, 141, 112, 255},
            {248, 212, 120, 255},
            {238, 112, 112, 255},
            {124, 183, 255, 255}
        }};
        const std::array<std::array<float, kSensorBuckets>, 5> sensorData {{
            info.plantSense,
            info.carrionSense,
            info.opportunitySense,
            info.threatSense,
            info.signalSense
        }};
        for (int sensorRow = 0; sensorRow < 5; ++sensorRow) {
            drawText(smallFont_, selectionCard.x + 12.0f, sy + 1.0f, sensorLabels[sensorRow], {205, 214, 222, 255});
            drawBucketStrip(selectionCard.x + 78.0f, sy, selectionViewport_.w - 92.0f, 12.0f, sensorData[sensorRow], sensorColors[sensorRow]);
            sy += 17.0f;
        }
    } else {
        const float sy = selectionViewport_.y + 8.0f - selectionScroll_;
        drawText(font_, selectionCard.x + 12.0f, sy, "click a creature to inspect it", {184, 192, 201, 255});
        drawText(smallFont_, selectionCard.x + 12.0f, sy + 26.0f, "selection shows body physics, chain stress proxies,", {125, 139, 155, 255});
        drawText(smallFont_, selectionCard.x + 12.0f, sy + 44.0f, "controller outputs, sensor activity, optional brain overlay,", {125, 139, 155, 255});
        drawText(smallFont_, selectionCard.x + 12.0f, sy + 62.0f, "and reef/substrate contact metrics.", {125, 139, 155, 255});
        drawText(smallFont_, selectionCard.x + 12.0f, sy + 86.0f, "drag empty water or use WASD to pan. T toggles the brain panel.", {125, 139, 155, 255});
    }

    setClipRect(renderer_, nullptr);
    if (maxSelectionScroll > 1.0f) {
        drawText(smallFont_, selectionCard.x + selectionCard.w - 84.0f, selectionCard.y + 7.0f, "wheel / [ ]", {124, 146, 164, 255});
        const SDL_FRect track {selectionCard.x + selectionCard.w - 8.0f, selectionViewport_.y, 4.0f, selectionViewport_.h};
        const float thumbHeight = std::max(24.0f, track.h * (selectionViewport_.h / selectionContentHeight));
        const float thumbTravel = std::max(0.0f, track.h - thumbHeight);
        const float thumbY = track.y + (selectionScroll_ / maxSelectionScroll) * thumbTravel;
        fillRect(renderer_, track, {34, 44, 56, 255});
        fillRect(renderer_, {track.x, thumbY, track.w, thumbHeight}, {114, 146, 170, 255});
    }

    const SDL_FRect controlsCard {panel.x + 14.0f, panel.y + panel.h - controlsCardHeight, panel.w - 28.0f, controlsCardHeight};
    drawCard(controlsCard, "Controls");
    const float buttonGap = 8.0f;
    const float buttonWidth = (controlsCard.w - 24.0f - buttonGap) * 0.5f;
    const float buttonHeight = 20.0f;
    randomSelectButton_ = {controlsCard.x + 12.0f, controlsCard.y + 32.0f, buttonWidth, buttonHeight};
    topEnergyButton_ = {randomSelectButton_.x + buttonWidth + buttonGap, controlsCard.y + 32.0f, buttonWidth, buttonHeight};
    dominantLineageButton_ = {controlsCard.x + 12.0f, controlsCard.y + 56.0f, buttonWidth, buttonHeight};
    newestLineageButton_ = {dominantLineageButton_.x + buttonWidth + buttonGap, controlsCard.y + 56.0f, buttonWidth, buttonHeight};
    overlayButton_ = {controlsCard.x + 12.0f, controlsCard.y + 80.0f, controlsCard.w - 24.0f, buttonHeight};
    drawButton(randomSelectButton_, "random subject (N)", {40, 74, 108, 255}, {227, 235, 241, 255});
    drawButton(topEnergyButton_, "top energy (F)", {54, 98, 84, 255}, {227, 235, 241, 255});
    drawButton(dominantLineageButton_, "dominant lineage (L)", {79, 70, 126, 255}, {232, 231, 244, 255});
    drawButton(newestLineageButton_, "newest branch (B)", {120, 76, 54, 255}, {244, 235, 227, 255});
    drawButton(overlayButton_, "habitat overlay: " + std::string(debugOverlayLabel()) + " (V)", {48, 90, 102, 255}, {228, 238, 242, 255});
    drawText(
        smallFont_,
        controlsCard.x + 12.0f,
        controlsCard.y + 104.0f,
        "drag/WASD pan. wheel or +/- zooms under cursor. G track. Z snap.",
        {154, 173, 190, 255}
    );

    presentFrame(renderer_, window_);
}

}  // namespace alife
