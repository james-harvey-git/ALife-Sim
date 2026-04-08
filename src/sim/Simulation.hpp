#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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
constexpr int kInternalInputs = 6;
constexpr int kMemorySize = 4;
constexpr int kInputCount = kSensorBuckets * kSensorChannels + kInternalInputs;
constexpr int kMaxHiddenCount = 16;
constexpr int kOutputCount = 6;

enum class DietClass : std::uint8_t {
    Grazer,
    Omnivore,
    Hunter
};

struct BrainGenome {
    int activeHidden = 12;
    std::array<float, (kInputCount + kMemorySize) * kMaxHiddenCount> hiddenWeights {};
    std::array<float, kMaxHiddenCount> hiddenBias {};
    std::array<float, kMaxHiddenCount * kOutputCount> outputWeights {};
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
    bool alive = true;
    std::array<float, kMemorySize> memory {};
    std::array<float, kOutputCount> outputs {};
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

struct Stats {
    std::size_t population = 0;
    std::size_t blooms = 0;
    std::size_t carrion = 0;
    std::uint64_t births = 0;
    std::uint64_t deaths = 0;
    std::uint64_t extinctions = 0;
    float avgPlantAffinity = 0.0f;
    float avgMeatAffinity = 0.0f;
    float avgMass = 0.0f;
    float avgSpeed = 0.0f;
    float season = 0.0f;
    int grazers = 0;
    int omnivores = 0;
    int hunters = 0;
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
};

class Simulation {
public:
    Simulation();

    void reset(std::uint64_t seed = 1);
    void step(float dt);

    void setSelectedCreature(std::uint64_t id);
    void clearSelection();
    std::uint64_t selectedCreature() const;

    std::optional<std::uint64_t> creatureAt(float worldX, float worldY, float radius) const;
    SelectionInfo selectionInfo() const;

    const std::vector<Creature>& creatures() const;
    const std::vector<Bloom>& blooms() const;
    const std::vector<Carrion>& carrion() const;
    const Stats& stats() const;

    float worldWidth() const;
    float worldHeight() const;
    float timeSeconds() const;
    float sampleNutrient(float x, float y) const;
    Vec2 sampleCurrent(float x, float y) const;

private:
    float worldWidth_ = 2400.0f;
    float worldHeight_ = 1800.0f;
    float timeSeconds_ = 0.0f;
    std::uint64_t seed_ = 1;
    std::uint64_t nextCreatureId_ = 1;
    std::uint64_t selectedCreatureId_ = 0;
    Stats stats_ {};
    Genome ancestorGenome_ {};
    std::mt19937_64 rng_ {};

    std::vector<Creature> creatures_ {};
    std::vector<Bloom> blooms_ {};
    std::vector<Carrion> carrion_ {};
};

std::string toString(DietClass dietClass);

}  // namespace alife
