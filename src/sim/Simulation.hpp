#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace alife {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

constexpr int kSensorBuckets = 5;
constexpr int kSensorChannels = 5;
constexpr int kInternalInputs = 9;
constexpr int kMemorySize = 4;
constexpr int kBodySegments = 4;
constexpr int kInputCount = kSensorBuckets * kSensorChannels + kInternalInputs;
constexpr int kMaxHiddenCount = 20;
constexpr int kMaxConnectionCount = 128;
constexpr int kOutputCount = 6;

enum class DietClass : std::uint8_t {
    Grazer,
    Omnivore,
    Hunter
};

struct BrainGenome {
    enum class NodeKind : std::uint8_t {
        Input,
        Memory,
        Hidden,
        Output
    };

    struct ConnectionGene {
        NodeKind fromKind = NodeKind::Input;
        std::uint8_t fromIndex = 0;
        NodeKind toKind = NodeKind::Output;
        std::uint8_t toIndex = 0;
        float weight = 0.0f;
        std::uint32_t innovation = 0;
    };

    int hiddenCount = 0;
    int connectionCount = 0;
    std::array<ConnectionGene, kMaxConnectionCount> connections {};
    std::array<std::uint32_t, kMaxHiddenCount> hiddenNodeIds {};
    std::array<float, kMaxHiddenCount> hiddenBias {};
    std::array<float, kOutputCount> outputBias {};
    std::array<float, kOutputCount * kMemorySize> memoryWeights {};
    std::array<float, kMemorySize> memoryBias {};
};

struct MorphologyGenome {
    float coreSize = 0.5f;
    float elongation = 0.5f;
    float finArea = 0.5f;
    float tailFlex = 0.5f;
    float armor = 0.5f;
    float jawLength = 0.5f;
    float jawArc = 0.5f;
    float sensorSpan = 0.5f;
    float sensorRange = 0.5f;
    float spikes = 0.5f;
    float hue = 0.5f;
    float pattern = 0.5f;
};

struct EcologyGenome {
    float plantAffinity = 0.5f;
    float meatAffinity = 0.5f;
    float aggression = 0.5f;
    float sociality = 0.5f;
    float reproductionBias = 0.5f;
    float offspringInvestment = 0.5f;
    float mutationVolatility = 0.5f;
    float scavengerBias = 0.5f;
};

struct Genome {
    MorphologyGenome morphology;
    EcologyGenome ecology;
    BrainGenome brain;
};

struct Traits {
    float majorRadius = 12.0f;
    float minorRadius = 8.0f;
    float collisionRadius = 12.0f;
    float finSpan = 8.0f;
    float segmentSpacing = 10.0f;
    float tailWaveAmplitude = 0.18f;
    float mass = 1.0f;
    float forwardThrust = 80.0f;
    float turnTorque = 4.0f;
    float forwardDrag = 1.2f;
    float lateralDrag = 1.8f;
    float angularDrag = 2.0f;
    float biteReach = 16.0f;
    float biteArc = 0.8f;
    float biteDamage = 14.0f;
    float grazeReach = 18.0f;
    float grazeRate = 12.0f;
    float carrionRate = 14.0f;
    float sensorRange = 150.0f;
    float sensorSpan = 3.14f;
    float upkeep = 3.0f;
    float reproductionThreshold = 120.0f;
    float offspringEnergy = 45.0f;
    float maxHealth = 100.0f;
    float signalRange = 120.0f;
    float carrionYield = 70.0f;
};

struct Creature {
    std::uint64_t id = 0;
    Genome genome;
    Traits traits;
    Vec2 position {};
    Vec2 velocity {};
    float angle = 0.0f;
    float angularVelocity = 0.0f;
    float energy = 80.0f;
    float health = 100.0f;
    float age = 0.0f;
    float signal = 0.0f;
    float cooldown = 0.0f;
    float recentIntake = 0.0f;
    float gaitPhase = 0.0f;
    float bodyCurvature = 0.0f;
    float bodySlip = 0.0f;
    float flowAlignment = 0.0f;
    float substrateProximity = 0.0f;
    float substrateContact = 0.0f;
    float substrateShelter = 0.0f;
    float localShear = 0.0f;
    std::uint32_t lineageId = 0;
    std::uint8_t lineageDepth = 0;
    std::uint64_t parentId = 0;
    float brainNovelty = 0.0f;
    bool alive = true;
    std::array<float, kMemorySize> memory {};
    std::array<float, kMaxHiddenCount> hiddenActivations {};
    std::array<float, kOutputCount> outputs {};
    std::array<float, kInputCount> lastInputs {};
    std::array<Vec2, kBodySegments> bodyPoints {};
    std::array<Vec2, kBodySegments> bodyVelocities {};
    std::array<float, kBodySegments> bodyRadii {};
};

struct Bloom {
    Vec2 position {};
    float energy = 65.0f;
    float maxEnergy = 90.0f;
    float regrowthRate = 10.0f;
};

struct Carrion {
    Vec2 position {};
    float energy = 40.0f;
    float decayRate = 5.0f;
};

struct Reef {
    Vec2 position {};
    float radius = 90.0f;
    float roughness = 0.5f;
    float nutrientBoost = 0.5f;
    float shear = 0.5f;
};

struct WorldConfig {
    float width = 2400.0f;
    float height = 1800.0f;
    std::size_t initialPopulation = 180;
    std::size_t maxPopulation = 900;
};

struct EnvironmentConfig {
    std::size_t targetBlooms = 220;
    std::size_t targetReefs = 8;
    float bloomRespawnChance = 1.75f;
    float spatialCellSize = 120.0f;
    int nutrientGridWidth = 96;
    int nutrientGridHeight = 72;
    float nutrientCellCapacity = 5.0f;
    float nutrientRecoveryRate = 0.42f;
    float nutrientDiffusionRate = 0.16f;
};

struct EvolutionConfig {
    float weightMutationBaseChance = 0.035f;
    float weightMutationVolatilityScale = 0.075f;
    float addConnectionBaseChance = 0.05f;
    float addConnectionVolatilityScale = 0.09f;
    float addHiddenBaseChance = 0.012f;
    float addHiddenVolatilityScale = 0.035f;
    float removeConnectionBaseChance = 0.016f;
    float removeConnectionVolatilityScale = 0.035f;
    float lineageBranchHiddenNoveltyThreshold = 0.12f;
    int lineageBranchStructuralDelta = 3;
    float lineageBranchCompatibilityThreshold = 0.52f;
    float lineageBranchStructuralCompatibilityThreshold = 0.3f;
};

struct DebugConfig {
    bool autoSelectOnReset = true;
};

struct SimulationConfig {
    WorldConfig world {};
    EnvironmentConfig environment {};
    EvolutionConfig evolution {};
    DebugConfig debug {};
};

struct Stats {
    std::size_t population = 0;
    std::size_t blooms = 0;
    std::size_t carrion = 0;
    std::size_t reefs = 0;
    std::uint64_t births = 0;
    std::uint64_t deaths = 0;
    std::uint64_t extinctions = 0;
    float avgPlantAffinity = 0.0f;
    float avgMeatAffinity = 0.0f;
    float avgMass = 0.0f;
    float avgSpeed = 0.0f;
    float avgBrainComplexity = 0.0f;
    float avgBrainConnections = 0.0f;
    float avgSubstrateContact = 0.0f;
    float avgSubstrateShelter = 0.0f;
    float avgLocalShear = 0.0f;
    float energyFromAmbientGrazing = 0.0f;
    float energyFromBloomHarvest = 0.0f;
    float energyFromCarrion = 0.0f;
    float energyFromPredation = 0.0f;
    float energySpentOnUpkeep = 0.0f;
    float energySpentOnReproduction = 0.0f;
    float season = 0.0f;
    int grazers = 0;
    int omnivores = 0;
    int hunters = 0;
    int activeLineages = 0;
    float dominantLineageShare = 0.0f;
};

struct HistorySample {
    float time = 0.0f;
    std::size_t population = 0;
    std::size_t blooms = 0;
    std::size_t carrion = 0;
    std::size_t reefs = 0;
    float avgPlantAffinity = 0.0f;
    float avgMeatAffinity = 0.0f;
    float avgMass = 0.0f;
    float avgBrainComplexity = 0.0f;
    float avgSubstrateContact = 0.0f;
    float avgSubstrateShelter = 0.0f;
    int grazers = 0;
    int omnivores = 0;
    int hunters = 0;
    int activeLineages = 0;
    float dominantLineageShare = 0.0f;
};

struct SelectionInfo {
    bool valid = false;
    std::uint64_t id = 0;
    DietClass dietClass = DietClass::Omnivore;
    float age = 0.0f;
    float energy = 0.0f;
    float health = 0.0f;
    float plantAffinity = 0.0f;
    float meatAffinity = 0.0f;
    float aggression = 0.0f;
    float majorRadius = 0.0f;
    float minorRadius = 0.0f;
    float mass = 0.0f;
    float thrust = 0.0f;
    float turnTorque = 0.0f;
    float sensorRange = 0.0f;
    float biteDamage = 0.0f;
    float grazeRate = 0.0f;
    float finSpan = 0.0f;
    float segmentSpacing = 0.0f;
    float tailWaveAmplitude = 0.0f;
    float upkeep = 0.0f;
    float reproductionThreshold = 0.0f;
    float bodyCurvature = 0.0f;
    float bodySlip = 0.0f;
    float flowAlignment = 0.0f;
    float substrateProximity = 0.0f;
    float substrateContact = 0.0f;
    float substrateShelter = 0.0f;
    float localShear = 0.0f;
    std::uint32_t lineageId = 0;
    std::uint32_t lineageParentId = 0;
    std::size_t lineagePopulation = 0;
    std::uint8_t lineageDepth = 0;
    float lineageAge = 0.0f;
    float brainNovelty = 0.0f;
    int brainHiddenCount = 0;
    int brainConnectionCount = 0;
    float brainComplexity = 0.0f;
    std::array<float, kInputCount> inputs {};
    std::array<float, kMaxHiddenCount> hiddenActivations {};
    std::array<BrainGenome::ConnectionGene, kMaxConnectionCount> connections {};
    std::array<float, kOutputCount> outputs {};
    std::array<float, kMemorySize> memory {};
    std::array<float, kSensorBuckets> plantSense {};
    std::array<float, kSensorBuckets> carrionSense {};
    std::array<float, kSensorBuckets> opportunitySense {};
    std::array<float, kSensorBuckets> threatSense {};
    std::array<float, kSensorBuckets> signalSense {};
};

struct EnvironmentProbe {
    float nutrient = 0.0f;
    Vec2 current {};
    float substrate = 0.0f;
    float shelter = 0.0f;
    float shear = 0.0f;
};

struct CreatureSnapshot {
    bool valid = false;
    std::uint64_t id = 0;
    std::uint32_t lineageId = 0;
    DietClass dietClass = DietClass::Omnivore;
    Vec2 position {};
    float energy = 0.0f;
    float health = 0.0f;
    float age = 0.0f;
    float mass = 0.0f;
    float majorRadius = 0.0f;
    float minorRadius = 0.0f;
    float sensorRange = 0.0f;
    float brainComplexity = 0.0f;
    float plantAffinity = 0.0f;
    float meatAffinity = 0.0f;
    float aggression = 0.0f;
    float substrateProximity = 0.0f;
    float substrateShelter = 0.0f;
    float localShear = 0.0f;
};

struct LineageSnapshot {
    std::uint32_t id = 0;
    std::uint32_t parentId = 0;
    std::uint8_t depth = 0;
    float age = 0.0f;
    float noveltyAtBranch = 0.0f;
    std::size_t population = 0;
    std::size_t peakPopulation = 0;
    float avgBrainComplexity = 0.0f;
};

struct WorldSnapshot {
    std::uint64_t seed = 0;
    float timeSeconds = 0.0f;
    float worldWidth = 0.0f;
    float worldHeight = 0.0f;
    SimulationConfig config {};
    std::size_t population = 0;
    std::size_t blooms = 0;
    std::size_t carrion = 0;
    std::size_t reefs = 0;
    std::uint64_t births = 0;
    std::uint64_t deaths = 0;
    std::uint64_t extinctions = 0;
    float season = 0.0f;
    int activeLineages = 0;
    float dominantLineageShare = 0.0f;
    CreatureSnapshot selectedCreature {};
    CreatureSnapshot topEnergyCreature {};
    std::vector<LineageSnapshot> topLineages {};
};

struct InnovationRecord {
    std::uint32_t fromNodeId = 0;
    std::uint32_t toNodeId = 0;
    std::uint32_t innovation = 0;
};

struct LineageRecord {
    std::uint32_t id = 0;
    std::uint32_t parentId = 0;
    std::uint8_t depth = 0;
    std::uint64_t founderCreatureId = 0;
    float founderTime = 0.0f;
    float noveltyAtBranch = 0.0f;
    float lastSeenTime = 0.0f;
    std::size_t currentPopulation = 0;
    std::size_t peakPopulation = 0;
    float avgBrainComplexity = 0.0f;
};

class Simulation {
public:
    explicit Simulation(const SimulationConfig& config = {});

    void reset(std::uint64_t seed = 1);
    void step(float dt);

    void setSelectedCreature(std::uint64_t id);
    void clearSelection();
    std::uint64_t selectedCreature() const;
    bool selectRandomCreature();
    bool selectTopEnergyCreature();
    bool selectDominantLineageCreature();
    bool selectNewestLineageCreature();

    std::optional<std::uint64_t> creatureAt(float worldX, float worldY, float radius) const;
    SelectionInfo selectionInfo() const;

    const std::vector<Creature>& creatures() const;
    const std::vector<Bloom>& blooms() const;
    const std::vector<Carrion>& carrion() const;
    const std::vector<Reef>& reefs() const;
    const std::deque<HistorySample>& history() const;
    const Stats& stats() const;

    float worldWidth() const;
    float worldHeight() const;
    float timeSeconds() const;
    const SimulationConfig& config() const;
    float sampleNutrient(float x, float y) const;
    Vec2 sampleCurrent(float x, float y) const;
    EnvironmentProbe probeEnvironment(float x, float y) const;
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
    CreatureSnapshot selectedCreatureSnapshot() const;
    CreatureSnapshot topEnergyCreatureSnapshot() const;
    std::vector<LineageSnapshot> topLineageSnapshots(std::size_t maxCount = 5) const;
    WorldSnapshot worldSnapshot(std::size_t topLineageCount = 5) const;

private:
    SimulationConfig config_ {};
    float worldWidth_ = 2400.0f;
    float worldHeight_ = 1800.0f;
    float timeSeconds_ = 0.0f;
    std::uint64_t seed_ = 1;
    std::uint64_t nextCreatureId_ = 1;
    std::uint64_t selectedCreatureId_ = 0;
    std::uint32_t trackedLineageId_ = 0;
    bool autoSelectionEnabled_ = true;
    Stats stats_ {};
    float historyAccumulator_ = 0.0f;
    Genome ancestorGenome_ {};
    std::mt19937_64 rng_ {};
    std::uint32_t nextInnovationId_ = 1;
    std::uint32_t nextHiddenNodeId_ = 1;
    std::uint32_t nextLineageId_ = 1;

    std::vector<Creature> creatures_ {};
    std::vector<Bloom> blooms_ {};
    std::vector<Carrion> carrion_ {};
    std::vector<Reef> reefs_ {};
    std::vector<float> nutrientGrid_ {};
    std::vector<float> nutrientScratch_ {};
    std::deque<HistorySample> history_ {};
    std::vector<InnovationRecord> innovations_ {};
    std::vector<LineageRecord> lineages_ {};

    bool selectRepresentativeInLineage(std::uint32_t lineageId);
    CreatureSnapshot makeCreatureSnapshot(const Creature& creature) const;
};

std::string toString(DietClass dietClass);

}  // namespace alife
