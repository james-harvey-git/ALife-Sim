#pragma once

#include <optional>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "render/CreatureSDF.hpp"
#include "sim/Simulation.hpp"

namespace alife {

struct RendererState;

class Renderer {
public:
    enum class UiAction {
        None,
        SelectRandom,
        SelectTopEnergy,
        SelectDominantLineage,
        SelectNewestLineage,
        CycleOverlay
    };

    enum class DebugOverlay {
        None,
        Nutrient,
        Shelter,
        Shear
    };

    Renderer() = default;
    ~Renderer();

    bool initialize();
    void shutdown();
    void draw(const Simulation& simulation, bool paused, int timeScale);
    void cycleDebugOverlay();
    void scrollSelection(float deltaPixels);
    void zoomView(float zoomSteps, const Simulation& simulation, std::optional<SDL_Point> anchor = std::nullopt);
    void resetCamera(const Simulation& simulation);
    void focusSelection(const Simulation& simulation, bool snap = false);
    void toggleFollowSelection(const Simulation& simulation);
    void toggleBrainOverlay();
    void beginWorldDrag(int screenX, int screenY);
    void updateWorldDrag(int screenX, int screenY, const Simulation& simulation);
    void endWorldDrag();
    bool isDraggingWorld() const;
    void panCameraWorld(const Vec2& delta);
    float zoomLevel() const;

    bool screenPointInWorld(int screenX, int screenY) const;
    bool screenPointInSelection(int screenX, int screenY) const;
    std::optional<Vec2> screenToWorld(int screenX, int screenY, const Simulation& simulation) const;
    UiAction uiActionAt(int screenX, int screenY) const;

private:
    static constexpr int kWindowWidth = 1600;
    static constexpr int kWindowHeight = 960;
    static constexpr int kPanelWidth = 404;
    static constexpr int kMargin = 24;

    SDL_Window* window_ = nullptr;
    RendererState* renderer_ = nullptr;
    TTF_Font* titleFont_ = nullptr;
    TTF_Font* font_ = nullptr;
    TTF_Font* smallFont_ = nullptr;
    SDL_FRect worldViewport_ {static_cast<float>(kMargin), static_cast<float>(kMargin), 100.0f, 100.0f};
    SDL_FRect selectionViewport_ {0.0f, 0.0f, 0.0f, 0.0f};
    SDL_FRect randomSelectButton_ {0.0f, 0.0f, 0.0f, 0.0f};
    SDL_FRect topEnergyButton_ {0.0f, 0.0f, 0.0f, 0.0f};
    SDL_FRect dominantLineageButton_ {0.0f, 0.0f, 0.0f, 0.0f};
    SDL_FRect newestLineageButton_ {0.0f, 0.0f, 0.0f, 0.0f};
    SDL_FRect overlayButton_ {0.0f, 0.0f, 0.0f, 0.0f};
    DebugOverlay debugOverlay_ = DebugOverlay::None;
    float selectionScroll_ = 0.0f;
    std::uint64_t lastSelectedCreatureId_ = 0;
    float cameraZoom_ = 2.6f;
    Vec2 cameraCenter_ {0.0f, 0.0f};
    bool cameraInitialized_ = false;
    bool followSelection_ = false;
    bool showBrainOverlay_ = false;
    bool draggingWorld_ = false;
    int dragStartScreenX_ = 0;
    int dragStartScreenY_ = 0;
    Vec2 dragStartCamera_ {0.0f, 0.0f};
    CreatureSDF creatureSdf_;

    void updateViewport(const Simulation& simulation);
    void updateCamera(const Simulation& simulation);
    float worldScale(const Simulation& simulation) const;
    Vec2 visibleWorldExtents(const Simulation& simulation) const;
    Vec2 wrappedPositionNearCamera(const Vec2& point, const Simulation& simulation) const;
    SDL_FPoint worldToScreen(const Vec2& world, const Simulation& simulation) const;
    const char* debugOverlayLabel() const;
};

}  // namespace alife
