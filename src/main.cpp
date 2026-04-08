#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include <SDL2/SDL.h>

#include "render/Renderer.hpp"
#include "sim/Simulation.hpp"

namespace {

struct CliOptions {
    bool smokeTest = false;
    std::uint64_t seed = 1;
    int smokeSteps = 3600;
};

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options {};

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--smoke-test") {
            options.smokeTest = true;
        } else if (argument == "--seed" && index + 1 < argc) {
            options.seed = static_cast<std::uint64_t>(std::strtoull(argv[++index], nullptr, 10));
        } else if (argument == "--smoke-steps" && index + 1 < argc) {
            options.smokeSteps = std::max(1, std::atoi(argv[++index]));
        }
    }

    return options;
}

}  // namespace

int main(int argc, char** argv) {
    const CliOptions options = parseArgs(argc, argv);

    alife::Simulation simulation;
    simulation.reset(options.seed);

    if (options.smokeTest) {
        constexpr float kStep = 1.0f / 60.0f;
        for (int index = 0; index < options.smokeSteps; ++index) {
            simulation.step(kStep);
        }

        const alife::Stats& stats = simulation.stats();
        float averageEnergy = 0.0f;
        float maxEnergy = 0.0f;
        float averageThreshold = 0.0f;
        for (const auto& creature : simulation.creatures()) {
            averageEnergy += creature.energy;
            maxEnergy = std::max(maxEnergy, creature.energy);
            averageThreshold += creature.traits.reproductionThreshold;
        }
        if (!simulation.creatures().empty()) {
            averageEnergy /= static_cast<float>(simulation.creatures().size());
            averageThreshold /= static_cast<float>(simulation.creatures().size());
        }
        std::cout
            << "smoke-test population=" << stats.population
            << " blooms=" << stats.blooms
            << " carrion=" << stats.carrion
            << " reefs=" << stats.reefs
            << " births=" << stats.births
            << " deaths=" << stats.deaths
            << " lineages=" << stats.activeLineages
            << " dom_lineage=" << stats.dominantLineageShare
            << " avg_energy=" << averageEnergy
            << " max_energy=" << maxEnergy
            << " avg_brain_load=" << stats.avgBrainComplexity
            << " avg_contact=" << stats.avgSubstrateContact
            << " avg_shear=" << stats.avgLocalShear
            << " feed_ambient=" << stats.energyFromAmbientGrazing
            << " feed_bloom=" << stats.energyFromBloomHarvest
            << " feed_carrion=" << stats.energyFromCarrion
            << " feed_predation=" << stats.energyFromPredation
            << " spend_upkeep=" << stats.energySpentOnUpkeep
            << " spend_repro=" << stats.energySpentOnReproduction
            << " avg_repro_threshold=" << averageThreshold
            << '\n';
        return stats.population > 0 ? 0 : 1;
    }

    alife::Renderer renderer;
    if (!renderer.initialize()) {
        std::cerr << "Failed to initialize SDL renderer.\n";
        return 1;
    }

    bool running = true;
    bool paused = false;
    int timeScale = 1;
    std::uint64_t seedCounter = options.seed;
    constexpr float kFixedDelta = 1.0f / 60.0f;

    auto previousTime = std::chrono::steady_clock::now();
    float accumulator = 0.0f;

    while (running) {
        SDL_Event event {};
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            running = false;
                            break;
                        case SDLK_SPACE:
                            paused = !paused;
                            break;
                        case SDLK_1:
                            timeScale = 1;
                            break;
                        case SDLK_2:
                            timeScale = 2;
                            break;
                        case SDLK_3:
                            timeScale = 4;
                            break;
                        case SDLK_r:
                            ++seedCounter;
                            simulation.reset(seedCounter);
                            break;
                        case SDLK_c:
                            simulation.clearSelection();
                            break;
                        case SDLK_n:
                            simulation.selectRandomCreature();
                            break;
                        case SDLK_f:
                            simulation.selectTopEnergyCreature();
                            break;
                        case SDLK_l:
                            simulation.selectDominantLineageCreature();
                            break;
                        case SDLK_b:
                            simulation.selectNewestLineageCreature();
                            break;
                        default:
                            break;
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        const auto uiAction = renderer.uiActionAt(event.button.x, event.button.y);
                        if (uiAction == alife::Renderer::UiAction::SelectRandom) {
                            simulation.selectRandomCreature();
                            break;
                        }
                        if (uiAction == alife::Renderer::UiAction::SelectTopEnergy) {
                            simulation.selectTopEnergyCreature();
                            break;
                        }
                        if (uiAction == alife::Renderer::UiAction::SelectDominantLineage) {
                            simulation.selectDominantLineageCreature();
                            break;
                        }
                        if (uiAction == alife::Renderer::UiAction::SelectNewestLineage) {
                            simulation.selectNewestLineageCreature();
                            break;
                        }
                    }

                    if (event.button.button == SDL_BUTTON_LEFT
                        && renderer.screenPointInWorld(event.button.x, event.button.y)) {
                        const auto worldPoint = renderer.screenToWorld(event.button.x, event.button.y, simulation);
                        if (worldPoint.has_value()) {
                            const auto selection = simulation.creatureAt(worldPoint->x, worldPoint->y, 30.0f);
                            if (selection.has_value()) {
                                simulation.setSelectedCreature(*selection);
                            } else {
                                simulation.clearSelection();
                            }
                        }
                    }
                    break;
                default:
                    break;
            }
        }

        const auto currentTime = std::chrono::steady_clock::now();
        const float frameSeconds = std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;
        accumulator = std::min(0.25f, accumulator + frameSeconds);

        while (accumulator >= kFixedDelta) {
            if (!paused) {
                for (int step = 0; step < timeScale; ++step) {
                    simulation.step(kFixedDelta);
                }
            }
            accumulator -= kFixedDelta;
        }

        renderer.draw(simulation, paused, timeScale);
    }

    renderer.shutdown();
    return 0;
}
