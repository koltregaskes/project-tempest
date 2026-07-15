#include "TempestSimulation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>

namespace Tempest {
namespace {

constexpr std::int32_t MoveDistancePerTick = 120;
constexpr std::int32_t CaptureRange = 700;
constexpr std::int32_t CaptureTicksRequired = TicksPerSecond * 4;
constexpr std::int32_t AttackRange = 2200;
constexpr std::int32_t ArcPulseRange = 3200;
constexpr std::int32_t ArcPulseAbilityChargeCost = 25;
constexpr std::int32_t ArcPulseCooldownTicks = TicksPerSecond * 12;

constexpr std::array<UnitDefinition, static_cast<std::size_t>(UnitKind::Count)> UnitDefinitions {{
    { "Fabricator rig", Faction::Freegrid, 240, 250, TicksPerSecond * 5, 2, 8, 12 },
    { "Courier scout", Faction::Freegrid, 180, 150, TicksPerSecond * 3, 1, 20, 9 },
    { "Lancer crew", Faction::Freegrid, 260, 240, TicksPerSecond * 6, 2, 28, 10 },
    { "Coil carrier", Faction::Freegrid, 520, 450, TicksPerSecond * 10, 4, 55, 18 },
    { "Skitter", Faction::Chorus, 110, 0, 0, 0, 11, 12 },
    { "Warden", Faction::Chorus, 280, 0, 0, 0, 24, 14 },
    { "Harrower", Faction::Chorus, 650, 0, 0, 0, 48, 20 },
}};

constexpr std::array<BuildingDefinition, static_cast<std::size_t>(BuildingKind::Count)> BuildingDefinitions {{
    { "Relay Core", Faction::Freegrid, 900, 0, 0, 4 },
    { "Fabricator Bay", Faction::Freegrid, 650, 300, TicksPerSecond * 6, 2 },
    { "Dynamo", Faction::Freegrid, 260, 200, TicksPerSecond * 4, 4 },
    { "Arc Sentry", Faction::Freegrid, 400, 250, TicksPerSecond * 5, 0 },
    { "Machine Nest", Faction::Chorus, 500, 0, 0, 0 },
    { "Signal Pylon", Faction::Chorus, 340, 0, 0, 0 },
    { "Chorus Spire", Faction::Chorus, 800, 0, 0, 0 },
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

void MoveTowards(Point &position, Point destination)
{
    const std::int64_t dx = static_cast<std::int64_t>(destination.x) - position.x;
    const std::int64_t dy = static_cast<std::int64_t>(destination.y) - position.y;
    const std::int64_t distance = std::abs(dx) + std::abs(dy);
    if (distance <= MoveDistancePerTick) {
        position = destination;
        return;
    }
    position.x += static_cast<std::int32_t>(
        (static_cast<std::int64_t>(MoveDistancePerTick) * dx) / distance);
    position.y += static_cast<std::int32_t>(
        (static_cast<std::int64_t>(MoveDistancePerTick) * dy) / distance);
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
    UpdateCombat();
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
    const Building *producer = FindBuilding(producerId);
    const UnitDefinition &unit = GetUnitDefinition(kind);
    return producer && producer->faction == Faction::Freegrid && producer->complete && producer->hitPoints > 0 &&
        producer->kind == BuildingKind::FabricatorBay && unit.faction == Faction::Freegrid &&
        m_state.salvage >= unit.salvageCost && UsedFreegridCapacity() + unit.capacityCost <= FreegridCapacity();
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
    if (command.kind == CommandKind::BuildRelay) {
        const BuildingDefinition &dynamo = GetBuildingDefinition(BuildingKind::Dynamo);
        ControlNode *node = FindNode(command.targetId);
        if (!node || node->owner != Faction::Freegrid || m_state.salvage < dynamo.salvageCost) {
            return;
        }
        const bool alreadyBuilt = std::any_of(
            m_state.buildings.begin(), m_state.buildings.end(), [node](const Building &building) {
                return building.kind == BuildingKind::Dynamo && building.hitPoints > 0 &&
                    IsWithin(building.position, node->position, 100);
            });
        if (!alreadyBuilt) {
            m_state.salvage -= dynamo.salvageCost;
            AddBuilding(Faction::Freegrid, BuildingKind::Dynamo, node->position, false);
        }
        return;
    }

    if (command.kind == CommandKind::ProduceCourier) {
        const UnitDefinition &courier = GetUnitDefinition(UnitKind::CourierScout);
        if (!CanProduceUnit(command.actorId, UnitKind::CourierScout)) {
            return;
        }
        m_state.salvage -= courier.salvageCost;
        m_state.production.push_back({ command.actorId, UnitKind::CourierScout, courier.buildTicks });
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

        Point destination = unit.destination;
        if (unit.order == OrderKind::Attack) {
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

        const bool shouldMove = unit.order == OrderKind::Move || unit.order == OrderKind::Capture ||
            (unit.order == OrderKind::Attack && !IsWithin(unit.position, destination, AttackRange));
        if (shouldMove) {
            MoveTowards(unit.position, destination);
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
        if (!targetHitPoints || !IsWithin(attacker.position, targetPosition, AttackRange)) {
            continue;
        }

        const UnitDefinition &definition = GetUnitDefinition(attacker.kind);
        *targetHitPoints -= definition.attackDamage;
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
        AddUnit(Faction::Chorus, UnitKind::Skitter, { machineNest->position.x - 900, machineNest->position.y - 500 });
    }

    for (Unit &drone : m_state.units) {
        if (!drone.alive || drone.faction != Faction::Chorus) {
            continue;
        }
        if ((drone.id % 3U) == 1U) {
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
        HashValue(hash, command.sequence);
    }
    HashValue(hash, m_nextCommandSequence);
    return hash;
}

} // namespace Tempest
