#pragma once

#include <optional>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "sim/Simulation.hpp"

namespace alife {

class Renderer {
public:
    enum class UiAction {
        None,
        SelectRandom,
        SelectTopEnergy
    };

    Renderer() = default;
    ~Renderer();

    bool initialize();
    void shutdown();
    void draw(const Simulation& simulation, bool paused, int timeScale);

    bool screenPointInWorld(int screenX, int screenY) const;
    std::optional<Vec2> screenToWorld(int screenX, int screenY, const Simulation& simulation) const;
    UiAction uiActionAt(int screenX, int screenY) const;

private:
    static constexpr int kWindowWidth = 1600;
    static constexpr int kWindowHeight = 960;
    static constexpr int kPanelWidth = 404;
    static constexpr int kMargin = 24;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* titleFont_ = nullptr;
    TTF_Font* font_ = nullptr;
    TTF_Font* smallFont_ = nullptr;
    SDL_FRect worldViewport_ {static_cast<float>(kMargin), static_cast<float>(kMargin), 100.0f, 100.0f};
    SDL_FRect randomSelectButton_ {0.0f, 0.0f, 0.0f, 0.0f};
    SDL_FRect topEnergyButton_ {0.0f, 0.0f, 0.0f, 0.0f};

    void updateViewport(const Simulation& simulation);
};

}  // namespace alife
