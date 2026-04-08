#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "sim/Simulation.hpp"

namespace alife {

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool initialize();
    void shutdown();
    void draw(const Simulation& simulation, bool paused, int timeScale);

    bool screenPointInWorld(int screenX, int screenY) const;
    std::optional<Vec2> screenToWorld(int screenX, int screenY, const Simulation& simulation) const;

private:
    struct SpriteEntry {
        SDL_Texture* texture = nullptr;
        float worldWidth = 0.0f;
        float worldHeight = 0.0f;
    };

    static constexpr int kWindowWidth = 1600;
    static constexpr int kWindowHeight = 960;
    static constexpr int kPanelWidth = 336;
    static constexpr int kMargin = 24;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* font_ = nullptr;
    SDL_FRect worldViewport_ {static_cast<float>(kMargin), static_cast<float>(kMargin), 100.0f, 100.0f};

    std::unordered_map<std::uint64_t, SpriteEntry> spriteCache_ {};

    void updateViewport(const Simulation& simulation);
};

}  // namespace alife

