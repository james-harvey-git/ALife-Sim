#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "render/Renderer.hpp"
#include "sim/Simulation.hpp"

namespace {

enum class ReportFormat {
    Text,
    JsonLines
};

struct CliOptions {
    bool smokeTest = false;
    bool batchRun = false;
    std::uint64_t seed = 1;
    int smokeSteps = 3600;
    int batchCount = 8;
    int seedStride = 1;
    ReportFormat reportFormat = ReportFormat::Text;
};

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options {};

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--smoke-test") {
            options.smokeTest = true;
        } else if (argument == "--batch-run") {
            options.batchRun = true;
        } else if (argument == "--seed" && index + 1 < argc) {
            options.seed = static_cast<std::uint64_t>(std::strtoull(argv[++index], nullptr, 10));
        } else if (argument == "--smoke-steps" && index + 1 < argc) {
            options.smokeSteps = std::max(1, std::atoi(argv[++index]));
        } else if (argument == "--batch-count" && index + 1 < argc) {
            options.batchCount = std::max(1, std::atoi(argv[++index]));
        } else if (argument == "--seed-stride" && index + 1 < argc) {
            options.seedStride = std::max(1, std::atoi(argv[++index]));
        } else if (argument == "--report-format" && index + 1 < argc) {
            const std::string value = argv[++index];
            if (value == "jsonl") {
                options.reportFormat = ReportFormat::JsonLines;
            } else {
                options.reportFormat = ReportFormat::Text;
            }
        }
    }

    return options;
}

struct RunSummary {
    std::uint64_t seed = 0;
    int steps = 0;
    std::size_t population = 0;
    std::size_t blooms = 0;
    std::size_t carrion = 0;
    std::size_t reefs = 0;
    std::uint64_t births = 0;
    std::uint64_t deaths = 0;
    std::uint64_t extinctions = 0;
    int activeLineages = 0;
    float dominantLineageShare = 0.0f;
    float averageEnergy = 0.0f;
    float maxEnergy = 0.0f;
    float averageReproductionThreshold = 0.0f;
    float avgBrainLoad = 0.0f;
    float avgContact = 0.0f;
    float avgShelter = 0.0f;
    float avgShear = 0.0f;
    float feedAmbient = 0.0f;
    float feedBloom = 0.0f;
    float feedCarrion = 0.0f;
    float feedPredation = 0.0f;
    float spendUpkeep = 0.0f;
    float spendReproduction = 0.0f;
};

RunSummary runSummaryForSeed(std::uint64_t seed, int steps) {
    constexpr float kStep = 1.0f / 60.0f;

    alife::Simulation simulation;
    simulation.reset(seed);
    for (int index = 0; index < steps; ++index) {
        simulation.step(kStep);
    }

    const alife::Stats& stats = simulation.stats();
    RunSummary summary {};
    summary.seed = seed;
    summary.steps = steps;
    summary.population = stats.population;
    summary.blooms = stats.blooms;
    summary.carrion = stats.carrion;
    summary.reefs = stats.reefs;
    summary.births = stats.births;
    summary.deaths = stats.deaths;
    summary.extinctions = stats.extinctions;
    summary.activeLineages = stats.activeLineages;
    summary.dominantLineageShare = stats.dominantLineageShare;
    summary.avgBrainLoad = stats.avgBrainComplexity;
    summary.avgContact = stats.avgSubstrateContact;
    summary.avgShelter = stats.avgSubstrateShelter;
    summary.avgShear = stats.avgLocalShear;
    summary.feedAmbient = stats.energyFromAmbientGrazing;
    summary.feedBloom = stats.energyFromBloomHarvest;
    summary.feedCarrion = stats.energyFromCarrion;
    summary.feedPredation = stats.energyFromPredation;
    summary.spendUpkeep = stats.energySpentOnUpkeep;
    summary.spendReproduction = stats.energySpentOnReproduction;

    for (const auto& creature : simulation.creatures()) {
        summary.averageEnergy += creature.energy;
        summary.maxEnergy = std::max(summary.maxEnergy, creature.energy);
        summary.averageReproductionThreshold += creature.traits.reproductionThreshold;
    }
    if (!simulation.creatures().empty()) {
        const float divisor = static_cast<float>(simulation.creatures().size());
        summary.averageEnergy /= divisor;
        summary.averageReproductionThreshold /= divisor;
    }

    return summary;
}

void printSmokeSummary(const RunSummary& summary) {
    std::cout
        << "smoke-test population=" << summary.population
        << " blooms=" << summary.blooms
        << " carrion=" << summary.carrion
        << " reefs=" << summary.reefs
        << " births=" << summary.births
        << " deaths=" << summary.deaths
        << " extinctions=" << summary.extinctions
        << " lineages=" << summary.activeLineages
        << " dom_lineage=" << summary.dominantLineageShare
        << " avg_energy=" << summary.averageEnergy
        << " max_energy=" << summary.maxEnergy
        << " avg_brain_load=" << summary.avgBrainLoad
        << " avg_contact=" << summary.avgContact
        << " avg_shelter=" << summary.avgShelter
        << " avg_shear=" << summary.avgShear
        << " feed_ambient=" << summary.feedAmbient
        << " feed_bloom=" << summary.feedBloom
        << " feed_carrion=" << summary.feedCarrion
        << " feed_predation=" << summary.feedPredation
        << " spend_upkeep=" << summary.spendUpkeep
        << " spend_repro=" << summary.spendReproduction
        << " avg_repro_threshold=" << summary.averageReproductionThreshold
        << '\n';
}

void printJsonRunSummary(const RunSummary& summary) {
    std::cout
        << std::fixed << std::setprecision(6)
        << "{\"kind\":\"run\""
        << ",\"seed\":" << summary.seed
        << ",\"steps\":" << summary.steps
        << ",\"population\":" << summary.population
        << ",\"blooms\":" << summary.blooms
        << ",\"carrion\":" << summary.carrion
        << ",\"reefs\":" << summary.reefs
        << ",\"births\":" << summary.births
        << ",\"deaths\":" << summary.deaths
        << ",\"extinctions\":" << summary.extinctions
        << ",\"lineages\":" << summary.activeLineages
        << ",\"dominant_lineage_share\":" << summary.dominantLineageShare
        << ",\"avg_energy\":" << summary.averageEnergy
        << ",\"max_energy\":" << summary.maxEnergy
        << ",\"avg_brain_load\":" << summary.avgBrainLoad
        << ",\"avg_contact\":" << summary.avgContact
        << ",\"avg_shelter\":" << summary.avgShelter
        << ",\"avg_shear\":" << summary.avgShear
        << ",\"feed_ambient\":" << summary.feedAmbient
        << ",\"feed_bloom\":" << summary.feedBloom
        << ",\"feed_carrion\":" << summary.feedCarrion
        << ",\"feed_predation\":" << summary.feedPredation
        << ",\"spend_upkeep\":" << summary.spendUpkeep
        << ",\"spend_reproduction\":" << summary.spendReproduction
        << ",\"avg_reproduction_threshold\":" << summary.averageReproductionThreshold
        << "}\n";
}

void printTextBatchSummary(
    const std::vector<RunSummary>& runs,
    int extinctRuns,
    float meanPopulation,
    float minPopulation,
    float maxPopulation,
    float meanLineages,
    float meanDominantLineageShare,
    float meanAverageEnergy,
    float meanAverageShelter
) {
    std::cout
        << "batch-run count=" << runs.size()
        << " steps=" << (runs.empty() ? 0 : runs.front().steps)
        << " start_seed=" << (runs.empty() ? 0 : runs.front().seed)
        << '\n';
    for (const RunSummary& run : runs) {
        std::cout
            << "seed=" << run.seed
            << " population=" << run.population
            << " births=" << run.births
            << " deaths=" << run.deaths
            << " extinctions=" << run.extinctions
            << " lineages=" << run.activeLineages
            << " dom_lineage=" << run.dominantLineageShare
            << " avg_energy=" << run.averageEnergy
            << " avg_shelter=" << run.avgShelter
            << '\n';
    }
    std::cout
        << "summary runs=" << runs.size()
        << " extinct_runs=" << extinctRuns
        << " mean_population=" << meanPopulation
        << " min_population=" << minPopulation
        << " max_population=" << maxPopulation
        << " mean_lineages=" << meanLineages
        << " mean_dom_lineage=" << meanDominantLineageShare
        << " mean_avg_energy=" << meanAverageEnergy
        << " mean_avg_shelter=" << meanAverageShelter
        << '\n';
}

void printJsonBatchSummary(
    const std::vector<RunSummary>& runs,
    int extinctRuns,
    float meanPopulation,
    float minPopulation,
    float maxPopulation,
    float meanLineages,
    float meanDominantLineageShare,
    float meanAverageEnergy,
    float meanAverageShelter
) {
    for (const RunSummary& run : runs) {
        printJsonRunSummary(run);
    }
    std::cout
        << std::fixed << std::setprecision(6)
        << "{\"kind\":\"summary\""
        << ",\"runs\":" << runs.size()
        << ",\"steps\":" << (runs.empty() ? 0 : runs.front().steps)
        << ",\"start_seed\":" << (runs.empty() ? 0 : runs.front().seed)
        << ",\"extinct_runs\":" << extinctRuns
        << ",\"mean_population\":" << meanPopulation
        << ",\"min_population\":" << minPopulation
        << ",\"max_population\":" << maxPopulation
        << ",\"mean_lineages\":" << meanLineages
        << ",\"mean_dominant_lineage_share\":" << meanDominantLineageShare
        << ",\"mean_avg_energy\":" << meanAverageEnergy
        << ",\"mean_avg_shelter\":" << meanAverageShelter
        << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    const CliOptions options = parseArgs(argc, argv);

    if (options.smokeTest) {
        const RunSummary summary = runSummaryForSeed(options.seed, options.smokeSteps);
        printSmokeSummary(summary);
        return summary.population > 0 ? 0 : 1;
    }

    if (options.batchRun) {
        std::vector<RunSummary> runs;
        runs.reserve(static_cast<std::size_t>(options.batchCount));

        float minPopulation = std::numeric_limits<float>::max();
        float maxPopulation = 0.0f;
        float totalPopulation = 0.0f;
        float totalLineages = 0.0f;
        float totalDominantLineageShare = 0.0f;
        float totalAverageEnergy = 0.0f;
        float totalAverageShelter = 0.0f;
        int extinctRuns = 0;

        for (int runIndex = 0; runIndex < options.batchCount; ++runIndex) {
            const std::uint64_t runSeed = options.seed + static_cast<std::uint64_t>(runIndex) * static_cast<std::uint64_t>(options.seedStride);
            const RunSummary summary = runSummaryForSeed(runSeed, options.smokeSteps);
            runs.push_back(summary);

            const float population = static_cast<float>(summary.population);
            minPopulation = std::min(minPopulation, population);
            maxPopulation = std::max(maxPopulation, population);
            totalPopulation += population;
            totalLineages += static_cast<float>(summary.activeLineages);
            totalDominantLineageShare += summary.dominantLineageShare;
            totalAverageEnergy += summary.averageEnergy;
            totalAverageShelter += summary.avgShelter;
            if (summary.extinctions > 0 || summary.population == 0) {
                ++extinctRuns;
            }
        }

        const float runCount = static_cast<float>(runs.size());
        const float meanPopulation = runCount > 0.0f ? totalPopulation / runCount : 0.0f;
        const float meanLineages = runCount > 0.0f ? totalLineages / runCount : 0.0f;
        const float meanDominantLineageShare = runCount > 0.0f ? totalDominantLineageShare / runCount : 0.0f;
        const float meanAverageEnergy = runCount > 0.0f ? totalAverageEnergy / runCount : 0.0f;
        const float meanAverageShelter = runCount > 0.0f ? totalAverageShelter / runCount : 0.0f;
        const float safeMinPopulation = runs.empty() ? 0.0f : minPopulation;

        if (options.reportFormat == ReportFormat::JsonLines) {
            printJsonBatchSummary(
                runs,
                extinctRuns,
                meanPopulation,
                safeMinPopulation,
                maxPopulation,
                meanLineages,
                meanDominantLineageShare,
                meanAverageEnergy,
                meanAverageShelter
            );
        } else {
            printTextBatchSummary(
                runs,
                extinctRuns,
                meanPopulation,
                safeMinPopulation,
                maxPopulation,
                meanLineages,
                meanDominantLineageShare,
                meanAverageEnergy,
                meanAverageShelter
            );
        }

        return extinctRuns == 0 ? 0 : 1;
    }

    alife::Simulation simulation;
    simulation.reset(options.seed);

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
                        case SDLK_v:
                            renderer.cycleDebugOverlay();
                            break;
                        case SDLK_LEFTBRACKET:
                        case SDLK_PAGEUP:
                            renderer.scrollSelection(-72.0f);
                            break;
                        case SDLK_RIGHTBRACKET:
                        case SDLK_PAGEDOWN:
                            renderer.scrollSelection(72.0f);
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
                        if (uiAction == alife::Renderer::UiAction::CycleOverlay) {
                            renderer.cycleDebugOverlay();
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
                case SDL_MOUSEWHEEL: {
                    int mouseX = 0;
                    int mouseY = 0;
                    SDL_GetMouseState(&mouseX, &mouseY);
                    if (renderer.screenPointInSelection(mouseX, mouseY)) {
                        renderer.scrollSelection(static_cast<float>(-event.wheel.y) * 24.0f);
                    }
                    break;
                }
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
