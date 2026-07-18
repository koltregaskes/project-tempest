#include "TempestSimulation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>

namespace Tempest {
namespace {

constexpr std::int32_t CaptureRange = 700;
constexpr std::int32_t CaptureTicksRequired = TicksPerSecond * 4;
constexpr std::int32_t RepairRange = 900;
constexpr std::int32_t RepairHitPoints = 20;
constexpr std::int32_t RepairCooldownTicks = TicksPerSecond / 2;
constexpr std::int32_t RepairHitPointsPerSalvage = 4;
constexpr std::int32_t ArcPulseRange = 3200;
constexpr std::int32_t ArcPulseAbilityChargeCost = 25;
constexpr std::int32_t ArcPulseCooldownTicks = TicksPerSecond * 12;
constexpr std::int32_t ArenaExtent = 18000;
constexpr std::int32_t OverchargeDamageNumerator = 3;
constexpr std::int32_t OverchargeDamageDenominator = 2;
constexpr std::int32_t OverchargeMoveNumerator = 5;
constexpr std::int32_t OverchargeMoveDenominator = 4;

constexpr std::array<UnitDefinition, static_cast<std::size_t>(UnitKind::Count)> UnitDefinitions {{
    { "Fabricator rig", Faction::Freegrid, 240, 250, TicksPerSecond * 5, 2, 80, 8, 12, 1200 },
    { "Courier scout", Faction::Freegrid, 180, 150, TicksPerSecond * 3, 1, 150, 20, 9, 1800 },
    { "Lancer crew", Faction::Freegrid, 260, 240, TicksPerSecond * 6, 2, 110, 28, 10, 2600 },
    { "Coil carrier", Faction::Freegrid, 520, 450, TicksPerSecond * 10, 4, 70, 55, 18, 3200 },
    { "Skitter", Faction::Chorus, 110, 0, 0, 0, 160, 11, 12, 1000 },
    { "Warden", Faction::Chorus, 280, 0, 0, 0, 95, 24, 14, 2200 },
    { "Harrower", Faction::Chorus, 650, 0, 0, 0, 60, 48, 20, 3000 },
}};

constexpr std::array<BuildingDefinition, static_cast<std::size_t>(BuildingKind::Count)> BuildingDefinitions {{
    { "Relay Core", Faction::Freegrid, 900, 0, 0, 4, 0, 0, 0 },
    { "Fabricator Bay", Faction::Freegrid, 650, 300, TicksPerSecond * 6, 2, 0, 0, 0 },
    { "Dynamo", Faction::Freegrid, 260, 200, TicksPerSecond * 4, 4, 0, 0, 0 },
    { "Arc Sentry", Faction::Freegrid, 400, 250, TicksPerSecond * 5, 0, 30, TicksPerSecond, 2800 },
    { "Machine Nest", Faction::Chorus, 500, 0, 0, 0, 0, 0, 0 },
    { "Signal Pylon", Faction::Chorus, 340, 0, 0, 0, 22, TicksPerSecond * 3 / 2, 3200 },
    { "Chorus Spire", Faction::Chorus, 800, 0, 0, 0, 0, 0, 0 },
}};

constexpr std::array<AbilityDefinition, static_cast<std::size_t>(AbilityKind::Count)> AbilityDefinitions {{
    { "Grid-link scan", 20, TicksPerSecond * 15, TicksPerSecond * 6 },
    { "Emergency overcharge", 35, TicksPerSecond * 24, TicksPerSecond * 8 },
}};

template <typename Kind, typename Definitions>
const typename Definitions::value_type &DefinitionAt(Kind kind, const Definitions &definitions)
{
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= definitions.size()) {
        std::abort();
    }
    return definitions[index];
}

std::int64_t DistanceSquared(Point left, Point right)
{
    const std::int64_t dx = static_cast<std::int64_t>(left.x) - right.x;
    const std::int64_t dy = static_cast<std::int64_t>(left.y) - right.y;
    return (dx * dx) + (dy * dy);
}

bool IsWithin(Point left, Point right, std::int32_t range)
{
    return DistanceSquared(left, right) <= static_cast<std::int64_t>(range) * range;
}

Point StructurePosition(BuildingKind kind, Point nodePosition)
{
    if (kind == BuildingKind::ArcSentry) {
        return { nodePosition.x + 900, nodePosition.y - 650 };
    }
    return nodePosition;
}

void MoveTowards(Point &position, Point destination, std::int32_t moveDistancePerTick)
{
    const std::int64_t dx = static_cast<std::int64_t>(destination.x) - position.x;
    const std::int64_t dy = static_cast<std::int64_t>(destination.y) - position.y;
    const std::int64_t distance = std::abs(dx) + std::abs(dy);
    if (distance <= moveDistancePerTick) {
        position = destination;
        return;
    }
    position.x += static_cast<std::int32_t>(
        (static_cast<std::int64_t>(moveDistancePerTick) * dx) / distance);
    position.y += static_cast<std::int32_t>(
        (static_cast<std::int64_t>(moveDistancePerTick) * dy) / distance);
}

template <typename Value>
void HashValue(std::uint64_t &hash, Value value)
{
    const std::uint64_t bits = static_cast<std::uint64_t>(value);
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        hash ^= (bits >> (index * 8U)) & 0xFFU;
        hash *= 1099511628211ULL;
    }
}

} // namespace

const UnitDefinition &GetUnitDefinition(UnitKind kind)
{
    return DefinitionAt(kind, UnitDefinitions);
}

const BuildingDefinition &GetBuildingDefinition(BuildingKind kind)
{
    return DefinitionAt(kind, BuildingDefinitions);
}

const AbilityDefinition &GetAbilityDefinition(AbilityKind kind)
{
    return DefinitionAt(kind, AbilityDefinitions);
}

Simulation::Simulation()
{
    Reset();
}

void Simulation::Reset()
{
    m_state = MatchState {};
    m_commands.clear();
    m_nextCommandSequence = 1;
    m_restartedDuringCommand = false;
    m_state.salvage = 500;
    m_state.abilityCharge = 50;

    AddUnit(Faction::Freegrid, UnitKind::FabricatorRig, { -12400, -9400 });
    AddUnit(Faction::Freegrid, UnitKind::CourierScout, { -11600, -8600 });
    AddBuilding(Faction::Freegrid, BuildingKind::RelayCore, { -14500, -10500 });
    AddBuilding(Faction::Freegrid, BuildingKind::FabricatorBay, { -12800, -10800 });
    AddBuilding(Faction::Chorus, BuildingKind::MachineNest, { 12600, 9400 });
    AddBuilding(Faction::Chorus, BuildingKind::SignalPylon, { 7200, 6100 });
    AddBuilding(Faction::Chorus, BuildingKind::ChorusSpire, { 14500, 10500 });

    for (Point position : { Point { -4500, -2500 }, Point { 1000, 2500 }, Point { 6500, 6500 } }) {
        ControlNode node;
        node.id = m_state.nextEntityId++;
        node.position = position;
        m_state.nodes.push_back(node);
    }

    const std::uint32_t droneId = AddUnit(Faction::Chorus, UnitKind::Skitter, { 9000, 7000 });
    if (Unit *drone = FindUnit(droneId)) {
        drone->order = OrderKind::Attack;
        drone->targetId = m_state.units.front().id;
    }
}

void Simulation::Submit(Command command)
{
    command.sequence = m_nextCommandSequence++;
    m_commands.push_back(command);
    std::stable_sort(m_commands.begin(), m_commands.end(), [](const Command &left, const Command &right) {
        if (left.executeTick != right.executeTick) {
            return left.executeTick < right.executeTick;
        }
        return left.sequence < right.sequence;
    });
}

void Simulation::Step()
{
    m_restartedDuringCommand = false;
    ExecuteDueCommands();
    if (m_restartedDuringCommand || m_state.paused || m_state.outcome != MatchOutcome::InProgress) {
        return;
    }

    UpdateConstructionAndProduction();
    UpdateMovementAndCapture();
    UpdateRepairs();
    UpdateCombat();
    UpdateBuildingCombat();
    UpdateAbilities();
    UpdateEconomyAndAi();
    UpdateOutcome();
    ++m_state.tick;
}

void Simulation::Step(std::uint32_t tickCount)
{
    for (std::uint32_t index = 0; index < tickCount; ++index) {
        Step();
    }
}

Unit *Simulation::FindUnit(std::uint32_t id)
{
    const auto found = std::find_if(m_state.units.begin(), m_state.units.end(), [id](const Unit &unit) {
        return unit.id == id;
    });
    return found == m_state.units.end() ? nullptr : &*found;
}

Building *Simulation::FindBuilding(std::uint32_t id)
{
    const auto found = std::find_if(m_state.buildings.begin(), m_state.buildings.end(), [id](const Building &building) {
        return building.id == id;
    });
    return found == m_state.buildings.end() ? nullptr : &*found;
}

ControlNode *Simulation::FindNode(std::uint32_t id)
{
    const auto found = std::find_if(m_state.nodes.begin(), m_state.nodes.end(), [id](const ControlNode &node) {
        return node.id == id;
    });
    return found == m_state.nodes.end() ? nullptr : &*found;
}

const Unit *Simulation::FindUnit(std::uint32_t id) const
{
    const auto found = std::find_if(m_state.units.begin(), m_state.units.end(), [id](const Unit &unit) {
        return unit.id == id;
    });
    return found == m_state.units.end() ? nullptr : &*found;
}

const Building *Simulation::FindBuilding(std::uint32_t id) const
{
    const auto found = std::find_if(m_state.buildings.begin(), m_state.buildings.end(), [id](const Building &building) {
        return building.id == id;
    });
    return found == m_state.buildings.end() ? nullptr : &*found;
}

const ControlNode *Simulation::FindNode(std::uint32_t id) const
{
    const auto found = std::find_if(m_state.nodes.begin(), m_state.nodes.end(), [id](const ControlNode &node) {
        return node.id == id;
    });
    return found == m_state.nodes.end() ? nullptr : &*found;
}

std::uint32_t Simulation::AddUnit(Faction faction, UnitKind kind, Point position)
{
    const UnitDefinition &definition = GetUnitDefinition(kind);
    Unit unit;
    unit.id = m_state.nextEntityId++;
    unit.faction = faction;
    unit.kind = kind;
    unit.position = position;
    unit.destination = position;
    unit.maximumHitPoints = definition.maximumHitPoints;
    unit.hitPoints = unit.maximumHitPoints;
    m_state.units.push_back(unit);
    return unit.id;
}

std::uint32_t Simulation::AddBuilding(
    Faction faction,
    BuildingKind kind,
    Point position,
    bool complete)
{
    const BuildingDefinition &definition = GetBuildingDefinition(kind);
    Building building;
    building.id = m_state.nextEntityId++;
    building.faction = faction;
    building.kind = kind;
    building.position = position;
    building.complete = complete;
    building.remainingBuildTicks = complete ? 0 : definition.buildTicks;
    building.maximumHitPoints = definition.maximumHitPoints;
    building.hitPoints = building.maximumHitPoints;
    m_state.buildings.push_back(building);
    return building.id;
}

std::int32_t Simulation::FreegridCapacity() const
{
    std::int32_t capacity = 0;
    for (const Building &building : m_state.buildings) {
        if (building.faction == Faction::Freegrid && building.complete && building.hitPoints > 0) {
            capacity += GetBuildingDefinition(building.kind).capacityProvided;
        }
    }
    return capacity;
}

std::int32_t Simulation::UsedFreegridCapacity() const
{
    std::int32_t used = 0;
    for (const Unit &unit : m_state.units) {
        if (unit.faction == Faction::Freegrid && unit.alive) {
            used += GetUnitDefinition(unit.kind).capacityCost;
        }
    }
    for (const ProductionOrder &order : m_state.production) {
        used += GetUnitDefinition(order.kind).capacityCost;
    }
    return used;
}

bool Simulation::CanProduceUnit(std::uint32_t producerId, UnitKind kind) const
{
    if (static_cast<std::size_t>(kind) >= static_cast<std::size_t>(UnitKind::Count)) {
        return false;
    }
    const Building *producer = FindBuilding(producerId);
    const UnitDefinition &unit = GetUnitDefinition(kind);
    return producer && producer->faction == Faction::Freegrid && producer->complete && producer->hitPoints > 0 &&
        producer->kind == BuildingKind::FabricatorBay && unit.faction == Faction::Freegrid &&
        m_state.salvage >= unit.salvageCost && UsedFreegridCapacity() + unit.capacityCost <= FreegridCapacity();
}

// TheSuperHackers @feature koltregaskes 15/07/2026 Add deterministic player structures and governed faction abilities.
bool Simulation::CanBuildStructure(std::uint32_t actorId, std::uint32_t nodeId, BuildingKind kind) const
{
    if (kind != BuildingKind::Dynamo && kind != BuildingKind::ArcSentry) {
        return false;
    }
    const Unit *actor = FindUnit(actorId);
    const ControlNode *node = FindNode(nodeId);
    const BuildingDefinition &definition = GetBuildingDefinition(kind);
    if (!actor || !actor->alive || actor->faction != Faction::Freegrid ||
        actor->kind != UnitKind::FabricatorRig || !node || node->owner != Faction::Freegrid ||
        m_state.salvage < definition.salvageCost) {
        return false;
    }
    const Point position = StructurePosition(kind, node->position);
    return std::none_of(m_state.buildings.begin(), m_state.buildings.end(), [kind, position](const Building &building) {
        return building.kind == kind && building.hitPoints > 0 && IsWithin(building.position, position, 100);
    });
}

bool Simulation::CanActivateAbility(AbilityKind kind) const
{
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= static_cast<std::size_t>(AbilityKind::Count)) {
        return false;
    }
    const AbilityDefinition &definition = GetAbilityDefinition(kind);
    return m_state.outcome == MatchOutcome::InProgress && !m_state.paused &&
        m_state.abilityCharge >= definition.abilityChargeCost && m_state.abilityCooldownTicks[index] == 0;
}

void Simulation::ExecuteDueCommands()
{
    while (!m_commands.empty() && m_commands.front().executeTick <= m_state.tick) {
        const Command command = m_commands.front();
        m_commands.erase(m_commands.begin());
        Execute(command);
    }
}

void Simulation::Execute(const Command &command)
{
    if (command.kind == CommandKind::Restart) {
        Reset();
        m_restartedDuringCommand = true;
        return;
    }
    if (command.kind == CommandKind::TogglePause) {
        m_state.paused = !m_state.paused;
        return;
    }
    if (m_state.paused || m_state.outcome != MatchOutcome::InProgress) {
        return;
    }

    Unit *actor = FindUnit(command.actorId);
    if (command.kind == CommandKind::BuildStructure) {
        if (!CanBuildStructure(command.actorId, command.targetId, command.buildingKind)) {
            return;
        }
        const ControlNode *node = FindNode(command.targetId);
        const BuildingDefinition &definition = GetBuildingDefinition(command.buildingKind);
        if (node) {
            m_state.salvage -= definition.salvageCost;
            AddBuilding(
                Faction::Freegrid,
                command.buildingKind,
                StructurePosition(command.buildingKind, node->position),
                false);
        }
        return;
    }

    if (command.kind == CommandKind::ProduceUnit) {
        if (!CanProduceUnit(command.actorId, command.unitKind)) {
            return;
        }
        const UnitDefinition &unit = GetUnitDefinition(command.unitKind);
        m_state.salvage -= unit.salvageCost;
        m_state.production.push_back({ command.actorId, command.unitKind, unit.buildTicks });
        return;
    }

    if (command.kind == CommandKind::ActivateAbility) {
        if (!CanActivateAbility(command.abilityKind)) {
            return;
        }
        if (command.abilityKind == AbilityKind::GridLinkScan &&
            (std::abs(static_cast<std::int64_t>(command.point.x)) > ArenaExtent ||
                std::abs(static_cast<std::int64_t>(command.point.y)) > ArenaExtent)) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(command.abilityKind);
        const AbilityDefinition &definition = GetAbilityDefinition(command.abilityKind);
        m_state.abilityCharge -= definition.abilityChargeCost;
        m_state.abilityCooldownTicks[index] = definition.cooldownTicks;
        m_state.abilityDurationTicks[index] = definition.durationTicks;
        if (command.abilityKind == AbilityKind::GridLinkScan) {
            m_state.scanCenter = command.point;
        }
        return;
    }

    if (!actor || !actor->alive || actor->faction != Faction::Freegrid) {
        return;
    }

    switch (command.kind) {
        case CommandKind::Move:
            actor->destination = command.point;
            actor->order = OrderKind::Move;
            actor->targetId = 0;
            break;
        case CommandKind::Capture:
            if (ControlNode *node = FindNode(command.targetId)) {
                actor->destination = node->position;
                actor->order = OrderKind::Capture;
                actor->targetId = node->id;
            }
            break;
        case CommandKind::Attack:
            if (const Unit *unitTarget = FindUnit(command.targetId);
                unitTarget && unitTarget->alive && unitTarget->faction != actor->faction) {
                actor->order = OrderKind::Attack;
                actor->targetId = command.targetId;
            } else if (const Building *buildingTarget = FindBuilding(command.targetId);
                       buildingTarget && buildingTarget->hitPoints > 0 &&
                       buildingTarget->faction != actor->faction) {
                actor->order = OrderKind::Attack;
                actor->targetId = command.targetId;
            }
            break;
        case CommandKind::Repair: {
            if (actor->kind != UnitKind::FabricatorRig) {
                break;
            }
            const Unit *unitTarget = FindUnit(command.targetId);
            const Building *buildingTarget = FindBuilding(command.targetId);
            const bool validUnit = unitTarget && unitTarget->alive && unitTarget->faction == Faction::Freegrid &&
                unitTarget->hitPoints < unitTarget->maximumHitPoints;
            const bool validBuilding = buildingTarget && buildingTarget->hitPoints > 0 &&
                buildingTarget->faction == Faction::Freegrid &&
                buildingTarget->hitPoints < buildingTarget->maximumHitPoints;
            if (validUnit || validBuilding) {
                actor->order = OrderKind::Repair;
                actor->targetId = command.targetId;
            }
            break;
        }
        case CommandKind::ArcPulse:
            if (actor->abilityCooldownTicks == 0 && m_state.abilityCharge >= ArcPulseAbilityChargeCost &&
                IsWithin(actor->position, command.point, ArcPulseRange)) {
                m_state.abilityCharge -= ArcPulseAbilityChargeCost;
                actor->abilityCooldownTicks = ArcPulseCooldownTicks;
                for (Unit &unit : m_state.units) {
                    if (unit.alive && unit.faction == Faction::Chorus && IsWithin(unit.position, command.point, ArcPulseRange)) {
                        unit.hitPoints = std::max(0, unit.hitPoints - 70);
                        if (unit.hitPoints == 0) {
                            unit.alive = false;
                            unit.order = OrderKind::Idle;
                            unit.targetId = 0;
                        }
                    }
                }
                for (Building &building : m_state.buildings) {
                    if (building.faction == Faction::Chorus && IsWithin(building.position, command.point, ArcPulseRange)) {
                        building.hitPoints = std::max(0, building.hitPoints - 45);
                    }
                }
            }
            break;
        default:
            break;
    }
}

void Simulation::UpdateConstructionAndProduction()
{
    for (Building &building : m_state.buildings) {
        if (!building.complete && building.remainingBuildTicks > 0 && --building.remainingBuildTicks == 0) {
            building.complete = true;
        }
    }

    if (!m_state.production.empty()) {
        ProductionOrder &order = m_state.production.front();
        Building *producer = FindBuilding(order.producerId);
        if (!producer || producer->hitPoints <= 0) {
            m_state.production.pop_front();
        } else if (producer->complete && --order.remainingTicks <= 0) {
            AddUnit(Faction::Freegrid, order.kind, { producer->position.x + 800, producer->position.y + 500 });
            m_state.production.pop_front();
        }
    }
}

void Simulation::UpdateMovementAndCapture()
{
    for (Unit &unit : m_state.units) {
        if (!unit.alive) {
            continue;
        }
        if (unit.attackCooldownTicks > 0) {
            --unit.attackCooldownTicks;
        }
        if (unit.abilityCooldownTicks > 0) {
            --unit.abilityCooldownTicks;
        }
        if (unit.repairCooldownTicks > 0) {
            --unit.repairCooldownTicks;
        }

        Point destination = unit.destination;
        if (unit.order == OrderKind::Attack || unit.order == OrderKind::Repair) {
            if (const Unit *targetUnit = FindUnit(unit.targetId); targetUnit && targetUnit->alive) {
                destination = targetUnit->position;
            } else if (const Building *targetBuilding = FindBuilding(unit.targetId);
                       targetBuilding && targetBuilding->hitPoints > 0) {
                destination = targetBuilding->position;
            } else {
                unit.order = OrderKind::Idle;
                unit.targetId = 0;
            }
        }

        const std::int32_t interactionRange = unit.order == OrderKind::Repair
            ? RepairRange
            : GetUnitDefinition(unit.kind).attackRange;
        const bool shouldMove = unit.order == OrderKind::Move || unit.order == OrderKind::Capture ||
            ((unit.order == OrderKind::Attack || unit.order == OrderKind::Repair) &&
                !IsWithin(unit.position, destination, interactionRange));
        if (shouldMove) {
            std::int32_t moveDistance = GetUnitDefinition(unit.kind).moveDistancePerTick;
            const std::size_t overchargeIndex = static_cast<std::size_t>(AbilityKind::EmergencyOvercharge);
            if (unit.faction == Faction::Freegrid && m_state.abilityDurationTicks[overchargeIndex] > 0) {
                moveDistance = moveDistance * OverchargeMoveNumerator / OverchargeMoveDenominator;
            }
            MoveTowards(unit.position, destination, moveDistance);
            if (unit.order == OrderKind::Move && unit.position.x == destination.x && unit.position.y == destination.y) {
                unit.order = OrderKind::Idle;
            }
        }
    }

    for (ControlNode &node : m_state.nodes) {
        bool freegridPresent = false;
        bool chorusPresent = false;
        for (const Unit &unit : m_state.units) {
            if (!unit.alive || unit.order != OrderKind::Capture || unit.targetId != node.id ||
                !IsWithin(unit.position, node.position, CaptureRange)) {
                continue;
            }
            freegridPresent |= unit.faction == Faction::Freegrid;
            chorusPresent |= unit.faction == Faction::Chorus;
        }
        if (freegridPresent == chorusPresent) {
            continue;
        }
        if (freegridPresent) {
            node.chorusCaptureTicks = 0;
            if (++node.freegridCaptureTicks >= CaptureTicksRequired) {
                node.owner = Faction::Freegrid;
                node.freegridCaptureTicks = CaptureTicksRequired;
            }
        } else {
            node.freegridCaptureTicks = 0;
            if (++node.chorusCaptureTicks >= CaptureTicksRequired) {
                node.owner = Faction::Chorus;
                node.chorusCaptureTicks = CaptureTicksRequired;
            }
        }
    }
}

void Simulation::UpdateRepairs()
{
    for (Unit &repairer : m_state.units) {
        if (!repairer.alive || repairer.faction != Faction::Freegrid ||
            repairer.kind != UnitKind::FabricatorRig || repairer.order != OrderKind::Repair ||
            repairer.repairCooldownTicks > 0) {
            continue;
        }

        Point targetPosition;
        std::int32_t *targetHitPoints = nullptr;
        std::int32_t targetMaximumHitPoints = 0;
        if (Unit *targetUnit = FindUnit(repairer.targetId);
            targetUnit && targetUnit->alive && targetUnit->faction == Faction::Freegrid) {
            targetPosition = targetUnit->position;
            targetHitPoints = &targetUnit->hitPoints;
            targetMaximumHitPoints = targetUnit->maximumHitPoints;
        } else if (Building *targetBuilding = FindBuilding(repairer.targetId);
                   targetBuilding && targetBuilding->hitPoints > 0 &&
                   targetBuilding->faction == Faction::Freegrid) {
            targetPosition = targetBuilding->position;
            targetHitPoints = &targetBuilding->hitPoints;
            targetMaximumHitPoints = targetBuilding->maximumHitPoints;
        }
        if (!targetHitPoints || *targetHitPoints >= targetMaximumHitPoints) {
            repairer.order = OrderKind::Idle;
            repairer.targetId = 0;
            continue;
        }
        if (!IsWithin(repairer.position, targetPosition, RepairRange)) {
            continue;
        }

        const std::int32_t restored = std::min(RepairHitPoints, targetMaximumHitPoints - *targetHitPoints);
        const std::int32_t salvageCost =
            (restored + RepairHitPointsPerSalvage - 1) / RepairHitPointsPerSalvage;
        if (m_state.salvage < salvageCost) {
            continue;
        }
        m_state.salvage -= salvageCost;
        *targetHitPoints += restored;
        repairer.repairCooldownTicks = RepairCooldownTicks;
        if (*targetHitPoints >= targetMaximumHitPoints) {
            repairer.order = OrderKind::Idle;
            repairer.targetId = 0;
        }
    }
}

void Simulation::UpdateAbilities()
{
    for (std::size_t index = 0; index < m_state.abilityCooldownTicks.size(); ++index) {
        if (m_state.abilityCooldownTicks[index] > 0) {
            --m_state.abilityCooldownTicks[index];
        }
        if (m_state.abilityDurationTicks[index] > 0) {
            --m_state.abilityDurationTicks[index];
        }
    }
}

void Simulation::UpdateCombat()
{
    for (Unit &attacker : m_state.units) {
        if (!attacker.alive || attacker.order != OrderKind::Attack || attacker.attackCooldownTicks > 0) {
            continue;
        }

        Point targetPosition;
        std::int32_t *targetHitPoints = nullptr;
        if (Unit *targetUnit = FindUnit(attacker.targetId);
            targetUnit && targetUnit->alive && targetUnit->faction != attacker.faction) {
            targetPosition = targetUnit->position;
            targetHitPoints = &targetUnit->hitPoints;
        } else if (Building *targetBuilding = FindBuilding(attacker.targetId);
                   targetBuilding && targetBuilding->hitPoints > 0 && targetBuilding->faction != attacker.faction) {
            targetPosition = targetBuilding->position;
            targetHitPoints = &targetBuilding->hitPoints;
        }
        const UnitDefinition &definition = GetUnitDefinition(attacker.kind);
        if (!targetHitPoints || !IsWithin(attacker.position, targetPosition, definition.attackRange)) {
            continue;
        }

        std::int32_t attackDamage = definition.attackDamage;
        const std::size_t overchargeIndex = static_cast<std::size_t>(AbilityKind::EmergencyOvercharge);
        if (attacker.faction == Faction::Freegrid && m_state.abilityDurationTicks[overchargeIndex] > 0) {
            attackDamage = attackDamage * OverchargeDamageNumerator / OverchargeDamageDenominator;
        }
        *targetHitPoints -= attackDamage;
        attacker.attackCooldownTicks = definition.attackCooldownTicks;
    }

    for (Unit &unit : m_state.units) {
        if (unit.hitPoints <= 0) {
            unit.hitPoints = 0;
            unit.alive = false;
            unit.order = OrderKind::Idle;
        }
    }
    for (Building &building : m_state.buildings) {
        building.hitPoints = std::max(0, building.hitPoints);
    }
}

void Simulation::UpdateBuildingCombat()
{
    for (Building &attacker : m_state.buildings) {
        const BuildingDefinition &definition = GetBuildingDefinition(attacker.kind);
        if (attacker.hitPoints <= 0 || !attacker.complete || definition.attackDamage <= 0) {
            continue;
        }
        if (attacker.attackCooldownTicks > 0) {
            --attacker.attackCooldownTicks;
            continue;
        }

        Unit *target = nullptr;
        std::int64_t nearestDistance = std::numeric_limits<std::int64_t>::max();
        for (Unit &candidate : m_state.units) {
            if (!candidate.alive || candidate.faction == attacker.faction || candidate.faction == Faction::Neutral) {
                continue;
            }
            const std::int64_t distance = DistanceSquared(attacker.position, candidate.position);
            if (distance <= static_cast<std::int64_t>(definition.attackRange) * definition.attackRange &&
                (distance < nearestDistance ||
                    (distance == nearestDistance && (!target || candidate.id < target->id)))) {
                nearestDistance = distance;
                target = &candidate;
            }
        }
        if (!target) {
            continue;
        }

        std::int32_t attackDamage = definition.attackDamage;
        const std::size_t overchargeIndex = static_cast<std::size_t>(AbilityKind::EmergencyOvercharge);
        if (attacker.faction == Faction::Freegrid && m_state.abilityDurationTicks[overchargeIndex] > 0) {
            attackDamage = attackDamage * OverchargeDamageNumerator / OverchargeDamageDenominator;
        }
        target->hitPoints = std::max(0, target->hitPoints - attackDamage);
        if (target->hitPoints == 0) {
            target->alive = false;
            target->order = OrderKind::Idle;
            target->targetId = 0;
        }
        attacker.attackCooldownTicks = definition.attackCooldownTicks;
    }
}

void Simulation::UpdateEconomyAndAi()
{
    if (++m_state.incomeRemainderTicks >= TicksPerSecond) {
        m_state.incomeRemainderTicks = 0;
        const std::int32_t ownedNodes = static_cast<std::int32_t>(std::count_if(
            m_state.nodes.begin(), m_state.nodes.end(), [](const ControlNode &node) {
                return node.owner == Faction::Freegrid;
            }));
        m_state.salvage += ownedNodes * 10;
        m_state.abilityCharge = std::min(100, m_state.abilityCharge + (ownedNodes * 3));
    }

    const Building *machineNest = nullptr;
    for (const Building &building : m_state.buildings) {
        if (building.kind == BuildingKind::MachineNest && building.hitPoints > 0) {
            machineNest = &building;
            break;
        }
    }
    if (machineNest && ++m_state.chorusSpawnTicks >= TicksPerSecond * 15) {
        m_state.chorusSpawnTicks = 0;
        ++m_state.chorusWave;
        const UnitKind reinforcement = m_state.chorusWave % 3U == 0U
            ? UnitKind::Harrower
            : (m_state.chorusWave % 3U == 2U ? UnitKind::Warden : UnitKind::Skitter);
        AddUnit(Faction::Chorus, reinforcement, { machineNest->position.x - 900, machineNest->position.y - 500 });
    }

    for (Unit &drone : m_state.units) {
        if (!drone.alive || drone.faction != Faction::Chorus) {
            continue;
        }
        if (drone.kind == UnitKind::Skitter) {
            if (drone.order == OrderKind::Capture) {
                if (const ControlNode *target = FindNode(drone.targetId);
                    target && target->owner != Faction::Chorus) {
                    continue;
                }
            }

            std::int64_t nearestNodeDistance = std::numeric_limits<std::int64_t>::max();
            std::uint32_t nearestNodeId = 0;
            Point nearestNodePosition;
            for (const ControlNode &node : m_state.nodes) {
                if (node.owner == Faction::Chorus) {
                    continue;
                }
                const std::int64_t distance = DistanceSquared(drone.position, node.position);
                if (distance < nearestNodeDistance ||
                    (distance == nearestNodeDistance && node.id < nearestNodeId)) {
                    nearestNodeDistance = distance;
                    nearestNodeId = node.id;
                    nearestNodePosition = node.position;
                }
            }
            if (nearestNodeId != 0) {
                drone.order = OrderKind::Capture;
                drone.targetId = nearestNodeId;
                drone.destination = nearestNodePosition;
                continue;
            }
        }

        bool hasTarget = false;
        if (drone.order == OrderKind::Attack) {
            if (const Unit *target = FindUnit(drone.targetId)) {
                hasTarget = target->alive && target->faction == Faction::Freegrid;
            }
            if (!hasTarget) {
                if (const Building *target = FindBuilding(drone.targetId)) {
                    hasTarget = target->hitPoints > 0 && target->faction == Faction::Freegrid;
                }
            }
        }
        if (!hasTarget) {
            std::int64_t nearestDistance = std::numeric_limits<std::int64_t>::max();
            std::uint32_t nearestId = 0;
            for (const Unit &candidate : m_state.units) {
                if (!candidate.alive || candidate.faction != Faction::Freegrid) {
                    continue;
                }
                const std::int64_t distance = DistanceSquared(drone.position, candidate.position);
                if (distance < nearestDistance || (distance == nearestDistance && candidate.id < nearestId)) {
                    nearestDistance = distance;
                    nearestId = candidate.id;
                }
            }
            if (nearestId == 0) {
                for (const Building &candidate : m_state.buildings) {
                    if (candidate.hitPoints <= 0 || candidate.faction != Faction::Freegrid) {
                        continue;
                    }
                    const std::int64_t distance = DistanceSquared(drone.position, candidate.position);
                    if (distance < nearestDistance || (distance == nearestDistance && candidate.id < nearestId)) {
                        nearestDistance = distance;
                        nearestId = candidate.id;
                    }
                }
            }
            if (nearestId != 0) {
                drone.order = OrderKind::Attack;
                drone.targetId = nearestId;
            }
        }
    }
}

void Simulation::UpdateOutcome()
{
    bool relayCoreAlive = false;
    bool chorusSpireAlive = false;
    for (const Building &building : m_state.buildings) {
        relayCoreAlive |= building.faction == Faction::Freegrid &&
            building.kind == BuildingKind::RelayCore && building.hitPoints > 0;
        chorusSpireAlive |= building.faction == Faction::Chorus &&
            building.kind == BuildingKind::ChorusSpire && building.hitPoints > 0;
    }
    if (!chorusSpireAlive) {
        m_state.outcome = MatchOutcome::Victory;
    } else if (!relayCoreAlive) {
        m_state.outcome = MatchOutcome::Defeat;
    }
}

std::uint64_t Simulation::Checksum() const
{
    std::uint64_t hash = 1469598103934665603ULL;
    HashValue(hash, m_state.tick);
    HashValue(hash, static_cast<std::uint8_t>(m_state.paused));
    HashValue(hash, static_cast<std::uint8_t>(m_state.outcome));
    HashValue(hash, m_state.salvage);
    HashValue(hash, m_state.abilityCharge);
    HashValue(hash, m_state.incomeRemainderTicks);
    HashValue(hash, m_state.chorusSpawnTicks);
    HashValue(hash, m_state.chorusWave);
    for (const std::int32_t cooldown : m_state.abilityCooldownTicks) {
        HashValue(hash, cooldown);
    }
    for (const std::int32_t duration : m_state.abilityDurationTicks) {
        HashValue(hash, duration);
    }
    HashValue(hash, m_state.scanCenter.x);
    HashValue(hash, m_state.scanCenter.y);
    HashValue(hash, m_state.nextEntityId);
    HashValue(hash, static_cast<std::uint64_t>(m_state.units.size()));
    for (const Unit &unit : m_state.units) {
        HashValue(hash, unit.id);
        HashValue(hash, static_cast<std::uint8_t>(unit.faction));
        HashValue(hash, static_cast<std::uint8_t>(unit.kind));
        HashValue(hash, unit.position.x);
        HashValue(hash, unit.position.y);
        HashValue(hash, unit.destination.x);
        HashValue(hash, unit.destination.y);
        HashValue(hash, unit.hitPoints);
        HashValue(hash, unit.maximumHitPoints);
        HashValue(hash, unit.attackCooldownTicks);
        HashValue(hash, unit.abilityCooldownTicks);
        HashValue(hash, unit.repairCooldownTicks);
        HashValue(hash, static_cast<std::uint8_t>(unit.alive));
        HashValue(hash, static_cast<std::uint8_t>(unit.order));
        HashValue(hash, unit.targetId);
    }
    HashValue(hash, static_cast<std::uint64_t>(m_state.buildings.size()));
    for (const Building &building : m_state.buildings) {
        HashValue(hash, building.id);
        HashValue(hash, static_cast<std::uint8_t>(building.faction));
        HashValue(hash, static_cast<std::uint8_t>(building.kind));
        HashValue(hash, building.position.x);
        HashValue(hash, building.position.y);
        HashValue(hash, building.hitPoints);
        HashValue(hash, building.maximumHitPoints);
        HashValue(hash, static_cast<std::uint8_t>(building.complete));
        HashValue(hash, building.remainingBuildTicks);
        HashValue(hash, building.attackCooldownTicks);
    }
    HashValue(hash, static_cast<std::uint64_t>(m_state.nodes.size()));
    for (const ControlNode &node : m_state.nodes) {
        HashValue(hash, node.id);
        HashValue(hash, node.position.x);
        HashValue(hash, node.position.y);
        HashValue(hash, static_cast<std::uint8_t>(node.owner));
        HashValue(hash, node.freegridCaptureTicks);
        HashValue(hash, node.chorusCaptureTicks);
    }
    HashValue(hash, static_cast<std::uint64_t>(m_state.production.size()));
    for (const ProductionOrder &order : m_state.production) {
        HashValue(hash, order.producerId);
        HashValue(hash, static_cast<std::uint8_t>(order.kind));
        HashValue(hash, order.remainingTicks);
    }
    HashValue(hash, static_cast<std::uint64_t>(m_commands.size()));
    for (const Command &command : m_commands) {
        HashValue(hash, command.executeTick);
        HashValue(hash, static_cast<std::uint8_t>(command.kind));
        HashValue(hash, command.actorId);
        HashValue(hash, command.targetId);
        HashValue(hash, command.point.x);
        HashValue(hash, command.point.y);
        HashValue(hash, static_cast<std::uint8_t>(command.unitKind));
        HashValue(hash, static_cast<std::uint8_t>(command.buildingKind));
        HashValue(hash, static_cast<std::uint8_t>(command.abilityKind));
        HashValue(hash, command.sequence);
    }
    HashValue(hash, m_nextCommandSequence);
    return hash;
}

} // namespace Tempest
