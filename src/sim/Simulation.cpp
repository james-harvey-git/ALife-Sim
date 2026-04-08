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
constexpr std::size_t kTargetReefs = 8;
constexpr float kBloomRespawnChance = 1.75f;
constexpr float kSpatialCellSize = 120.0f;
constexpr std::uint32_t kInputNodeBase = 1;
constexpr std::uint32_t kMemoryNodeBase = kInputNodeBase + kInputCount;
constexpr std::uint32_t kOutputNodeBase = kMemoryNodeBase + kMemorySize;
constexpr std::uint32_t kFirstHiddenNodeId = kOutputNodeBase + kOutputCount;

using NodeKind = BrainGenome::NodeKind;
using ConnectionGene = BrainGenome::ConnectionGene;

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

float cross(const Vec2& lhs, const Vec2& rhs) {
    return lhs.x * rhs.y - lhs.y * rhs.x;
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
    return 0.58f + 0.16f * bandA + 0.13f * bandB + 0.09f * swirl;
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

struct HabitatSample {
    float proximity = 0.0f;
    float contact = 0.0f;
    float nutrientBoost = 0.0f;
    float shear = 0.0f;
    Vec2 currentOffset {};
};

HabitatSample sampleHabitatField(
    float x,
    float y,
    const std::vector<Reef>& reefs,
    float worldWidth,
    float worldHeight
) {
    HabitatSample sample {};
    const Vec2 point {x, y};

    for (const Reef& reef : reefs) {
        const Vec2 delta = shortestWrappedDelta(reef.position, point, worldWidth, worldHeight);
        const float haloRadius = reef.radius * (1.45f + reef.shear * 0.35f);
        const float distSq = lengthSquared(delta);
        if (distSq > haloRadius * haloRadius) {
            continue;
        }

        const float dist = std::sqrt(std::max(distSq, 1e-4f));
        const Vec2 radial = dist > 1e-4f ? delta / dist : Vec2 {1.0f, 0.0f};
        const Vec2 tangential {-radial.y, radial.x};
        const float contact = clamp01(1.0f - dist / std::max(reef.radius, 1.0f));
        const float halo = clamp01(1.0f - dist / haloRadius);
        const float edgeWidth = reef.radius * (0.45f + reef.roughness * 0.35f);
        const float edge = clamp01(1.0f - std::abs(dist - reef.radius) / std::max(edgeWidth, 1.0f));

        sample.proximity = std::max(sample.proximity, std::max(halo, contact));
        sample.contact = std::max(sample.contact, contact);
        sample.shear = std::max(sample.shear, clamp01(edge * (0.3f + reef.shear * 0.25f) + halo * 0.08f));
        sample.nutrientBoost += edge * (0.12f + reef.nutrientBoost * 0.28f)
            + contact * (0.04f + reef.nutrientBoost * 0.08f);
        sample.currentOffset = sample.currentOffset
            + tangential * ((1.5f + reef.shear * 4.5f) * edge * halo)
            - radial * ((1.5f + reef.roughness * 3.0f) * contact);
    }

    sample.nutrientBoost = clamp01(sample.nutrientBoost);
    return sample;
}

float sampleNutrientWithReefs(
    float x,
    float y,
    float timeSeconds,
    const std::vector<Reef>& reefs,
    float worldWidth,
    float worldHeight
) {
    const HabitatSample habitat = sampleHabitatField(x, y, reefs, worldWidth, worldHeight);
    const float base = sampleNutrientField(x, y, timeSeconds);
    return clamp01(base * (1.0f - habitat.contact * 0.35f) + habitat.nutrientBoost);
}

Vec2 sampleCurrentWithReefs(
    float x,
    float y,
    float timeSeconds,
    const std::vector<Reef>& reefs,
    float worldWidth,
    float worldHeight
) {
    const HabitatSample habitat = sampleHabitatField(x, y, reefs, worldWidth, worldHeight);
    const Vec2 base = sampleCurrentField(x, y, timeSeconds);
    const float slowdown = 1.0f - habitat.contact * (0.22f + habitat.proximity * 0.08f);
    return base * slowdown + habitat.currentOffset;
}

float sampleLocalShear(
    float x,
    float y,
    float probeDistance,
    float timeSeconds,
    const std::vector<Reef>& reefs,
    float worldWidth,
    float worldHeight
) {
    const float probe = std::max(18.0f, probeDistance);
    const Vec2 ahead = sampleCurrentWithReefs(wrapAxis(x + probe, worldWidth), y, timeSeconds, reefs, worldWidth, worldHeight);
    const Vec2 behind = sampleCurrentWithReefs(wrapAxis(x - probe, worldWidth), y, timeSeconds, reefs, worldWidth, worldHeight);
    const Vec2 above = sampleCurrentWithReefs(x, wrapAxis(y + probe, worldHeight), timeSeconds, reefs, worldWidth, worldHeight);
    const Vec2 below = sampleCurrentWithReefs(x, wrapAxis(y - probe, worldHeight), timeSeconds, reefs, worldWidth, worldHeight);
    const HabitatSample habitat = sampleHabitatField(x, y, reefs, worldWidth, worldHeight);
    const float gradient = length(ahead - behind) + length(above - below);
    return clamp01(gradient / (88.0f + probe * 0.28f) + habitat.shear * 0.12f);
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

float brainComplexityScore(const BrainGenome& brain) {
    const float hiddenLoad = static_cast<float>(brain.hiddenCount) / static_cast<float>(kMaxHiddenCount);
    const float connectionLoad = static_cast<float>(brain.connectionCount) / static_cast<float>(kMaxConnectionCount);
    return 1.0f + hiddenLoad * 0.7f + connectionLoad * 0.95f;
}

std::uint32_t fixedNodeId(NodeKind kind, int index) {
    switch (kind) {
        case NodeKind::Input:
            return kInputNodeBase + static_cast<std::uint32_t>(index);
        case NodeKind::Memory:
            return kMemoryNodeBase + static_cast<std::uint32_t>(index);
        case NodeKind::Output:
            return kOutputNodeBase + static_cast<std::uint32_t>(index);
        default:
            return 0;
    }
}

std::uint32_t nodeIdForNode(const BrainGenome& brain, NodeKind kind, int index) {
    if (kind == NodeKind::Hidden) {
        return brain.hiddenNodeIds[static_cast<std::size_t>(index)];
    }
    return fixedNodeId(kind, index);
}

std::uint32_t registerInnovation(
    std::vector<InnovationRecord>& innovations,
    std::uint32_t& nextInnovationId,
    std::uint32_t fromNodeId,
    std::uint32_t toNodeId
) {
    for (const InnovationRecord& record : innovations) {
        if (record.fromNodeId == fromNodeId && record.toNodeId == toNodeId) {
            return record.innovation;
        }
    }

    const std::uint32_t innovation = nextInnovationId++;
    innovations.push_back(InnovationRecord {
        .fromNodeId = fromNodeId,
        .toNodeId = toNodeId,
        .innovation = innovation
    });
    return innovation;
}

std::uint32_t allocateHiddenNodeId(std::uint32_t& nextHiddenNodeId) {
    return nextHiddenNodeId++;
}

struct BrainDistance {
    float compatibility = 0.0f;
    float topologyNovelty = 0.0f;
    int unmatchedInnovations = 0;
};

BrainDistance compareBrains(const BrainGenome& lhs, const BrainGenome& rhs) {
    std::vector<ConnectionGene> leftConnections;
    std::vector<ConnectionGene> rightConnections;
    leftConnections.reserve(static_cast<std::size_t>(lhs.connectionCount));
    rightConnections.reserve(static_cast<std::size_t>(rhs.connectionCount));

    for (int index = 0; index < lhs.connectionCount; ++index) {
        leftConnections.push_back(lhs.connections[index]);
    }
    for (int index = 0; index < rhs.connectionCount; ++index) {
        rightConnections.push_back(rhs.connections[index]);
    }

    auto byInnovation = [](const ConnectionGene& a, const ConnectionGene& b) {
        return a.innovation < b.innovation;
    };
    std::sort(leftConnections.begin(), leftConnections.end(), byInnovation);
    std::sort(rightConnections.begin(), rightConnections.end(), byInnovation);

    int leftIndex = 0;
    int rightIndex = 0;
    int matching = 0;
    int disjoint = 0;
    int excess = 0;
    float weightDifference = 0.0f;

    while (leftIndex < static_cast<int>(leftConnections.size()) && rightIndex < static_cast<int>(rightConnections.size())) {
        const ConnectionGene& left = leftConnections[static_cast<std::size_t>(leftIndex)];
        const ConnectionGene& right = rightConnections[static_cast<std::size_t>(rightIndex)];
        if (left.innovation == right.innovation) {
            ++matching;
            weightDifference += std::abs(left.weight - right.weight);
            ++leftIndex;
            ++rightIndex;
            continue;
        }
        if (left.innovation < right.innovation) {
            ++disjoint;
            ++leftIndex;
        } else {
            ++disjoint;
            ++rightIndex;
        }
    }

    excess += static_cast<int>(leftConnections.size()) - leftIndex;
    excess += static_cast<int>(rightConnections.size()) - rightIndex;

    const float normalizer = std::max(12.0f, static_cast<float>(std::max(lhs.connectionCount, rhs.connectionCount)));
    const float hiddenGap = static_cast<float>(std::abs(lhs.hiddenCount - rhs.hiddenCount));
    const float averageWeightDifference = matching > 0 ? weightDifference / static_cast<float>(matching) : 0.45f;
    BrainDistance distance {};
    distance.unmatchedInnovations = disjoint + excess;
    distance.topologyNovelty = hiddenGap * 0.24f + static_cast<float>(distance.unmatchedInnovations) / normalizer;
    distance.compatibility = distance.topologyNovelty * 0.95f + averageWeightDifference * 0.32f;
    return distance;
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
    traits.finSpan = traits.minorRadius * lerp(0.7f, 1.65f, genome.morphology.finArea);
    traits.segmentSpacing = lerp(7.0f, 15.0f, genome.morphology.elongation) * (0.9f + genome.morphology.coreSize * 0.45f);
    traits.tailWaveAmplitude = lerp(0.05f, 0.42f, genome.morphology.tailFlex)
        * (0.65f + genome.morphology.finArea * 0.5f);
    const float bodyCoverage = traits.majorRadius + traits.segmentSpacing * static_cast<float>(kBodySegments - 1);
    traits.collisionRadius = bodyCoverage * 0.58f + spikeBonus * 0.25f;

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

    const float brainComplexity = brainComplexityScore(genome.brain);
    traits.upkeep = 0.85f + traits.mass * 0.04f + traits.sensorRange * 0.008f
        + genome.morphology.armor * 0.65f + brainComplexity * 0.55f;
    traits.reproductionThreshold = 42.0f + traits.mass * 0.45f + genome.ecology.reproductionBias * 24.0f;
    traits.offspringEnergy = lerp(24.0f, 68.0f, genome.ecology.offspringInvestment) + traits.mass * 0.16f;
    traits.maxHealth = 52.0f + traits.mass * 2.25f + genome.morphology.armor * 22.0f;
    traits.carrionYield = traits.mass * 1.7f + traits.maxHealth * 0.36f;

    return traits;
}

bool connectionExists(
    const BrainGenome& brain,
    NodeKind fromKind,
    int fromIndex,
    NodeKind toKind,
    int toIndex
) {
    for (int index = 0; index < brain.connectionCount; ++index) {
        const ConnectionGene& connection = brain.connections[index];
        if (connection.fromKind == fromKind
            && connection.fromIndex == fromIndex
            && connection.toKind == toKind
            && connection.toIndex == toIndex) {
            return true;
        }
    }
    return false;
}

bool canConnect(
    const BrainGenome& brain,
    NodeKind fromKind,
    int fromIndex,
    NodeKind toKind,
    int toIndex
) {
    if (fromIndex < 0 || toIndex < 0) {
        return false;
    }
    if (fromKind == NodeKind::Output || toKind == NodeKind::Input || toKind == NodeKind::Memory) {
        return false;
    }
    if (fromKind == NodeKind::Input && fromIndex >= kInputCount) {
        return false;
    }
    if (fromKind == NodeKind::Memory && fromIndex >= kMemorySize) {
        return false;
    }
    if (fromKind == NodeKind::Hidden && fromIndex >= brain.hiddenCount) {
        return false;
    }
    if (toKind == NodeKind::Hidden && toIndex >= brain.hiddenCount) {
        return false;
    }
    if (toKind == NodeKind::Output && toIndex >= kOutputCount) {
        return false;
    }
    if (toKind == NodeKind::Hidden && fromKind == NodeKind::Hidden && fromIndex >= toIndex) {
        return false;
    }
    return !connectionExists(brain, fromKind, fromIndex, toKind, toIndex);
}

void appendConnection(
    BrainGenome& brain,
    std::vector<InnovationRecord>& innovations,
    std::uint32_t& nextInnovationId,
    NodeKind fromKind,
    int fromIndex,
    NodeKind toKind,
    int toIndex,
    float weight
) {
    if (brain.connectionCount >= kMaxConnectionCount || !canConnect(brain, fromKind, fromIndex, toKind, toIndex)) {
        return;
    }
    const std::uint32_t fromNodeId = nodeIdForNode(brain, fromKind, fromIndex);
    const std::uint32_t toNodeId = nodeIdForNode(brain, toKind, toIndex);

    brain.connections[brain.connectionCount++] = ConnectionGene {
        .fromKind = fromKind,
        .fromIndex = static_cast<std::uint8_t>(fromIndex),
        .toKind = toKind,
        .toIndex = static_cast<std::uint8_t>(toIndex),
        .weight = weight,
        .innovation = registerInnovation(innovations, nextInnovationId, fromNodeId, toNodeId)
    };
}

void removeConnection(BrainGenome& brain, int connectionIndex) {
    if (connectionIndex < 0 || connectionIndex >= brain.connectionCount) {
        return;
    }
    const int lastIndex = brain.connectionCount - 1;
    brain.connections[connectionIndex] = brain.connections[lastIndex];
    brain.connections[lastIndex] = ConnectionGene {};
    --brain.connectionCount;
}

void compactBrain(BrainGenome& brain) {
    std::array<bool, kMaxHiddenCount> keepHidden {};
    for (int index = 0; index < brain.connectionCount; ++index) {
        const ConnectionGene& connection = brain.connections[index];
        if (connection.fromKind == NodeKind::Hidden && connection.fromIndex < brain.hiddenCount) {
            keepHidden[connection.fromIndex] = true;
        }
    }

    std::array<int, kMaxHiddenCount> remap {};
    remap.fill(-1);
    std::array<float, kMaxHiddenCount> newBias {};
    std::array<std::uint32_t, kMaxHiddenCount> newHiddenNodeIds {};
    int newHiddenCount = 0;
    for (int hiddenIndex = 0; hiddenIndex < brain.hiddenCount; ++hiddenIndex) {
        if (!keepHidden[hiddenIndex]) {
            continue;
        }
        remap[hiddenIndex] = newHiddenCount;
        newBias[newHiddenCount] = brain.hiddenBias[hiddenIndex];
        newHiddenNodeIds[newHiddenCount] = brain.hiddenNodeIds[hiddenIndex];
        ++newHiddenCount;
    }

    std::array<ConnectionGene, kMaxConnectionCount> newConnections {};
    int newConnectionCount = 0;
    for (int index = 0; index < brain.connectionCount; ++index) {
        ConnectionGene connection = brain.connections[index];
        if (connection.fromKind == NodeKind::Hidden) {
            if (connection.fromIndex >= brain.hiddenCount || remap[connection.fromIndex] < 0) {
                continue;
            }
            connection.fromIndex = static_cast<std::uint8_t>(remap[connection.fromIndex]);
        }
        if (connection.toKind == NodeKind::Hidden) {
            if (connection.toIndex >= brain.hiddenCount || remap[connection.toIndex] < 0) {
                continue;
            }
            connection.toIndex = static_cast<std::uint8_t>(remap[connection.toIndex]);
        }
        if (connection.toKind == NodeKind::Hidden
            && connection.fromKind == NodeKind::Hidden
            && connection.fromIndex >= connection.toIndex) {
            continue;
        }

        bool duplicate = false;
        for (int priorIndex = 0; priorIndex < newConnectionCount; ++priorIndex) {
            const ConnectionGene& prior = newConnections[priorIndex];
            if (prior.fromKind == connection.fromKind
                && prior.fromIndex == connection.fromIndex
                && prior.toKind == connection.toKind
                && prior.toIndex == connection.toIndex) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        newConnections[newConnectionCount++] = connection;
    }

    brain.hiddenCount = newHiddenCount;
    brain.hiddenBias = newBias;
    brain.hiddenNodeIds = newHiddenNodeIds;
    brain.connectionCount = newConnectionCount;
    brain.connections = newConnections;
}

bool addRandomConnection(
    BrainGenome& brain,
    std::mt19937_64& rng,
    float volatility,
    std::vector<InnovationRecord>& innovations,
    std::uint32_t& nextInnovationId
) {
    if (brain.connectionCount >= kMaxConnectionCount) {
        return false;
    }

    std::vector<ConnectionGene> candidates;
    candidates.reserve((kInputCount + kMemorySize + kMaxHiddenCount) * (kOutputCount + kMaxHiddenCount));

    for (int outputIndex = 0; outputIndex < kOutputCount; ++outputIndex) {
        for (int inputIndex = 0; inputIndex < kInputCount; ++inputIndex) {
            if (canConnect(brain, NodeKind::Input, inputIndex, NodeKind::Output, outputIndex)) {
                candidates.push_back(ConnectionGene {
                    .fromKind = NodeKind::Input,
                    .fromIndex = static_cast<std::uint8_t>(inputIndex),
                    .toKind = NodeKind::Output,
                    .toIndex = static_cast<std::uint8_t>(outputIndex)
                });
            }
        }
        for (int memoryIndex = 0; memoryIndex < kMemorySize; ++memoryIndex) {
            if (canConnect(brain, NodeKind::Memory, memoryIndex, NodeKind::Output, outputIndex)) {
                candidates.push_back(ConnectionGene {
                    .fromKind = NodeKind::Memory,
                    .fromIndex = static_cast<std::uint8_t>(memoryIndex),
                    .toKind = NodeKind::Output,
                    .toIndex = static_cast<std::uint8_t>(outputIndex)
                });
            }
        }
        for (int hiddenIndex = 0; hiddenIndex < brain.hiddenCount; ++hiddenIndex) {
            if (canConnect(brain, NodeKind::Hidden, hiddenIndex, NodeKind::Output, outputIndex)) {
                candidates.push_back(ConnectionGene {
                    .fromKind = NodeKind::Hidden,
                    .fromIndex = static_cast<std::uint8_t>(hiddenIndex),
                    .toKind = NodeKind::Output,
                    .toIndex = static_cast<std::uint8_t>(outputIndex)
                });
            }
        }
    }

    for (int hiddenIndex = 0; hiddenIndex < brain.hiddenCount; ++hiddenIndex) {
        for (int inputIndex = 0; inputIndex < kInputCount; ++inputIndex) {
            if (canConnect(brain, NodeKind::Input, inputIndex, NodeKind::Hidden, hiddenIndex)) {
                candidates.push_back(ConnectionGene {
                    .fromKind = NodeKind::Input,
                    .fromIndex = static_cast<std::uint8_t>(inputIndex),
                    .toKind = NodeKind::Hidden,
                    .toIndex = static_cast<std::uint8_t>(hiddenIndex)
                });
            }
        }
        for (int memoryIndex = 0; memoryIndex < kMemorySize; ++memoryIndex) {
            if (canConnect(brain, NodeKind::Memory, memoryIndex, NodeKind::Hidden, hiddenIndex)) {
                candidates.push_back(ConnectionGene {
                    .fromKind = NodeKind::Memory,
                    .fromIndex = static_cast<std::uint8_t>(memoryIndex),
                    .toKind = NodeKind::Hidden,
                    .toIndex = static_cast<std::uint8_t>(hiddenIndex)
                });
            }
        }
        for (int sourceHidden = 0; sourceHidden < hiddenIndex; ++sourceHidden) {
            if (canConnect(brain, NodeKind::Hidden, sourceHidden, NodeKind::Hidden, hiddenIndex)) {
                candidates.push_back(ConnectionGene {
                    .fromKind = NodeKind::Hidden,
                    .fromIndex = static_cast<std::uint8_t>(sourceHidden),
                    .toKind = NodeKind::Hidden,
                    .toIndex = static_cast<std::uint8_t>(hiddenIndex)
                });
            }
        }
    }

    if (candidates.empty()) {
        return false;
    }

    const ConnectionGene& candidate = candidates[static_cast<std::size_t>(randomInt(rng, 0, static_cast<int>(candidates.size()) - 1))];
    appendConnection(
        brain,
        innovations,
        nextInnovationId,
        candidate.fromKind,
        candidate.fromIndex,
        candidate.toKind,
        candidate.toIndex,
        randomFloat(rng, -1.25f, 1.25f) * (0.75f + volatility * 0.35f)
    );
    return true;
}

bool addRandomHidden(
    BrainGenome& brain,
    std::mt19937_64& rng,
    std::vector<InnovationRecord>& innovations,
    std::uint32_t& nextInnovationId,
    std::uint32_t& nextHiddenNodeId
) {
    if (brain.hiddenCount >= kMaxHiddenCount || brain.connectionCount >= kMaxConnectionCount) {
        return false;
    }

    std::vector<int> splittableConnections;
    for (int index = 0; index < brain.connectionCount; ++index) {
        if (brain.connections[index].toKind == NodeKind::Output) {
            splittableConnections.push_back(index);
        }
    }
    if (splittableConnections.empty()) {
        return false;
    }

    const int splitIndex = splittableConnections[static_cast<std::size_t>(randomInt(rng, 0, static_cast<int>(splittableConnections.size()) - 1))];
    const ConnectionGene original = brain.connections[splitIndex];
    const int newHiddenIndex = brain.hiddenCount++;
    brain.hiddenNodeIds[newHiddenIndex] = allocateHiddenNodeId(nextHiddenNodeId);
    brain.hiddenBias[newHiddenIndex] = randomFloat(rng, -0.12f, 0.12f);
    brain.connections[splitIndex] = ConnectionGene {
        .fromKind = original.fromKind,
        .fromIndex = original.fromIndex,
        .toKind = NodeKind::Hidden,
        .toIndex = static_cast<std::uint8_t>(newHiddenIndex),
        .weight = 1.0f,
        .innovation = registerInnovation(
            innovations,
            nextInnovationId,
            nodeIdForNode(brain, original.fromKind, original.fromIndex),
            nodeIdForNode(brain, NodeKind::Hidden, newHiddenIndex)
        )
    };
    appendConnection(
        brain,
        innovations,
        nextInnovationId,
        NodeKind::Hidden,
        newHiddenIndex,
        NodeKind::Output,
        original.toIndex,
        original.weight
    );
    return true;
}

float sourceNodeValue(
    const Creature& creature,
    const std::array<float, kInputCount>& inputs,
    NodeKind kind,
    int index
) {
    switch (kind) {
        case NodeKind::Input:
            return inputs[index];
        case NodeKind::Memory:
            return creature.memory[index];
        case NodeKind::Hidden:
            return creature.hiddenActivations[index];
        default:
            return creature.outputs[index];
    }
}

Genome makeAncestorGenome(
    std::mt19937_64& rng,
    std::vector<InnovationRecord>& innovations,
    std::uint32_t& nextInnovationId,
    std::uint32_t& nextHiddenNodeId
) {
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

    for (float& value : genome.brain.hiddenBias) {
        value = 0.0f;
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

    genome.brain.hiddenCount = 8;
    for (int hiddenIndex = 0; hiddenIndex < genome.brain.hiddenCount; ++hiddenIndex) {
        genome.brain.hiddenNodeIds[hiddenIndex] = allocateHiddenNodeId(nextHiddenNodeId);
    }

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 10, NodeKind::Hidden, 0, 2.4f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 29, NodeKind::Hidden, 0, 1.0f);
    genome.brain.hiddenBias[0] = -0.15f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 0, NodeKind::Hidden, 1, 1.7f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 5, NodeKind::Hidden, 1, 1.8f);
    genome.brain.hiddenBias[1] = -0.25f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 15, NodeKind::Hidden, 2, 1.8f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 20, NodeKind::Hidden, 2, 1.7f);
    genome.brain.hiddenBias[2] = -0.25f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 25, NodeKind::Hidden, 3, -2.2f);
    genome.brain.hiddenBias[3] = 0.95f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 11, NodeKind::Hidden, 4, 1.7f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 12, NodeKind::Hidden, 4, 2.0f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 16, NodeKind::Hidden, 4, 1.1f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 17, NodeKind::Hidden, 4, 1.2f);
    genome.brain.hiddenBias[4] = -0.35f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 3, NodeKind::Hidden, 5, 1.8f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 8, NodeKind::Hidden, 5, 1.4f);
    genome.brain.hiddenBias[5] = -0.3f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 18, NodeKind::Hidden, 6, 1.4f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 23, NodeKind::Hidden, 6, 1.8f);
    genome.brain.hiddenBias[6] = -0.3f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 25, NodeKind::Hidden, 7, 2.4f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 26, NodeKind::Hidden, 7, 0.8f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 27, NodeKind::Hidden, 7, 1.8f);
    genome.brain.hiddenBias[7] = -1.7f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 1, NodeKind::Output, 0, 1.0f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 2, NodeKind::Output, 0, -1.0f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 5, NodeKind::Output, 0, 0.75f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 6, NodeKind::Output, 0, -0.75f);
    genome.brain.outputBias[0] = 0.05f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 0, NodeKind::Output, 1, 1.3f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 1, NodeKind::Output, 1, 0.6f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 2, NodeKind::Output, 1, 0.6f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 3, NodeKind::Output, 1, 1.0f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 5, NodeKind::Output, 1, -0.55f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 6, NodeKind::Output, 1, -0.55f);
    genome.brain.outputBias[1] = 0.2f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 0, NodeKind::Output, 2, 1.4f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 1, NodeKind::Output, 2, 0.7f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 2, NodeKind::Output, 2, 0.7f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 3, NodeKind::Output, 2, 0.7f);
    // Keep the ancestral lineage biased toward trying to graze so early worlds
    // do not fail simply because no creature attempts to harvest energy.
    genome.brain.outputBias[2] = 0.48f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 4, NodeKind::Output, 3, 1.4f);
    genome.brain.outputBias[3] = -0.55f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 5, NodeKind::Output, 4, 0.9f);
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 6, NodeKind::Output, 4, 0.9f);
    genome.brain.outputBias[4] = -0.6f;

    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Hidden, 7, NodeKind::Output, 5, 1.8f);
    genome.brain.outputBias[5] = -0.12f;

    compactBrain(genome.brain);
    return genome;
}

void mutateScalar(std::mt19937_64& rng, float volatility, float& value, float extra = 1.0f) {
    const float amount = randomFloat(rng, -0.13f, 0.13f) * (0.45f + volatility * 0.8f) * extra;
    value = clamp01(value + amount);
}

Genome mutateGenome(
    const Genome& parent,
    std::mt19937_64& rng,
    std::vector<InnovationRecord>& innovations,
    std::uint32_t& nextInnovationId,
    std::uint32_t& nextHiddenNodeId
) {
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
    for (int index = 0; index < child.brain.connectionCount; ++index) {
        float& value = child.brain.connections[index].weight;
        if (randomFloat(rng, 0.0f, 1.0f) < weightMutationChance) {
            value = std::clamp(value + randomFloat(rng, -0.65f, 0.65f), -2.0f, 2.0f);
        }
    }
    for (int hiddenIndex = 0; hiddenIndex < child.brain.hiddenCount; ++hiddenIndex) {
        float& value = child.brain.hiddenBias[hiddenIndex];
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

    if (randomFloat(rng, 0.0f, 1.0f) < 0.05f + volatility * 0.09f) {
        addRandomConnection(child.brain, rng, volatility, innovations, nextInnovationId);
    }
    if (randomFloat(rng, 0.0f, 1.0f) < 0.012f + volatility * 0.035f) {
        addRandomHidden(child.brain, rng, innovations, nextInnovationId, nextHiddenNodeId);
    }
    if (child.brain.connectionCount > 10 && randomFloat(rng, 0.0f, 1.0f) < 0.016f + volatility * 0.035f) {
        removeConnection(child.brain, randomInt(rng, 0, child.brain.connectionCount - 1));
    }

    compactBrain(child.brain);
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
    std::array<float, kMemorySize> nextMemory {};
    creature.hiddenActivations.fill(0.0f);

    for (int hiddenIndex = 0; hiddenIndex < genome.brain.hiddenCount; ++hiddenIndex) {
        float sum = genome.brain.hiddenBias[hiddenIndex];
        for (int connectionIndex = 0; connectionIndex < genome.brain.connectionCount; ++connectionIndex) {
            const ConnectionGene& connection = genome.brain.connections[connectionIndex];
            if (connection.toKind != NodeKind::Hidden || connection.toIndex != hiddenIndex) {
                continue;
            }
            sum += sourceNodeValue(creature, inputs, connection.fromKind, connection.fromIndex) * connection.weight;
        }
        creature.hiddenActivations[hiddenIndex] = std::tanh(sum);
    }

    for (int outputIndex = 0; outputIndex < kOutputCount; ++outputIndex) {
        float sum = genome.brain.outputBias[outputIndex];
        for (int connectionIndex = 0; connectionIndex < genome.brain.connectionCount; ++connectionIndex) {
            const ConnectionGene& connection = genome.brain.connections[connectionIndex];
            if (connection.toKind != NodeKind::Output || connection.toIndex != outputIndex) {
                continue;
            }
            sum += sourceNodeValue(creature, inputs, connection.fromKind, connection.fromIndex) * connection.weight;
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

float segmentSpacingFor(const Creature& creature, int segmentIndex) {
    const float t = static_cast<float>(segmentIndex) / static_cast<float>(kBodySegments - 1);
    return creature.traits.segmentSpacing * lerp(0.92f, 1.18f, t);
}

void computeBodyObservables(
    Creature& creature,
    float worldWidth,
    float worldHeight,
    float timeSeconds,
    const std::vector<Reef>& reefs
) {
    float curvature = 0.0f;
    int curvatureCount = 0;
    for (int segmentIndex = 1; segmentIndex < kBodySegments - 1; ++segmentIndex) {
        const Vec2 front = normalize(shortestWrappedDelta(
            creature.bodyPoints[segmentIndex],
            creature.bodyPoints[segmentIndex - 1],
            worldWidth,
            worldHeight
        ));
        const Vec2 back = normalize(shortestWrappedDelta(
            creature.bodyPoints[segmentIndex],
            creature.bodyPoints[segmentIndex + 1],
            worldWidth,
            worldHeight
        ));
        curvature += std::abs(wrapAngle(std::atan2(front.y, front.x) - std::atan2(back.y, back.x)));
        ++curvatureCount;
    }
    creature.bodyCurvature = curvatureCount > 0 ? curvature / static_cast<float>(curvatureCount) : 0.0f;

    const Vec2 tailToHead = normalize(shortestWrappedDelta(
        creature.bodyPoints[kBodySegments - 1],
        creature.bodyPoints[0],
        worldWidth,
        worldHeight
    ));
    const Vec2 facing {std::cos(creature.angle), std::sin(creature.angle)};
    creature.bodySlip = 1.0f - std::max(0.0f, dot(tailToHead, facing));

    Vec2 meanCurrent {};
    for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
        meanCurrent = meanCurrent + sampleCurrentWithReefs(
            creature.bodyPoints[segmentIndex].x,
            creature.bodyPoints[segmentIndex].y,
            timeSeconds,
            reefs,
            worldWidth,
            worldHeight
        );
    }
    meanCurrent = meanCurrent / static_cast<float>(kBodySegments);
    if (lengthSquared(meanCurrent) < 1e-5f) {
        creature.flowAlignment = 0.5f;
    } else {
        creature.flowAlignment = 0.5f + 0.5f * dot(normalize(meanCurrent), facing);
    }
}

void seedBodyPose(Creature& creature, float worldWidth, float worldHeight, const std::vector<Reef>& reefs) {
    creature.bodyPoints[0] = creature.position;

    const float headRadius = creature.traits.minorRadius * lerp(0.86f, 1.12f, creature.genome.morphology.jawLength);
    creature.bodyRadii[0] = headRadius;
    creature.bodyVelocities[0] = creature.velocity;

    for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
        const float t = static_cast<float>(segmentIndex) / static_cast<float>(kBodySegments - 1);
        const float sway = std::sin(creature.gaitPhase - t * 1.3f) * creature.traits.tailWaveAmplitude * t;
        const Vec2 direction {std::cos(creature.angle + sway), std::sin(creature.angle + sway)};
        const float spacing = segmentSpacingFor(creature, segmentIndex);
        creature.bodyPoints[segmentIndex] = wrapPosition(
            creature.bodyPoints[segmentIndex - 1] - direction * spacing,
            worldWidth,
            worldHeight
        );
        creature.bodyVelocities[segmentIndex] = creature.velocity;
        creature.bodyRadii[segmentIndex] = lerp(
            creature.traits.minorRadius * 0.95f,
            creature.traits.minorRadius * 0.26f,
            t
        );
    }
    computeBodyObservables(creature, worldWidth, worldHeight, 0.0f, reefs);
}

void translateBody(Creature& creature, const Vec2& delta, float worldWidth, float worldHeight) {
    for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
        creature.bodyPoints[segmentIndex] = wrapPosition(creature.bodyPoints[segmentIndex] + delta, worldWidth, worldHeight);
    }
}

void integrateBodyChain(
    Creature& creature,
    float dt,
    float worldWidth,
    float worldHeight,
    float timeSeconds,
    const std::vector<Reef>& reefs
) {
    const auto previousPoints = creature.bodyPoints;
    const auto previousVelocities = creature.bodyVelocities;
    const Vec2 headCurrent = sampleCurrentWithReefs(
        creature.position.x,
        creature.position.y,
        timeSeconds,
        reefs,
        worldWidth,
        worldHeight
    );
    const Vec2 facing {std::cos(creature.angle), std::sin(creature.angle)};
    const Vec2 side {-facing.y, facing.x};

    creature.bodyPoints[0] = creature.position;
    creature.bodyVelocities[0] = creature.velocity;

    for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
        const float t = static_cast<float>(segmentIndex) / static_cast<float>(kBodySegments - 1);
        const float spacing = segmentSpacingFor(creature, segmentIndex);
        const float sway = std::sin(creature.gaitPhase - t * 1.45f) * creature.traits.tailWaveAmplitude * lerp(0.2f, 1.0f, t);
        const Vec2 desiredDir = normalize(facing - side * sway);
        const Vec2 desiredPoint = creature.bodyPoints[segmentIndex - 1] - desiredDir * spacing;
        const Vec2 currentAtSegment = sampleCurrentWithReefs(
            previousPoints[segmentIndex].x,
            previousPoints[segmentIndex].y,
            timeSeconds,
            reefs,
            worldWidth,
            worldHeight
        );
        const Vec2 deltaToTarget = shortestWrappedDelta(previousPoints[segmentIndex], desiredPoint, worldWidth, worldHeight);
        const Vec2 spring = deltaToTarget * (7.5f + creature.genome.morphology.tailFlex * 8.0f);
        const Vec2 flowDrag = (currentAtSegment - previousVelocities[segmentIndex]) * (1.2f + t * 0.9f);
        const Vec2 tailDrive = desiredDir * (std::sin(creature.gaitPhase - t * 1.2f) * creature.traits.forwardThrust * 0.015f * t);

        creature.bodyVelocities[segmentIndex] = previousVelocities[segmentIndex] + (spring + flowDrag + tailDrive) * dt;
        creature.bodyVelocities[segmentIndex] = creature.bodyVelocities[segmentIndex] * std::exp(-(2.2f + t * 1.1f) * dt);
        creature.bodyPoints[segmentIndex] = wrapPosition(
            previousPoints[segmentIndex] + creature.bodyVelocities[segmentIndex] * dt,
            worldWidth,
            worldHeight
        );
    }

    for (int iteration = 0; iteration < 2; ++iteration) {
        creature.bodyPoints[0] = creature.position;
        for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
            const float spacing = segmentSpacingFor(creature, segmentIndex);
            Vec2 delta = shortestWrappedDelta(
                creature.bodyPoints[segmentIndex - 1],
                creature.bodyPoints[segmentIndex],
                worldWidth,
                worldHeight
            );
            float distance = length(delta);
            if (distance < 1e-4f) {
                delta = facing * -spacing;
                distance = spacing;
            }
            creature.bodyPoints[segmentIndex] = wrapPosition(
                creature.bodyPoints[segmentIndex - 1] + delta * (spacing / distance),
                worldWidth,
                worldHeight
            );
        }
    }

    for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
        const Vec2 displacement = shortestWrappedDelta(
            previousPoints[segmentIndex],
            creature.bodyPoints[segmentIndex],
            worldWidth,
            worldHeight
        );
        creature.bodyVelocities[segmentIndex] = displacement / std::max(dt, 1e-4f);
    }

    const Vec2 tailCurrent = sampleCurrentWithReefs(
        creature.bodyPoints[kBodySegments - 1].x,
        creature.bodyPoints[kBodySegments - 1].y,
        timeSeconds,
        reefs,
        worldWidth,
        worldHeight
    );
    creature.angularVelocity += cross(facing, tailCurrent - headCurrent) * creature.traits.segmentSpacing * 0.00045f;
    computeBodyObservables(creature, worldWidth, worldHeight, timeSeconds, reefs);
}

Vec2 mouthPosition(const Creature& creature) {
    const Vec2 forward {std::cos(creature.angle), std::sin(creature.angle)};
    return creature.bodyPoints[0] + forward * (creature.traits.biteReach * 0.55f);
}

Creature makeCreature(
    std::mt19937_64& rng,
    std::uint64_t id,
    const Genome& genome,
    const std::vector<Reef>& reefs,
    float worldWidth,
    float worldHeight,
    Vec2 position,
    float energy,
    std::uint32_t lineageId,
    std::uint8_t lineageDepth,
    std::uint64_t parentId,
    float brainNovelty
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
    creature.gaitPhase = randomFloat(rng, 0.0f, kTau);
    creature.lineageId = lineageId;
    creature.lineageDepth = lineageDepth;
    creature.parentId = parentId;
    creature.brainNovelty = brainNovelty;
    seedBodyPose(creature, worldWidth, worldHeight, reefs);
    return creature;
}

LineageRecord* findLineage(std::vector<LineageRecord>& lineages, std::uint32_t lineageId) {
    auto it = std::find_if(lineages.begin(), lineages.end(), [&](const LineageRecord& lineage) {
        return lineage.id == lineageId;
    });
    return it != lineages.end() ? &(*it) : nullptr;
}

const LineageRecord* findLineage(const std::vector<LineageRecord>& lineages, std::uint32_t lineageId) {
    auto it = std::find_if(lineages.begin(), lineages.end(), [&](const LineageRecord& lineage) {
        return lineage.id == lineageId;
    });
    return it != lineages.end() ? &(*it) : nullptr;
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
    autoSelectionEnabled_ = true;
    timeSeconds_ = 0.0f;
    historyAccumulator_ = 0.0f;
    stats_ = {};
    stats_.extinctions = priorExtinctions;

    creatures_.clear();
    blooms_.clear();
    carrion_.clear();
    reefs_.clear();
    history_.clear();
    innovations_.clear();
    lineages_.clear();
    nextInnovationId_ = 1;
    nextHiddenNodeId_ = kFirstHiddenNodeId;
    nextLineageId_ = 1;

    ancestorGenome_ = makeAncestorGenome(rng_, innovations_, nextInnovationId_, nextHiddenNodeId_);
    lineages_.push_back(LineageRecord {
        .id = nextLineageId_++,
        .parentId = 0,
        .depth = 0,
        .founderCreatureId = 0,
        .founderTime = 0.0f,
        .noveltyAtBranch = 0.0f,
        .lastSeenTime = 0.0f,
        .currentPopulation = 0,
        .peakPopulation = kInitialPopulation,
        .avgBrainComplexity = brainComplexityScore(ancestorGenome_.brain)
    });

    int reefAttempts = 0;
    while (reefs_.size() < kTargetReefs && reefAttempts < static_cast<int>(kTargetReefs) * 24) {
        ++reefAttempts;
        Reef reef {};
        reef.position = {
            randomFloat(rng_, 0.0f, worldWidth_),
            randomFloat(rng_, 0.0f, worldHeight_)
        };
        reef.radius = randomFloat(rng_, 68.0f, 138.0f);
        reef.roughness = randomFloat(rng_, 0.25f, 1.0f);
        reef.nutrientBoost = randomFloat(rng_, 0.45f, 1.0f);
        reef.shear = randomFloat(rng_, 0.15f, 0.55f);

        bool crowded = false;
        for (const Reef& other : reefs_) {
            const float minimumSpacing = reef.radius + other.radius * 0.72f;
            if (lengthSquared(shortestWrappedDelta(other.position, reef.position, worldWidth_, worldHeight_))
                < minimumSpacing * minimumSpacing) {
                crowded = true;
                break;
            }
        }

        if (!crowded || randomFloat(rng_, 0.0f, 1.0f) < 0.14f) {
            reefs_.push_back(reef);
        }
    }

    for (std::size_t index = 0; index < kInitialPopulation; ++index) {
        const Vec2 position {
            randomFloat(rng_, 0.0f, worldWidth_),
            randomFloat(rng_, 0.0f, worldHeight_)
        };
        creatures_.push_back(makeCreature(
            rng_,
            nextCreatureId_++,
            ancestorGenome_,
            reefs_,
            worldWidth_,
            worldHeight_,
            position,
            130.0f,
            1,
            0,
            0,
            0.0f
        ));
    }
    if (!lineages_.empty()) {
        lineages_.front().currentPopulation = creatures_.size();
        lineages_.front().peakPopulation = creatures_.size();
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

    selectRandomCreature();
}

void Simulation::setSelectedCreature(std::uint64_t id) {
    selectedCreatureId_ = id;
    trackedLineageId_ = 0;
    if (const auto it = std::find_if(creatures_.begin(), creatures_.end(), [&](const Creature& creature) {
            return creature.id == id;
        });
        it != creatures_.end()) {
        trackedLineageId_ = it->lineageId;
    }
    autoSelectionEnabled_ = true;
}

void Simulation::clearSelection() {
    selectedCreatureId_ = 0;
    trackedLineageId_ = 0;
    autoSelectionEnabled_ = false;
}

std::uint64_t Simulation::selectedCreature() const {
    return selectedCreatureId_;
}

bool Simulation::selectRandomCreature() {
    if (creatures_.empty()) {
        selectedCreatureId_ = 0;
        trackedLineageId_ = 0;
        return false;
    }

    const int index = randomInt(rng_, 0, static_cast<int>(creatures_.size()) - 1);
    const Creature& creature = creatures_[static_cast<std::size_t>(index)];
    selectedCreatureId_ = creature.id;
    trackedLineageId_ = creature.lineageId;
    autoSelectionEnabled_ = true;
    return true;
}

bool Simulation::selectTopEnergyCreature() {
    if (creatures_.empty()) {
        selectedCreatureId_ = 0;
        trackedLineageId_ = 0;
        return false;
    }

    const auto it = std::max_element(creatures_.begin(), creatures_.end(), [](const Creature& lhs, const Creature& rhs) {
        return lhs.energy < rhs.energy;
    });
    if (it == creatures_.end()) {
        selectedCreatureId_ = 0;
        trackedLineageId_ = 0;
        return false;
    }

    selectedCreatureId_ = it->id;
    trackedLineageId_ = it->lineageId;
    autoSelectionEnabled_ = true;
    return true;
}

bool Simulation::selectRepresentativeInLineage(std::uint32_t lineageId) {
    if (lineageId == 0) {
        return false;
    }

    const Creature* best = nullptr;
    for (const Creature& creature : creatures_) {
        if (!creature.alive || creature.lineageId != lineageId) {
            continue;
        }
        if (best == nullptr
            || creature.energy > best->energy
            || (creature.energy == best->energy && creature.brainNovelty > best->brainNovelty)) {
            best = &creature;
        }
    }

    if (best == nullptr) {
        if (trackedLineageId_ == lineageId) {
            selectedCreatureId_ = 0;
        }
        return false;
    }

    selectedCreatureId_ = best->id;
    trackedLineageId_ = best->lineageId;
    autoSelectionEnabled_ = true;
    return true;
}

bool Simulation::selectDominantLineageCreature() {
    if (creatures_.empty()) {
        selectedCreatureId_ = 0;
        trackedLineageId_ = 0;
        return false;
    }

    const LineageRecord* dominant = nullptr;
    for (const LineageRecord& lineage : lineages_) {
        if (lineage.currentPopulation == 0) {
            continue;
        }
        if (dominant == nullptr
            || lineage.currentPopulation > dominant->currentPopulation
            || (lineage.currentPopulation == dominant->currentPopulation && lineage.peakPopulation > dominant->peakPopulation)) {
            dominant = &lineage;
        }
    }

    if (dominant != nullptr && selectRepresentativeInLineage(dominant->id)) {
        return true;
    }

    return selectTopEnergyCreature();
}

bool Simulation::selectNewestLineageCreature() {
    if (creatures_.empty()) {
        selectedCreatureId_ = 0;
        trackedLineageId_ = 0;
        return false;
    }

    const LineageRecord* newest = nullptr;
    for (const LineageRecord& lineage : lineages_) {
        if (lineage.currentPopulation == 0) {
            continue;
        }
        if (newest == nullptr
            || lineage.founderTime > newest->founderTime
            || (lineage.founderTime == newest->founderTime && lineage.id > newest->id)) {
            newest = &lineage;
        }
    }

    if (newest != nullptr && selectRepresentativeInLineage(newest->id)) {
        return true;
    }

    return selectTopEnergyCreature();
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

const std::vector<Reef>& Simulation::reefs() const {
    return reefs_;
}

const std::deque<HistorySample>& Simulation::history() const {
    return history_;
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
    return sampleNutrientWithReefs(
        wrapAxis(x, worldWidth_),
        wrapAxis(y, worldHeight_),
        timeSeconds_,
        reefs_,
        worldWidth_,
        worldHeight_
    );
}

Vec2 Simulation::sampleCurrent(float x, float y) const {
    return sampleCurrentWithReefs(
        wrapAxis(x, worldWidth_),
        wrapAxis(y, worldHeight_),
        timeSeconds_,
        reefs_,
        worldWidth_,
        worldHeight_
    );
}

std::optional<std::uint64_t> Simulation::creatureAt(float worldX, float worldY, float radius) const {
    std::optional<std::uint64_t> bestId;
    float bestDistanceSq = radius * radius;
    const Vec2 point {worldX, worldY};

    for (const Creature& creature : creatures_) {
        if (!creature.alive) {
            continue;
        }
        for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
            const Vec2 delta = shortestWrappedDelta(point, creature.bodyPoints[segmentIndex], worldWidth_, worldHeight_);
            const float segmentRadius = radius + creature.bodyRadii[segmentIndex];
            const float distSq = lengthSquared(delta);
            if (distSq < bestDistanceSq && distSq < segmentRadius * segmentRadius) {
                bestDistanceSq = distSq;
                bestId = creature.id;
            }
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
    info.finSpan = it->traits.finSpan;
    info.segmentSpacing = it->traits.segmentSpacing;
    info.tailWaveAmplitude = it->traits.tailWaveAmplitude;
    info.upkeep = it->traits.upkeep;
    info.reproductionThreshold = it->traits.reproductionThreshold;
    info.bodyCurvature = it->bodyCurvature;
    info.bodySlip = it->bodySlip;
    info.flowAlignment = it->flowAlignment;
    info.substrateProximity = it->substrateProximity;
    info.substrateContact = it->substrateContact;
    info.localShear = it->localShear;
    info.lineageId = it->lineageId;
    info.lineageDepth = it->lineageDepth;
    info.brainNovelty = it->brainNovelty;
    info.brainHiddenCount = it->genome.brain.hiddenCount;
    info.brainConnectionCount = it->genome.brain.connectionCount;
    info.brainComplexity = brainComplexityScore(it->genome.brain);
    info.inputs = it->lastInputs;
    info.hiddenActivations = it->hiddenActivations;
    info.connections = it->genome.brain.connections;
    info.outputs = it->outputs;
    info.memory = it->memory;

    for (int bucket = 0; bucket < kSensorBuckets; ++bucket) {
        const int base = bucket * kSensorChannels;
        info.plantSense[bucket] = it->lastInputs[base + 0];
        info.carrionSense[bucket] = it->lastInputs[base + 1];
        info.opportunitySense[bucket] = it->lastInputs[base + 2];
        info.threatSense[bucket] = it->lastInputs[base + 3];
        info.signalSense[bucket] = it->lastInputs[base + 4];
    }

    if (const LineageRecord* lineage = findLineage(lineages_, it->lineageId)) {
        info.lineageParentId = lineage->parentId;
        info.lineagePopulation = lineage->currentPopulation;
        info.lineageAge = timeSeconds_ - lineage->founderTime;
    }

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
        const HabitatSample habitat = sampleHabitatField(
            creature.position.x,
            creature.position.y,
            reefs_,
            worldWidth_,
            worldHeight_
        );
        creature.substrateProximity = std::max(habitat.proximity, habitat.contact);
        creature.substrateContact = habitat.contact;
        creature.localShear = sampleLocalShear(
            creature.position.x,
            creature.position.y,
            std::max(creature.traits.segmentSpacing * 1.6f, creature.traits.collisionRadius * 0.85f),
            timeSeconds_,
            reefs_,
            worldWidth_,
            worldHeight_
        );

        inputs[kSensorBuckets * kSensorChannels + 0] = clamp01(creature.energy / creature.traits.reproductionThreshold);
        inputs[kSensorBuckets * kSensorChannels + 1] = clamp01(creature.health / creature.traits.maxHealth);
        inputs[kSensorBuckets * kSensorChannels + 2] = clamp01(creature.age / 140.0f);
        inputs[kSensorBuckets * kSensorChannels + 3] = clamp01(speed / 120.0f);
        inputs[kSensorBuckets * kSensorChannels + 4] = sampleNutrient(creature.position.x, creature.position.y);
        inputs[kSensorBuckets * kSensorChannels + 5] = 0.5f + 0.5f * dot(normalize(current), forward);
        inputs[kSensorBuckets * kSensorChannels + 6] = creature.substrateProximity;
        inputs[kSensorBuckets * kSensorChannels + 7] = creature.localShear;

        creature.lastInputs = inputs;
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

        creature.gaitPhase += dt * (2.5f + outputDrive(thrustInput) * (1.0f + creature.genome.morphology.tailFlex * 3.2f));
        const float tailPulse = 0.82f + 0.18f * std::sin(creature.gaitPhase);
        forwardVelocity += thrustInput * creature.traits.forwardThrust * tailPulse * dt;
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

        integrateBodyChain(creature, dt, worldWidth_, worldHeight_, timeSeconds_, reefs_);

        const float movementCost = creature.traits.upkeep * dt
            * (0.65f + std::abs(thrustInput) * 0.55f + std::abs(turnInput) * 0.25f + creature.signal * 0.3f
                + creature.bodySlip * 0.08f + creature.bodyCurvature * 0.025f);
        creature.energy -= movementCost;
        stats_.energySpentOnUpkeep += movementCost;

        if (creature.energy > creature.traits.reproductionThreshold * 0.62f) {
            creature.health = std::min(creature.traits.maxHealth, creature.health + dt * (1.0f + creature.genome.morphology.armor * 2.0f));
        }
        if (creature.energy < 0.0f) {
            creature.health += creature.energy;
            creature.energy = 0.0f;
        }
    }

    std::vector<Vec2> reefPositionDelta(creatures_.size(), Vec2 {});
    std::vector<Vec2> reefVelocityDelta(creatures_.size(), Vec2 {});
    std::vector<float> reefContact(creatures_.size(), 0.0f);

    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        Creature& creature = creatures_[index];
        if (!creature.alive) {
            continue;
        }

        Vec2 positionDelta {};
        Vec2 velocityDelta {};
        float contactAmount = 0.0f;

        for (const Reef& reef : reefs_) {
            float strongestOverlap = 0.0f;
            Vec2 strongestNormal {1.0f, 0.0f};

            for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
                const Vec2 delta = shortestWrappedDelta(reef.position, creature.bodyPoints[segmentIndex], worldWidth_, worldHeight_);
                const float distSq = lengthSquared(delta);
                const float minDistance = reef.radius + creature.bodyRadii[segmentIndex] * 0.7f;
                if (distSq >= minDistance * minDistance) {
                    continue;
                }

                const float dist = std::sqrt(std::max(distSq, 1e-4f));
                const float overlap = minDistance - dist;
                if (overlap > strongestOverlap) {
                    strongestOverlap = overlap;
                    strongestNormal = delta / dist;
                }
            }

            if (strongestOverlap <= 0.0f) {
                continue;
            }

            positionDelta = positionDelta + strongestNormal * (strongestOverlap * (0.36f + reef.roughness * 0.16f));
            velocityDelta = velocityDelta + strongestNormal * (strongestOverlap * (0.45f + reef.roughness * 0.35f));
            contactAmount = std::max(
                contactAmount,
                clamp01(strongestOverlap / (creature.traits.minorRadius * 0.8f + reef.radius * 0.06f + 1.0f))
            );
        }

        reefPositionDelta[index] = positionDelta;
        reefVelocityDelta[index] = velocityDelta;
        reefContact[index] = contactAmount;
    }

    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        if (reefContact[index] <= 0.0f) {
            continue;
        }

        Creature& creature = creatures_[index];
        creature.position = wrapPosition(creature.position + reefPositionDelta[index], worldWidth_, worldHeight_);
        translateBody(creature, reefPositionDelta[index], worldWidth_, worldHeight_);
        const float damping = std::clamp(1.0f - reefContact[index] * 0.18f, 0.64f, 1.0f);
        creature.velocity = (creature.velocity + reefVelocityDelta[index]) * damping;
        creature.angularVelocity *= damping;
        for (Vec2& velocity : creature.bodyVelocities) {
            velocity = (velocity + reefVelocityDelta[index]) * damping;
        }
        creature.energy -= reefContact[index] * (0.1f + creature.traits.mass * 0.004f) * dt;
        creature.substrateContact = std::max(creature.substrateContact, reefContact[index]);
    }

    hash.clear();
    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        hash.insert(static_cast<int>(index), creatures_[index].position);
    }

    std::vector<Vec2> collisionPositionDelta(creatures_.size(), Vec2 {});
    std::vector<Vec2> collisionVelocityDelta(creatures_.size(), Vec2 {});

    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        Creature& creature = creatures_[index];
        if (!creature.alive) {
            continue;
        }

        hash.query(creature.position, creature.traits.collisionRadius * 1.8f, nearby);
        for (int otherIndex : nearby) {
            if (otherIndex <= static_cast<int>(index)) {
                continue;
            }

            Creature& other = creatures_[static_cast<std::size_t>(otherIndex)];
            if (!other.alive) {
                continue;
            }

            float strongestOverlap = 0.0f;
            Vec2 strongestNormal {1.0f, 0.0f};

            for (int a = 0; a < kBodySegments; ++a) {
                for (int b = 0; b < kBodySegments; ++b) {
                    const Vec2 delta = shortestWrappedDelta(
                        creature.bodyPoints[a],
                        other.bodyPoints[b],
                        worldWidth_,
                        worldHeight_
                    );
                    const float distSq = lengthSquared(delta);
                    const float minDistance = creature.bodyRadii[a] + other.bodyRadii[b];
                    if (distSq >= minDistance * minDistance) {
                        continue;
                    }

                    const float dist = std::sqrt(std::max(distSq, 1e-4f));
                    const float overlap = minDistance - dist;
                    if (overlap > strongestOverlap) {
                        strongestOverlap = overlap;
                        strongestNormal = delta / dist;
                    }
                }
            }

            if (strongestOverlap <= 0.0f) {
                continue;
            }

            const float totalMass = creature.traits.mass + other.traits.mass;
            const float creatureWeight = other.traits.mass / totalMass;
            const float otherWeight = creature.traits.mass / totalMass;
            const Vec2 correction = strongestNormal * strongestOverlap * 0.55f;

            collisionPositionDelta[index] = collisionPositionDelta[index] - correction * creatureWeight;
            collisionPositionDelta[static_cast<std::size_t>(otherIndex)]
                = collisionPositionDelta[static_cast<std::size_t>(otherIndex)] + correction * otherWeight;
            collisionVelocityDelta[index] = collisionVelocityDelta[index] - strongestNormal * (strongestOverlap * 3.0f * creatureWeight);
            collisionVelocityDelta[static_cast<std::size_t>(otherIndex)]
                = collisionVelocityDelta[static_cast<std::size_t>(otherIndex)] + strongestNormal * (strongestOverlap * 3.0f * otherWeight);
        }
    }

    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        creatures_[index].position = wrapPosition(creatures_[index].position + collisionPositionDelta[index], worldWidth_, worldHeight_);
        translateBody(creatures_[index], collisionPositionDelta[index], worldWidth_, worldHeight_);
        creatures_[index].velocity = creatures_[index].velocity + collisionVelocityDelta[index];
        for (Vec2& velocity : creatures_[index].bodyVelocities) {
            velocity = velocity + collisionVelocityDelta[index];
        }
        computeBodyObservables(creatures_[index], worldWidth_, worldHeight_, timeSeconds_, reefs_);
        const HabitatSample habitat = sampleHabitatField(
            creatures_[index].position.x,
            creatures_[index].position.y,
            reefs_,
            worldWidth_,
            worldHeight_
        );
        creatures_[index].substrateProximity = std::max(habitat.proximity, habitat.contact);
        creatures_[index].substrateContact = std::max(creatures_[index].substrateContact, habitat.contact);
        creatures_[index].localShear = sampleLocalShear(
            creatures_[index].position.x,
            creatures_[index].position.y,
            std::max(creatures_[index].traits.segmentSpacing * 1.6f, creatures_[index].traits.collisionRadius * 0.85f),
            timeSeconds_,
            reefs_,
            worldWidth_,
            worldHeight_
        );
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
        const Vec2 mouth = mouthPosition(creature);
        const float reproductiveReadiness = std::clamp(
            (creature.energy - creature.traits.reproductionThreshold * 0.78f)
                / std::max(16.0f, creature.traits.reproductionThreshold * 0.42f),
            0.0f,
            1.0f
        ) * std::clamp(creature.age / 28.0f, 0.0f, 1.0f);
        const float reproductionIntent = std::max(reproduceDrive, reproductiveReadiness);

        if (grazeDrive > 0.18f) {
            const float ambientNutrient = sampleNutrient(creature.position.x, creature.position.y);
            const float ambientGain = ambientNutrient * grazeDrive * dt
                * (1.4f + creature.genome.ecology.plantAffinity * 2.35f);
            creature.energy += ambientGain;
            stats_.energyFromAmbientGrazing += ambientGain;

            Bloom* bestBloom = nullptr;
            float bestDistSq = std::numeric_limits<float>::max();

            for (Bloom& bloom : blooms_) {
                const Vec2 delta = shortestWrappedDelta(mouth, bloom.position, worldWidth_, worldHeight_);
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
                const float harvestGain = harvest * (0.9f + creature.genome.ecology.plantAffinity * 1.22f);
                creature.energy += harvestGain;
                stats_.energyFromBloomHarvest += harvestGain;
            }
        }

        if (biteDrive > 0.15f && creature.cooldown <= 0.0f) {
            Carrion* bestCarrion = nullptr;
            float bestCarrionScore = -1.0f;

            for (Carrion& chunk : carrion_) {
                const Vec2 delta = shortestWrappedDelta(mouth, chunk.position, worldWidth_, worldHeight_);
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
                const float carrionGain = harvest * (0.35f + creature.genome.ecology.meatAffinity * 0.6f
                    + creature.genome.ecology.scavengerBias * 0.4f);
                creature.energy += carrionGain;
                stats_.energyFromCarrion += carrionGain;
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

                    Vec2 targetPoint = other.bodyPoints[0];
                    float closestSegmentSq = lengthSquared(shortestWrappedDelta(mouth, targetPoint, worldWidth_, worldHeight_));
                    for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
                        const float candidateSq = lengthSquared(shortestWrappedDelta(
                            mouth,
                            other.bodyPoints[segmentIndex],
                            worldWidth_,
                            worldHeight_
                        ));
                        if (candidateSq < closestSegmentSq) {
                            closestSegmentSq = candidateSq;
                            targetPoint = other.bodyPoints[segmentIndex];
                        }
                    }

                    const Vec2 delta = shortestWrappedDelta(mouth, targetPoint, worldWidth_, worldHeight_);
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
                    const float predationGain = impact * (0.08f + creature.genome.ecology.meatAffinity * 0.16f);
                    creature.energy += predationGain;
                    stats_.energyFromPredation += predationGain;
                    creature.cooldown = 0.22f;
                }
            }
        }

        if (reproductionIntent > 0.4f
            && creature.energy > creature.traits.reproductionThreshold * 0.92f
            && creature.age > 18.0f
            && creatures_.size() + pendingSpawns.size() < kMaxPopulation) {
            const Genome childGenome = mutateGenome(
                creature.genome,
                rng_,
                innovations_,
                nextInnovationId_,
                nextHiddenNodeId_
            );
            const BrainDistance brainDistance = compareBrains(childGenome.brain, creature.genome.brain);
            std::uint32_t childLineageId = creature.lineageId;
            std::uint8_t childLineageDepth = creature.lineageDepth;
            const int structuralDelta = std::abs(childGenome.brain.hiddenCount - creature.genome.brain.hiddenCount)
                + std::abs(childGenome.brain.connectionCount - creature.genome.brain.connectionCount);
            const bool branchLineage = (childGenome.brain.hiddenCount > creature.genome.brain.hiddenCount && brainDistance.topologyNovelty > 0.12f)
                || (structuralDelta >= 3 && brainDistance.compatibility > 0.3f)
                || brainDistance.compatibility > 0.52f;
            if (branchLineage) {
                childLineageId = nextLineageId_++;
                childLineageDepth = static_cast<std::uint8_t>(std::min<int>(255, creature.lineageDepth + 1));
                lineages_.push_back(LineageRecord {
                    .id = childLineageId,
                    .parentId = creature.lineageId,
                    .depth = childLineageDepth,
                    .founderCreatureId = nextCreatureId_,
                    .founderTime = timeSeconds_,
                    .noveltyAtBranch = brainDistance.compatibility,
                    .lastSeenTime = timeSeconds_,
                    .currentPopulation = 0,
                    .peakPopulation = 0,
                    .avgBrainComplexity = 0.0f
                });
            }
            const float childEnergy = std::min(creature.energy * 0.46f, creature.traits.offspringEnergy * 1.35f);
            creature.energy -= childEnergy;
            creature.health -= childEnergy * 0.03f;
            stats_.energySpentOnReproduction += childEnergy;

            const Vec2 offset {
                std::cos(creature.angle + kPi) * (creature.traits.collisionRadius + 12.0f),
                std::sin(creature.angle + kPi) * (creature.traits.collisionRadius + 12.0f)
            };
            pendingSpawns.push_back(makeCreature(
                rng_,
                nextCreatureId_++,
                childGenome,
                reefs_,
                worldWidth_,
                worldHeight_,
                creature.position + offset,
                std::max(28.0f, childEnergy * 0.78f),
                childLineageId,
                childLineageDepth,
                creature.id,
                brainDistance.compatibility
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

    if (autoSelectionEnabled_ && selectedCreatureId_ == 0 && !creatures_.empty()) {
        if (trackedLineageId_ != 0 && selectRepresentativeInLineage(trackedLineageId_)) {
            // Keep the observer anchored to the same clade when a watched individual dies.
        } else if (!selectDominantLineageCreature()) {
            selectTopEnergyCreature();
        }
    }

    if (creatures_.empty()) {
        ++stats_.extinctions;
        reset(seed_ + stats_.extinctions + 1);
        return;
    }

    stats_.population = creatures_.size();
    stats_.blooms = blooms_.size();
    stats_.carrion = carrion_.size();
    stats_.reefs = reefs_.size();
    stats_.grazers = 0;
    stats_.omnivores = 0;
    stats_.hunters = 0;
    stats_.avgPlantAffinity = 0.0f;
    stats_.avgMeatAffinity = 0.0f;
    stats_.avgMass = 0.0f;
    stats_.avgSpeed = 0.0f;
    stats_.avgBrainComplexity = 0.0f;
    stats_.avgBrainConnections = 0.0f;
    stats_.avgSubstrateContact = 0.0f;
    stats_.avgLocalShear = 0.0f;
    stats_.activeLineages = 0;
    stats_.dominantLineageShare = 0.0f;

    for (LineageRecord& lineage : lineages_) {
        lineage.currentPopulation = 0;
        lineage.avgBrainComplexity = 0.0f;
    }

    for (const Creature& creature : creatures_) {
        stats_.avgPlantAffinity += creature.genome.ecology.plantAffinity;
        stats_.avgMeatAffinity += creature.genome.ecology.meatAffinity;
        stats_.avgMass += creature.traits.mass;
        stats_.avgSpeed += length(creature.velocity);
        stats_.avgBrainComplexity += brainComplexityScore(creature.genome.brain);
        stats_.avgBrainConnections += static_cast<float>(creature.genome.brain.connectionCount);
        stats_.avgSubstrateContact += creature.substrateContact;
        stats_.avgLocalShear += creature.localShear;

        if (LineageRecord* lineage = findLineage(lineages_, creature.lineageId)) {
            ++lineage->currentPopulation;
            lineage->peakPopulation = std::max(lineage->peakPopulation, lineage->currentPopulation);
            lineage->avgBrainComplexity += brainComplexityScore(creature.genome.brain);
            lineage->lastSeenTime = timeSeconds_;
        }

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
    stats_.avgBrainComplexity /= divisor;
    stats_.avgBrainConnections /= divisor;
    stats_.avgSubstrateContact /= divisor;
    stats_.avgLocalShear /= divisor;

    std::size_t dominantLineagePopulation = 0;
    for (LineageRecord& lineage : lineages_) {
        if (lineage.currentPopulation == 0) {
            continue;
        }
        ++stats_.activeLineages;
        dominantLineagePopulation = std::max(dominantLineagePopulation, lineage.currentPopulation);
        lineage.avgBrainComplexity /= static_cast<float>(lineage.currentPopulation);
    }
    stats_.dominantLineageShare = dominantLineagePopulation > 0
        ? static_cast<float>(dominantLineagePopulation) / divisor
        : 0.0f;

    historyAccumulator_ += dt;
    if (historyAccumulator_ >= 0.5f) {
        historyAccumulator_ -= 0.5f;
        history_.push_back(HistorySample {
            .time = timeSeconds_,
            .population = stats_.population,
            .blooms = stats_.blooms,
            .carrion = stats_.carrion,
            .reefs = stats_.reefs,
            .avgPlantAffinity = stats_.avgPlantAffinity,
            .avgMeatAffinity = stats_.avgMeatAffinity,
            .avgMass = stats_.avgMass,
            .avgBrainComplexity = stats_.avgBrainComplexity,
            .avgSubstrateContact = stats_.avgSubstrateContact,
            .grazers = stats_.grazers,
            .omnivores = stats_.omnivores,
            .hunters = stats_.hunters,
            .activeLineages = stats_.activeLineages,
            .dominantLineageShare = stats_.dominantLineageShare
        });
        while (history_.size() > 180) {
            history_.pop_front();
        }
    }
}

}  // namespace alife
