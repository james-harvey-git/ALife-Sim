#include "sim/Simulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace alife {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;
constexpr std::size_t kInitialPopulation = 180;
constexpr std::size_t kMaxPopulation = 900;
constexpr std::size_t kTargetBlooms = 220;
constexpr float kBloomRespawnChance = 1.75f;
constexpr float kSpatialCellSize = 120.0f;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float randomFloat(std::mt19937_64& rng, float minValue, float maxValue) {
    std::uniform_real_distribution<float> dist(minValue, maxValue);
    return dist(rng);
}

int randomInt(std::mt19937_64& rng, int minValue, int maxValue) {
    std::uniform_int_distribution<int> dist(minValue, maxValue);
    return dist(rng);
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

Vec2 operator/(const Vec2& value, float scale) {
    return {value.x / scale, value.y / scale};
}

float dot(const Vec2& lhs, const Vec2& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

float lengthSquared(const Vec2& value) {
    return dot(value, value);
}

float length(const Vec2& value) {
    return std::sqrt(lengthSquared(value));
}

Vec2 normalize(const Vec2& value) {
    const float len = length(value);
    if (len < 1e-5f) {
        return {1.0f, 0.0f};
    }
    return value / len;
}

float wrapAxis(float value, float extent) {
    while (value < 0.0f) {
        value += extent;
    }
    while (value >= extent) {
        value -= extent;
    }
    return value;
}

Vec2 wrapPosition(Vec2 position, float worldWidth, float worldHeight) {
    position.x = wrapAxis(position.x, worldWidth);
    position.y = wrapAxis(position.y, worldHeight);
    return position;
}

float wrapAngle(float radians) {
    while (radians < -kPi) {
        radians += kTau;
    }
    while (radians > kPi) {
        radians -= kTau;
    }
    return radians;
}

Vec2 shortestWrappedDelta(const Vec2& from, const Vec2& to, float worldWidth, float worldHeight) {
    Vec2 delta {to.x - from.x, to.y - from.y};

    if (delta.x > worldWidth * 0.5f) {
        delta.x -= worldWidth;
    } else if (delta.x < -worldWidth * 0.5f) {
        delta.x += worldWidth;
    }

    if (delta.y > worldHeight * 0.5f) {
        delta.y -= worldHeight;
    } else if (delta.y < -worldHeight * 0.5f) {
        delta.y += worldHeight;
    }

    return delta;
}

float localNoise(float x, float y, float timeSeconds) {
    const float bandA = std::sin(x * 0.0041f + timeSeconds * 0.19f);
    const float bandB = std::cos(y * 0.0034f - timeSeconds * 0.13f);
    const float swirl = std::sin((x + y) * 0.0026f + timeSeconds * 0.07f);
    return 0.5f + 0.25f * bandA + 0.2f * bandB + 0.15f * swirl;
}

float sampleNutrientField(float x, float y, float timeSeconds) {
    return clamp01(localNoise(x, y, timeSeconds));
}

Vec2 sampleCurrentField(float x, float y, float timeSeconds) {
    const float flowX = std::sin(y * 0.0053f + timeSeconds * 0.33f) * 22.0f
        + std::cos((x + y) * 0.0017f - timeSeconds * 0.18f) * 10.0f;
    const float flowY = std::cos(x * 0.0042f - timeSeconds * 0.21f) * 18.0f
        + std::sin((x - y) * 0.0021f + timeSeconds * 0.12f) * 8.0f;
    return {flowX, flowY};
}

float seasonFactor(float timeSeconds) {
    return 0.7f + 0.3f * std::sin(timeSeconds * 0.045f);
}

DietClass classifyDiet(const Genome& genome) {
    const float grazerBias = genome.ecology.plantAffinity * 0.9f + (1.0f - genome.ecology.aggression) * 0.2f;
    const float hunterBias = genome.ecology.meatAffinity * 0.9f + genome.ecology.aggression * 0.25f
        + genome.morphology.jawLength * 0.25f;

    if (hunterBias > grazerBias + 0.18f) {
        return DietClass::Hunter;
    }
    if (grazerBias > hunterBias + 0.18f) {
        return DietClass::Grazer;
    }
    return DietClass::Omnivore;
}

Traits deriveTraits(const Genome& genome) {
    Traits traits {};

    const float coreRadius = lerp(8.0f, 18.0f, genome.morphology.coreSize);
    const float elongation = lerp(1.0f, 1.85f, genome.morphology.elongation);
    const float finBonus = lerp(0.6f, 1.45f, genome.morphology.finArea);
    const float armorBonus = lerp(0.85f, 1.85f, genome.morphology.armor);
    const float spikeBonus = lerp(0.0f, 5.5f, genome.morphology.spikes);

    traits.majorRadius = coreRadius * elongation;
    traits.minorRadius = coreRadius / lerp(1.0f, 1.32f, genome.morphology.elongation);
    traits.collisionRadius = std::max(traits.majorRadius, traits.minorRadius) + spikeBonus * 0.25f;

    const float bodyArea = traits.majorRadius * traits.minorRadius * kPi;
    traits.mass = (bodyArea * 0.07f + 10.0f) * (0.9f + 0.45f * armorBonus);

    traits.forwardThrust = (lerp(120.0f, 255.0f, genome.morphology.tailFlex) * finBonus)
        / (0.55f + std::sqrt(traits.mass) * 0.18f);
    traits.turnTorque = (lerp(2.0f, 8.2f, genome.morphology.tailFlex) * (1.2f + genome.morphology.finArea))
        / (0.75f + traits.mass * 0.03f);

    traits.forwardDrag = 0.85f + armorBonus * 0.26f - genome.morphology.elongation * 0.18f;
    traits.lateralDrag = 1.15f + genome.morphology.finArea * 1.05f + genome.morphology.armor * 0.35f;
    traits.angularDrag = 1.35f + genome.morphology.finArea * 0.4f + genome.morphology.armor * 0.5f;

    traits.biteReach = traits.majorRadius * lerp(0.8f, 1.55f, genome.morphology.jawLength);
    traits.biteArc = lerp(0.45f, 1.18f, genome.morphology.jawArc);
    traits.biteDamage = lerp(8.0f, 28.0f, genome.morphology.jawLength)
        * (0.55f + genome.ecology.meatAffinity * 0.9f);

    traits.grazeReach = traits.majorRadius * lerp(0.7f, 1.2f, genome.morphology.jawArc);
    traits.grazeRate = lerp(14.0f, 34.0f, genome.ecology.plantAffinity)
        * (0.7f + genome.morphology.jawArc * 0.4f);
    traits.carrionRate = lerp(6.0f, 24.0f, genome.ecology.scavengerBias)
        * (0.65f + genome.ecology.meatAffinity * 0.7f);

    traits.sensorRange = lerp(80.0f, 260.0f, genome.morphology.sensorRange);
    traits.sensorSpan = lerp(1.6f, 4.7f, genome.morphology.sensorSpan);
    traits.signalRange = traits.sensorRange * lerp(0.45f, 0.95f, genome.ecology.sociality);

    const float brainComplexity = 1.0f + static_cast<float>(genome.brain.activeHidden) / static_cast<float>(kMaxHiddenCount);
    traits.upkeep = 0.85f + traits.mass * 0.04f + traits.sensorRange * 0.008f
        + genome.morphology.armor * 0.65f + brainComplexity * 0.55f;
    traits.reproductionThreshold = 42.0f + traits.mass * 0.45f + genome.ecology.reproductionBias * 24.0f;
    traits.offspringEnergy = lerp(24.0f, 68.0f, genome.ecology.offspringInvestment) + traits.mass * 0.16f;
    traits.maxHealth = 52.0f + traits.mass * 2.25f + genome.morphology.armor * 22.0f;
    traits.carrionYield = traits.mass * 1.7f + traits.maxHealth * 0.36f;

    return traits;
}

Genome makeAncestorGenome(std::mt19937_64& rng) {
    Genome genome {};

    genome.morphology.coreSize = 0.55f;
    genome.morphology.elongation = 0.42f;
    genome.morphology.finArea = 0.58f;
    genome.morphology.tailFlex = 0.56f;
    genome.morphology.armor = 0.24f;
    genome.morphology.jawLength = 0.48f;
    genome.morphology.jawArc = 0.52f;
    genome.morphology.sensorSpan = 0.64f;
    genome.morphology.sensorRange = 0.57f;
    genome.morphology.spikes = 0.18f;
    genome.morphology.hue = 0.58f;
    genome.morphology.pattern = 0.48f;

    genome.ecology.plantAffinity = 0.58f;
    genome.ecology.meatAffinity = 0.42f;
    genome.ecology.aggression = 0.34f;
    genome.ecology.sociality = 0.52f;
    genome.ecology.reproductionBias = 0.46f;
    genome.ecology.offspringInvestment = 0.5f;
    genome.ecology.mutationVolatility = 0.34f;
    genome.ecology.scavengerBias = 0.38f;

    genome.brain.activeHidden = 12;

    for (float& value : genome.brain.hiddenWeights) {
        value = randomFloat(rng, -0.08f, 0.08f);
    }
    for (float& value : genome.brain.hiddenBias) {
        value = 0.0f;
    }
    for (float& value : genome.brain.outputWeights) {
        value = randomFloat(rng, -0.06f, 0.06f);
    }
    for (float& value : genome.brain.outputBias) {
        value = 0.0f;
    }
    for (float& value : genome.brain.memoryWeights) {
        value = randomFloat(rng, -0.04f, 0.04f);
    }
    for (float& value : genome.brain.memoryBias) {
        value = 0.0f;
    }

    genome.brain.activeHidden = 8;
    const auto hiddenSlot = [](int hiddenIndex, int inputIndex) {
        return hiddenIndex * (kInputCount + kMemorySize) + inputIndex;
    };
    const auto outputSlot = [](int hiddenIndex, int outputIndex) {
        return hiddenIndex * kOutputCount + outputIndex;
    };

    genome.brain.hiddenWeights[hiddenSlot(0, 10)] = 2.4f;
    genome.brain.hiddenWeights[hiddenSlot(0, 29)] = 1.0f;
    genome.brain.hiddenBias[0] = -0.15f;

    genome.brain.hiddenWeights[hiddenSlot(1, 0)] = 1.7f;
    genome.brain.hiddenWeights[hiddenSlot(1, 5)] = 1.8f;
    genome.brain.hiddenBias[1] = -0.25f;

    genome.brain.hiddenWeights[hiddenSlot(2, 15)] = 1.8f;
    genome.brain.hiddenWeights[hiddenSlot(2, 20)] = 1.7f;
    genome.brain.hiddenBias[2] = -0.25f;

    genome.brain.hiddenWeights[hiddenSlot(3, 25)] = -2.2f;
    genome.brain.hiddenBias[3] = 0.95f;

    genome.brain.hiddenWeights[hiddenSlot(4, 11)] = 1.7f;
    genome.brain.hiddenWeights[hiddenSlot(4, 12)] = 2.0f;
    genome.brain.hiddenWeights[hiddenSlot(4, 16)] = 1.1f;
    genome.brain.hiddenWeights[hiddenSlot(4, 17)] = 1.2f;
    genome.brain.hiddenBias[4] = -0.35f;

    genome.brain.hiddenWeights[hiddenSlot(5, 3)] = 1.8f;
    genome.brain.hiddenWeights[hiddenSlot(5, 8)] = 1.4f;
    genome.brain.hiddenBias[5] = -0.3f;

    genome.brain.hiddenWeights[hiddenSlot(6, 18)] = 1.4f;
    genome.brain.hiddenWeights[hiddenSlot(6, 23)] = 1.8f;
    genome.brain.hiddenBias[6] = -0.3f;

    genome.brain.hiddenWeights[hiddenSlot(7, 25)] = 2.4f;
    genome.brain.hiddenWeights[hiddenSlot(7, 26)] = 0.8f;
    genome.brain.hiddenWeights[hiddenSlot(7, 27)] = 1.8f;
    genome.brain.hiddenBias[7] = -1.7f;

    genome.brain.outputWeights[outputSlot(1, 0)] = 1.0f;
    genome.brain.outputWeights[outputSlot(2, 0)] = -1.0f;
    genome.brain.outputWeights[outputSlot(5, 0)] = 0.75f;
    genome.brain.outputWeights[outputSlot(6, 0)] = -0.75f;
    genome.brain.outputBias[0] = 0.05f;

    genome.brain.outputWeights[outputSlot(0, 1)] = 1.3f;
    genome.brain.outputWeights[outputSlot(1, 1)] = 0.6f;
    genome.brain.outputWeights[outputSlot(2, 1)] = 0.6f;
    genome.brain.outputWeights[outputSlot(3, 1)] = 1.0f;
    genome.brain.outputWeights[outputSlot(5, 1)] = -0.55f;
    genome.brain.outputWeights[outputSlot(6, 1)] = -0.55f;
    genome.brain.outputBias[1] = 0.2f;

    genome.brain.outputWeights[outputSlot(0, 2)] = 1.4f;
    genome.brain.outputWeights[outputSlot(1, 2)] = 0.7f;
    genome.brain.outputWeights[outputSlot(2, 2)] = 0.7f;
    genome.brain.outputWeights[outputSlot(3, 2)] = 0.7f;
    genome.brain.outputBias[2] = 0.35f;

    genome.brain.outputWeights[outputSlot(4, 3)] = 1.4f;
    genome.brain.outputBias[3] = -0.55f;

    genome.brain.outputWeights[outputSlot(5, 4)] = 0.9f;
    genome.brain.outputWeights[outputSlot(6, 4)] = 0.9f;
    genome.brain.outputBias[4] = -0.6f;

    genome.brain.outputWeights[outputSlot(7, 5)] = 1.8f;
    genome.brain.outputBias[5] = -0.12f;

    return genome;
}

void mutateScalar(std::mt19937_64& rng, float volatility, float& value, float extra = 1.0f) {
    const float amount = randomFloat(rng, -0.13f, 0.13f) * (0.45f + volatility * 0.8f) * extra;
    value = clamp01(value + amount);
}

Genome mutateGenome(const Genome& parent, std::mt19937_64& rng) {
    Genome child = parent;
    const float volatility = parent.ecology.mutationVolatility;

    mutateScalar(rng, volatility, child.morphology.coreSize);
    mutateScalar(rng, volatility, child.morphology.elongation);
    mutateScalar(rng, volatility, child.morphology.finArea);
    mutateScalar(rng, volatility, child.morphology.tailFlex);
    mutateScalar(rng, volatility, child.morphology.armor);
    mutateScalar(rng, volatility, child.morphology.jawLength);
    mutateScalar(rng, volatility, child.morphology.jawArc);
    mutateScalar(rng, volatility, child.morphology.sensorSpan);
    mutateScalar(rng, volatility, child.morphology.sensorRange);
    mutateScalar(rng, volatility, child.morphology.spikes);
    mutateScalar(rng, volatility, child.morphology.hue, 0.45f);
    mutateScalar(rng, volatility, child.morphology.pattern, 0.55f);

    mutateScalar(rng, volatility, child.ecology.plantAffinity);
    mutateScalar(rng, volatility, child.ecology.meatAffinity);
    mutateScalar(rng, volatility, child.ecology.aggression);
    mutateScalar(rng, volatility, child.ecology.sociality);
    mutateScalar(rng, volatility, child.ecology.reproductionBias);
    mutateScalar(rng, volatility, child.ecology.offspringInvestment);
    mutateScalar(rng, volatility, child.ecology.mutationVolatility, 0.4f);
    mutateScalar(rng, volatility, child.ecology.scavengerBias);

    const float weightMutationChance = 0.035f + volatility * 0.075f;
    for (float& value : child.brain.hiddenWeights) {
        if (randomFloat(rng, 0.0f, 1.0f) < weightMutationChance) {
            value = std::clamp(value + randomFloat(rng, -0.55f, 0.55f), -2.0f, 2.0f);
        }
    }
    for (float& value : child.brain.outputWeights) {
        if (randomFloat(rng, 0.0f, 1.0f) < weightMutationChance) {
            value = std::clamp(value + randomFloat(rng, -0.65f, 0.65f), -2.0f, 2.0f);
        }
    }
    for (float& value : child.brain.hiddenBias) {
        if (randomFloat(rng, 0.0f, 1.0f) < weightMutationChance * 0.8f) {
            value = std::clamp(value + randomFloat(rng, -0.2f, 0.2f), -1.2f, 1.2f);
        }
    }
    for (float& value : child.brain.outputBias) {
        if (randomFloat(rng, 0.0f, 1.0f) < weightMutationChance * 0.8f) {
            value = std::clamp(value + randomFloat(rng, -0.2f, 0.2f), -1.2f, 1.2f);
        }
    }
    for (float& value : child.brain.memoryWeights) {
        if (randomFloat(rng, 0.0f, 1.0f) < weightMutationChance) {
            value = std::clamp(value + randomFloat(rng, -0.4f, 0.4f), -1.5f, 1.5f);
        }
    }
    for (float& value : child.brain.memoryBias) {
        if (randomFloat(rng, 0.0f, 1.0f) < weightMutationChance * 0.7f) {
            value = std::clamp(value + randomFloat(rng, -0.14f, 0.14f), -0.9f, 0.9f);
        }
    }

    if (randomFloat(rng, 0.0f, 1.0f) < 0.08f + volatility * 0.1f) {
        child.brain.activeHidden += randomInt(rng, -1, 1);
        child.brain.activeHidden = std::clamp(child.brain.activeHidden, 6, kMaxHiddenCount);
    }

    return child;
}

struct SpatialHash {
    float worldWidth = 1.0f;
    float worldHeight = 1.0f;
    float cellSize = 1.0f;
    int columns = 1;
    int rows = 1;
    std::vector<std::vector<int>> cells;

    SpatialHash(float width, float height, float requestedCellSize)
        : worldWidth(width),
          worldHeight(height),
          cellSize(requestedCellSize),
          columns(std::max(1, static_cast<int>(std::ceil(width / requestedCellSize)))),
          rows(std::max(1, static_cast<int>(std::ceil(height / requestedCellSize)))),
          cells(static_cast<std::size_t>(columns * rows)) {}

    void clear() {
        for (auto& bucket : cells) {
            bucket.clear();
        }
    }

    int wrappedColumn(int column) const {
        if (column >= 0) {
            return column % columns;
        }
        return (columns + column % columns) % columns;
    }

    int wrappedRow(int row) const {
        if (row >= 0) {
            return row % rows;
        }
        return (rows + row % rows) % rows;
    }

    int indexFor(float x, float y) const {
        const int column = wrappedColumn(static_cast<int>(x / cellSize));
        const int row = wrappedRow(static_cast<int>(y / cellSize));
        return row * columns + column;
    }

    void insert(int creatureIndex, const Vec2& position) {
        cells[static_cast<std::size_t>(indexFor(position.x, position.y))].push_back(creatureIndex);
    }

    void query(const Vec2& position, float radius, std::vector<int>& out) const {
        out.clear();
        const int cellRadius = std::max(1, static_cast<int>(std::ceil(radius / cellSize)));
        const int centerColumn = static_cast<int>(position.x / cellSize);
        const int centerRow = static_cast<int>(position.y / cellSize);

        for (int rowOffset = -cellRadius; rowOffset <= cellRadius; ++rowOffset) {
            for (int columnOffset = -cellRadius; columnOffset <= cellRadius; ++columnOffset) {
                const int row = wrappedRow(centerRow + rowOffset);
                const int column = wrappedColumn(centerColumn + columnOffset);
                const auto& bucket = cells[static_cast<std::size_t>(row * columns + column)];
                out.insert(out.end(), bucket.begin(), bucket.end());
            }
        }
    }
};

float relativeAngle(const Creature& creature, const Vec2& offset) {
    const float targetAngle = std::atan2(offset.y, offset.x);
    return wrapAngle(targetAngle - creature.angle);
}

int sectorIndex(float relAngle, float span) {
    const float halfSpan = span * 0.5f;
    if (std::abs(relAngle) > halfSpan) {
        return -1;
    }
    const float normalized = (relAngle + halfSpan) / span;
    const int index = static_cast<int>(normalized * static_cast<float>(kSensorBuckets));
    return std::clamp(index, 0, kSensorBuckets - 1);
}

float outputDrive(float rawOutput) {
    return clamp01(rawOutput * 0.5f + 0.5f);
}

void forwardBrain(const Genome& genome, Creature& creature, const std::array<float, kInputCount>& inputs) {
    std::array<float, kMaxHiddenCount> hidden {};
    std::array<float, kMemorySize> nextMemory {};

    for (int hiddenIndex = 0; hiddenIndex < genome.brain.activeHidden; ++hiddenIndex) {
        float sum = genome.brain.hiddenBias[hiddenIndex];
        const int hiddenOffset = hiddenIndex * (kInputCount + kMemorySize);

        for (int inputIndex = 0; inputIndex < kInputCount; ++inputIndex) {
            sum += inputs[inputIndex] * genome.brain.hiddenWeights[hiddenOffset + inputIndex];
        }
        for (int memoryIndex = 0; memoryIndex < kMemorySize; ++memoryIndex) {
            sum += creature.memory[memoryIndex]
                * genome.brain.hiddenWeights[hiddenOffset + kInputCount + memoryIndex];
        }

        hidden[hiddenIndex] = std::tanh(sum);
    }

    for (int outputIndex = 0; outputIndex < kOutputCount; ++outputIndex) {
        float sum = genome.brain.outputBias[outputIndex];
        for (int hiddenIndex = 0; hiddenIndex < genome.brain.activeHidden; ++hiddenIndex) {
            sum += hidden[hiddenIndex] * genome.brain.outputWeights[hiddenIndex * kOutputCount + outputIndex];
        }
        creature.outputs[outputIndex] = std::tanh(sum);
    }

    for (int memoryIndex = 0; memoryIndex < kMemorySize; ++memoryIndex) {
        float sum = genome.brain.memoryBias[memoryIndex];
        for (int outputIndex = 0; outputIndex < kOutputCount; ++outputIndex) {
            sum += creature.outputs[outputIndex]
                * genome.brain.memoryWeights[memoryIndex * kOutputCount + outputIndex];
        }
        nextMemory[memoryIndex] = std::tanh(sum);
    }

    for (int memoryIndex = 0; memoryIndex < kMemorySize; ++memoryIndex) {
        creature.memory[memoryIndex] = std::lerp(creature.memory[memoryIndex], nextMemory[memoryIndex], 0.24f);
    }
}

Creature makeCreature(
    std::mt19937_64& rng,
    std::uint64_t id,
    const Genome& genome,
    float worldWidth,
    float worldHeight,
    Vec2 position,
    float energy
) {
    Creature creature {};
    creature.id = id;
    creature.genome = genome;
    creature.traits = deriveTraits(genome);
    creature.position = {
        wrapAxis(position.x, worldWidth),
        wrapAxis(position.y, worldHeight)
    };
    creature.velocity = {randomFloat(rng, -8.0f, 8.0f), randomFloat(rng, -8.0f, 8.0f)};
    creature.angle = randomFloat(rng, -kPi, kPi);
    creature.angularVelocity = randomFloat(rng, -0.5f, 0.5f);
    creature.energy = energy;
    creature.health = creature.traits.maxHealth;
    creature.age = randomFloat(rng, 0.0f, 20.0f);
    creature.signal = 0.0f;
    return creature;
}

}  // namespace

std::string toString(DietClass dietClass) {
    switch (dietClass) {
        case DietClass::Grazer:
            return "Grazer";
        case DietClass::Hunter:
            return "Hunter";
        default:
            return "Omnivore";
    }
}

Simulation::Simulation() {
    reset(1);
}

void Simulation::reset(std::uint64_t seed) {
    const std::uint64_t priorExtinctions = stats_.extinctions;
    seed_ = seed == 0 ? 1 : seed;
    rng_.seed(seed_);
    nextCreatureId_ = 1;
    selectedCreatureId_ = 0;
    timeSeconds_ = 0.0f;
    stats_ = {};
    stats_.extinctions = priorExtinctions;

    creatures_.clear();
    blooms_.clear();
    carrion_.clear();

    ancestorGenome_ = makeAncestorGenome(rng_);

    for (std::size_t index = 0; index < kInitialPopulation; ++index) {
        const Vec2 position {
            randomFloat(rng_, 0.0f, worldWidth_),
            randomFloat(rng_, 0.0f, worldHeight_)
        };
        creatures_.push_back(makeCreature(
            rng_,
            nextCreatureId_++,
            ancestorGenome_,
            worldWidth_,
            worldHeight_,
            position,
            130.0f
        ));
    }

    for (std::size_t index = 0; index < kTargetBlooms; ++index) {
        Bloom bloom {};
        bloom.position = {
            randomFloat(rng_, 0.0f, worldWidth_),
            randomFloat(rng_, 0.0f, worldHeight_)
        };
        bloom.maxEnergy = randomFloat(rng_, 78.0f, 130.0f);
        bloom.energy = bloom.maxEnergy * randomFloat(rng_, 0.55f, 1.0f);
        bloom.regrowthRate = randomFloat(rng_, 5.0f, 14.0f);
        blooms_.push_back(bloom);
    }
}

void Simulation::setSelectedCreature(std::uint64_t id) {
    selectedCreatureId_ = id;
}

void Simulation::clearSelection() {
    selectedCreatureId_ = 0;
}

std::uint64_t Simulation::selectedCreature() const {
    return selectedCreatureId_;
}

const std::vector<Creature>& Simulation::creatures() const {
    return creatures_;
}

const std::vector<Bloom>& Simulation::blooms() const {
    return blooms_;
}

const std::vector<Carrion>& Simulation::carrion() const {
    return carrion_;
}

const Stats& Simulation::stats() const {
    return stats_;
}

float Simulation::worldWidth() const {
    return worldWidth_;
}

float Simulation::worldHeight() const {
    return worldHeight_;
}

float Simulation::timeSeconds() const {
    return timeSeconds_;
}

float Simulation::sampleNutrient(float x, float y) const {
    return sampleNutrientField(wrapAxis(x, worldWidth_), wrapAxis(y, worldHeight_), timeSeconds_);
}

Vec2 Simulation::sampleCurrent(float x, float y) const {
    return sampleCurrentField(wrapAxis(x, worldWidth_), wrapAxis(y, worldHeight_), timeSeconds_);
}

std::optional<std::uint64_t> Simulation::creatureAt(float worldX, float worldY, float radius) const {
    std::optional<std::uint64_t> bestId;
    float bestDistanceSq = radius * radius;
    const Vec2 point {worldX, worldY};

    for (const Creature& creature : creatures_) {
        if (!creature.alive) {
            continue;
        }
        const Vec2 delta = shortestWrappedDelta(point, creature.position, worldWidth_, worldHeight_);
        const float distSq = lengthSquared(delta);
        if (distSq < bestDistanceSq) {
            bestDistanceSq = distSq;
            bestId = creature.id;
        }
    }

    return bestId;
}

SelectionInfo Simulation::selectionInfo() const {
    SelectionInfo info {};
    if (selectedCreatureId_ == 0) {
        return info;
    }

    auto it = std::find_if(creatures_.begin(), creatures_.end(), [&](const Creature& creature) {
        return creature.id == selectedCreatureId_;
    });
    if (it == creatures_.end()) {
        return info;
    }

    info.valid = true;
    info.id = it->id;
    info.dietClass = classifyDiet(it->genome);
    info.age = it->age;
    info.energy = it->energy;
    info.health = it->health;
    info.plantAffinity = it->genome.ecology.plantAffinity;
    info.meatAffinity = it->genome.ecology.meatAffinity;
    info.aggression = it->genome.ecology.aggression;
    info.majorRadius = it->traits.majorRadius;
    info.minorRadius = it->traits.minorRadius;
    info.mass = it->traits.mass;
    info.thrust = it->traits.forwardThrust;
    info.turnTorque = it->traits.turnTorque;
    info.sensorRange = it->traits.sensorRange;
    info.biteDamage = it->traits.biteDamage;
    info.grazeRate = it->traits.grazeRate;

    return info;
}

void Simulation::step(float dt) {
    if (creatures_.empty()) {
        ++stats_.extinctions;
        reset(seed_ + stats_.extinctions + 1);
        return;
    }

    timeSeconds_ += dt;
    stats_.season = seasonFactor(timeSeconds_);

    for (Bloom& bloom : blooms_) {
        const float nutrient = sampleNutrient(bloom.position.x, bloom.position.y);
        bloom.energy = std::min(
            bloom.maxEnergy,
            bloom.energy + bloom.regrowthRate * nutrient * stats_.season * dt
        );
    }

    for (Carrion& chunk : carrion_) {
        chunk.energy -= chunk.decayRate * dt;
    }
    carrion_.erase(std::remove_if(carrion_.begin(), carrion_.end(), [](const Carrion& carrion) {
        return carrion.energy <= 1.0f;
    }), carrion_.end());

    while (blooms_.size() < kTargetBlooms) {
        Bloom bloom {};
        bloom.position = {
            randomFloat(rng_, 0.0f, worldWidth_),
            randomFloat(rng_, 0.0f, worldHeight_)
        };
        bloom.maxEnergy = randomFloat(rng_, 78.0f, 130.0f);
        bloom.energy = bloom.maxEnergy * randomFloat(rng_, 0.55f, 1.0f);
        bloom.regrowthRate = randomFloat(rng_, 5.0f, 14.0f);
        blooms_.push_back(bloom);
    }

    if (randomFloat(rng_, 0.0f, 1.0f) < kBloomRespawnChance * dt) {
        Bloom bloom {};
        bloom.position = {
            randomFloat(rng_, 0.0f, worldWidth_),
            randomFloat(rng_, 0.0f, worldHeight_)
        };
        bloom.maxEnergy = randomFloat(rng_, 78.0f, 130.0f);
        bloom.energy = bloom.maxEnergy * randomFloat(rng_, 0.35f, 1.0f);
        bloom.regrowthRate = randomFloat(rng_, 5.0f, 14.0f);
        blooms_.push_back(bloom);
    }

    SpatialHash hash(worldWidth_, worldHeight_, kSpatialCellSize);
    std::vector<int> nearby;
    hash.clear();
    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        hash.insert(static_cast<int>(index), creatures_[index].position);
    }

    for (Creature& creature : creatures_) {
        std::array<float, kInputCount> inputs {};
        const float sensorRadius = creature.traits.sensorRange;

        hash.query(creature.position, sensorRadius, nearby);
        for (int otherIndex : nearby) {
            const Creature& other = creatures_[static_cast<std::size_t>(otherIndex)];
            if (other.id == creature.id || !other.alive) {
                continue;
            }

            const Vec2 delta = shortestWrappedDelta(creature.position, other.position, worldWidth_, worldHeight_);
            const float distSq = lengthSquared(delta);
            if (distSq > sensorRadius * sensorRadius || distSq < 1e-4f) {
                continue;
            }

            const float dist = std::sqrt(distSq);
            const int bucket = sectorIndex(relativeAngle(creature, delta), creature.traits.sensorSpan);
            if (bucket < 0) {
                continue;
            }

            const float closeness = 1.0f - dist / sensorRadius;
            const float opportunity = clamp01(
                (creature.traits.biteDamage * 1.4f - other.health * 0.18f) * 0.04f
                + (other.traits.mass < creature.traits.mass ? 0.35f : 0.0f)
                + creature.genome.ecology.meatAffinity * 0.4f
            ) * closeness;
            const float threat = clamp01(
                (other.traits.biteDamage * 0.06f)
                + (other.traits.mass > creature.traits.mass ? 0.35f : 0.0f)
                + outputDrive(other.outputs[3]) * 0.35f
            ) * closeness;
            const float signal = other.signal * closeness;

            inputs[bucket * kSensorChannels + 2] += opportunity;
            inputs[bucket * kSensorChannels + 3] += threat;
            inputs[bucket * kSensorChannels + 4] += signal;
        }

        for (const Bloom& bloom : blooms_) {
            const Vec2 delta = shortestWrappedDelta(creature.position, bloom.position, worldWidth_, worldHeight_);
            const float distSq = lengthSquared(delta);
            if (distSq > sensorRadius * sensorRadius || distSq < 1e-4f) {
                continue;
            }
            const int bucket = sectorIndex(relativeAngle(creature, delta), creature.traits.sensorSpan);
            if (bucket < 0) {
                continue;
            }
            const float dist = std::sqrt(distSq);
            const float closeness = 1.0f - dist / sensorRadius;
            inputs[bucket * kSensorChannels + 0] += closeness * (bloom.energy / bloom.maxEnergy);
        }

        for (const Carrion& chunk : carrion_) {
            const Vec2 delta = shortestWrappedDelta(creature.position, chunk.position, worldWidth_, worldHeight_);
            const float distSq = lengthSquared(delta);
            if (distSq > sensorRadius * sensorRadius || distSq < 1e-4f) {
                continue;
            }
            const int bucket = sectorIndex(relativeAngle(creature, delta), creature.traits.sensorSpan);
            if (bucket < 0) {
                continue;
            }
            const float dist = std::sqrt(distSq);
            const float closeness = 1.0f - dist / sensorRadius;
            inputs[bucket * kSensorChannels + 1] += closeness * clamp01(chunk.energy / 80.0f);
        }

        const Vec2 current = sampleCurrent(creature.position.x, creature.position.y);
        const Vec2 forward {std::cos(creature.angle), std::sin(creature.angle)};
        const float speed = length(creature.velocity);

        inputs[kSensorBuckets * kSensorChannels + 0] = clamp01(creature.energy / creature.traits.reproductionThreshold);
        inputs[kSensorBuckets * kSensorChannels + 1] = clamp01(creature.health / creature.traits.maxHealth);
        inputs[kSensorBuckets * kSensorChannels + 2] = clamp01(creature.age / 140.0f);
        inputs[kSensorBuckets * kSensorChannels + 3] = clamp01(speed / 120.0f);
        inputs[kSensorBuckets * kSensorChannels + 4] = sampleNutrient(creature.position.x, creature.position.y);
        inputs[kSensorBuckets * kSensorChannels + 5] = 0.5f + 0.5f * dot(normalize(current), forward);

        forwardBrain(creature.genome, creature, inputs);
    }

    for (Creature& creature : creatures_) {
        const float turnInput = creature.outputs[0];
        const float thrustInput = creature.outputs[1];
        const float signalDrive = outputDrive(creature.outputs[4]);
        const Vec2 current = sampleCurrent(creature.position.x, creature.position.y);

        const Vec2 forward {std::cos(creature.angle), std::sin(creature.angle)};
        const Vec2 side {-forward.y, forward.x};

        const Vec2 relativeVelocity = creature.velocity - current;
        float forwardVelocity = dot(relativeVelocity, forward);
        float lateralVelocity = dot(relativeVelocity, side);

        forwardVelocity += thrustInput * creature.traits.forwardThrust * dt;
        forwardVelocity *= std::exp(-creature.traits.forwardDrag * dt);
        lateralVelocity *= std::exp(-creature.traits.lateralDrag * dt);

        creature.angularVelocity += turnInput * creature.traits.turnTorque * dt;
        creature.angularVelocity *= std::exp(-creature.traits.angularDrag * dt);
        creature.angle = wrapAngle(creature.angle + creature.angularVelocity * dt);

        const Vec2 newRelative = forward * forwardVelocity + side * lateralVelocity;
        creature.velocity = current + newRelative;
        creature.position = wrapPosition(creature.position + creature.velocity * dt, worldWidth_, worldHeight_);
        creature.signal = signalDrive * lerp(0.2f, 1.0f, creature.genome.ecology.sociality);
        creature.age += dt;
        creature.cooldown = std::max(0.0f, creature.cooldown - dt);

        const float movementCost = creature.traits.upkeep * dt
            * (0.65f + std::abs(thrustInput) * 0.55f + std::abs(turnInput) * 0.25f + creature.signal * 0.3f);
        creature.energy -= movementCost;

        if (creature.energy > creature.traits.reproductionThreshold * 0.62f) {
            creature.health = std::min(creature.traits.maxHealth, creature.health + dt * (1.0f + creature.genome.morphology.armor * 2.0f));
        }
        if (creature.energy < 0.0f) {
            creature.health += creature.energy;
            creature.energy = 0.0f;
        }
    }

    hash.clear();
    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        hash.insert(static_cast<int>(index), creatures_[index].position);
    }

    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        Creature& creature = creatures_[index];
        if (!creature.alive) {
            continue;
        }

        hash.query(creature.position, creature.traits.collisionRadius * 2.3f, nearby);
        for (int otherIndex : nearby) {
            if (otherIndex <= static_cast<int>(index)) {
                continue;
            }

            Creature& other = creatures_[static_cast<std::size_t>(otherIndex)];
            if (!other.alive) {
                continue;
            }

            Vec2 delta = shortestWrappedDelta(creature.position, other.position, worldWidth_, worldHeight_);
            float distSq = lengthSquared(delta);
            const float minDistance = creature.traits.collisionRadius + other.traits.collisionRadius;
            if (distSq >= minDistance * minDistance) {
                continue;
            }

            float dist = std::sqrt(std::max(distSq, 1e-4f));
            Vec2 normal = delta / dist;
            const float overlap = minDistance - dist;
            const float totalMass = creature.traits.mass + other.traits.mass;
            const float creatureWeight = other.traits.mass / totalMass;
            const float otherWeight = creature.traits.mass / totalMass;

            creature.position = wrapPosition(creature.position - normal * (overlap * creatureWeight * 0.55f), worldWidth_, worldHeight_);
            other.position = wrapPosition(other.position + normal * (overlap * otherWeight * 0.55f), worldWidth_, worldHeight_);

            creature.velocity = creature.velocity - normal * (overlap * 3.5f * creatureWeight);
            other.velocity = other.velocity + normal * (overlap * 3.5f * otherWeight);
        }
    }

    std::vector<Creature> pendingSpawns;
    pendingSpawns.reserve(16);

    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        Creature& creature = creatures_[index];
        if (!creature.alive) {
            continue;
        }

        const float grazeDrive = outputDrive(creature.outputs[2]);
        const float biteDrive = outputDrive(creature.outputs[3]);
        const float reproduceDrive = outputDrive(creature.outputs[5]);
        const float reproductiveReadiness = std::clamp(
            (creature.energy - creature.traits.reproductionThreshold * 0.78f)
                / std::max(16.0f, creature.traits.reproductionThreshold * 0.42f),
            0.0f,
            1.0f
        ) * std::clamp(creature.age / 28.0f, 0.0f, 1.0f);
        const float reproductionIntent = std::max(reproduceDrive, reproductiveReadiness);

        if (grazeDrive > 0.18f) {
            const float ambientNutrient = sampleNutrient(creature.position.x, creature.position.y);
            creature.energy += ambientNutrient * grazeDrive * dt
                * (1.6f + creature.genome.ecology.plantAffinity * 2.6f);

            Bloom* bestBloom = nullptr;
            float bestDistSq = std::numeric_limits<float>::max();

            for (Bloom& bloom : blooms_) {
                const Vec2 delta = shortestWrappedDelta(creature.position, bloom.position, worldWidth_, worldHeight_);
                const float distSq = lengthSquared(delta);
                if (distSq < bestDistSq) {
                    const float dist = std::sqrt(distSq);
                    const float reach = creature.traits.grazeReach + std::sqrt(std::max(bloom.energy, 1.0f)) * 0.4f;
                    const Vec2 forward {std::cos(creature.angle), std::sin(creature.angle)};
                    if (dist < reach && dot(normalize(delta), forward) > -0.25f) {
                        bestDistSq = distSq;
                        bestBloom = &bloom;
                    }
                }
            }

            if (bestBloom != nullptr && bestBloom->energy > 1.0f) {
                const float harvest = std::min(
                    bestBloom->energy,
                    creature.traits.grazeRate * grazeDrive * dt
                );
                bestBloom->energy -= harvest;
                creature.energy += harvest * (0.95f + creature.genome.ecology.plantAffinity * 1.35f);
            }
        }

        if (biteDrive > 0.15f && creature.cooldown <= 0.0f) {
            Carrion* bestCarrion = nullptr;
            float bestCarrionScore = -1.0f;

            for (Carrion& chunk : carrion_) {
                const Vec2 delta = shortestWrappedDelta(creature.position, chunk.position, worldWidth_, worldHeight_);
                const float dist = length(delta);
                if (dist > creature.traits.biteReach + 12.0f) {
                    continue;
                }
                const float score = chunk.energy / (dist + 12.0f);
                if (score > bestCarrionScore) {
                    bestCarrionScore = score;
                    bestCarrion = &chunk;
                }
            }

            if (bestCarrion != nullptr && bestCarrion->energy > 1.0f) {
                const float harvest = std::min(
                    bestCarrion->energy,
                    creature.traits.carrionRate * biteDrive * dt
                );
                bestCarrion->energy -= harvest;
                creature.energy += harvest * (0.35f + creature.genome.ecology.meatAffinity * 0.6f
                    + creature.genome.ecology.scavengerBias * 0.4f);
                creature.cooldown = 0.12f;
            } else {
                hash.query(creature.position, creature.traits.biteReach * 1.6f, nearby);
                Creature* bestTarget = nullptr;
                float bestTargetScore = -std::numeric_limits<float>::infinity();

                for (int otherIndex : nearby) {
                    Creature& other = creatures_[static_cast<std::size_t>(otherIndex)];
                    if (!other.alive || other.id == creature.id) {
                        continue;
                    }

                    const Vec2 delta = shortestWrappedDelta(creature.position, other.position, worldWidth_, worldHeight_);
                    const float dist = length(delta);
                    if (dist > creature.traits.biteReach + other.traits.collisionRadius * 0.35f) {
                        continue;
                    }

                    const Vec2 forward {std::cos(creature.angle), std::sin(creature.angle)};
                    const float facing = dot(normalize(delta), forward);
                    if (facing < std::cos(creature.traits.biteArc)) {
                        continue;
                    }

                    const float score = creature.genome.ecology.meatAffinity * 0.7f
                        + creature.genome.ecology.aggression * 0.55f
                        + (other.traits.mass < creature.traits.mass ? 0.35f : -0.15f)
                        + (other.health < creature.traits.biteDamage * 3.0f ? 0.25f : 0.0f)
                        - dist * 0.015f;
                    if (score > bestTargetScore) {
                        bestTargetScore = score;
                        bestTarget = &other;
                    }
                }

                if (bestTarget != nullptr) {
                    const float impact = creature.traits.biteDamage * biteDrive * dt;
                    const float mitigation = 1.0f - bestTarget->genome.morphology.armor * 0.42f;
                    bestTarget->health -= impact * mitigation;
                    creature.energy += impact * (0.08f + creature.genome.ecology.meatAffinity * 0.16f);
                    creature.cooldown = 0.22f;
                }
            }
        }

        if (reproductionIntent > 0.4f
            && creature.energy > creature.traits.reproductionThreshold * 0.92f
            && creature.age > 18.0f
            && creatures_.size() + pendingSpawns.size() < kMaxPopulation) {
            const Genome childGenome = mutateGenome(creature.genome, rng_);
            const float childEnergy = std::min(creature.energy * 0.46f, creature.traits.offspringEnergy * 1.35f);
            creature.energy -= childEnergy;
            creature.health -= childEnergy * 0.03f;

            const Vec2 offset {
                std::cos(creature.angle + kPi) * (creature.traits.collisionRadius + 12.0f),
                std::sin(creature.angle + kPi) * (creature.traits.collisionRadius + 12.0f)
            };
            pendingSpawns.push_back(makeCreature(
                rng_,
                nextCreatureId_++,
                childGenome,
                worldWidth_,
                worldHeight_,
                creature.position + offset,
                std::max(28.0f, childEnergy * 0.78f)
            ));
            pendingSpawns.back().age = 0.0f;
            pendingSpawns.back().velocity = creature.velocity * 0.35f;
            ++stats_.births;
        }

        if (creature.health <= 0.0f || creature.age > 420.0f) {
            creature.alive = false;
        }
    }

    for (const Creature& spawn : pendingSpawns) {
        creatures_.push_back(spawn);
    }

    for (std::size_t index = 0; index < creatures_.size();) {
        Creature& creature = creatures_[index];
        if (creature.alive) {
            ++index;
            continue;
        }

        Carrion chunk {};
        chunk.position = creature.position;
        chunk.energy = std::max(18.0f, creature.traits.carrionYield + creature.energy * 0.25f);
        chunk.decayRate = lerp(3.0f, 7.5f, creature.genome.morphology.armor);
        carrion_.push_back(chunk);

        if (selectedCreatureId_ == creature.id) {
            selectedCreatureId_ = 0;
        }

        ++stats_.deaths;
        creatures_[index] = creatures_.back();
        creatures_.pop_back();
    }

    if (creatures_.empty()) {
        ++stats_.extinctions;
        reset(seed_ + stats_.extinctions + 1);
        return;
    }

    stats_.population = creatures_.size();
    stats_.blooms = blooms_.size();
    stats_.carrion = carrion_.size();
    stats_.grazers = 0;
    stats_.omnivores = 0;
    stats_.hunters = 0;
    stats_.avgPlantAffinity = 0.0f;
    stats_.avgMeatAffinity = 0.0f;
    stats_.avgMass = 0.0f;
    stats_.avgSpeed = 0.0f;

    for (const Creature& creature : creatures_) {
        stats_.avgPlantAffinity += creature.genome.ecology.plantAffinity;
        stats_.avgMeatAffinity += creature.genome.ecology.meatAffinity;
        stats_.avgMass += creature.traits.mass;
        stats_.avgSpeed += length(creature.velocity);

        switch (classifyDiet(creature.genome)) {
            case DietClass::Grazer:
                ++stats_.grazers;
                break;
            case DietClass::Hunter:
                ++stats_.hunters;
                break;
            default:
                ++stats_.omnivores;
                break;
        }
    }

    const float divisor = static_cast<float>(creatures_.size());
    stats_.avgPlantAffinity /= divisor;
    stats_.avgMeatAffinity /= divisor;
    stats_.avgMass /= divisor;
    stats_.avgSpeed /= divisor;
}

}  // namespace alife
