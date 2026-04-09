#include "sim/Simulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <type_traits>

namespace alife {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;
constexpr std::uint32_t kInputNodeBase = 1;
constexpr std::uint32_t kMemoryNodeBase = kInputNodeBase + kInputCount;
constexpr std::uint32_t kOutputNodeBase = kMemoryNodeBase + kMemorySize;
constexpr std::uint32_t kFirstHiddenNodeId = kOutputNodeBase + kOutputCount;

using NodeKind = BrainGenome::NodeKind;
using ConnectionGene = BrainGenome::ConnectionGene;

constexpr char kSaveMagic[] = "ALIFESM1";
constexpr std::uint32_t kSaveVersion = 6;

template <typename T>
bool writePod(std::ostream& stream, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(stream);
}

template <typename T>
bool readPod(std::istream& stream, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(stream);
}

bool writeString(std::ostream& stream, const std::string& value) {
    const std::uint64_t size = static_cast<std::uint64_t>(value.size());
    return writePod(stream, size)
        && static_cast<bool>(stream.write(value.data(), static_cast<std::streamsize>(size)));
}

bool readString(std::istream& stream, std::string& value) {
    std::uint64_t size = 0;
    if (!readPod(stream, size)) {
        return false;
    }
    value.resize(static_cast<std::size_t>(size));
    stream.read(value.data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(stream);
}

template <typename T>
bool writeVector(std::ostream& stream, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    const std::uint64_t size = static_cast<std::uint64_t>(values.size());
    if (!writePod(stream, size)) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    stream.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(sizeof(T) * values.size()));
    return static_cast<bool>(stream);
}

template <typename T>
bool readVector(std::istream& stream, std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::uint64_t size = 0;
    if (!readPod(stream, size)) {
        return false;
    }
    values.resize(static_cast<std::size_t>(size));
    if (size == 0) {
        return true;
    }
    stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(sizeof(T) * values.size()));
    return static_cast<bool>(stream);
}

template <typename T>
bool writeDeque(std::ostream& stream, const std::deque<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    const std::uint64_t size = static_cast<std::uint64_t>(values.size());
    if (!writePod(stream, size)) {
        return false;
    }
    for (const T& value : values) {
        if (!writePod(stream, value)) {
            return false;
        }
    }
    return true;
}

template <typename T>
bool readDeque(std::istream& stream, std::deque<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::uint64_t size = 0;
    if (!readPod(stream, size)) {
        return false;
    }
    values.clear();
    for (std::uint64_t index = 0; index < size; ++index) {
        T value {};
        if (!readPod(stream, value)) {
            return false;
        }
        values.push_back(value);
    }
    return true;
}

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

Vec2 rotate(const Vec2& value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {
        value.x * c - value.y * s,
        value.x * s + value.y * c
    };
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
    // Currents should create niches and bias movement, not overwhelm the
    // distinction between active swimmers and passive drift.
    const float flowX = std::sin(y * 0.0053f + timeSeconds * 0.29f) * 7.6f
        + std::cos((x + y) * 0.0017f - timeSeconds * 0.16f) * 3.2f;
    const float flowY = std::cos(x * 0.0042f - timeSeconds * 0.18f) * 6.5f
        + std::sin((x - y) * 0.0021f + timeSeconds * 0.11f) * 2.9f;
    return {flowX, flowY};
}

float seasonFactor(float timeSeconds) {
    return 0.7f + 0.3f * std::sin(timeSeconds * 0.045f);
}

int wrapGridCoord(int value, int size) {
    while (value < 0) {
        value += size;
    }
    while (value >= size) {
        value -= size;
    }
    return value;
}

SimulationConfig sanitizeConfig(SimulationConfig config) {
    config.world.width = std::max(320.0f, config.world.width);
    config.world.height = std::max(240.0f, config.world.height);
    config.world.initialPopulation = std::max<std::size_t>(1, config.world.initialPopulation);
    config.world.maxPopulation = std::max(config.world.initialPopulation, config.world.maxPopulation);

    config.environment.targetBlooms = std::max<std::size_t>(1, config.environment.targetBlooms);
    config.environment.targetReefs = std::max<std::size_t>(1, config.environment.targetReefs);
    config.environment.bloomRespawnChance = std::max(0.0f, config.environment.bloomRespawnChance);
    config.environment.spatialCellSize = std::max(16.0f, config.environment.spatialCellSize);
    config.environment.nutrientGridWidth = std::max(8, config.environment.nutrientGridWidth);
    config.environment.nutrientGridHeight = std::max(8, config.environment.nutrientGridHeight);
    config.environment.nutrientCellCapacity = std::max(0.1f, config.environment.nutrientCellCapacity);
    config.environment.nutrientRecoveryRate = std::max(0.0f, config.environment.nutrientRecoveryRate);
    config.environment.nutrientDiffusionRate = std::max(0.0f, config.environment.nutrientDiffusionRate);

    config.evolution.weightMutationBaseChance = std::clamp(config.evolution.weightMutationBaseChance, 0.0f, 1.0f);
    config.evolution.weightMutationVolatilityScale = std::max(0.0f, config.evolution.weightMutationVolatilityScale);
    config.evolution.addConnectionBaseChance = std::clamp(config.evolution.addConnectionBaseChance, 0.0f, 1.0f);
    config.evolution.addConnectionVolatilityScale = std::max(0.0f, config.evolution.addConnectionVolatilityScale);
    config.evolution.addHiddenBaseChance = std::clamp(config.evolution.addHiddenBaseChance, 0.0f, 1.0f);
    config.evolution.addHiddenVolatilityScale = std::max(0.0f, config.evolution.addHiddenVolatilityScale);
    config.evolution.removeConnectionBaseChance = std::clamp(config.evolution.removeConnectionBaseChance, 0.0f, 1.0f);
    config.evolution.removeConnectionVolatilityScale = std::max(0.0f, config.evolution.removeConnectionVolatilityScale);
    config.evolution.lineageBranchHiddenNoveltyThreshold = std::max(0.0f, config.evolution.lineageBranchHiddenNoveltyThreshold);
    config.evolution.lineageBranchStructuralDelta = std::max(1, config.evolution.lineageBranchStructuralDelta);
    config.evolution.lineageBranchCompatibilityThreshold = std::max(0.0f, config.evolution.lineageBranchCompatibilityThreshold);
    config.evolution.lineageBranchStructuralCompatibilityThreshold = std::max(
        0.0f,
        config.evolution.lineageBranchStructuralCompatibilityThreshold
    );

    return config;
}

std::size_t nutrientIndex(int x, int y, const EnvironmentConfig& environment) {
    return static_cast<std::size_t>(
        wrapGridCoord(y, environment.nutrientGridHeight) * environment.nutrientGridWidth
        + wrapGridCoord(x, environment.nutrientGridWidth)
    );
}

struct HabitatSample {
    float proximity = 0.0f;
    float contact = 0.0f;
    float shelter = 0.0f;
    float nutrientBoost = 0.0f;
    float shear = 0.0f;
    Vec2 currentOffset {};
};

HabitatSample sampleHabitatField(
    float x,
    float y,
    float timeSeconds,
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

        const Vec2 reefFlow = sampleCurrentField(reef.position.x, reef.position.y, timeSeconds);
        if (lengthSquared(reefFlow) > 1e-4f) {
            const Vec2 flowDir = normalize(reefFlow);
            const float downstream = dot(delta, flowDir);
            const float lateral = std::abs(cross(delta, flowDir));
            if (downstream > 0.0f) {
                const float wakeLength = reef.radius * (2.1f + reef.shear * 0.9f);
                const float wakeWidth = reef.radius * (0.7f + reef.roughness * 0.28f);
                const float lee = clamp01(1.0f - downstream / wakeLength)
                    * clamp01(1.0f - lateral / wakeWidth);
                sample.shelter = std::max(sample.shelter, lee);
                sample.nutrientBoost += lee * (0.035f + reef.nutrientBoost * 0.085f);
                sample.currentOffset = sample.currentOffset - flowDir * (length(reefFlow) * lee * 0.52f);
            }
        }
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
    const HabitatSample habitat = sampleHabitatField(x, y, timeSeconds, reefs, worldWidth, worldHeight);
    const float base = sampleNutrientField(x, y, timeSeconds);
    return clamp01(base * (1.0f - habitat.contact * 0.35f) + habitat.nutrientBoost);
}

float sampleNutrientGridNormalized(
    const std::vector<float>& grid,
    float x,
    float y,
    float worldWidth,
    float worldHeight,
    const EnvironmentConfig& environment
) {
    if (grid.empty()) {
        return 0.0f;
    }

    const float gx = wrapAxis(x, worldWidth) / worldWidth * static_cast<float>(environment.nutrientGridWidth);
    const float gy = wrapAxis(y, worldHeight) / worldHeight * static_cast<float>(environment.nutrientGridHeight);
    const int x0 = static_cast<int>(std::floor(gx));
    const int y0 = static_cast<int>(std::floor(gy));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float tx = gx - std::floor(gx);
    const float ty = gy - std::floor(gy);

    const float v00 = grid[nutrientIndex(x0, y0, environment)];
    const float v10 = grid[nutrientIndex(x1, y0, environment)];
    const float v01 = grid[nutrientIndex(x0, y1, environment)];
    const float v11 = grid[nutrientIndex(x1, y1, environment)];

    const float top = std::lerp(v00, v10, tx);
    const float bottom = std::lerp(v01, v11, tx);
    return clamp01(std::lerp(top, bottom, ty) / environment.nutrientCellCapacity);
}

float harvestNutrientGrid(
    std::vector<float>& grid,
    float x,
    float y,
    float amount,
    float worldWidth,
    float worldHeight,
    const EnvironmentConfig& environment
) {
    if (grid.empty() || amount <= 0.0f) {
        return 0.0f;
    }

    const float gx = wrapAxis(x, worldWidth) / worldWidth * static_cast<float>(environment.nutrientGridWidth);
    const float gy = wrapAxis(y, worldHeight) / worldHeight * static_cast<float>(environment.nutrientGridHeight);
    const int x0 = static_cast<int>(std::floor(gx));
    const int y0 = static_cast<int>(std::floor(gy));
    const float tx = gx - std::floor(gx);
    const float ty = gy - std::floor(gy);

    struct WeightedCell {
        std::size_t index = 0;
        float weight = 0.0f;
    };

    std::array<WeightedCell, 4> cells {{
        {nutrientIndex(x0, y0, environment), (1.0f - tx) * (1.0f - ty)},
        {nutrientIndex(x0 + 1, y0, environment), tx * (1.0f - ty)},
        {nutrientIndex(x0, y0 + 1, environment), (1.0f - tx) * ty},
        {nutrientIndex(x0 + 1, y0 + 1, environment), tx * ty}
    }};

    std::sort(cells.begin(), cells.end(), [](const WeightedCell& lhs, const WeightedCell& rhs) {
        return lhs.weight > rhs.weight;
    });

    float remaining = amount;
    float harvested = 0.0f;
    for (const WeightedCell& cell : cells) {
        if (remaining <= 1e-6f || cell.weight <= 1e-6f) {
            continue;
        }
        const float requested = std::max(remaining * cell.weight, remaining * 0.18f);
        const float take = std::min(grid[cell.index], requested);
        grid[cell.index] -= take;
        harvested += take;
        remaining -= take;
    }

    if (remaining > 1e-6f) {
        for (const WeightedCell& cell : cells) {
            if (remaining <= 1e-6f) {
                break;
            }
            const float take = std::min(grid[cell.index], remaining);
            grid[cell.index] -= take;
            harvested += take;
            remaining -= take;
        }
    }

    return harvested;
}

void initializeNutrientGrid(
    std::vector<float>& grid,
    float timeSeconds,
    const std::vector<Reef>& reefs,
    float worldWidth,
    float worldHeight,
    const EnvironmentConfig& environment
) {
    grid.assign(static_cast<std::size_t>(environment.nutrientGridWidth * environment.nutrientGridHeight), 0.0f);
    const float cellWidth = worldWidth / static_cast<float>(environment.nutrientGridWidth);
    const float cellHeight = worldHeight / static_cast<float>(environment.nutrientGridHeight);

    for (int y = 0; y < environment.nutrientGridHeight; ++y) {
        for (int x = 0; x < environment.nutrientGridWidth; ++x) {
            const float worldX = (static_cast<float>(x) + 0.5f) * cellWidth;
            const float worldY = (static_cast<float>(y) + 0.5f) * cellHeight;
            grid[nutrientIndex(x, y, environment)] = sampleNutrientWithReefs(
                worldX,
                worldY,
                timeSeconds,
                reefs,
                worldWidth,
                worldHeight
            ) * environment.nutrientCellCapacity;
        }
    }
}

void updateNutrientGrid(
    std::vector<float>& grid,
    std::vector<float>& scratch,
    float timeSeconds,
    const std::vector<Reef>& reefs,
    float worldWidth,
    float worldHeight,
    float dt,
    const EnvironmentConfig& environment
) {
    if (grid.empty()) {
        initializeNutrientGrid(grid, timeSeconds, reefs, worldWidth, worldHeight, environment);
    }
    scratch.resize(grid.size());

    const float cellWidth = worldWidth / static_cast<float>(environment.nutrientGridWidth);
    const float cellHeight = worldHeight / static_cast<float>(environment.nutrientGridHeight);

    for (int y = 0; y < environment.nutrientGridHeight; ++y) {
        for (int x = 0; x < environment.nutrientGridWidth; ++x) {
            const std::size_t index = nutrientIndex(x, y, environment);
            const float worldX = (static_cast<float>(x) + 0.5f) * cellWidth;
            const float worldY = (static_cast<float>(y) + 0.5f) * cellHeight;
            const float target = sampleNutrientWithReefs(
                worldX,
                worldY,
                timeSeconds,
                reefs,
                worldWidth,
                worldHeight
            ) * environment.nutrientCellCapacity;
            const float current = grid[index];
            const float neighborAverage = (
                grid[nutrientIndex(x - 1, y, environment)] +
                grid[nutrientIndex(x + 1, y, environment)] +
                grid[nutrientIndex(x, y - 1, environment)] +
                grid[nutrientIndex(x, y + 1, environment)]
            ) * 0.25f;
            const float recovery = std::max(0.0f, target - current) * environment.nutrientRecoveryRate * dt;
            const float diffusion = (neighborAverage - current) * environment.nutrientDiffusionRate * dt;
            scratch[index] = std::clamp(current + recovery + diffusion, 0.0f, environment.nutrientCellCapacity);
        }
    }

    grid.swap(scratch);
}

Vec2 sampleBloomSpawnPosition(
    std::mt19937_64& rng,
    const std::vector<float>& nutrientGrid,
    float worldWidth,
    float worldHeight,
    const EnvironmentConfig& environment
) {
    Vec2 best {
        randomFloat(rng, 0.0f, worldWidth),
        randomFloat(rng, 0.0f, worldHeight)
    };
    float bestScore = sampleNutrientGridNormalized(nutrientGrid, best.x, best.y, worldWidth, worldHeight, environment);

    for (int attempt = 0; attempt < 5; ++attempt) {
        const Vec2 candidate {
            randomFloat(rng, 0.0f, worldWidth),
            randomFloat(rng, 0.0f, worldHeight)
        };
        const float score = sampleNutrientGridNormalized(
            nutrientGrid,
            candidate.x,
            candidate.y,
            worldWidth,
            worldHeight,
            environment
        );
        if (score > bestScore) {
            best = candidate;
            bestScore = score;
        }
    }

    return best;
}

Vec2 sampleCurrentWithReefs(
    float x,
    float y,
    float timeSeconds,
    const std::vector<Reef>& reefs,
    float worldWidth,
    float worldHeight
) {
    const HabitatSample habitat = sampleHabitatField(x, y, timeSeconds, reefs, worldWidth, worldHeight);
    const Vec2 base = sampleCurrentField(x, y, timeSeconds);
    const float slowdown = 1.0f - habitat.contact * (0.22f + habitat.proximity * 0.08f);
    return base * slowdown + habitat.currentOffset;
}

void advectLooseResource(
    Vec2& position,
    Vec2& velocity,
    float dt,
    float flowCoupling,
    float responseRate,
    float worldWidth,
    float worldHeight,
    float timeSeconds,
    const std::vector<Reef>& reefs
) {
    const Vec2 flow = sampleCurrentWithReefs(position.x, position.y, timeSeconds, reefs, worldWidth, worldHeight);
    const float blend = std::clamp(responseRate * dt, 0.0f, 1.0f);
    velocity = velocity + (flow * flowCoupling - velocity) * blend;
    position = wrapPosition(position + velocity * dt, worldWidth, worldHeight);
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
    const HabitatSample habitat = sampleHabitatField(x, y, timeSeconds, reefs, worldWidth, worldHeight);
    const float gradient = length(ahead - behind) + length(above - below);
    return clamp01(gradient / (88.0f + probe * 0.28f) + habitat.shear * 0.12f);
}

float segmentIntegrity(const Creature& creature, int segmentIndex) {
    const float durability = std::max(creature.traits.segmentDurability[segmentIndex], 1.0f);
    return clamp01(1.0f - creature.segmentDamage[segmentIndex] / durability);
}

float averageSegmentIntegrity(const Creature& creature, int startIndex, int endIndexExclusive) {
    float total = 0.0f;
    int count = 0;
    for (int segmentIndex = startIndex; segmentIndex < endIndexExclusive; ++segmentIndex) {
        total += segmentIntegrity(creature, segmentIndex);
        ++count;
    }
    return count > 0 ? total / static_cast<float>(count) : 1.0f;
}

Vec2 segmentForwardAxis(const Creature& creature, int segmentIndex, float worldWidth, float worldHeight) {
    if (segmentIndex <= 0) {
        return normalize(shortestWrappedDelta(
            creature.bodyPoints[std::min(1, kBodySegments - 1)],
            creature.bodyPoints[0],
            worldWidth,
            worldHeight
        ));
    }
    if (segmentIndex >= kBodySegments - 1) {
        return normalize(shortestWrappedDelta(
            creature.bodyPoints[kBodySegments - 1],
            creature.bodyPoints[kBodySegments - 2],
            worldWidth,
            worldHeight
        ));
    }
    return normalize(shortestWrappedDelta(
        creature.bodyPoints[segmentIndex + 1],
        creature.bodyPoints[segmentIndex - 1],
        worldWidth,
        worldHeight
    ));
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
    const float segmentLengthFactor = 4.0f / static_cast<float>(kBodySegments - 1);

    const float coreRadius = lerp(8.0f, 18.0f, genome.morphology.coreSize);
    const float elongation = lerp(1.0f, 1.85f, genome.morphology.elongation);
    const float taperExponent = lerp(0.7f, 2.1f, genome.morphology.bodyTaper);
    const float finBonus = lerp(0.6f, 1.45f, genome.morphology.finArea);
    const float armorBonus = lerp(0.85f, 1.85f, genome.morphology.armor);
    const float spikeBonus = lerp(0.0f, 5.5f, genome.morphology.spikes);
    const float armorDistribution = lerp(-0.55f, 0.55f, genome.morphology.armorDistribution);

    traits.majorRadius = coreRadius * elongation;
    traits.minorRadius = coreRadius / lerp(1.0f, 1.32f, genome.morphology.elongation);
    traits.finSpan = traits.minorRadius * lerp(0.7f, 1.65f, genome.morphology.finArea);
    traits.segmentSpacing = lerp(7.0f, 15.0f, genome.morphology.elongation)
        * (0.9f + genome.morphology.coreSize * 0.45f)
        * segmentLengthFactor;
    traits.tailLengthScale = lerp(0.74f, 1.42f, genome.morphology.tailLength);
    traits.finPlacement = genome.morphology.finPlacement;
    traits.tailFork = lerp(0.2f, 1.15f, genome.morphology.tailFork);
    traits.jawOffset = lerp(0.06f, 0.42f, genome.morphology.jawOffset);
    traits.tailWaveAmplitude = lerp(0.05f, 0.42f, genome.morphology.tailFlex)
        * (0.65f + genome.morphology.finArea * 0.5f);
    traits.segmentRestRadii[0] = traits.minorRadius * (0.9f + genome.morphology.jawLength * 0.18f);
    traits.segmentSpacingScale[0] = 0.0f;

    float bodyCoverage = traits.majorRadius;
    float bodyMass = 0.0f;
    float driveSum = 0.0f;
    float forwardDragSum = 0.0f;
    float lateralDragSum = 0.0f;
    float angularLeverage = 0.0f;

    for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
        const float t = static_cast<float>(segmentIndex) / static_cast<float>(kBodySegments - 1);
        const float localArmor = clamp01(genome.morphology.armor + (0.5f - t) * armorDistribution);
        const float rearBias = std::pow(t, taperExponent);
        if (segmentIndex > 0) {
            const float radiusScale = std::lerp(0.92f, 0.18f, rearBias);
            traits.segmentRestRadii[segmentIndex] = traits.minorRadius * radiusScale;
            traits.segmentSpacingScale[segmentIndex] = std::lerp(0.92f, traits.tailLengthScale, std::pow(t, 1.35f));
            const float minSpacingScale = ((traits.segmentRestRadii[segmentIndex - 1] + traits.segmentRestRadii[segmentIndex]) * 0.58f)
                / std::max(traits.segmentSpacing, 1.0f);
            traits.segmentSpacingScale[segmentIndex] = std::max(traits.segmentSpacingScale[segmentIndex], minSpacingScale);
            bodyCoverage += traits.segmentSpacing * traits.segmentSpacingScale[segmentIndex];
        }

        const float finInfluence = clamp01(
            1.0f - std::abs(t - traits.finPlacement) / std::lerp(0.22f, 0.42f, genome.morphology.finArea)
        );
        const float localFinArea = 0.7f + finInfluence * genome.morphology.finArea * 1.4f;
        const float localArea = traits.segmentRestRadii[segmentIndex] * traits.segmentRestRadii[segmentIndex] * kPi;
        traits.segmentArmor[segmentIndex] = 0.75f + localArmor * 1.25f;
        traits.segmentMass[segmentIndex] = (localArea * 0.065f * segmentLengthFactor + 0.95f)
            * (0.9f + localArmor * 0.55f);
        traits.segmentDrive[segmentIndex] = std::pow(t, 1.15f) * (0.22f + genome.morphology.tailFlex * 1.45f)
            * (0.7f + traits.tailLengthScale * 0.45f);
        traits.segmentJointStiffness[segmentIndex] = (7.5f + localArmor * 11.0f)
            * std::lerp(1.2f, 0.62f, genome.morphology.tailFlex)
            * std::lerp(1.12f, 0.72f, t);
        traits.segmentForwardDragProfile[segmentIndex] = (0.34f + localArea * 0.0022f) * (1.0f + localArmor * 0.14f);
        traits.segmentLateralDragProfile[segmentIndex] = traits.segmentForwardDragProfile[segmentIndex]
            * (1.35f + localFinArea * 1.2f + traits.segmentRestRadii[segmentIndex] * 0.02f);
        traits.segmentDurability[segmentIndex] = 18.0f + traits.segmentMass[segmentIndex] * 2.4f + traits.segmentArmor[segmentIndex] * 13.0f;
        traits.segmentSubstrateGrip[segmentIndex] = 0.22f
            + localArmor * 0.32f
            + genome.morphology.spikes * 0.3f
            + finInfluence * 0.08f;
        traits.segmentScrapeSensitivity[segmentIndex] = 0.55f
            + (1.0f - localArmor) * 0.42f
            + genome.morphology.spikes * 0.18f
            + finInfluence * 0.42f
            + genome.morphology.tailFlex * 0.12f;
        traits.segmentJointRecoil[segmentIndex] = (0.16f + genome.morphology.tailFlex * 0.7f)
            * std::lerp(0.7f, 1.22f, t);
        traits.segmentBendLimit[segmentIndex] = std::lerp(0.18f, 0.95f, genome.morphology.tailFlex)
            * std::lerp(0.72f, 1.35f, t)
            * std::lerp(0.86f, 0.64f, localArmor);

        bodyMass += traits.segmentMass[segmentIndex];
        driveSum += traits.segmentDrive[segmentIndex] * (1.0f + finInfluence * 0.28f) * segmentLengthFactor;
        forwardDragSum += traits.segmentForwardDragProfile[segmentIndex];
        lateralDragSum += traits.segmentLateralDragProfile[segmentIndex];
        angularLeverage += t * traits.segmentDrive[segmentIndex] * (0.8f + finInfluence * 0.45f) * segmentLengthFactor;
    }

    traits.collisionRadius = bodyCoverage * 0.58f + spikeBonus * 0.25f;
    traits.mass = (bodyMass + 6.0f) * (0.92f + armorBonus * 0.12f);
    traits.forwardThrust = (58.0f + driveSum * 56.0f * finBonus) / (0.62f + std::sqrt(traits.mass) * 0.14f);
    traits.turnTorque = (1.6f + angularLeverage * 3.6f) / (0.78f + traits.mass * 0.022f);
    traits.forwardDrag = forwardDragSum / static_cast<float>(kBodySegments);
    traits.lateralDrag = lateralDragSum / static_cast<float>(kBodySegments);
    traits.angularDrag = 1.2f + genome.morphology.finArea * 0.28f + genome.morphology.armor * 0.62f
        + traits.tailLengthScale * 0.12f;

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
    traits.upkeep = 0.55f + traits.mass * 0.026f + traits.sensorRange * 0.0052f
        + genome.morphology.armor * 0.36f + brainComplexity * 0.4f;
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
    genome.morphology.bodyTaper = 0.48f;
    genome.morphology.finArea = 0.34f;
    genome.morphology.finPlacement = 0.46f;
    genome.morphology.tailFlex = 0.56f;
    genome.morphology.tailLength = 0.52f;
    genome.morphology.tailFork = 0.24f;
    genome.morphology.armor = 0.24f;
    genome.morphology.armorDistribution = 0.36f;
    genome.morphology.jawLength = 0.48f;
    genome.morphology.jawArc = 0.52f;
    genome.morphology.jawOffset = 0.42f;
    genome.morphology.sensorSpan = 0.64f;
    genome.morphology.sensorRange = 0.57f;
    genome.morphology.spikes = 0.04f;
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
    appendConnection(genome.brain, innovations, nextInnovationId, NodeKind::Input, 33, NodeKind::Hidden, 0, 0.95f);
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
    genome.brain.outputBias[1] = 0.38f;

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
    const EvolutionConfig& evolution,
    std::vector<InnovationRecord>& innovations,
    std::uint32_t& nextInnovationId,
    std::uint32_t& nextHiddenNodeId
) {
    Genome child = parent;
    const float volatility = parent.ecology.mutationVolatility;

    mutateScalar(rng, volatility, child.morphology.coreSize);
    mutateScalar(rng, volatility, child.morphology.elongation);
    mutateScalar(rng, volatility, child.morphology.bodyTaper);
    mutateScalar(rng, volatility, child.morphology.finArea);
    mutateScalar(rng, volatility, child.morphology.finPlacement);
    mutateScalar(rng, volatility, child.morphology.tailFlex);
    mutateScalar(rng, volatility, child.morphology.tailLength);
    mutateScalar(rng, volatility, child.morphology.tailFork);
    mutateScalar(rng, volatility, child.morphology.armor);
    mutateScalar(rng, volatility, child.morphology.armorDistribution);
    mutateScalar(rng, volatility, child.morphology.jawLength);
    mutateScalar(rng, volatility, child.morphology.jawArc);
    mutateScalar(rng, volatility, child.morphology.jawOffset);
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

    const float weightMutationChance = evolution.weightMutationBaseChance
        + volatility * evolution.weightMutationVolatilityScale;
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

    if (randomFloat(rng, 0.0f, 1.0f) < evolution.addConnectionBaseChance + volatility * evolution.addConnectionVolatilityScale) {
        addRandomConnection(child.brain, rng, volatility, innovations, nextInnovationId);
    }
    if (randomFloat(rng, 0.0f, 1.0f) < evolution.addHiddenBaseChance + volatility * evolution.addHiddenVolatilityScale) {
        addRandomHidden(child.brain, rng, innovations, nextInnovationId, nextHiddenNodeId);
    }
    if (child.brain.connectionCount > 10
        && randomFloat(rng, 0.0f, 1.0f) < evolution.removeConnectionBaseChance + volatility * evolution.removeConnectionVolatilityScale) {
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
    if (segmentIndex <= 0) {
        return 0.0f;
    }
    const float integrity = segmentIntegrity(creature, segmentIndex);
    return creature.traits.segmentSpacing
        * creature.traits.segmentSpacingScale[segmentIndex]
        * lerp(0.88f, 1.0f, integrity);
}

float targetSegmentSway(const Creature& creature, int segmentIndex, float thrustDrive) {
    const float t = static_cast<float>(segmentIndex) / static_cast<float>(kBodySegments - 1);
    const float integrity = segmentIntegrity(creature, segmentIndex);
    return std::sin(creature.gaitPhase - t * 1.45f)
        * creature.traits.tailWaveAmplitude
        * lerp(0.2f, 1.1f, t)
        * creature.traits.segmentDrive[segmentIndex]
        * (0.35f + thrustDrive * 0.65f)
        * (0.25f + integrity * 0.75f);
}

Vec2 targetSegmentDirection(const Creature& creature, int segmentIndex, const Vec2& parentAxis, float thrustDrive) {
    const Vec2 parentSide {-parentAxis.y, parentAxis.x};
    return normalize(parentAxis - parentSide * targetSegmentSway(creature, segmentIndex, thrustDrive));
}

void computeBodyObservables(
    Creature& creature,
    float worldWidth,
    float worldHeight,
    float timeSeconds,
    const std::vector<Reef>& reefs
) {
    const float thrustDrive = clamp01(std::abs(creature.outputs[1]));
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

    float totalStrain = 0.0f;
    float totalCompression = 0.0f;
    float totalCoupling = 0.0f;
    for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
        const float spacing = std::max(segmentSpacingFor(creature, segmentIndex), 1e-4f);
        const Vec2 delta = shortestWrappedDelta(
            creature.bodyPoints[segmentIndex - 1],
            creature.bodyPoints[segmentIndex],
            worldWidth,
            worldHeight
        );
        float distance = length(delta);
        Vec2 currentDir = distance > 1e-4f ? delta / distance : facing * -1.0f;
        if (distance <= 1e-4f) {
            distance = spacing;
        }
        const Vec2 parentAxis = segmentIndex == 1
            ? facing
            : normalize(shortestWrappedDelta(
                creature.bodyPoints[segmentIndex - 2],
                creature.bodyPoints[segmentIndex - 1],
                worldWidth,
                worldHeight
            ));
        const Vec2 targetDir = targetSegmentDirection(creature, segmentIndex, parentAxis, thrustDrive);
        const Vec2 relaxedDir = targetDir * -1.0f;
        const float stretchStrain = std::abs(distance - spacing) / spacing;
        const float compression = std::max(0.0f, spacing - distance) / spacing;
        const float bendError = std::abs(wrapAngle(std::atan2(currentDir.y, currentDir.x) - std::atan2(relaxedDir.y, relaxedDir.x)));
        const float bendLimit = std::max(creature.traits.segmentBendLimit[segmentIndex], 0.08f);
        const float strain = clamp01(stretchStrain * 1.7f + bendError / bendLimit * 0.65f);
        creature.jointStrain[segmentIndex] = strain;
        totalStrain += strain;
        totalCompression += compression;
        totalCoupling += clamp01(std::abs(cross(parentAxis, currentDir)) * (0.65f + creature.traits.segmentDrive[segmentIndex] * 0.35f));
    }
    creature.jointStrain[0] = 0.0f;
    const float jointCount = static_cast<float>(kBodySegments - 1);
    creature.bodyStrain = totalStrain / jointCount;
    creature.bodyCompression = totalCompression / jointCount;
    creature.propulsionCoupling = totalCoupling / jointCount;
}

void seedBodyPose(Creature& creature, float worldWidth, float worldHeight, const std::vector<Reef>& reefs) {
    creature.bodyPoints[0] = creature.position;
    creature.bodyRadii[0] = creature.traits.segmentRestRadii[0];
    creature.bodyVelocities[0] = creature.velocity;

    for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
        const Vec2 parentAxis = segmentIndex == 1
            ? Vec2 {std::cos(creature.angle), std::sin(creature.angle)}
            : normalize(shortestWrappedDelta(
                creature.bodyPoints[segmentIndex - 2],
                creature.bodyPoints[segmentIndex - 1],
                worldWidth,
                worldHeight
            ));
        const Vec2 direction = targetSegmentDirection(creature, segmentIndex, parentAxis, 1.0f);
        const float spacing = segmentSpacingFor(creature, segmentIndex);
        creature.bodyPoints[segmentIndex] = wrapPosition(
            creature.bodyPoints[segmentIndex - 1] - direction * spacing,
            worldWidth,
            worldHeight
        );
        creature.bodyVelocities[segmentIndex] = creature.velocity;
        creature.bodyRadii[segmentIndex] = creature.traits.segmentRestRadii[segmentIndex];
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
    const float thrustDrive = clamp01(std::abs(creature.outputs[1]));

    creature.bodyPoints[0] = creature.position;
    creature.bodyVelocities[0] = creature.velocity;
    creature.bodyRadii[0] = creature.traits.segmentRestRadii[0] * lerp(0.82f, 1.0f, segmentIntegrity(creature, 0));

    for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
        const float t = static_cast<float>(segmentIndex) / static_cast<float>(kBodySegments - 1);
        const float integrity = segmentIntegrity(creature, segmentIndex);
        const float spacing = segmentSpacingFor(creature, segmentIndex);
        const Vec2 parentAxis = segmentIndex == 1
            ? facing
            : normalize(shortestWrappedDelta(previousPoints[segmentIndex], previousPoints[segmentIndex - 1], worldWidth, worldHeight));
        const Vec2 desiredDir = targetSegmentDirection(creature, segmentIndex, parentAxis, thrustDrive);
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
        const Vec2 axis = normalize(shortestWrappedDelta(previousPoints[segmentIndex - 1], previousPoints[segmentIndex], worldWidth, worldHeight));
        const Vec2 localNormal {-axis.y, axis.x};
        const Vec2 relativeVelocity = previousVelocities[segmentIndex] - currentAtSegment;
        const float axisVelocity = dot(relativeVelocity, axis);
        const float normalVelocity = dot(relativeVelocity, localNormal);
        const Vec2 spring = deltaToTarget * creature.traits.segmentJointStiffness[segmentIndex] * (0.45f + integrity * 0.55f);
        const Vec2 dragForce = axis * (-axisVelocity * std::abs(axisVelocity) * creature.traits.segmentForwardDragProfile[segmentIndex] * 0.08f)
            + localNormal * (-normalVelocity * std::abs(normalVelocity) * creature.traits.segmentLateralDragProfile[segmentIndex] * 0.1f);
        const Vec2 tailDrive = localNormal
            * (std::sin(creature.gaitPhase - t * 1.18f)
                * creature.traits.segmentDrive[segmentIndex]
                * creature.traits.forwardThrust
                * (0.24f + thrustDrive * 0.6f)
                * (0.2f + integrity * 0.8f));

        creature.bodyVelocities[segmentIndex] = previousVelocities[segmentIndex] + (spring + dragForce + tailDrive) * dt;
        creature.bodyVelocities[segmentIndex] = creature.bodyVelocities[segmentIndex] * std::exp(-(1.7f + t * 1.2f) * dt);
        creature.bodyPoints[segmentIndex] = wrapPosition(
            previousPoints[segmentIndex] + creature.bodyVelocities[segmentIndex] * dt,
            worldWidth,
            worldHeight
        );
        creature.bodyRadii[segmentIndex] = creature.traits.segmentRestRadii[segmentIndex] * lerp(0.8f, 1.0f, integrity);
    }

    for (int iteration = 0; iteration < 3; ++iteration) {
        for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
            const float spacing = segmentSpacingFor(creature, segmentIndex);
            const int parentIndex = segmentIndex - 1;
            Vec2 delta = shortestWrappedDelta(
                creature.bodyPoints[parentIndex],
                creature.bodyPoints[segmentIndex],
                worldWidth,
                worldHeight
            );
            float distance = length(delta);
            if (distance < 1e-4f) {
                delta = facing * -spacing;
                distance = spacing;
            }
            const Vec2 currentDir = delta / distance;
            const float distanceError = distance - spacing;
            const Vec2 spacingCorrection = currentDir * distanceError;
            const float parentIntegrity = segmentIntegrity(creature, parentIndex);
            const float childIntegrity = segmentIntegrity(creature, segmentIndex);
            const float parentInverseMass = (parentIndex == 0 ? 0.72f : 1.0f)
                / std::max(creature.traits.segmentMass[parentIndex] * (0.38f + parentIntegrity * 0.62f), 1.0f);
            const float childInverseMass = 1.0f
                / std::max(creature.traits.segmentMass[segmentIndex] * (0.38f + childIntegrity * 0.62f), 1.0f);
            const float totalInverseMass = std::max(parentInverseMass + childInverseMass, 1e-4f);
            const float parentShare = parentInverseMass / totalInverseMass;
            const float childShare = childInverseMass / totalInverseMass;
            const Vec2 parentAxis = parentIndex == 0
                ? facing
                : normalize(shortestWrappedDelta(
                    creature.bodyPoints[parentIndex - 1],
                    creature.bodyPoints[parentIndex],
                    worldWidth,
                    worldHeight
                ));
            const Vec2 desiredDir = targetSegmentDirection(creature, segmentIndex, parentAxis, thrustDrive);
            const Vec2 desiredPoint = creature.bodyPoints[parentIndex] - desiredDir * spacing;
            const Vec2 offsetToTarget = shortestWrappedDelta(
                creature.bodyPoints[segmentIndex],
                desiredPoint,
                worldWidth,
                worldHeight
            );
            const Vec2 bendCorrection = (offsetToTarget - currentDir * dot(offsetToTarget, currentDir))
                * std::clamp(creature.traits.segmentJointStiffness[segmentIndex] * 0.011f, 0.08f, 0.42f);
            const Vec2 parentDelta = spacingCorrection * parentShare - bendCorrection * parentShare * 0.55f;
            const Vec2 childDelta = spacingCorrection * (-childShare) + bendCorrection * childShare;

            creature.bodyPoints[parentIndex] = wrapPosition(creature.bodyPoints[parentIndex] + parentDelta, worldWidth, worldHeight);
            creature.bodyPoints[segmentIndex] = wrapPosition(creature.bodyPoints[segmentIndex] + childDelta, worldWidth, worldHeight);

            const float recoilScale = creature.traits.segmentJointRecoil[segmentIndex] / std::max(dt, 1e-4f) * 0.24f;
            creature.bodyVelocities[parentIndex] = creature.bodyVelocities[parentIndex] + parentDelta * recoilScale;
            creature.bodyVelocities[segmentIndex] = creature.bodyVelocities[segmentIndex] + childDelta * recoilScale;
        }
    }

    for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
        const int parentIndex = segmentIndex - 1;
        const float spacing = segmentSpacingFor(creature, segmentIndex);
        const Vec2 parentAxis = parentIndex == 0
            ? facing
            : normalize(shortestWrappedDelta(
                creature.bodyPoints[parentIndex - 1],
                creature.bodyPoints[parentIndex],
                worldWidth,
                worldHeight
            ));
        const Vec2 relaxedDir = targetSegmentDirection(creature, segmentIndex, parentAxis, thrustDrive) * -1.0f;
        Vec2 delta = shortestWrappedDelta(
            creature.bodyPoints[parentIndex],
            creature.bodyPoints[segmentIndex],
            worldWidth,
            worldHeight
        );
        if (lengthSquared(delta) < 1e-6f) {
            delta = relaxedDir * spacing;
        }
        const float bend = wrapAngle(std::atan2(delta.y, delta.x) - std::atan2(relaxedDir.y, relaxedDir.x));
        const float bendLimit = std::max(creature.traits.segmentBendLimit[segmentIndex], 0.08f) * 1.05f;
        const float clampedBend = std::clamp(bend, -bendLimit, bendLimit);
        const Vec2 clampedDir = rotate(relaxedDir, clampedBend);
        creature.bodyPoints[segmentIndex] = wrapPosition(
            creature.bodyPoints[parentIndex] + clampedDir * spacing,
            worldWidth,
            worldHeight
        );
    }

    creature.position = wrapPosition(creature.bodyPoints[0], worldWidth, worldHeight);
    creature.bodyVelocities[0] = shortestWrappedDelta(previousPoints[0], creature.bodyPoints[0], worldWidth, worldHeight) / std::max(dt, 1e-4f);

    for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
        const Vec2 displacement = shortestWrappedDelta(
            previousPoints[segmentIndex],
            creature.bodyPoints[segmentIndex],
            worldWidth,
            worldHeight
        );
        creature.bodyVelocities[segmentIndex] = displacement / std::max(dt, 1e-4f);
    }

    Vec2 hydrodynamicForce {};
    float hydrodynamicTorque = 0.0f;
    for (int segmentIndex = 1; segmentIndex < kBodySegments; ++segmentIndex) {
        const float integrity = segmentIntegrity(creature, segmentIndex);
        const Vec2 currentAtSegment = sampleCurrentWithReefs(
            creature.bodyPoints[segmentIndex].x,
            creature.bodyPoints[segmentIndex].y,
            timeSeconds,
            reefs,
            worldWidth,
            worldHeight
        );
        const Vec2 axis = normalize(shortestWrappedDelta(
            creature.bodyPoints[segmentIndex - 1],
            creature.bodyPoints[segmentIndex],
            worldWidth,
            worldHeight
        ));
        const Vec2 normal {-axis.y, axis.x};
        const Vec2 relativeVelocity = creature.bodyVelocities[segmentIndex] - currentAtSegment;
        const float axisVelocity = dot(relativeVelocity, axis);
        const float normalVelocity = dot(relativeVelocity, normal);
        Vec2 fluidForce = axis * (-axisVelocity * std::abs(axisVelocity) * creature.traits.segmentForwardDragProfile[segmentIndex])
            + normal * (-normalVelocity * std::abs(normalVelocity) * creature.traits.segmentLateralDragProfile[segmentIndex]);
        fluidForce = fluidForce * (0.0026f + creature.traits.segmentDrive[segmentIndex] * 0.0009f) * integrity;
        hydrodynamicForce = hydrodynamicForce + fluidForce;
        const Vec2 lever = shortestWrappedDelta(creature.position, creature.bodyPoints[segmentIndex], worldWidth, worldHeight);
        hydrodynamicTorque += cross(lever, fluidForce);
    }

    const float inverseMass = 1.0f / std::max(creature.traits.mass, 1.0f);
    const Vec2 reactionImpulse = hydrodynamicForce * (inverseMass * dt * 18.0f);
    creature.velocity = creature.velocity + reactionImpulse;
    const Vec2 reactionDisplacement = reactionImpulse * (dt * 0.46f);
    creature.position = wrapPosition(creature.position + reactionDisplacement, worldWidth, worldHeight);
    translateBody(creature, reactionDisplacement, worldWidth, worldHeight);

    const Vec2 tailCurrent = sampleCurrentWithReefs(
        creature.bodyPoints[kBodySegments - 1].x,
        creature.bodyPoints[kBodySegments - 1].y,
        timeSeconds,
        reefs,
        worldWidth,
        worldHeight
    );
    const float tailIntegrity = averageSegmentIntegrity(creature, std::max(1, kBodySegments - 2), kBodySegments);
    creature.angularVelocity += cross(facing, tailCurrent - headCurrent) * creature.traits.segmentSpacing * 0.00045f;
    creature.angularVelocity += hydrodynamicTorque
        / std::max(creature.traits.mass * creature.traits.collisionRadius * creature.traits.collisionRadius, 1.0f)
        * dt
        * (0.22f + tailIntegrity * 0.35f);
    computeBodyObservables(creature, worldWidth, worldHeight, timeSeconds, reefs);
}

Vec2 mouthPosition(const Creature& creature) {
    const Vec2 forward {std::cos(creature.angle), std::sin(creature.angle)};
    return creature.bodyPoints[0] + forward * (creature.bodyRadii[0] * (0.48f + creature.traits.jawOffset) + creature.traits.biteReach * 0.32f);
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
    creature.age = randomFloat(rng, 0.0f, 8.0f);
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

Simulation::Simulation(const SimulationConfig& config)
    : config_(sanitizeConfig(config)),
      worldWidth_(config_.world.width),
      worldHeight_(config_.world.height) {
    reset(1);
}

void Simulation::reset(std::uint64_t seed) {
    const std::uint64_t priorExtinctions = stats_.extinctions;
    config_ = sanitizeConfig(config_);
    worldWidth_ = config_.world.width;
    worldHeight_ = config_.world.height;
    seed_ = seed == 0 ? 1 : seed;
    rng_.seed(seed_);
    nextCreatureId_ = 1;
    selectedCreatureId_ = 0;
    autoSelectionEnabled_ = config_.debug.autoSelectOnReset;
    timeSeconds_ = 0.0f;
    historyAccumulator_ = 0.0f;
    stats_ = {};
    stats_.extinctions = priorExtinctions;

    creatures_.clear();
    blooms_.clear();
    carrion_.clear();
    reefs_.clear();
    nutrientGrid_.clear();
    nutrientScratch_.clear();
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
        .peakPopulation = config_.world.initialPopulation,
        .avgBrainComplexity = brainComplexityScore(ancestorGenome_.brain)
    });

    int reefAttempts = 0;
    while (reefs_.size() < config_.environment.targetReefs
        && reefAttempts < static_cast<int>(config_.environment.targetReefs) * 24) {
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

    initializeNutrientGrid(nutrientGrid_, timeSeconds_, reefs_, worldWidth_, worldHeight_, config_.environment);
    nutrientScratch_.resize(nutrientGrid_.size());

    for (std::size_t index = 0; index < config_.world.initialPopulation; ++index) {
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

    for (std::size_t index = 0; index < config_.environment.targetBlooms; ++index) {
        Bloom bloom {};
        bloom.position = sampleBloomSpawnPosition(rng_, nutrientGrid_, worldWidth_, worldHeight_, config_.environment);
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

const SimulationConfig& Simulation::config() const {
    return config_;
}

float Simulation::sampleNutrient(float x, float y) const {
    return sampleNutrientGridNormalized(nutrientGrid_, x, y, worldWidth_, worldHeight_, config_.environment);
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

EnvironmentProbe Simulation::probeEnvironment(float x, float y) const {
    const float wrappedX = wrapAxis(x, worldWidth_);
    const float wrappedY = wrapAxis(y, worldHeight_);
    const HabitatSample habitat = sampleHabitatField(wrappedX, wrappedY, timeSeconds_, reefs_, worldWidth_, worldHeight_);
    return EnvironmentProbe {
        .nutrient = sampleNutrientGridNormalized(
            nutrientGrid_,
            wrappedX,
            wrappedY,
            worldWidth_,
            worldHeight_,
            config_.environment
        ),
        .current = sampleCurrentWithReefs(wrappedX, wrappedY, timeSeconds_, reefs_, worldWidth_, worldHeight_),
        .substrate = std::max(habitat.proximity, habitat.contact),
        .shelter = habitat.shelter,
        .shear = sampleLocalShear(wrappedX, wrappedY, 42.0f, timeSeconds_, reefs_, worldWidth_, worldHeight_)
    };
}

CreatureSnapshot Simulation::makeCreatureSnapshot(const Creature& creature) const {
    const Vec2 current = sampleCurrent(creature.position.x, creature.position.y);
    const Vec2 swimVelocity = creature.velocity - current;
    return CreatureSnapshot {
        .valid = true,
        .id = creature.id,
        .lineageId = creature.lineageId,
        .dietClass = classifyDiet(creature.genome),
        .position = creature.position,
        .energy = creature.energy,
        .health = creature.health,
        .age = creature.age,
        .mass = creature.traits.mass,
        .majorRadius = creature.traits.majorRadius,
        .minorRadius = creature.traits.minorRadius,
        .sensorRange = creature.traits.sensorRange,
        .brainComplexity = brainComplexityScore(creature.genome.brain),
        .plantAffinity = creature.genome.ecology.plantAffinity,
        .meatAffinity = creature.genome.ecology.meatAffinity,
        .aggression = creature.genome.ecology.aggression,
        .substrateProximity = creature.substrateProximity,
        .substrateContact = creature.substrateContact,
        .substrateGrip = creature.substrateGrip,
        .substrateScrape = creature.substrateScrape,
        .substrateShelter = creature.substrateShelter,
        .localShear = creature.localShear,
        .bodyStrain = creature.bodyStrain,
        .bodyCompression = creature.bodyCompression,
        .propulsionCoupling = creature.propulsionCoupling,
        .worldSpeed = length(creature.velocity),
        .swimSpeed = length(swimVelocity),
        .currentSpeed = length(current),
        .headIntegrity = segmentIntegrity(creature, 0),
        .tailIntegrity = averageSegmentIntegrity(creature, std::max(1, kBodySegments - 2), kBodySegments)
    };
}

CreatureSnapshot Simulation::selectedCreatureSnapshot() const {
    if (selectedCreatureId_ == 0) {
        return {};
    }

    const auto it = std::find_if(creatures_.begin(), creatures_.end(), [&](const Creature& creature) {
        return creature.id == selectedCreatureId_ && creature.alive;
    });
    if (it == creatures_.end()) {
        return {};
    }
    return makeCreatureSnapshot(*it);
}

CreatureSnapshot Simulation::topEnergyCreatureSnapshot() const {
    const auto it = std::max_element(creatures_.begin(), creatures_.end(), [](const Creature& lhs, const Creature& rhs) {
        return lhs.energy < rhs.energy;
    });
    if (it == creatures_.end()) {
        return {};
    }
    return makeCreatureSnapshot(*it);
}

std::vector<LineageSnapshot> Simulation::topLineageSnapshots(std::size_t maxCount) const {
    std::vector<LineageSnapshot> snapshots;
    snapshots.reserve(lineages_.size());
    for (const LineageRecord& lineage : lineages_) {
        if (lineage.currentPopulation == 0) {
            continue;
        }
        snapshots.push_back(LineageSnapshot {
            .id = lineage.id,
            .parentId = lineage.parentId,
            .depth = lineage.depth,
            .age = timeSeconds_ - lineage.founderTime,
            .noveltyAtBranch = lineage.noveltyAtBranch,
            .population = lineage.currentPopulation,
            .peakPopulation = lineage.peakPopulation,
            .avgBrainComplexity = lineage.avgBrainComplexity
        });
    }

    std::sort(snapshots.begin(), snapshots.end(), [](const LineageSnapshot& lhs, const LineageSnapshot& rhs) {
        if (lhs.population != rhs.population) {
            return lhs.population > rhs.population;
        }
        return lhs.avgBrainComplexity > rhs.avgBrainComplexity;
    });
    if (snapshots.size() > maxCount) {
        snapshots.resize(maxCount);
    }
    return snapshots;
}

WorldSnapshot Simulation::worldSnapshot(std::size_t topLineageCount) const {
    return WorldSnapshot {
        .seed = seed_,
        .timeSeconds = timeSeconds_,
        .worldWidth = worldWidth_,
        .worldHeight = worldHeight_,
        .config = config_,
        .population = stats_.population,
        .blooms = stats_.blooms,
        .carrion = stats_.carrion,
        .reefs = stats_.reefs,
        .births = stats_.births,
        .deaths = stats_.deaths,
        .extinctions = stats_.extinctions,
        .season = stats_.season,
        .activeLineages = stats_.activeLineages,
        .dominantLineageShare = stats_.dominantLineageShare,
        .selectedCreature = selectedCreatureSnapshot(),
        .topEnergyCreature = topEnergyCreatureSnapshot(),
        .topLineages = topLineageSnapshots(topLineageCount)
    };
}

bool Simulation::saveToFile(const std::string& path) const {
    std::ofstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }

    const std::string rngState = [&]() {
        std::ostringstream buffer;
        buffer << rng_;
        return buffer.str();
    }();

    stream.write(kSaveMagic, sizeof(kSaveMagic) - 1);
    return static_cast<bool>(stream)
        && writePod(stream, kSaveVersion)
        && writePod(stream, config_)
        && writePod(stream, worldWidth_)
        && writePod(stream, worldHeight_)
        && writePod(stream, timeSeconds_)
        && writePod(stream, seed_)
        && writePod(stream, nextCreatureId_)
        && writePod(stream, selectedCreatureId_)
        && writePod(stream, trackedLineageId_)
        && writePod(stream, autoSelectionEnabled_)
        && writePod(stream, stats_)
        && writePod(stream, historyAccumulator_)
        && writePod(stream, ancestorGenome_)
        && writePod(stream, nextInnovationId_)
        && writePod(stream, nextHiddenNodeId_)
        && writePod(stream, nextLineageId_)
        && writeVector(stream, creatures_)
        && writeVector(stream, blooms_)
        && writeVector(stream, carrion_)
        && writeVector(stream, reefs_)
        && writeVector(stream, nutrientGrid_)
        && writeVector(stream, nutrientScratch_)
        && writeDeque(stream, history_)
        && writeVector(stream, innovations_)
        && writeVector(stream, lineages_)
        && writeString(stream, rngState);
}

bool Simulation::loadFromFile(const std::string& path) {
    struct LoadedState {
        SimulationConfig config {};
        float worldWidth = 0.0f;
        float worldHeight = 0.0f;
        float timeSeconds = 0.0f;
        std::uint64_t seed = 1;
        std::uint64_t nextCreatureId = 1;
        std::uint64_t selectedCreatureId = 0;
        std::uint32_t trackedLineageId = 0;
        bool autoSelectionEnabled = true;
        Stats stats {};
        float historyAccumulator = 0.0f;
        Genome ancestorGenome {};
        std::uint32_t nextInnovationId = 1;
        std::uint32_t nextHiddenNodeId = kFirstHiddenNodeId;
        std::uint32_t nextLineageId = 1;
        std::vector<Creature> creatures {};
        std::vector<Bloom> blooms {};
        std::vector<Carrion> carrion {};
        std::vector<Reef> reefs {};
        std::vector<float> nutrientGrid {};
        std::vector<float> nutrientScratch {};
        std::deque<HistorySample> history {};
        std::vector<InnovationRecord> innovations {};
        std::vector<LineageRecord> lineages {};
        std::string rngState {};
    };

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }

    char magic[sizeof(kSaveMagic) - 1] {};
    stream.read(magic, sizeof(magic));
    if (!stream || std::string(magic, sizeof(magic)) != std::string(kSaveMagic, sizeof(kSaveMagic) - 1)) {
        return false;
    }

    std::uint32_t version = 0;
    LoadedState loaded {};
    if (!readPod(stream, version) || version != kSaveVersion
        || !readPod(stream, loaded.config)
        || !readPod(stream, loaded.worldWidth)
        || !readPod(stream, loaded.worldHeight)
        || !readPod(stream, loaded.timeSeconds)
        || !readPod(stream, loaded.seed)
        || !readPod(stream, loaded.nextCreatureId)
        || !readPod(stream, loaded.selectedCreatureId)
        || !readPod(stream, loaded.trackedLineageId)
        || !readPod(stream, loaded.autoSelectionEnabled)
        || !readPod(stream, loaded.stats)
        || !readPod(stream, loaded.historyAccumulator)
        || !readPod(stream, loaded.ancestorGenome)
        || !readPod(stream, loaded.nextInnovationId)
        || !readPod(stream, loaded.nextHiddenNodeId)
        || !readPod(stream, loaded.nextLineageId)
        || !readVector(stream, loaded.creatures)
        || !readVector(stream, loaded.blooms)
        || !readVector(stream, loaded.carrion)
        || !readVector(stream, loaded.reefs)
        || !readVector(stream, loaded.nutrientGrid)
        || !readVector(stream, loaded.nutrientScratch)
        || !readDeque(stream, loaded.history)
        || !readVector(stream, loaded.innovations)
        || !readVector(stream, loaded.lineages)
        || !readString(stream, loaded.rngState)) {
        return false;
    }

    std::istringstream rngStream(loaded.rngState);
    std::mt19937_64 restoredRng {};
    rngStream >> restoredRng;
    if (!rngStream) {
        return false;
    }

    config_ = sanitizeConfig(loaded.config);
    config_.world.width = loaded.worldWidth;
    config_.world.height = loaded.worldHeight;
    worldWidth_ = loaded.worldWidth;
    worldHeight_ = loaded.worldHeight;
    timeSeconds_ = loaded.timeSeconds;
    seed_ = loaded.seed;
    nextCreatureId_ = loaded.nextCreatureId;
    selectedCreatureId_ = loaded.selectedCreatureId;
    trackedLineageId_ = loaded.trackedLineageId;
    autoSelectionEnabled_ = loaded.autoSelectionEnabled;
    stats_ = loaded.stats;
    historyAccumulator_ = loaded.historyAccumulator;
    ancestorGenome_ = loaded.ancestorGenome;
    rng_ = restoredRng;
    nextInnovationId_ = loaded.nextInnovationId;
    nextHiddenNodeId_ = loaded.nextHiddenNodeId;
    nextLineageId_ = loaded.nextLineageId;
    creatures_ = std::move(loaded.creatures);
    blooms_ = std::move(loaded.blooms);
    carrion_ = std::move(loaded.carrion);
    reefs_ = std::move(loaded.reefs);
    nutrientGrid_ = std::move(loaded.nutrientGrid);
    nutrientScratch_ = std::move(loaded.nutrientScratch);
    history_ = std::move(loaded.history);
    innovations_ = std::move(loaded.innovations);
    lineages_ = std::move(loaded.lineages);

    if (nutrientScratch_.size() != nutrientGrid_.size()) {
        nutrientScratch_.resize(nutrientGrid_.size());
    }

    if (selectedCreatureId_ != 0) {
        const auto selectedIt = std::find_if(creatures_.begin(), creatures_.end(), [&](const Creature& creature) {
            return creature.id == selectedCreatureId_ && creature.alive;
        });
        if (selectedIt == creatures_.end()) {
            selectedCreatureId_ = 0;
            trackedLineageId_ = 0;
        }
    }

    return true;
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
    info.finPlacement = it->traits.finPlacement;
    info.tailLengthScale = it->traits.tailLengthScale;
    info.tailFork = it->traits.tailFork;
    info.upkeep = it->traits.upkeep;
    info.reproductionThreshold = it->traits.reproductionThreshold;
    info.bodyCurvature = it->bodyCurvature;
    info.bodySlip = it->bodySlip;
    info.flowAlignment = it->flowAlignment;
    info.bodyStrain = it->bodyStrain;
    info.bodyCompression = it->bodyCompression;
    info.propulsionCoupling = it->propulsionCoupling;
    const Vec2 current = sampleCurrent(it->position.x, it->position.y);
    info.worldSpeed = length(it->velocity);
    info.swimSpeed = length(it->velocity - current);
    info.currentSpeed = length(current);
    info.substrateProximity = it->substrateProximity;
    info.substrateContact = it->substrateContact;
    info.substrateGrip = it->substrateGrip;
    info.substrateScrape = it->substrateScrape;
    info.substrateShelter = it->substrateShelter;
    info.localShear = it->localShear;
    info.headIntegrity = segmentIntegrity(*it, 0);
    info.tailIntegrity = averageSegmentIntegrity(*it, std::max(1, kBodySegments - 2), kBodySegments);
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
    updateNutrientGrid(
        nutrientGrid_,
        nutrientScratch_,
        timeSeconds_,
        reefs_,
        worldWidth_,
        worldHeight_,
        dt,
        config_.environment
    );

    for (Bloom& bloom : blooms_) {
        advectLooseResource(
            bloom.position,
            bloom.velocity,
            dt,
            0.08f,
            0.95f,
            worldWidth_,
            worldHeight_,
            timeSeconds_,
            reefs_
        );
        const float nutrient = sampleNutrient(bloom.position.x, bloom.position.y);
        const float bloomDraw = bloom.regrowthRate * nutrient * stats_.season * dt;
        const float harvested = harvestNutrientGrid(
            nutrientGrid_,
            bloom.position.x,
            bloom.position.y,
            bloomDraw,
            worldWidth_,
            worldHeight_,
            config_.environment
        );
        bloom.energy = std::min(bloom.maxEnergy, bloom.energy + harvested);
    }

    for (Carrion& chunk : carrion_) {
        advectLooseResource(
            chunk.position,
            chunk.velocity,
            dt,
            0.22f,
            1.25f,
            worldWidth_,
            worldHeight_,
            timeSeconds_,
            reefs_
        );
        chunk.energy -= chunk.decayRate * dt;
    }
    carrion_.erase(std::remove_if(carrion_.begin(), carrion_.end(), [](const Carrion& carrion) {
        return carrion.energy <= 1.0f;
    }), carrion_.end());

    while (blooms_.size() < config_.environment.targetBlooms) {
        Bloom bloom {};
        bloom.position = sampleBloomSpawnPosition(rng_, nutrientGrid_, worldWidth_, worldHeight_, config_.environment);
        bloom.maxEnergy = randomFloat(rng_, 78.0f, 130.0f);
        bloom.energy = bloom.maxEnergy * randomFloat(rng_, 0.55f, 1.0f);
        bloom.regrowthRate = randomFloat(rng_, 5.0f, 14.0f);
        blooms_.push_back(bloom);
    }

    if (randomFloat(rng_, 0.0f, 1.0f) < config_.environment.bloomRespawnChance * dt) {
        Bloom bloom {};
        bloom.position = sampleBloomSpawnPosition(rng_, nutrientGrid_, worldWidth_, worldHeight_, config_.environment);
        bloom.maxEnergy = randomFloat(rng_, 78.0f, 130.0f);
        bloom.energy = bloom.maxEnergy * randomFloat(rng_, 0.35f, 1.0f);
        bloom.regrowthRate = randomFloat(rng_, 5.0f, 14.0f);
        blooms_.push_back(bloom);
    }

    SpatialHash hash(worldWidth_, worldHeight_, config_.environment.spatialCellSize);
    std::vector<int> nearby;
    hash.clear();
    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        hash.insert(static_cast<int>(index), creatures_[index].position);
    }

    for (Creature& creature : creatures_) {
        std::array<float, kInputCount> inputs {};
        const float headIntegrity = segmentIntegrity(creature, 0);
        const float sensorRadius = creature.traits.sensorRange * (0.45f + headIntegrity * 0.55f);

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
            timeSeconds_,
            reefs_,
            worldWidth_,
            worldHeight_
        );
        creature.substrateProximity = std::max(habitat.proximity, habitat.contact);
        creature.substrateContact = habitat.contact;
        creature.substrateGrip = 0.0f;
        creature.substrateScrape = 0.0f;
        creature.substrateShelter = habitat.shelter;
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
        inputs[kSensorBuckets * kSensorChannels + 8] = creature.substrateShelter;

        creature.lastInputs = inputs;
        forwardBrain(creature.genome, creature, inputs);
    }

    for (Creature& creature : creatures_) {
        const float headIntegrity = segmentIntegrity(creature, 0);
        const float midIntegrity = averageSegmentIntegrity(creature, 1, kBodySegments - 1);
        const float tailIntegrity = averageSegmentIntegrity(creature, std::max(1, kBodySegments - 3), kBodySegments);
        const float turnInput = creature.outputs[0] * (0.35f + midIntegrity * 0.65f);
        const float thrustInput = creature.outputs[1] * (0.28f + tailIntegrity * 0.72f);
        const float signalDrive = outputDrive(creature.outputs[4]);
        const Vec2 current = sampleCurrent(creature.position.x, creature.position.y);

        const Vec2 forward {std::cos(creature.angle), std::sin(creature.angle)};
        const Vec2 side {-forward.y, forward.x};

        const Vec2 relativeVelocity = creature.velocity - current;
        float forwardVelocity = dot(relativeVelocity, forward);
        float lateralVelocity = dot(relativeVelocity, side);

        creature.gaitPhase += dt * (2.2f + outputDrive(thrustInput) * (1.0f + creature.genome.morphology.tailFlex * 3.2f));
        const float tailPulse = 0.82f + 0.18f * std::sin(creature.gaitPhase);
        const float swimCoupling = 0.34f + creature.propulsionCoupling * 0.92f;
        forwardVelocity += thrustInput
            * creature.traits.forwardThrust
            * tailPulse
            * (0.48f + tailIntegrity * 0.34f + swimCoupling * 0.46f)
            * dt;
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

        // Keep the controller heading coupled to the actual body posture so
        // selected creatures do not look like their body and control frame are
        // fighting each other.
        const Vec2 bodyForward = normalize(shortestWrappedDelta(
            creature.bodyPoints[1],
            creature.bodyPoints[0],
            worldWidth_,
            worldHeight_
        ));
        const float bodyHeading = std::atan2(bodyForward.y, bodyForward.x);
        const float headingError = wrapAngle(bodyHeading - creature.angle);
        creature.angularVelocity = creature.angularVelocity * 0.82f + headingError * 1.6f;
        creature.angle = wrapAngle(creature.angle + headingError * 0.32f);

        const float movementCost = creature.traits.upkeep * dt
            * (0.48f + std::abs(thrustInput) * 0.46f + std::abs(turnInput) * 0.22f + creature.signal * 0.24f
                + creature.bodySlip * 0.08f + creature.bodyCurvature * 0.025f
                + creature.bodyStrain * 0.14f + creature.bodyCompression * 0.08f
                + (1.0f - tailIntegrity) * 0.1f + (1.0f - midIntegrity) * 0.06f);
        creature.energy -= movementCost;
        stats_.energySpentOnUpkeep += movementCost;

        if (creature.bodyStrain > 0.72f) {
            creature.health -= (creature.bodyStrain - 0.72f) * (0.38f + creature.traits.mass * 0.002f) * dt;
        }

        if (creature.energy > creature.traits.reproductionThreshold * 0.62f) {
            creature.health = std::min(creature.traits.maxHealth, creature.health + dt * (1.0f + creature.genome.morphology.armor * 2.0f));
            for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
                creature.segmentDamage[segmentIndex] = std::max(
                    0.0f,
                    creature.segmentDamage[segmentIndex]
                        - dt * (0.24f + creature.traits.segmentArmor[segmentIndex] * 0.04f)
                            * (0.25f + headIntegrity * 0.75f)
                );
            }
        }
        if (creature.energy < 0.0f) {
            creature.health += creature.energy;
            creature.energy = 0.0f;
        }
    }

    std::vector<Vec2> reefPositionDelta(creatures_.size(), Vec2 {});
    std::vector<Vec2> reefVelocityDelta(creatures_.size(), Vec2 {});
    std::vector<float> reefAngularDelta(creatures_.size(), 0.0f);
    std::vector<float> reefContact(creatures_.size(), 0.0f);
    std::vector<float> reefGrip(creatures_.size(), 0.0f);
    std::vector<float> reefScrape(creatures_.size(), 0.0f);
    std::vector<float> reefEnergyDrain(creatures_.size(), 0.0f);
    std::vector<float> reefHealthDrain(creatures_.size(), 0.0f);
    std::vector<std::array<float, kBodySegments>> reefSegmentDamage(creatures_.size());

    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        Creature& creature = creatures_[index];
        if (!creature.alive) {
            continue;
        }

        Vec2 positionDelta {};
        Vec2 velocityDelta {};
        float contactAmount = 0.0f;
        float gripAmount = 0.0f;
        float scrapeAmount = 0.0f;
        float angularDelta = 0.0f;
        float energyDrain = 0.0f;
        float healthDrain = 0.0f;

        for (const Reef& reef : reefs_) {
            for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
                const Vec2 delta = shortestWrappedDelta(reef.position, creature.bodyPoints[segmentIndex], worldWidth_, worldHeight_);
                const float distSq = lengthSquared(delta);
                const float minDistance = reef.radius + creature.bodyRadii[segmentIndex] * 0.7f;
                if (distSq >= minDistance * minDistance) {
                    continue;
                }

                const float dist = std::sqrt(std::max(distSq, 1e-4f));
                const float overlap = minDistance - dist;
                const Vec2 normal = delta / dist;
                const Vec2 tangent {-normal.y, normal.x};
                const Vec2 axis = segmentForwardAxis(creature, segmentIndex, worldWidth_, worldHeight_);
                const float edgeAlign = std::abs(dot(axis, tangent));
                const float contact = clamp01(overlap / (creature.bodyRadii[segmentIndex] * 0.65f + reef.radius * 0.04f + 1.0f));
                const float integrity = segmentIntegrity(creature, segmentIndex);
                const float grip = clamp01(
                    creature.traits.segmentSubstrateGrip[segmentIndex]
                    * (0.42f + edgeAlign * 0.58f)
                    * (0.35f + integrity * 0.65f)
                );
                const float tangentSpeed = dot(creature.bodyVelocities[segmentIndex], tangent);
                const float inwardSpeed = std::max(0.0f, -dot(creature.bodyVelocities[segmentIndex], normal));
                const Vec2 lever = shortestWrappedDelta(creature.position, creature.bodyPoints[segmentIndex], worldWidth_, worldHeight_);
                const Vec2 tangentialHold = tangent * (
                    -tangentSpeed
                    * (0.012f + grip * (0.03f + reef.roughness * 0.018f))
                    * contact
                );
                const Vec2 normalResponse = normal * (overlap * (0.28f + reef.roughness * 0.18f));

                positionDelta = positionDelta + normalResponse * (0.85f - grip * 0.12f);
                velocityDelta = velocityDelta + normal * (overlap * (0.32f + reef.roughness * 0.24f)) + tangentialHold;
                angularDelta += cross(lever, tangentialHold + normalResponse * 0.12f) * 0.0007f;
                contactAmount = std::max(contactAmount, contact);
                gripAmount = std::max(gripAmount, contact * grip);

                const float scrapeStress = contact
                    * (std::abs(tangentSpeed) * (0.008f + reef.roughness * 0.004f)
                        + inwardSpeed * (0.012f + reef.roughness * 0.006f))
                    * creature.traits.segmentScrapeSensitivity[segmentIndex]
                    * dt;
                reefSegmentDamage[index][segmentIndex] += scrapeStress;
                energyDrain += contact * (0.04f + grip * 0.05f + creature.traits.mass * 0.0015f) * dt
                    + scrapeStress * (0.8f + creature.traits.mass * 0.015f);
                healthDrain += scrapeStress * 0.16f;
                scrapeAmount = std::max(scrapeAmount, clamp01(scrapeStress * 4.0f));
            }
        }

        reefPositionDelta[index] = positionDelta;
        reefVelocityDelta[index] = velocityDelta;
        reefAngularDelta[index] = angularDelta;
        reefContact[index] = contactAmount;
        reefGrip[index] = gripAmount;
        reefScrape[index] = scrapeAmount;
        reefEnergyDrain[index] = energyDrain;
        reefHealthDrain[index] = healthDrain;
    }

    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        if (reefContact[index] <= 0.0f) {
            continue;
        }

        Creature& creature = creatures_[index];
        creature.position = wrapPosition(creature.position + reefPositionDelta[index], worldWidth_, worldHeight_);
        translateBody(creature, reefPositionDelta[index], worldWidth_, worldHeight_);
        const float damping = std::clamp(1.0f - reefContact[index] * (0.08f + reefGrip[index] * 0.18f), 0.48f, 1.0f);
        creature.velocity = (creature.velocity + reefVelocityDelta[index]) * damping;
        creature.angularVelocity = (creature.angularVelocity + reefAngularDelta[index])
            * std::clamp(1.0f - reefGrip[index] * 0.24f, 0.52f, 1.0f);
        for (Vec2& velocity : creature.bodyVelocities) {
            velocity = (velocity + reefVelocityDelta[index] * 0.45f) * damping;
        }
        for (int segmentIndex = 0; segmentIndex < kBodySegments; ++segmentIndex) {
            creature.segmentDamage[segmentIndex] += reefSegmentDamage[index][segmentIndex];
        }
        creature.energy -= reefEnergyDrain[index];
        creature.health -= reefHealthDrain[index];
        creature.substrateContact = std::max(creature.substrateContact, reefContact[index]);
        creature.substrateGrip = std::max(creature.substrateGrip, reefGrip[index]);
        creature.substrateScrape = std::max(creature.substrateScrape, reefScrape[index]);
        stats_.energySpentOnUpkeep += reefEnergyDrain[index];
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
            timeSeconds_,
            reefs_,
            worldWidth_,
            worldHeight_
        );
        creatures_[index].substrateProximity = std::max(habitat.proximity, habitat.contact);
        creatures_[index].substrateContact = std::max(creatures_[index].substrateContact, habitat.contact);
        creatures_[index].substrateShelter = habitat.shelter;
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
        const float headIntegrity = segmentIntegrity(creature, 0);
        const Vec2 mouth = mouthPosition(creature);
        float intakeThisStep = 0.0f;
        const float reproductiveReadiness = std::clamp(
            (creature.energy - creature.traits.reproductionThreshold * 0.78f)
                / std::max(16.0f, creature.traits.reproductionThreshold * 0.42f),
            0.0f,
            1.0f
        ) * std::clamp(creature.age / 28.0f, 0.0f, 1.0f);

        if (grazeDrive > 0.18f) {
            const float substrateAccess = std::lerp(0.38f, 1.0f, creature.substrateProximity);
            const float ambientHarvest = harvestNutrientGrid(
                nutrientGrid_,
                mouth.x,
                mouth.y,
                grazeDrive * dt * (0.7f + creature.genome.ecology.plantAffinity * 1.55f) * substrateAccess
                    * (0.35f + headIntegrity * 0.65f),
                worldWidth_,
                worldHeight_,
                config_.environment
            );
            const float ambientGain = ambientHarvest * (0.95f + creature.genome.ecology.plantAffinity * 0.45f);
            creature.energy += ambientGain;
            intakeThisStep += ambientGain;
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
                    creature.traits.grazeRate * grazeDrive * dt * (0.35f + headIntegrity * 0.65f)
                );
                bestBloom->energy -= harvest;
                const float harvestGain = harvest * (0.9f + creature.genome.ecology.plantAffinity * 1.22f);
                creature.energy += harvestGain;
                intakeThisStep += harvestGain;
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
                    creature.traits.carrionRate * biteDrive * dt * (0.35f + headIntegrity * 0.65f)
                );
                bestCarrion->energy -= harvest;
                const float carrionGain = harvest * (0.35f + creature.genome.ecology.meatAffinity * 0.6f
                    + creature.genome.ecology.scavengerBias * 0.4f);
                creature.energy += carrionGain;
                intakeThisStep += carrionGain;
                stats_.energyFromCarrion += carrionGain;
                creature.cooldown = 0.12f;
            } else {
                hash.query(creature.position, creature.traits.biteReach * 1.6f, nearby);
                Creature* bestTarget = nullptr;
                int bestTargetSegment = 0;
                float bestTargetScore = -std::numeric_limits<float>::infinity();

                for (int otherIndex : nearby) {
                    Creature& other = creatures_[static_cast<std::size_t>(otherIndex)];
                    if (!other.alive || other.id == creature.id) {
                        continue;
                    }

                    Vec2 targetPoint = other.bodyPoints[0];
                    int targetSegmentIndex = 0;
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
                            targetSegmentIndex = segmentIndex;
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
                        + (targetSegmentIndex >= kBodySegments - 1 ? 0.08f : 0.0f)
                        - dist * 0.015f;
                    if (score > bestTargetScore) {
                        bestTargetScore = score;
                        bestTarget = &other;
                        bestTargetSegment = targetSegmentIndex;
                    }
                }

                if (bestTarget != nullptr) {
                    const int struckSegment = std::clamp(bestTargetSegment, 0, kBodySegments - 1);
                    const float localArmor = bestTarget->traits.segmentArmor[struckSegment];
                    const float impact = creature.traits.biteDamage * biteDrive * dt * (0.35f + headIntegrity * 0.65f);
                    const float mitigation = 1.0f - std::min(0.6f, localArmor * 0.18f + bestTarget->genome.morphology.armor * 0.24f);
                    const float appliedImpact = impact * mitigation;
                    bestTarget->health -= appliedImpact * (struckSegment == 0 ? 1.0f : 0.82f);
                    bestTarget->segmentDamage[struckSegment] += appliedImpact * (0.9f + (struckSegment == 0 ? 0.2f : 0.0f));
                    const float predationGain = impact * (0.08f + creature.genome.ecology.meatAffinity * 0.16f);
                    creature.energy += predationGain;
                    intakeThisStep += predationGain;
                    stats_.energyFromPredation += predationGain;
                    creature.cooldown = 0.22f;
                }
            }
        }

        const float intakeRate = intakeThisStep / std::max(dt, 1e-4f);
        creature.recentIntake = std::lerp(
            creature.recentIntake,
            intakeRate,
            intakeRate > creature.recentIntake ? 0.14f : 0.06f
        );
        const float intakeMomentum = std::clamp(
            creature.recentIntake / (0.08f + creature.traits.mass * 0.0008f),
            0.0f,
            1.0f
        );
        const float reproductionIntent = std::max(reproduceDrive, reproductiveReadiness)
            * (0.25f + intakeMomentum * 0.75f);

        if (reproductionIntent > 0.4f
            && creature.energy > creature.traits.reproductionThreshold * 0.92f
            && creature.age > 18.0f
            && creatures_.size() + pendingSpawns.size() < config_.world.maxPopulation) {
            const Genome childGenome = mutateGenome(
                creature.genome,
                rng_,
                config_.evolution,
                innovations_,
                nextInnovationId_,
                nextHiddenNodeId_
            );
            const BrainDistance brainDistance = compareBrains(childGenome.brain, creature.genome.brain);
            std::uint32_t childLineageId = creature.lineageId;
            std::uint8_t childLineageDepth = creature.lineageDepth;
            const int structuralDelta = std::abs(childGenome.brain.hiddenCount - creature.genome.brain.hiddenCount)
                + std::abs(childGenome.brain.connectionCount - creature.genome.brain.connectionCount);
            const bool branchLineage = (
                childGenome.brain.hiddenCount > creature.genome.brain.hiddenCount
                && brainDistance.topologyNovelty > config_.evolution.lineageBranchHiddenNoveltyThreshold
            ) || (
                structuralDelta >= config_.evolution.lineageBranchStructuralDelta
                && brainDistance.compatibility > config_.evolution.lineageBranchStructuralCompatibilityThreshold
            ) || brainDistance.compatibility > config_.evolution.lineageBranchCompatibilityThreshold;
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
        chunk.velocity = creature.velocity * 0.35f;
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
    stats_.avgSubstrateGrip = 0.0f;
    stats_.avgSubstrateScrape = 0.0f;
    stats_.avgSubstrateShelter = 0.0f;
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
        stats_.avgSubstrateGrip += creature.substrateGrip;
        stats_.avgSubstrateScrape += creature.substrateScrape;
        stats_.avgSubstrateShelter += creature.substrateShelter;
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
    stats_.avgSubstrateGrip /= divisor;
    stats_.avgSubstrateScrape /= divisor;
    stats_.avgSubstrateShelter /= divisor;
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
            .avgSubstrateGrip = stats_.avgSubstrateGrip,
            .avgSubstrateScrape = stats_.avgSubstrateScrape,
            .avgSubstrateShelter = stats_.avgSubstrateShelter,
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
