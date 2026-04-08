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

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
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

std::uint64_t quantize(float value, int levels) {
    const auto clamped = static_cast<std::uint64_t>(std::clamp(static_cast<int>(std::round(value * (levels - 1))), 0, levels - 1));
    return clamped;
}

std::uint64_t phenotypeKey(const Genome& genome) {
    std::uint64_t key = 0;
    int shift = 0;

    auto push = [&](float value, int levels, int bits) {
        key |= (quantize(value, levels) << shift);
        shift += bits;
    };

    push(genome.morphology.coreSize, 16, 4);
    push(genome.morphology.elongation, 16, 4);
    push(genome.morphology.finArea, 16, 4);
    push(genome.morphology.armor, 16, 4);
    push(genome.morphology.jawLength, 16, 4);
    push(genome.morphology.jawArc, 16, 4);
    push(genome.morphology.sensorRange, 16, 4);
    push(genome.morphology.spikes, 16, 4);
    push(genome.morphology.hue, 32, 5);
    push(genome.morphology.pattern, 16, 4);
    push(genome.ecology.plantAffinity, 16, 4);
    push(genome.ecology.meatAffinity, 16, 4);
    push(static_cast<float>(genome.brain.activeHidden) / static_cast<float>(kMaxHiddenCount), 16, 4);

    return key;
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
        font_ = TTF_OpenFont(fontPath->c_str(), 18);
    }

    return true;
}

void Renderer::shutdown() {
    for (auto& [_, sprite] : spriteCache_) {
        if (sprite.texture != nullptr) {
            SDL_DestroyTexture(sprite.texture);
            sprite.texture = nullptr;
        }
    }
    spriteCache_.clear();

    if (font_ != nullptr) {
        TTF_CloseFont(font_);
        font_ = nullptr;
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

void Renderer::draw(const Simulation& simulation, bool paused, int timeScale) {
    if (renderer_ == nullptr) {
        return;
    }

    updateViewport(simulation);

    const auto worldToScreen = [&](const Vec2& world) {
        return SDL_FPoint {
            worldViewport_.x + (world.x / simulation.worldWidth()) * worldViewport_.w,
            worldViewport_.y + (world.y / simulation.worldHeight()) * worldViewport_.h
        };
    };

    const float worldScale = worldViewport_.w / simulation.worldWidth();

    auto ensureSprite = [&](const Creature& creature) -> const SpriteEntry& {
        const std::uint64_t key = phenotypeKey(creature.genome);
        const auto found = spriteCache_.find(key);
        if (found != spriteCache_.end()) {
            return found->second;
        }

        SpriteEntry entry {};
        entry.worldWidth = creature.traits.majorRadius * 2.8f + creature.genome.morphology.finArea * 10.0f;
        entry.worldHeight = std::max(
            creature.traits.minorRadius * 2.7f + creature.genome.morphology.spikes * 9.0f,
            creature.traits.collisionRadius * 2.1f
        );

        const int textureWidth = std::clamp(static_cast<int>(std::ceil(entry.worldWidth * 3.4f)), 84, 220);
        const int textureHeight = std::clamp(static_cast<int>(std::ceil(entry.worldHeight * 3.4f)), 64, 200);
        entry.texture = SDL_CreateTexture(
            renderer_,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET,
            textureWidth,
            textureHeight
        );
        SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(renderer_, entry.texture);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
        SDL_RenderClear(renderer_);

        const float cx = textureWidth * 0.47f;
        const float cy = textureHeight * 0.5f;
        const float pixelsPerWorld = std::min(
            static_cast<float>(textureWidth) / entry.worldWidth,
            static_cast<float>(textureHeight) / entry.worldHeight
        );
        const float bodyRx = creature.traits.majorRadius * pixelsPerWorld;
        const float bodyRy = creature.traits.minorRadius * pixelsPerWorld;
        const float finLength = lerp(bodyRx * 0.3f, bodyRx * 0.95f, creature.genome.morphology.finArea);
        const float finHeight = lerp(bodyRy * 0.35f, bodyRy * 0.95f, creature.genome.morphology.finArea);
        const float jawLength = lerp(bodyRx * 0.25f, bodyRx * 0.9f, creature.genome.morphology.jawLength);
        const float spikeLength = lerp(0.0f, bodyRy * 0.75f, creature.genome.morphology.spikes);

        const SDL_Color body = hsv(
            creature.genome.morphology.hue,
            lerp(0.45f, 0.8f, creature.genome.morphology.armor),
            lerp(0.7f, 0.95f, creature.genome.ecology.plantAffinity)
        );
        const SDL_Color accent = hsv(
            creature.genome.morphology.hue + 0.08f,
            0.45f,
            0.98f,
            220
        );
        const SDL_Color shell = tint(body, 0.62f, 255);
        const SDL_Color shadow = tint(body, 0.42f, 200);
        const SDL_Color eye = hsv(0.13f, 0.18f, 0.98f);

        fillTriangle(
            renderer_,
            {cx - bodyRx * 0.95f, cy},
            {cx - bodyRx - finLength, cy - finHeight},
            {cx - bodyRx * 0.55f, cy - bodyRy * 0.18f},
            accent
        );
        fillTriangle(
            renderer_,
            {cx - bodyRx * 0.95f, cy},
            {cx - bodyRx - finLength, cy + finHeight},
            {cx - bodyRx * 0.55f, cy + bodyRy * 0.18f},
            accent
        );

        if (creature.genome.morphology.spikes > 0.12f) {
            fillTriangle(
                renderer_,
                {cx - bodyRx * 0.15f, cy - bodyRy * 0.9f},
                {cx + bodyRx * 0.1f, cy - bodyRy - spikeLength},
                {cx + bodyRx * 0.35f, cy - bodyRy * 0.78f},
                shell
            );
            fillTriangle(
                renderer_,
                {cx - bodyRx * 0.05f, cy + bodyRy * 0.9f},
                {cx + bodyRx * 0.2f, cy + bodyRy + spikeLength},
                {cx + bodyRx * 0.45f, cy + bodyRy * 0.78f},
                shell
            );
        }

        fillEllipse(renderer_, cx, cy, bodyRx, bodyRy, body);
        fillEllipse(renderer_, cx - bodyRx * 0.08f, cy, bodyRx * 0.62f, bodyRy * 0.62f, tint(body, 1.1f, 170));

        if (creature.genome.morphology.pattern < 0.5f) {
            fillEllipse(renderer_, cx - bodyRx * 0.2f, cy - bodyRy * 0.24f, bodyRx * 0.28f, bodyRy * 0.14f, shadow);
            fillEllipse(renderer_, cx + bodyRx * 0.12f, cy + bodyRy * 0.2f, bodyRx * 0.22f, bodyRy * 0.12f, shadow);
        } else {
            fillEllipse(renderer_, cx - bodyRx * 0.16f, cy, bodyRx * 0.12f, bodyRy * 0.85f, shadow);
            fillEllipse(renderer_, cx + bodyRx * 0.16f, cy, bodyRx * 0.1f, bodyRy * 0.7f, shadow);
        }

        fillTriangle(
            renderer_,
            {cx + bodyRx * 0.82f, cy},
            {cx + bodyRx + jawLength, cy - bodyRy * lerp(0.18f, 0.48f, creature.genome.morphology.jawArc)},
            {cx + bodyRx + jawLength, cy + bodyRy * lerp(0.18f, 0.48f, creature.genome.morphology.jawArc)},
            shell
        );

        fillEllipse(renderer_, cx + bodyRx * 0.28f, cy - bodyRy * 0.22f, bodyRx * 0.12f, bodyRy * 0.12f, eye);
        fillEllipse(renderer_, cx + bodyRx * 0.31f, cy - bodyRy * 0.22f, bodyRx * 0.04f, bodyRy * 0.04f, {18, 18, 22, 255});

        SDL_SetRenderTarget(renderer_, nullptr);
        const auto [inserted, _] = spriteCache_.emplace(key, entry);
        return inserted->second;
    };

    fillRect(renderer_, {0.0f, 0.0f, static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)}, {11, 14, 20, 255});
    fillRect(renderer_, worldViewport_, {20, 27, 36, 255});

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
        const SDL_FPoint screen = worldToScreen(creature.position);
        const SDL_FPoint trailEnd {
            screen.x - creature.velocity.x * worldScale * 0.05f,
            screen.y - creature.velocity.y * worldScale * 0.05f
        };
        drawLine(renderer_, screen, trailEnd, {73, 86, 107, 90});

        if (creature.signal > 0.08f) {
            const float aura = (creature.traits.signalRange * worldScale) * 0.12f;
            fillEllipse(renderer_, screen.x, screen.y, aura, aura * 0.85f, {88, 180, 214, static_cast<std::uint8_t>(18 + creature.signal * 28.0f)});
        }

        const SpriteEntry& sprite = ensureSprite(creature);
        SDL_FRect destination {
            screen.x - sprite.worldWidth * worldScale * 0.5f,
            screen.y - sprite.worldHeight * worldScale * 0.5f,
            sprite.worldWidth * worldScale,
            sprite.worldHeight * worldScale
        };
        SDL_RenderCopyExF(
            renderer_,
            sprite.texture,
            nullptr,
            &destination,
            creature.angle * 180.0 / kPi,
            nullptr,
            SDL_FLIP_NONE
        );

        if (creature.id == selectedId) {
            const float ringRadius = creature.traits.collisionRadius * worldScale * 1.15f;
            fillEllipse(renderer_, screen.x, screen.y, ringRadius, ringRadius, {242, 245, 247, 46});
            drawLine(
                renderer_,
                screen,
                {screen.x + std::cos(creature.angle) * ringRadius, screen.y + std::sin(creature.angle) * ringRadius},
                {243, 244, 246, 180}
            );
        }
    }

    const SDL_FRect panel {
        static_cast<float>(kWindowWidth - kPanelWidth - kMargin),
        static_cast<float>(kMargin),
        static_cast<float>(kPanelWidth),
        static_cast<float>(kWindowHeight - kMargin * 2)
    };
    fillRect(renderer_, panel, {15, 18, 24, 238});
    fillRect(renderer_, {panel.x, panel.y, panel.w, 52.0f}, {24, 34, 45, 255});

    auto drawText = [&](float x, float y, const std::string& text, SDL_Color color) {
        if (font_ == nullptr || text.empty()) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font_, text.c_str(), color);
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

    float textY = panel.y + 14.0f;
    drawText(panel.x + 18.0f, textY, "ALife Sim", {240, 244, 247, 255});
    textY += 22.0f;
    drawText(panel.x + 18.0f, textY, "native physics-first ecology", {129, 151, 176, 255});
    textY += 34.0f;

    const Stats& stats = simulation.stats();
    const auto info = simulation.selectionInfo();

    std::vector<std::string> lines {
        "time " + formatFloat(simulation.timeSeconds(), 1) + "s",
        std::string(paused ? "state paused" : "state running") + "  " + std::to_string(timeScale) + "x",
        "population " + std::to_string(stats.population),
        "blooms " + std::to_string(stats.blooms) + "  carrion " + std::to_string(stats.carrion),
        "births " + std::to_string(stats.births) + "  deaths " + std::to_string(stats.deaths),
        "season " + formatFloat(stats.season, 2),
        "avg plant " + formatFloat(stats.avgPlantAffinity, 2),
        "avg meat " + formatFloat(stats.avgMeatAffinity, 2),
        "avg mass " + formatFloat(stats.avgMass, 1),
        "roles G:" + std::to_string(stats.grazers)
            + " O:" + std::to_string(stats.omnivores)
            + " H:" + std::to_string(stats.hunters)
    };

    for (const std::string& line : lines) {
        drawText(panel.x + 18.0f, textY, line, {221, 228, 234, 255});
        textY += 22.0f;
    }

    textY += 8.0f;
    fillRect(renderer_, {panel.x + 14.0f, textY - 6.0f, panel.w - 28.0f, info.valid ? 240.0f : 92.0f}, {24, 31, 41, 255});
    drawText(panel.x + 18.0f, textY, "selection", {165, 198, 224, 255});
    textY += 24.0f;

    if (info.valid) {
        const std::vector<std::string> selectionLines {
            "#" + std::to_string(info.id) + "  " + toString(info.dietClass),
            "energy " + formatFloat(info.energy, 1) + "  health " + formatFloat(info.health, 1),
            "age " + formatFloat(info.age, 1),
            "plant " + formatFloat(info.plantAffinity, 2)
                + "  meat " + formatFloat(info.meatAffinity, 2),
            "aggression " + formatFloat(info.aggression, 2),
            "body " + formatFloat(info.majorRadius, 1) + " x " + formatFloat(info.minorRadius, 1),
            "mass " + formatFloat(info.mass, 1),
            "thrust " + formatFloat(info.thrust, 1),
            "turn " + formatFloat(info.turnTorque, 1),
            "sensor " + formatFloat(info.sensorRange, 1),
            "bite " + formatFloat(info.biteDamage, 1),
            "graze " + formatFloat(info.grazeRate, 1)
        };
        for (const std::string& line : selectionLines) {
            drawText(panel.x + 18.0f, textY, line, {230, 234, 238, 255});
            textY += 20.0f;
        }
    } else {
        drawText(panel.x + 18.0f, textY, "click a creature to inspect it", {179, 187, 196, 255});
        textY += 20.0f;
        drawText(panel.x + 18.0f, textY, "physics-derived stats show here", {125, 139, 155, 255});
        textY += 20.0f;
    }

    const float controlsY = panel.y + panel.h - 112.0f;
    fillRect(renderer_, {panel.x + 14.0f, controlsY - 6.0f, panel.w - 28.0f, 104.0f}, {24, 31, 41, 255});
    drawText(panel.x + 18.0f, controlsY, "controls", {165, 198, 224, 255});
    drawText(panel.x + 18.0f, controlsY + 24.0f, "space pause   1/2/3 speed", {221, 228, 234, 255});
    drawText(panel.x + 18.0f, controlsY + 46.0f, "r reseed      c clear selection", {221, 228, 234, 255});
    drawText(panel.x + 18.0f, controlsY + 68.0f, "left click inspect   esc quit", {221, 228, 234, 255});

    SDL_RenderPresent(renderer_);
}

}  // namespace alife
