#include "TempestSimulation.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::uint32_t FindCourier(const Tempest::MatchState &state)
{
    for (const Tempest::Unit &unit : state.units) {
        if (unit.faction == Tempest::Faction::Freegrid && unit.kind == Tempest::UnitKind::Courier && unit.alive) {
            return unit.id;
        }
    }
    return 0;
}

std::uint32_t FindBuilding(const Tempest::MatchState &state, Tempest::BuildingKind kind)
{
    for (const Tempest::Building &building : state.buildings) {
        if (building.kind == kind && building.hitPoints > 0) {
            return building.id;
        }
    }
    return 0;
}

std::vector<std::uint32_t> FindCouriers(const Tempest::MatchState &state)
{
    std::vector<std::uint32_t> ids;
    for (const Tempest::Unit &unit : state.units) {
        if (unit.faction == Tempest::Faction::Freegrid && unit.kind == Tempest::UnitKind::Courier && unit.alive) {
            ids.push_back(unit.id);
        }
    }
    return ids;
}

void TestInitialStateAndPause()
{
    Tempest::Simulation simulation;
    const Tempest::MatchState &initial = simulation.GetState();
    Expect(initial.tick == 0, "Substation 9 starts at tick zero");
    Expect(initial.freegridCredits == 500, "Freegrid starts with 500 credits");
    Expect(initial.freegridPower == 50, "Freegrid starts with 50 power");
    Expect(initial.nodes.size() == 3, "Substation 9 has three control nodes");
    Expect(FindCourier(initial) != 0, "Freegrid starts with a Courier");
    Expect(FindBuilding(initial, Tempest::BuildingKind::Workshop) != 0, "Freegrid starts with a Workshop");
    Expect(FindBuilding(initial, Tempest::BuildingKind::ChorusCore) != 0, "Chorus starts with a Core");

    simulation.Submit({ 0, Tempest::CommandKind::TogglePause });
    simulation.Step();
    Expect(simulation.GetState().paused, "pause command pauses simulation");
    Expect(simulation.GetState().tick == 0, "paused simulation does not advance ticks");
    simulation.Submit({ 0, Tempest::CommandKind::TogglePause });
    simulation.Step();
    Expect(!simulation.GetState().paused && simulation.GetState().tick == 1, "unpause resumes fixed ticks");

    simulation.Submit({ 1, Tempest::CommandKind::Restart });
    simulation.Step();
    Expect(simulation.GetState().tick == 0 && simulation.GetState().freegridCredits == 500, "restart restores scenario state");
}

void TestEconomyConstructionAndProduction()
{
    Tempest::Simulation simulation;
    const std::uint32_t courierId = FindCourier(simulation.GetState());
    const std::uint32_t nodeId = simulation.GetState().nodes.front().id;
    simulation.Submit({ 0, Tempest::CommandKind::Capture, courierId, nodeId });
    simulation.Step(260);

    const Tempest::ControlNode &node = simulation.GetState().nodes.front();
    Expect(node.owner == Tempest::Faction::Freegrid, "Courier captures the first substation");
    Expect(simulation.GetState().freegridCredits > 500, "captured substation produces deterministic income");

    const std::int32_t creditsBeforeRelay = simulation.GetState().freegridCredits;
    simulation.Submit({ simulation.GetState().tick, Tempest::CommandKind::BuildRelay, 0, nodeId });
    simulation.Step();
    Expect(simulation.GetState().freegridCredits == creditsBeforeRelay - 200, "Relay reserves its full credit cost");
    simulation.Step(Tempest::TicksPerSecond * 4);
    const std::uint32_t relayId = FindBuilding(simulation.GetState(), Tempest::BuildingKind::Relay);
    Expect(relayId != 0, "owned substation can construct a Relay");

    const std::size_t unitsBeforeProduction = simulation.GetState().units.size();
    simulation.Submit({ simulation.GetState().tick, Tempest::CommandKind::ProduceCourier, relayId });
    simulation.Step(Tempest::TicksPerSecond * 3 + 1);
    Expect(simulation.GetState().units.size() == unitsBeforeProduction + 1, "completed Relay produces a Courier");
}

void TestArcPulseRange()
{
    Tempest::Simulation simulation;
    const std::uint32_t courierId = FindCourier(simulation.GetState());
    const std::int32_t initialPower = simulation.GetState().freegridPower;
    simulation.Submit({ 0, Tempest::CommandKind::ArcPulse, courierId, 0, { 50000, 50000 } });
    simulation.Step();
    Expect(simulation.GetState().freegridPower == initialPower, "Arc Pulse rejects an out-of-range cast point");
}

void TestVictoryAndDefeat()
{
    Tempest::Simulation victorySimulation;
    const std::uint32_t workshopId = FindBuilding(victorySimulation.GetState(), Tempest::BuildingKind::Workshop);
    victorySimulation.Submit({ 0, Tempest::CommandKind::ProduceCourier, workshopId });
    victorySimulation.Submit({ 0, Tempest::CommandKind::ProduceCourier, workshopId });
    victorySimulation.Submit({ 0, Tempest::CommandKind::ProduceCourier, workshopId });
    victorySimulation.Step(Tempest::TicksPerSecond * 10);

    const std::uint32_t chorusCoreId = FindBuilding(victorySimulation.GetState(), Tempest::BuildingKind::ChorusCore);
    for (const std::uint32_t courierId : FindCouriers(victorySimulation.GetState())) {
        victorySimulation.Submit({ victorySimulation.GetState().tick, Tempest::CommandKind::Attack, courierId, chorusCoreId });
    }
    victorySimulation.Step(Tempest::TicksPerSecond * 120);
    if (victorySimulation.GetState().outcome != Tempest::MatchOutcome::Victory) {
        std::int32_t coreHitPoints = -1;
        for (const Tempest::Building &building : victorySimulation.GetState().buildings) {
            if (building.id == chorusCoreId) {
                coreHitPoints = building.hitPoints;
                break;
            }
        }
        std::cerr << "Victory diagnostic: tick=" << victorySimulation.GetState().tick
                  << " core_hp=" << coreHitPoints
                  << " living_couriers=" << FindCouriers(victorySimulation.GetState()).size() << '\n';
    }
    Expect(victorySimulation.GetState().outcome == Tempest::MatchOutcome::Victory,
        "destroying the Chorus Core produces victory");

    Tempest::Simulation defeatSimulation;
    defeatSimulation.Step(Tempest::TicksPerSecond * 300);
    Expect(defeatSimulation.GetState().outcome == Tempest::MatchOutcome::Defeat,
        "losing the Workshop produces defeat");
}

std::vector<std::uint64_t> RunDeterministicScript()
{
    Tempest::Simulation simulation;
    const std::uint32_t courierId = FindCourier(simulation.GetState());
    const std::uint32_t firstNode = simulation.GetState().nodes[0].id;
    const std::uint32_t secondNode = simulation.GetState().nodes[1].id;
    simulation.Submit({ 0, Tempest::CommandKind::Capture, courierId, firstNode });
    simulation.Submit({ 260, Tempest::CommandKind::Capture, courierId, secondNode });
    simulation.Submit({ 520, Tempest::CommandKind::ArcPulse, courierId, 0, { 1000, 2500 } });

    std::vector<std::uint64_t> checksums;
    for (int tick = 0; tick < 700; ++tick) {
        simulation.Step();
        checksums.push_back(simulation.Checksum());
    }
    return checksums;
}

void TestDeterministicReplay()
{
    constexpr std::uint64_t ExpectedFinalChecksum = 10627918652945665272ULL;
    const std::vector<std::uint64_t> first = RunDeterministicScript();
    const std::vector<std::uint64_t> second = RunDeterministicScript();
    Expect(first == second, "identical command streams produce identical per-tick checksums");
    Expect(!first.empty() && first.back() != 0, "determinism trace contains a final checksum");
    Expect(first.back() == ExpectedFinalChecksum, "deterministic replay matches the reviewed golden checksum");
    std::cout << "TRACE: deterministic_replay_final_checksum=" << first.back() << '\n';
}

} // namespace

int main()
{
    TestInitialStateAndPause();
    TestEconomyConstructionAndProduction();
    TestArcPulseRange();
    TestVictoryAndDefeat();
    TestDeterministicReplay();
    std::cout << "PASS: Project Tempest deterministic simulation tests\n";
    return 0;
}
