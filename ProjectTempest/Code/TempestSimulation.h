#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace Tempest {

constexpr std::int32_t TicksPerSecond = 20;

struct Point {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

enum class Faction : std::uint8_t {
    Neutral,
    Freegrid,
    Chorus,
};

enum class UnitKind : std::uint8_t {
    FabricatorRig,
    CourierScout,
    LancerCrew,
    CoilCarrier,
    Skitter,
    Warden,
    Harrower,
    Count,
};

enum class BuildingKind : std::uint8_t {
    RelayCore,
    FabricatorBay,
    Dynamo,
    ArcSentry,
    MachineNest,
    SignalPylon,
    ChorusSpire,
    Count,
};

enum class AbilityKind : std::uint8_t {
    GridLinkScan,
    EmergencyOvercharge,
    Count,
};

struct UnitDefinition {
    const char *displayName = "";
    Faction faction = Faction::Neutral;
    std::int32_t maximumHitPoints = 0;
    std::int32_t salvageCost = 0;
    std::int32_t buildTicks = 0;
    std::int32_t capacityCost = 0;
    std::int32_t attackDamage = 0;
    std::int32_t attackCooldownTicks = 0;
};

struct BuildingDefinition {
    const char *displayName = "";
    Faction faction = Faction::Neutral;
    std::int32_t maximumHitPoints = 0;
    std::int32_t salvageCost = 0;
    std::int32_t buildTicks = 0;
    std::int32_t capacityProvided = 0;
};

struct AbilityDefinition {
    const char *displayName = "";
    std::int32_t abilityChargeCost = 0;
    std::int32_t cooldownTicks = 0;
    std::int32_t durationTicks = 0;
};

const UnitDefinition &GetUnitDefinition(UnitKind kind);
const BuildingDefinition &GetBuildingDefinition(BuildingKind kind);
const AbilityDefinition &GetAbilityDefinition(AbilityKind kind);

enum class MatchOutcome : std::uint8_t {
    InProgress,
    Victory,
    Defeat,
};

enum class OrderKind : std::uint8_t {
    Idle,
    Move,
    Capture,
    Attack,
};

struct Unit {
    std::uint32_t id = 0;
    Faction faction = Faction::Neutral;
    UnitKind kind = UnitKind::CourierScout;
    Point position;
    Point destination;
    OrderKind order = OrderKind::Idle;
    std::uint32_t targetId = 0;
    std::int32_t hitPoints = 0;
    std::int32_t maximumHitPoints = 0;
    std::int32_t attackCooldownTicks = 0;
    std::int32_t abilityCooldownTicks = 0;
    bool alive = true;
};

struct Building {
    std::uint32_t id = 0;
    Faction faction = Faction::Neutral;
    BuildingKind kind = BuildingKind::RelayCore;
    Point position;
    std::int32_t hitPoints = 0;
    std::int32_t maximumHitPoints = 0;
    bool complete = true;
    std::int32_t remainingBuildTicks = 0;
};

struct ControlNode {
    std::uint32_t id = 0;
    Point position;
    Faction owner = Faction::Neutral;
    std::int32_t freegridCaptureTicks = 0;
    std::int32_t chorusCaptureTicks = 0;
};

enum class CommandKind : std::uint8_t {
    Move,
    Capture,
    Attack,
    BuildRelay,
    ProduceCourier,
    ArcPulse,
    TogglePause,
    Restart,
};

struct Command {
    std::uint64_t executeTick = 0;
    CommandKind kind = CommandKind::Move;
    std::uint32_t actorId = 0;
    std::uint32_t targetId = 0;
    Point point;
    std::uint64_t sequence = 0;
};

struct ProductionOrder {
    std::uint32_t producerId = 0;
    UnitKind kind = UnitKind::CourierScout;
    std::int32_t remainingTicks = 0;
};

struct MatchState {
    std::uint64_t tick = 0;
    bool paused = false;
    MatchOutcome outcome = MatchOutcome::InProgress;
    std::int32_t salvage = 0;
    std::int32_t abilityCharge = 0;
    std::int32_t incomeRemainderTicks = 0;
    std::int32_t chorusSpawnTicks = 0;
    std::uint32_t nextEntityId = 1;
    std::vector<Unit> units;
    std::vector<Building> buildings;
    std::vector<ControlNode> nodes;
    std::deque<ProductionOrder> production;
};

class Simulation {
public:
    Simulation();

    void Reset();
    void Submit(Command command);
    void Step();
    void Step(std::uint32_t tickCount);

    const MatchState &GetState() const { return m_state; }
    std::uint64_t Checksum() const;
    std::int32_t FreegridCapacity() const;
    std::int32_t UsedFreegridCapacity() const;
    bool CanProduceUnit(std::uint32_t producerId, UnitKind kind) const;

private:
    MatchState m_state;
    std::vector<Command> m_commands;
    std::uint64_t m_nextCommandSequence = 1;
    bool m_restartedDuringCommand = false;

    Unit *FindUnit(std::uint32_t id);
    Building *FindBuilding(std::uint32_t id);
    ControlNode *FindNode(std::uint32_t id);
    const Unit *FindUnit(std::uint32_t id) const;
    const Building *FindBuilding(std::uint32_t id) const;

    std::uint32_t AddUnit(Faction faction, UnitKind kind, Point position);
    std::uint32_t AddBuilding(Faction faction, BuildingKind kind, Point position, bool complete = true);
    void ExecuteDueCommands();
    void Execute(const Command &command);
    void UpdateConstructionAndProduction();
    void UpdateMovementAndCapture();
    void UpdateCombat();
    void UpdateEconomyAndAi();
    void UpdateOutcome();
};

} // namespace Tempest
