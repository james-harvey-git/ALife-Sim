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

enum class BenchmarkPreset {
    None,
    Quick,
    Ecology,
    Stress
};

struct BenchmarkScenario {
    const char* name = "custom";
    int smokeSteps = 3600;
    int batchCount = 1;
    int seedStride = 1;
};

struct CliOptions {
    bool smokeTest = false;
    bool batchRun = false;
    bool benchmarkRun = false;
    bool snapshotOutput = false;
    std::uint64_t seed = 1;
    int smokeSteps = 3600;
    int batchCount = 8;
    int seedStride = 1;
    ReportFormat reportFormat = ReportFormat::Text;
    BenchmarkPreset benchmarkPreset = BenchmarkPreset::None;
    int snapshotLineageCount = 5;
    std::string loadStatePath {};
    std::string saveStatePath {};
};

BenchmarkPreset parseBenchmarkPreset(const std::string& value) {
    if (value == "quick") {
        return BenchmarkPreset::Quick;
    }
    if (value == "stress") {
        return BenchmarkPreset::Stress;
    }
    if (value == "ecology") {
        return BenchmarkPreset::Ecology;
    }
    return BenchmarkPreset::None;
}

BenchmarkScenario benchmarkScenarioForPreset(BenchmarkPreset preset) {
    switch (preset) {
        case BenchmarkPreset::Quick:
            return BenchmarkScenario {.name = "quick", .smokeSteps = 3600, .batchCount = 3, .seedStride = 1};
        case BenchmarkPreset::Stress:
            return BenchmarkScenario {.name = "stress", .smokeSteps = 21600, .batchCount = 8, .seedStride = 1};
        case BenchmarkPreset::Ecology:
            return BenchmarkScenario {.name = "ecology", .smokeSteps = 10800, .batchCount = 5, .seedStride = 1};
        default:
            return BenchmarkScenario {};
    }
}

void applyBenchmarkPreset(CliOptions& options) {
    if (!options.benchmarkRun) {
        return;
    }
    if (options.benchmarkPreset == BenchmarkPreset::None) {
        options.benchmarkPreset = BenchmarkPreset::Ecology;
    }
    const BenchmarkScenario scenario = benchmarkScenarioForPreset(options.benchmarkPreset);
    options.batchRun = true;
    options.smokeSteps = scenario.smokeSteps;
    options.batchCount = scenario.batchCount;
    options.seedStride = scenario.seedStride;
}

std::string scenarioLabelForOptions(const CliOptions& options) {
    if (!options.benchmarkRun) {
        return "custom";
    }
    return benchmarkScenarioForPreset(options.benchmarkPreset).name;
}

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options {};

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--smoke-test") {
            options.smokeTest = true;
        } else if (argument == "--batch-run") {
            options.batchRun = true;
        } else if (argument == "--benchmark") {
            options.benchmarkRun = true;
        } else if (argument == "--seed" && index + 1 < argc) {
            options.seed = static_cast<std::uint64_t>(std::strtoull(argv[++index], nullptr, 10));
        } else if (argument == "--smoke-steps" && index + 1 < argc) {
            options.smokeSteps = std::max(1, std::atoi(argv[++index]));
        } else if (argument == "--batch-count" && index + 1 < argc) {
            options.batchCount = std::max(1, std::atoi(argv[++index]));
        } else if (argument == "--seed-stride" && index + 1 < argc) {
            options.seedStride = std::max(1, std::atoi(argv[++index]));
        } else if (argument == "--snapshot") {
            options.snapshotOutput = true;
        } else if (argument == "--snapshot-lineages" && index + 1 < argc) {
            options.snapshotLineageCount = std::max(1, std::atoi(argv[++index]));
        } else if (argument == "--load-state" && index + 1 < argc) {
            options.loadStatePath = argv[++index];
        } else if (argument == "--save-state" && index + 1 < argc) {
            options.saveStatePath = argv[++index];
        } else if (argument == "--benchmark-preset" && index + 1 < argc) {
            options.benchmarkRun = true;
            options.benchmarkPreset = parseBenchmarkPreset(argv[++index]);
        } else if (argument == "--report-format" && index + 1 < argc) {
            const std::string value = argv[++index];
            if (value == "jsonl") {
                options.reportFormat = ReportFormat::JsonLines;
            } else {
                options.reportFormat = ReportFormat::Text;
            }
        }
    }

    applyBenchmarkPreset(options);
    return options;
}

struct RunSummary {
    std::uint64_t seed = 0;
    int steps = 0;
    double wallSeconds = 0.0;
    double stepsPerSecond = 0.0;
    double simulatedSeconds = 0.0;
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
    bool hasSnapshot = false;
    alife::WorldSnapshot snapshot {};
};

struct BatchSummary {
    int extinctRuns = 0;
    double totalWallSeconds = 0.0;
    double meanStepsPerSecond = 0.0;
    float meanPopulation = 0.0f;
    float minPopulation = 0.0f;
    float maxPopulation = 0.0f;
    float meanLineages = 0.0f;
    float meanDominantLineageShare = 0.0f;
    float meanAverageEnergy = 0.0f;
    float meanAverageShelter = 0.0f;
};

bool initializeSimulation(alife::Simulation& simulation, const CliOptions& options, std::string& error) {
    if (!options.loadStatePath.empty()) {
        if (!simulation.loadFromFile(options.loadStatePath)) {
            error = "Failed to load simulation state from " + options.loadStatePath;
            return false;
        }
        return true;
    }

    simulation.reset(options.seed);
    return true;
}

RunSummary runSummaryForSimulation(
    alife::Simulation& simulation,
    int steps,
    bool captureSnapshot,
    int snapshotLineageCount,
    const std::string& saveStatePath
) {
    constexpr float kStep = 1.0f / 60.0f;

    const auto wallStart = std::chrono::steady_clock::now();
    for (int index = 0; index < steps; ++index) {
        simulation.step(kStep);
    }
    const auto wallEnd = std::chrono::steady_clock::now();

    const alife::Stats& stats = simulation.stats();
    RunSummary summary {};
    summary.seed = simulation.worldSnapshot(0).seed;
    summary.steps = steps;
    summary.wallSeconds = std::chrono::duration<double>(wallEnd - wallStart).count();
    summary.stepsPerSecond = summary.wallSeconds > 1e-9
        ? static_cast<double>(steps) / summary.wallSeconds
        : 0.0;
    summary.simulatedSeconds = static_cast<double>(steps) * kStep;
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

    if (captureSnapshot) {
        summary.hasSnapshot = true;
        summary.snapshot = simulation.worldSnapshot(static_cast<std::size_t>(snapshotLineageCount));
    }

    if (!saveStatePath.empty()) {
        simulation.saveToFile(saveStatePath);
    }

    return summary;
}

BatchSummary summarizeBatchRuns(const std::vector<RunSummary>& runs) {
    BatchSummary summary {};
    if (runs.empty()) {
        return summary;
    }

    float minPopulation = std::numeric_limits<float>::max();
    float maxPopulation = 0.0f;
    float totalPopulation = 0.0f;
    float totalLineages = 0.0f;
    float totalDominantLineageShare = 0.0f;
    float totalAverageEnergy = 0.0f;
    float totalAverageShelter = 0.0f;
    double totalWallSeconds = 0.0;
    double totalSteps = 0.0;

    for (const RunSummary& run : runs) {
        const float population = static_cast<float>(run.population);
        minPopulation = std::min(minPopulation, population);
        maxPopulation = std::max(maxPopulation, population);
        totalPopulation += population;
        totalLineages += static_cast<float>(run.activeLineages);
        totalDominantLineageShare += run.dominantLineageShare;
        totalAverageEnergy += run.averageEnergy;
        totalAverageShelter += run.avgShelter;
        totalWallSeconds += run.wallSeconds;
        totalSteps += static_cast<double>(run.steps);
        if (run.extinctions > 0 || run.population == 0) {
            ++summary.extinctRuns;
        }
    }

    const float runCount = static_cast<float>(runs.size());
    summary.totalWallSeconds = totalWallSeconds;
    summary.meanStepsPerSecond = totalWallSeconds > 1e-9 ? totalSteps / totalWallSeconds : 0.0;
    summary.meanPopulation = totalPopulation / runCount;
    summary.minPopulation = minPopulation;
    summary.maxPopulation = maxPopulation;
    summary.meanLineages = totalLineages / runCount;
    summary.meanDominantLineageShare = totalDominantLineageShare / runCount;
    summary.meanAverageEnergy = totalAverageEnergy / runCount;
    summary.meanAverageShelter = totalAverageShelter / runCount;
    return summary;
}

void printTextCreatureSnapshot(const char* label, const alife::CreatureSnapshot& snapshot) {
    if (!snapshot.valid) {
        std::cout << "  " << label << "=none\n";
        return;
    }

    std::cout
        << "  " << label
        << " id=" << snapshot.id
        << " lineage=" << snapshot.lineageId
        << " role=" << alife::toString(snapshot.dietClass)
        << " energy=" << snapshot.energy
        << " health=" << snapshot.health
        << " age=" << snapshot.age
        << " mass=" << snapshot.mass
        << " body=" << snapshot.majorRadius << "x" << snapshot.minorRadius
        << " shelter=" << snapshot.substrateShelter
        << " shear=" << snapshot.localShear
        << '\n';
}

void printTextWorldSnapshot(const alife::WorldSnapshot& snapshot) {
    std::cout
        << "  snapshot seed=" << snapshot.seed
        << " time=" << snapshot.timeSeconds
        << " world=" << snapshot.worldWidth << "x" << snapshot.worldHeight
        << " pop=" << snapshot.population
        << " blooms=" << snapshot.blooms
        << " carrion=" << snapshot.carrion
        << " reefs=" << snapshot.reefs
        << " lineages=" << snapshot.activeLineages
        << '\n';
    printTextCreatureSnapshot("selected", snapshot.selectedCreature);
    printTextCreatureSnapshot("top_energy", snapshot.topEnergyCreature);
    if (!snapshot.topLineages.empty()) {
        std::cout << "  top_lineages";
        for (const auto& lineage : snapshot.topLineages) {
            std::cout
                << " L" << lineage.id
                << "(pop=" << lineage.population
                << ",depth=" << static_cast<int>(lineage.depth)
                << ",brain=" << lineage.avgBrainComplexity
                << ")";
        }
        std::cout << '\n';
    }
}

void printJsonCreatureSnapshot(const alife::CreatureSnapshot& snapshot) {
    if (!snapshot.valid) {
        std::cout << "null";
        return;
    }

    std::cout
        << "{\"id\":" << snapshot.id
        << ",\"lineage\":" << snapshot.lineageId
        << ",\"diet\":\"" << alife::toString(snapshot.dietClass) << "\""
        << ",\"x\":" << snapshot.position.x
        << ",\"y\":" << snapshot.position.y
        << ",\"energy\":" << snapshot.energy
        << ",\"health\":" << snapshot.health
        << ",\"age\":" << snapshot.age
        << ",\"mass\":" << snapshot.mass
        << ",\"major_radius\":" << snapshot.majorRadius
        << ",\"minor_radius\":" << snapshot.minorRadius
        << ",\"sensor_range\":" << snapshot.sensorRange
        << ",\"brain_complexity\":" << snapshot.brainComplexity
        << ",\"plant_affinity\":" << snapshot.plantAffinity
        << ",\"meat_affinity\":" << snapshot.meatAffinity
        << ",\"aggression\":" << snapshot.aggression
        << ",\"substrate_proximity\":" << snapshot.substrateProximity
        << ",\"substrate_shelter\":" << snapshot.substrateShelter
        << ",\"local_shear\":" << snapshot.localShear
        << "}";
}

void printJsonLineageSnapshots(const std::vector<alife::LineageSnapshot>& snapshots) {
    std::cout << "[";
    for (std::size_t index = 0; index < snapshots.size(); ++index) {
        if (index > 0) {
            std::cout << ",";
        }
        const auto& lineage = snapshots[index];
        std::cout
            << "{\"id\":" << lineage.id
            << ",\"parent_id\":" << lineage.parentId
            << ",\"depth\":" << static_cast<int>(lineage.depth)
            << ",\"age\":" << lineage.age
            << ",\"novelty_at_branch\":" << lineage.noveltyAtBranch
            << ",\"population\":" << lineage.population
            << ",\"peak_population\":" << lineage.peakPopulation
            << ",\"avg_brain_complexity\":" << lineage.avgBrainComplexity
            << "}";
    }
    std::cout << "]";
}

void printJsonWorldSnapshot(const alife::WorldSnapshot& snapshot) {
    std::cout
        << "{\"seed\":" << snapshot.seed
        << ",\"time_seconds\":" << snapshot.timeSeconds
        << ",\"world_width\":" << snapshot.worldWidth
        << ",\"world_height\":" << snapshot.worldHeight
        << ",\"population\":" << snapshot.population
        << ",\"blooms\":" << snapshot.blooms
        << ",\"carrion\":" << snapshot.carrion
        << ",\"reefs\":" << snapshot.reefs
        << ",\"births\":" << snapshot.births
        << ",\"deaths\":" << snapshot.deaths
        << ",\"extinctions\":" << snapshot.extinctions
        << ",\"season\":" << snapshot.season
        << ",\"active_lineages\":" << snapshot.activeLineages
        << ",\"dominant_lineage_share\":" << snapshot.dominantLineageShare
        << ",\"selected_creature\":";
    printJsonCreatureSnapshot(snapshot.selectedCreature);
    std::cout << ",\"top_energy_creature\":";
    printJsonCreatureSnapshot(snapshot.topEnergyCreature);
    std::cout << ",\"top_lineages\":";
    printJsonLineageSnapshots(snapshot.topLineages);
    std::cout << "}";
}

void printSmokeSummary(const RunSummary& summary, bool snapshotOutput) {
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
        << " wall_seconds=" << summary.wallSeconds
        << " steps_per_second=" << summary.stepsPerSecond
        << " avg_repro_threshold=" << summary.averageReproductionThreshold
        << '\n';
    if (snapshotOutput && summary.hasSnapshot) {
        printTextWorldSnapshot(summary.snapshot);
    }
}

void printJsonRunSummary(const RunSummary& summary, bool snapshotOutput) {
    std::cout
        << std::fixed << std::setprecision(6)
        << "{\"kind\":\"run\""
        << ",\"seed\":" << summary.seed
        << ",\"steps\":" << summary.steps
        << ",\"wall_seconds\":" << summary.wallSeconds
        << ",\"steps_per_second\":" << summary.stepsPerSecond
        << ",\"simulated_seconds\":" << summary.simulatedSeconds
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
        << ",\"snapshot\":";
    if (snapshotOutput && summary.hasSnapshot) {
        printJsonWorldSnapshot(summary.snapshot);
    } else {
        std::cout << "null";
    }
    std::cout << "}\n";
}

void printTextBatchSummary(
    const std::vector<RunSummary>& runs,
    const BatchSummary& batch,
    bool snapshotOutput,
    const std::string& scenarioLabel
) {
    std::cout
        << "batch-run count=" << runs.size()
        << " steps=" << (runs.empty() ? 0 : runs.front().steps)
        << " start_seed=" << (runs.empty() ? 0 : runs.front().seed)
        << " scenario=" << scenarioLabel
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
            << " wall_seconds=" << run.wallSeconds
            << " steps_per_second=" << run.stepsPerSecond
            << '\n';
        if (snapshotOutput && run.hasSnapshot) {
            printTextWorldSnapshot(run.snapshot);
        }
    }
    std::cout
        << "summary runs=" << runs.size()
        << " extinct_runs=" << batch.extinctRuns
        << " total_wall_seconds=" << batch.totalWallSeconds
        << " mean_steps_per_second=" << batch.meanStepsPerSecond
        << " mean_population=" << batch.meanPopulation
        << " min_population=" << batch.minPopulation
        << " max_population=" << batch.maxPopulation
        << " mean_lineages=" << batch.meanLineages
        << " mean_dom_lineage=" << batch.meanDominantLineageShare
        << " mean_avg_energy=" << batch.meanAverageEnergy
        << " mean_avg_shelter=" << batch.meanAverageShelter
        << '\n';
}

void printJsonBatchSummary(
    const std::vector<RunSummary>& runs,
    const BatchSummary& batch,
    bool snapshotOutput,
    const std::string& scenarioLabel
) {
    for (const RunSummary& run : runs) {
        printJsonRunSummary(run, snapshotOutput);
    }
    std::cout
        << std::fixed << std::setprecision(6)
        << "{\"kind\":\"summary\""
        << ",\"runs\":" << runs.size()
        << ",\"steps\":" << (runs.empty() ? 0 : runs.front().steps)
        << ",\"start_seed\":" << (runs.empty() ? 0 : runs.front().seed)
        << ",\"scenario\":\"" << scenarioLabel << "\""
        << ",\"extinct_runs\":" << batch.extinctRuns
        << ",\"total_wall_seconds\":" << batch.totalWallSeconds
        << ",\"mean_steps_per_second\":" << batch.meanStepsPerSecond
        << ",\"mean_population\":" << batch.meanPopulation
        << ",\"min_population\":" << batch.minPopulation
        << ",\"max_population\":" << batch.maxPopulation
        << ",\"mean_lineages\":" << batch.meanLineages
        << ",\"mean_dominant_lineage_share\":" << batch.meanDominantLineageShare
        << ",\"mean_avg_energy\":" << batch.meanAverageEnergy
        << ",\"mean_avg_shelter\":" << batch.meanAverageShelter
        << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    const CliOptions options = parseArgs(argc, argv);
    const std::string scenarioLabel = scenarioLabelForOptions(options);

    if (options.batchRun && (!options.loadStatePath.empty() || !options.saveStatePath.empty())) {
        std::cerr << "Batch runs do not support --load-state or --save-state.\n";
        return 1;
    }

    if (options.smokeTest) {
        alife::Simulation simulation;
        std::string error;
        if (!initializeSimulation(simulation, options, error)) {
            std::cerr << error << '\n';
            return 1;
        }

        const RunSummary summary = runSummaryForSimulation(
            simulation,
            options.smokeSteps,
            options.snapshotOutput,
            options.snapshotLineageCount,
            options.saveStatePath
        );
        if (options.reportFormat == ReportFormat::JsonLines) {
            printJsonRunSummary(summary, options.snapshotOutput);
        } else {
            printSmokeSummary(summary, options.snapshotOutput);
        }
        return summary.population > 0 ? 0 : 1;
    }

    if (options.batchRun) {
        std::vector<RunSummary> runs;
        runs.reserve(static_cast<std::size_t>(options.batchCount));

        for (int runIndex = 0; runIndex < options.batchCount; ++runIndex) {
            const std::uint64_t runSeed = options.seed + static_cast<std::uint64_t>(runIndex) * static_cast<std::uint64_t>(options.seedStride);
            alife::Simulation simulation;
            simulation.reset(runSeed);
            const RunSummary summary = runSummaryForSimulation(
                simulation,
                options.smokeSteps,
                options.snapshotOutput,
                options.snapshotLineageCount,
                {}
            );
            runs.push_back(summary);
        }
        const BatchSummary batch = summarizeBatchRuns(runs);

        if (options.reportFormat == ReportFormat::JsonLines) {
            printJsonBatchSummary(
                runs,
                batch,
                options.snapshotOutput,
                scenarioLabel
            );
        } else {
            printTextBatchSummary(
                runs,
                batch,
                options.snapshotOutput,
                scenarioLabel
            );
        }

        return batch.extinctRuns == 0 ? 0 : 1;
    }

    alife::Simulation simulation;
    std::string error;
    if (!initializeSimulation(simulation, options, error)) {
        std::cerr << error << '\n';
        return 1;
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
    if (!options.saveStatePath.empty() && !simulation.saveToFile(options.saveStatePath)) {
        std::cerr << "Failed to save simulation state to " << options.saveStatePath << '\n';
        return 1;
    }
    return 0;
}
