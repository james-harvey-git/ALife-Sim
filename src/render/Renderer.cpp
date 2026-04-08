#include "render/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace alife {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
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

float lengthSquared(const Vec2& value) {
    return value.x * value.x + value.y * value.y;
}

Vec2 normalize(const Vec2& value) {
    const float lenSq = lengthSquared(value);
    if (lenSq < 1e-6f) {
        return {1.0f, 0.0f};
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    return value * invLen;
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

void setColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void fillRect(SDL_Renderer* renderer, const SDL_FRect& rect, SDL_Color color) {
    setColor(renderer, color);
    SDL_RenderFillRectF(renderer, &rect);
}

bool pointInRect(int x, int y, const SDL_FRect& rect) {
    const float px = static_cast<float>(x);
    const float py = static_cast<float>(y);
    return px >= rect.x && px <= rect.x + rect.w && py >= rect.y && py <= rect.y + rect.h;
}

void drawTexturedFan(
    SDL_Renderer* renderer,
    const std::vector<SDL_Vertex>& vertices,
    const std::vector<int>& indices
) {
    SDL_RenderGeometry(
        renderer,
        nullptr,
        vertices.data(),
        static_cast<int>(vertices.size()),
        indices.data(),
        static_cast<int>(indices.size())
    );
}

void fillEllipse(SDL_Renderer* renderer, float cx, float cy, float rx, float ry, SDL_Color color) {
    constexpr int kSegments = 28;
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    vertices.reserve(kSegments + 2);
    indices.reserve(kSegments * 3);

    vertices.push_back({{cx, cy}, color, {0.0f, 0.0f}});
    for (int index = 0; index <= kSegments; ++index) {
        const float angle = static_cast<float>(index) / static_cast<float>(kSegments) * kPi * 2.0f;
        vertices.push_back({{cx + std::cos(angle) * rx, cy + std::sin(angle) * ry}, color, {0.0f, 0.0f}});
        if (index > 0) {
            indices.push_back(0);
            indices.push_back(index);
            indices.push_back(index + 1);
        }
    }

    drawTexturedFan(renderer, vertices, indices);
}

void fillTriangle(SDL_Renderer* renderer, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_Color color) {
    const std::array<SDL_Vertex, 3> vertices {{
        {a, color, {0.0f, 0.0f}},
        {b, color, {0.0f, 0.0f}},
        {c, color, {0.0f, 0.0f}}
    }};
    const std::array<int, 3> indices {0, 1, 2};
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), 3, indices.data(), 3);
}

void drawLine(SDL_Renderer* renderer, SDL_FPoint a, SDL_FPoint b, SDL_Color color) {
    setColor(renderer, color);
    SDL_RenderDrawLineF(renderer, a.x, a.y, b.x, b.y);
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

    window_ = SDL_CreateWindow(
        "ALife Sim",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        kWindowWidth,
        kWindowHeight,
        SDL_WINDOW_SHOWN
    );
    if (window_ == nullptr) {
        shutdown();
        return false;
    }

    renderer_ = SDL_CreateRenderer(
        window_,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE
    );
    if (renderer_ == nullptr) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    }
    if (renderer_ == nullptr) {
        shutdown();
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    if (const auto fontPath = findFontPath(); fontPath.has_value()) {
        titleFont_ = TTF_OpenFont(fontPath->c_str(), 20);
        font_ = TTF_OpenFont(fontPath->c_str(), 16);
        smallFont_ = TTF_OpenFont(fontPath->c_str(), 13);
    }

    return true;
}

void Renderer::shutdown() {
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
        SDL_DestroyRenderer(renderer_);
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
    const float availableWidth = static_cast<float>(kWindowWidth - kPanelWidth - kMargin * 3);
    const float availableHeight = static_cast<float>(kWindowHeight - kMargin * 2);
    const float scale = std::min(availableWidth / simulation.worldWidth(), availableHeight / simulation.worldHeight());
    const float drawWidth = simulation.worldWidth() * scale;
    const float drawHeight = simulation.worldHeight() * scale;

    worldViewport_.x = static_cast<float>(kMargin) + (availableWidth - drawWidth) * 0.5f;
    worldViewport_.y = static_cast<float>(kMargin) + (availableHeight - drawHeight) * 0.5f;
    worldViewport_.w = drawWidth;
    worldViewport_.h = drawHeight;
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

    const float nx = (static_cast<float>(screenX) - worldViewport_.x) / worldViewport_.w;
    const float ny = (static_cast<float>(screenY) - worldViewport_.y) / worldViewport_.h;
    return Vec2 {nx * simulation.worldWidth(), ny * simulation.worldHeight()};
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
    return UiAction::None;
}

void Renderer::draw(const Simulation& simulation, bool paused, int timeScale) {
    if (renderer_ == nullptr) {
        return;
    }

    updateViewport(simulation);
    randomSelectButton_ = {0.0f, 0.0f, 0.0f, 0.0f};
    topEnergyButton_ = {0.0f, 0.0f, 0.0f, 0.0f};
    dominantLineageButton_ = {0.0f, 0.0f, 0.0f, 0.0f};
    newestLineageButton_ = {0.0f, 0.0f, 0.0f, 0.0f};

    const auto worldToScreen = [&](const Vec2& world) {
        return SDL_FPoint {
            worldViewport_.x + (world.x / simulation.worldWidth()) * worldViewport_.w,
            worldViewport_.y + (world.y / simulation.worldHeight()) * worldViewport_.h
        };
    };
    const float worldScale = worldViewport_.w / simulation.worldWidth();

    auto drawText = [&](TTF_Font* font, float x, float y, const std::string& text, SDL_Color color) {
        if (font == nullptr || text.empty()) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (surface == nullptr) {
            return;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        if (texture != nullptr) {
            SDL_FRect destination {x, y, static_cast<float>(surface->w), static_cast<float>(surface->h)};
            SDL_RenderCopyF(renderer_, texture, nullptr, &destination);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
    };

    auto forEachWrappedWorldPoint = [&](const Vec2& worldPoint, float radiusPixels, auto drawFn) {
        const SDL_FPoint base = worldToScreen(worldPoint);
        constexpr std::array<float, 3> offsets { -1.0f, 0.0f, 1.0f };
        for (float xOffset : offsets) {
            for (float yOffset : offsets) {
                const SDL_FPoint point {
                    base.x + xOffset * worldViewport_.w,
                    base.y + yOffset * worldViewport_.h
                };
                if (point.x + radiusPixels < worldViewport_.x
                    || point.x - radiusPixels > worldViewport_.x + worldViewport_.w
                    || point.y + radiusPixels < worldViewport_.y
                    || point.y - radiusPixels > worldViewport_.y + worldViewport_.h) {
                    continue;
                }
                drawFn(point);
            }
        }
    };

    const Stats& stats = simulation.stats();
    const auto info = simulation.selectionInfo();
    const auto& history = simulation.history();

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
        setColor(renderer_, color);
        for (std::size_t index = 1; index < history.size(); ++index) {
            const float x1 = rect.x + rect.w * (static_cast<float>(index - 1) / static_cast<float>(history.size() - 1));
            const float x2 = rect.x + rect.w * (static_cast<float>(index) / static_cast<float>(history.size() - 1));
            const float y1 = rect.y + rect.h - (getter(history[index - 1]) / maxValue) * rect.h;
            const float y2 = rect.y + rect.h - (getter(history[index]) / maxValue) * rect.h;
            SDL_RenderDrawLineF(renderer_, x1, y1, x2, y2);
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

    auto drawCreature = [&](const Creature& creature, bool selected) {
        const SDL_Color body = hsv(
            creature.genome.morphology.hue,
            lerp(0.42f, 0.82f, creature.genome.morphology.armor),
            lerp(0.7f, 0.96f, creature.genome.ecology.plantAffinity)
        );
        const SDL_Color accent = hsv(creature.genome.morphology.hue + 0.07f, 0.58f, 0.98f, 220);
        const SDL_Color shell = tint(body, 0.62f, 255);
        const SDL_Color shadow = tint(body, 0.42f, 160);
        const SDL_Color eye = hsv(0.14f, 0.18f, 0.98f);

        const SDL_FPoint head = worldToScreen(creature.bodyPoints[0]);
        const auto wrappedPointToScreen = [&](const Vec2& point) {
            Vec2 delta {
                point.x - creature.bodyPoints[0].x,
                point.y - creature.bodyPoints[0].y
            };
            if (delta.x > simulation.worldWidth() * 0.5f) {
                delta.x -= simulation.worldWidth();
            } else if (delta.x < -simulation.worldWidth() * 0.5f) {
                delta.x += simulation.worldWidth();
            }
            if (delta.y > simulation.worldHeight() * 0.5f) {
                delta.y -= simulation.worldHeight();
            } else if (delta.y < -simulation.worldHeight() * 0.5f) {
                delta.y += simulation.worldHeight();
            }
            return SDL_FPoint {
                head.x + delta.x * worldScale,
                head.y + delta.y * worldScale
            };
        };
        const SDL_FPoint trailEnd {
            head.x - creature.velocity.x * worldScale * 0.05f,
            head.y - creature.velocity.y * worldScale * 0.05f
        };
        drawLine(renderer_, head, trailEnd, {73, 86, 107, 90});

        if (creature.signal > 0.08f) {
            const float aura = (creature.traits.signalRange * worldScale) * 0.12f;
            fillEllipse(renderer_, head.x, head.y, aura, aura * 0.84f, {88, 180, 214, static_cast<std::uint8_t>(18 + creature.signal * 28.0f)});
        }

        for (int segmentIndex = kBodySegments - 1; segmentIndex >= 0; --segmentIndex) {
            const SDL_FPoint point = wrappedPointToScreen(creature.bodyPoints[segmentIndex]);
            const float radius = creature.bodyRadii[segmentIndex] * worldScale;
            const float shade = lerp(0.78f, 1.05f, 1.0f - static_cast<float>(segmentIndex) / static_cast<float>(kBodySegments - 1));
            const SDL_Color segmentColor = tint(body, shade, 240);
            fillEllipse(renderer_, point.x, point.y, radius * 1.08f, radius * 0.9f, segmentColor);
            fillEllipse(renderer_, point.x - radius * 0.12f, point.y, radius * 0.56f, radius * 0.42f, shadow);
        }

        const Vec2 forwardVector {std::cos(creature.angle), std::sin(creature.angle)};
        const Vec2 sideVector {-forwardVector.y, forwardVector.x};

        const Vec2 finBaseA = creature.bodyPoints[1];
        const Vec2 finBaseB = creature.bodyPoints[2];
        const Vec2 finTipTop = finBaseA + sideVector * creature.traits.finSpan - forwardVector * creature.traits.segmentSpacing * 0.2f;
        const Vec2 finTipBottom = finBaseA - sideVector * creature.traits.finSpan - forwardVector * creature.traits.segmentSpacing * 0.2f;
        fillTriangle(renderer_, wrappedPointToScreen(finBaseA), wrappedPointToScreen(finTipTop), wrappedPointToScreen(finBaseB), accent);
        fillTriangle(renderer_, wrappedPointToScreen(finBaseA), wrappedPointToScreen(finTipBottom), wrappedPointToScreen(finBaseB), accent);

        const Vec2 tailBase = creature.bodyPoints[kBodySegments - 1];
        const Vec2 tailAnchor = creature.bodyPoints[kBodySegments - 2];
        const Vec2 tailDirection = normalize(tailBase - tailAnchor);
        const Vec2 tailSide {-tailDirection.y, tailDirection.x};
        const float tailSpan = creature.bodyRadii[kBodySegments - 1] * 1.8f;
        const Vec2 tailTip = tailBase - tailDirection * (creature.traits.segmentSpacing * 1.4f);
        fillTriangle(
            renderer_,
            wrappedPointToScreen(tailBase + tailSide * tailSpan),
            wrappedPointToScreen(tailTip),
            wrappedPointToScreen(tailBase - tailSide * tailSpan),
            accent
        );

        const Vec2 jawBase = creature.bodyPoints[0] + forwardVector * (creature.bodyRadii[0] * 0.8f);
        const Vec2 jawTip = jawBase + forwardVector * (creature.traits.biteReach * 0.55f);
        const Vec2 jawSide = sideVector * (creature.bodyRadii[0] * lerp(0.2f, 0.5f, creature.genome.morphology.jawArc));
        fillTriangle(renderer_, wrappedPointToScreen(jawBase + jawSide), wrappedPointToScreen(jawTip), wrappedPointToScreen(jawBase - jawSide), shell);

        const SDL_FPoint eyePoint = wrappedPointToScreen(creature.bodyPoints[0] + forwardVector * creature.bodyRadii[0] * 0.18f - sideVector * creature.bodyRadii[0] * 0.24f);
        fillEllipse(renderer_, eyePoint.x, eyePoint.y, 2.8f, 2.8f, eye);
        fillEllipse(renderer_, eyePoint.x + 0.6f, eyePoint.y, 1.0f, 1.0f, {14, 16, 20, 255});

        if (creature.genome.morphology.pattern < 0.5f) {
            fillEllipse(renderer_, head.x - creature.bodyRadii[0] * worldScale * 0.16f, head.y + creature.bodyRadii[0] * worldScale * 0.12f, 3.2f, 2.2f, shadow);
            const SDL_FPoint torso = wrappedPointToScreen(creature.bodyPoints[1]);
            fillEllipse(renderer_, torso.x + creature.bodyRadii[1] * worldScale * 0.1f, torso.y - 1.0f, 2.6f, 1.8f, shadow);
        } else {
            for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
                const SDL_FPoint point = wrappedPointToScreen(creature.bodyPoints[segmentIndex]);
                fillEllipse(renderer_, point.x, point.y - creature.bodyRadii[segmentIndex] * worldScale * 0.24f, creature.bodyRadii[segmentIndex] * worldScale * 0.22f, 1.5f, shadow);
            }
        }

        if (selected) {
            const float sensorHalf = creature.traits.sensorSpan * 0.5f;
            const Vec2 sensorEdgeA {
                std::cos(creature.angle - sensorHalf),
                std::sin(creature.angle - sensorHalf)
            };
            const Vec2 sensorEdgeB {
                std::cos(creature.angle + sensorHalf),
                std::sin(creature.angle + sensorHalf)
            };
            drawLine(renderer_, head, wrappedPointToScreen(creature.bodyPoints[0] + sensorEdgeA * creature.traits.sensorRange), {138, 193, 255, 90});
            drawLine(renderer_, head, wrappedPointToScreen(creature.bodyPoints[0] + sensorEdgeB * creature.traits.sensorRange), {138, 193, 255, 90});
            drawLine(renderer_, head, wrappedPointToScreen(creature.bodyPoints[0] + forwardVector * creature.traits.sensorRange), {138, 193, 255, 46});

            const Vec2 biteEdgeA {
                std::cos(creature.angle - creature.traits.biteArc),
                std::sin(creature.angle - creature.traits.biteArc)
            };
            const Vec2 biteEdgeB {
                std::cos(creature.angle + creature.traits.biteArc),
                std::sin(creature.angle + creature.traits.biteArc)
            };
            drawLine(renderer_, head, wrappedPointToScreen(creature.bodyPoints[0] + biteEdgeA * creature.traits.biteReach), {255, 210, 160, 110});
            drawLine(renderer_, head, wrappedPointToScreen(creature.bodyPoints[0] + biteEdgeB * creature.traits.biteReach), {255, 210, 160, 110});

            const float ringRadius = creature.traits.collisionRadius * worldScale * 0.7f;
            fillEllipse(renderer_, head.x, head.y, ringRadius, ringRadius, {242, 245, 247, 36});
            drawLine(
                renderer_,
                head,
                {head.x + forwardVector.x * ringRadius, head.y + forwardVector.y * ringRadius},
                {243, 244, 246, 180}
            );
        }
    };

    fillRect(renderer_, {0.0f, 0.0f, static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)}, {11, 14, 20, 255});
    fillRect(renderer_, worldViewport_, {20, 27, 36, 255});

    const SDL_Rect worldClip {
        static_cast<int>(std::floor(worldViewport_.x)),
        static_cast<int>(std::floor(worldViewport_.y)),
        static_cast<int>(std::ceil(worldViewport_.w)),
        static_cast<int>(std::ceil(worldViewport_.h))
    };
    SDL_RenderSetClipRect(renderer_, &worldClip);

    constexpr int gridColumns = 26;
    constexpr int gridRows = 18;
    for (int row = 0; row < gridRows; ++row) {
        for (int column = 0; column < gridColumns; ++column) {
            const float tx = (static_cast<float>(column) + 0.5f) / static_cast<float>(gridColumns);
            const float ty = (static_cast<float>(row) + 0.5f) / static_cast<float>(gridRows);
            const Vec2 sample {
                tx * simulation.worldWidth(),
                ty * simulation.worldHeight()
            };
            const float nutrient = simulation.sampleNutrient(sample.x, sample.y);
            const Vec2 current = simulation.sampleCurrent(sample.x, sample.y);
            SDL_Color color = hsv(0.47f + nutrient * 0.1f, 0.42f, 0.16f + nutrient * 0.22f, static_cast<std::uint8_t>(32 + nutrient * 60.0f));

            const SDL_FRect cell {
                worldViewport_.x + tx * worldViewport_.w - worldViewport_.w / static_cast<float>(gridColumns) * 0.5f,
                worldViewport_.y + ty * worldViewport_.h - worldViewport_.h / static_cast<float>(gridRows) * 0.5f,
                worldViewport_.w / static_cast<float>(gridColumns) + 1.0f,
                worldViewport_.h / static_cast<float>(gridRows) + 1.0f
            };
            fillRect(renderer_, cell, color);

            const SDL_FPoint center {cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f};
            const SDL_FPoint arrow {
                center.x + current.x * 0.015f * worldScale,
                center.y + current.y * 0.015f * worldScale
            };
            drawLine(renderer_, center, arrow, {92, 156, 178, 64});
        }
    }

    for (const Reef& reef : simulation.reefs()) {
        const float radius = reef.radius * worldScale;
        const SDL_Color halo = {73, 108, 120, static_cast<std::uint8_t>(28 + reef.shear * 22.0f)};
        const SDL_Color body = {58, 74, 84, 214};
        const SDL_Color ridge = {112, 143, 154, 124};
        const SDL_Color moss = {74, 131, 114, static_cast<std::uint8_t>(54 + reef.nutrientBoost * 44.0f)};

        forEachWrappedWorldPoint(reef.position, radius * 1.35f, [&](SDL_FPoint center) {
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
        });
    }

    for (const Bloom& bloom : simulation.blooms()) {
        const SDL_FPoint screen = worldToScreen(bloom.position);
        const float radius = 3.5f + bloom.energy / bloom.maxEnergy * 8.0f;
        fillEllipse(renderer_, screen.x, screen.y, radius, radius * 0.82f, {74, 211, 153, 210});
        fillEllipse(renderer_, screen.x, screen.y, radius * 0.55f, radius * 0.45f, {176, 253, 193, 200});
    }

    for (const Carrion& chunk : simulation.carrion()) {
        const SDL_FPoint screen = worldToScreen(chunk.position);
        const float radius = 2.8f + std::sqrt(std::max(chunk.energy, 1.0f)) * 0.55f;
        fillEllipse(renderer_, screen.x, screen.y, radius, radius * 0.75f, {171, 77, 54, 190});
        fillEllipse(renderer_, screen.x + radius * 0.3f, screen.y - radius * 0.2f, radius * 0.35f, radius * 0.26f, {229, 155, 118, 160});
    }

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
    drawBrainOverlay(info);
    SDL_RenderSetClipRect(renderer_, nullptr);

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
    drawText(smallFont_, ecologyCard.x + 12.0f, ecologyCard.y + 96.0f, "avg plant " + formatFloat(stats.avgPlantAffinity, 2), {125, 218, 139, 255});
    drawText(smallFont_, ecologyCard.x + 136.0f, ecologyCard.y + 96.0f, "avg meat " + formatFloat(stats.avgMeatAffinity, 2), {235, 146, 104, 255});
    drawText(smallFont_, ecologyCard.x + 252.0f, ecologyCard.y + 96.0f, "contact " + formatFloat(stats.avgSubstrateContact, 2), {156, 188, 198, 255});
    drawText(smallFont_, ecologyCard.x + 12.0f, ecologyCard.y + 114.0f, "lin " + std::to_string(stats.activeLineages), {158, 194, 240, 255});
    drawText(smallFont_, ecologyCard.x + 88.0f, ecologyCard.y + 114.0f, "dom " + formatFloat(stats.dominantLineageShare, 2), {206, 212, 220, 255});
    drawText(smallFont_, ecologyCard.x + 176.0f, ecologyCard.y + 114.0f, "shear " + formatFloat(stats.avgLocalShear, 2), {128, 176, 212, 255});
    drawText(smallFont_, ecologyCard.x + 272.0f, ecologyCard.y + 114.0f, "brain " + formatFloat(stats.avgBrainComplexity, 2), {184, 172, 236, 255});
    textY += ecologyCard.h + 10.0f;

    const SDL_FRect selectionCard {panel.x + 14.0f, textY, panel.w - 28.0f, panel.y + panel.h - textY - 96.0f};
    drawCard(selectionCard, "Selection");

    if (info.valid) {
        float sy = selectionCard.y + 36.0f;
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
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "mass " + formatFloat(info.mass, 1) + "  body " + formatFloat(info.majorRadius, 1) + " x " + formatFloat(info.minorRadius, 1), {219, 226, 233, 255});
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "fin " + formatFloat(info.finSpan, 1) + "  spacing " + formatFloat(info.segmentSpacing, 1) + "  wave " + formatFloat(info.tailWaveAmplitude, 2), {219, 226, 233, 255});
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "sensor " + formatFloat(info.sensorRange, 1) + "  bite " + formatFloat(info.biteDamage, 1) + "  graze " + formatFloat(info.grazeRate, 1), {219, 226, 233, 255});
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "plant " + formatFloat(info.plantAffinity, 2) + "  meat " + formatFloat(info.meatAffinity, 2) + "  aggr " + formatFloat(info.aggression, 2), {219, 226, 233, 255});
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "upkeep " + formatFloat(info.upkeep, 1) + "  repro " + formatFloat(info.reproductionThreshold, 1), {179, 194, 210, 255});
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "slip " + formatFloat(info.bodySlip, 2) + "  curve " + formatFloat(info.bodyCurvature, 2) + "  flow " + formatFloat(info.flowAlignment, 2), {179, 194, 210, 255});
        sy += 18.0f;
        drawText(smallFont_, selectionCard.x + 12.0f, sy, "reef " + formatFloat(info.substrateProximity, 2) + "  contact " + formatFloat(info.substrateContact, 2) + "  shear " + formatFloat(info.localShear, 2), {160, 194, 208, 255});
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
            drawBucketStrip(selectionCard.x + 78.0f, sy, selectionCard.w - 96.0f, 12.0f, sensorData[sensorRow], sensorColors[sensorRow]);
            sy += 17.0f;
        }
    } else {
        drawText(font_, selectionCard.x + 12.0f, selectionCard.y + 40.0f, "click a creature to inspect it", {184, 192, 201, 255});
        drawText(smallFont_, selectionCard.x + 12.0f, selectionCard.y + 66.0f, "selection shows body physics, chain stress proxies,", {125, 139, 155, 255});
        drawText(smallFont_, selectionCard.x + 12.0f, selectionCard.y + 84.0f, "controller outputs, topology overlay, sensor activity,", {125, 139, 155, 255});
        drawText(smallFont_, selectionCard.x + 12.0f, selectionCard.y + 102.0f, "and reef/substrate contact metrics.", {125, 139, 155, 255});
        drawText(smallFont_, selectionCard.x + 12.0f, selectionCard.y + 126.0f, "observer picks can lock onto dominant or newly branched lineages.", {125, 139, 155, 255});
    }

    const SDL_FRect controlsCard {panel.x + 14.0f, panel.y + panel.h - 86.0f, panel.w - 28.0f, 86.0f};
    drawCard(controlsCard, "Controls");
    const float buttonGap = 8.0f;
    const float buttonWidth = (controlsCard.w - 24.0f - buttonGap) * 0.5f;
    const float buttonHeight = 20.0f;
    randomSelectButton_ = {controlsCard.x + 12.0f, controlsCard.y + 32.0f, buttonWidth, buttonHeight};
    topEnergyButton_ = {randomSelectButton_.x + buttonWidth + buttonGap, controlsCard.y + 32.0f, buttonWidth, buttonHeight};
    dominantLineageButton_ = {controlsCard.x + 12.0f, controlsCard.y + 56.0f, buttonWidth, buttonHeight};
    newestLineageButton_ = {dominantLineageButton_.x + buttonWidth + buttonGap, controlsCard.y + 56.0f, buttonWidth, buttonHeight};
    drawButton(randomSelectButton_, "random subject (N)", {40, 74, 108, 255}, {227, 235, 241, 255});
    drawButton(topEnergyButton_, "top energy (F)", {54, 98, 84, 255}, {227, 235, 241, 255});
    drawButton(dominantLineageButton_, "dominant lineage (L)", {79, 70, 126, 255}, {232, 231, 244, 255});
    drawButton(newestLineageButton_, "newest branch (B)", {120, 76, 54, 255}, {244, 235, 227, 255});

    SDL_RenderPresent(renderer_);
}

}  // namespace alife
