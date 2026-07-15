#include "TempestSimulation.h"
#include "TempestInterface.h"

#include <cstdlib>
#include <iostream>
#include <limits>
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

Tempest::Command MakeCommand(
    std::uint64_t executeTick,
    Tempest::CommandKind kind,
    std::uint32_t actorId = 0,
    std::uint32_t targetId = 0,
    Tempest::Point point = {})
{
    Tempest::Command command {};
    command.executeTick = executeTick;
    command.kind = kind;
    command.actorId = actorId;
    command.targetId = targetId;
    command.point = point;
    return command;
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

    simulation.Submit(MakeCommand(0, Tempest::CommandKind::TogglePause));
    simulation.Step();
    Expect(simulation.GetState().paused, "pause command pauses simulation");
    Expect(simulation.GetState().tick == 0, "paused simulation does not advance ticks");
    simulation.Submit(MakeCommand(0, Tempest::CommandKind::TogglePause));
    simulation.Step();
    Expect(!simulation.GetState().paused && simulation.GetState().tick == 1, "unpause resumes fixed ticks");

    simulation.Submit(MakeCommand(1, Tempest::CommandKind::Restart));
    simulation.Step();
    Expect(simulation.GetState().tick == 0 && simulation.GetState().freegridCredits == 500, "restart restores scenario state");
}

void TestEconomyConstructionAndProduction()
{
    Tempest::Simulation simulation;
    const std::uint32_t courierId = FindCourier(simulation.GetState());
    const std::uint32_t nodeId = simulation.GetState().nodes.front().id;
    simulation.Submit(MakeCommand(0, Tempest::CommandKind::Capture, courierId, nodeId));
    simulation.Step(260);

    const Tempest::ControlNode &node = simulation.GetState().nodes.front();
    Expect(node.owner == Tempest::Faction::Freegrid, "Courier captures the first substation");
    Expect(simulation.GetState().freegridCredits > 500, "captured substation produces deterministic income");

    const std::int32_t creditsBeforeRelay = simulation.GetState().freegridCredits;
    simulation.Submit(MakeCommand(simulation.GetState().tick, Tempest::CommandKind::BuildRelay, 0, nodeId));
    simulation.Step();
    Expect(simulation.GetState().freegridCredits == creditsBeforeRelay - 200, "Relay reserves its full credit cost");
    simulation.Step(Tempest::TicksPerSecond * 4);
    const std::uint32_t relayId = FindBuilding(simulation.GetState(), Tempest::BuildingKind::Relay);
    Expect(relayId != 0, "owned substation can construct a Relay");

    const std::size_t unitsBeforeProduction = simulation.GetState().units.size();
    simulation.Submit(MakeCommand(simulation.GetState().tick, Tempest::CommandKind::ProduceCourier, relayId));
    simulation.Step();
    Expect(simulation.GetState().production.empty(), "a grid Relay cannot produce combat units");
    Expect(simulation.GetState().units.size() == unitsBeforeProduction, "rejected Relay production creates no unit");

    const std::uint32_t workshopId = FindBuilding(simulation.GetState(), Tempest::BuildingKind::Workshop);
    simulation.Submit(MakeCommand(simulation.GetState().tick, Tempest::CommandKind::ProduceCourier, workshopId));
    simulation.Step(Tempest::TicksPerSecond * 3 + 1);
    Expect(simulation.GetState().units.size() == unitsBeforeProduction + 1, "completed Workshop produces a Courier");
}

void TestCommandValidationAndChorusTerritoryAi()
{
    Tempest::Simulation validationSimulation;
    const std::uint32_t courierId = FindCourier(validationSimulation.GetState());
    const std::uint32_t workshopId = FindBuilding(validationSimulation.GetState(), Tempest::BuildingKind::Workshop);
    validationSimulation.Submit(MakeCommand(0, Tempest::CommandKind::Attack, courierId, workshopId));
    validationSimulation.Step();
    const Tempest::Unit *courier = nullptr;
    for (const Tempest::Unit &unit : validationSimulation.GetState().units) {
        if (unit.id == courierId) {
            courier = &unit;
            break;
        }
    }
    Expect(courier && courier->order == Tempest::OrderKind::Idle,
        "friendly targets are rejected before creating an unreachable attack order");

    validationSimulation.Submit(MakeCommand(
        validationSimulation.GetState().tick,
        Tempest::CommandKind::Move,
        courierId,
        0,
        { std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min() }));
    validationSimulation.Step();
    Expect(courier->position.x > -12000 && courier->position.y < -9000,
        "extreme move coordinates are widened before deterministic movement arithmetic");

    Tempest::Simulation territorySimulation;
    territorySimulation.Step(Tempest::TicksPerSecond * 10);
    bool chorusCapturedNode = false;
    for (const Tempest::ControlNode &node : territorySimulation.GetState().nodes) {
        chorusCapturedNode |= node.owner == Tempest::Faction::Chorus;
    }
    Expect(chorusCapturedNode, "Chorus AI can contest and capture a control node");
}

void TestArcPulseRange()
{
    Tempest::Simulation simulation;
    const std::uint32_t courierId = FindCourier(simulation.GetState());
    const std::int32_t initialPower = simulation.GetState().freegridPower;
    simulation.Submit(MakeCommand(0, Tempest::CommandKind::ArcPulse, courierId, 0, { 50000, 50000 }));
    simulation.Step();
    Expect(simulation.GetState().freegridPower == initialPower, "Arc Pulse rejects an out-of-range cast point");
}

void TestVictoryAndDefeat()
{
    Tempest::Simulation victorySimulation;
    const std::uint32_t workshopId = FindBuilding(victorySimulation.GetState(), Tempest::BuildingKind::Workshop);
    victorySimulation.Submit(MakeCommand(0, Tempest::CommandKind::ProduceCourier, workshopId));
    victorySimulation.Submit(MakeCommand(0, Tempest::CommandKind::ProduceCourier, workshopId));
    victorySimulation.Submit(MakeCommand(0, Tempest::CommandKind::ProduceCourier, workshopId));
    victorySimulation.Step(Tempest::TicksPerSecond * 10);

    const std::uint32_t chorusCoreId = FindBuilding(victorySimulation.GetState(), Tempest::BuildingKind::ChorusCore);
    for (const std::uint32_t courierId : FindCouriers(victorySimulation.GetState())) {
        victorySimulation.Submit(MakeCommand(
            victorySimulation.GetState().tick,
            Tempest::CommandKind::Attack,
            courierId,
            chorusCoreId));
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
    simulation.Submit(MakeCommand(0, Tempest::CommandKind::Capture, courierId, firstNode));
    simulation.Submit(MakeCommand(260, Tempest::CommandKind::Capture, courierId, secondNode));
    simulation.Submit(MakeCommand(520, Tempest::CommandKind::ArcPulse, courierId, 0, { 1000, 2500 }));

    std::vector<std::uint64_t> checksums;
    for (int tick = 0; tick < 700; ++tick) {
        simulation.Step();
        checksums.push_back(simulation.Checksum());
    }
    return checksums;
}

void TestDeterministicReplay()
{
    constexpr std::uint64_t ExpectedFinalChecksum = 4421283840936625681ULL;
    const std::vector<std::uint64_t> first = RunDeterministicScript();
    const std::vector<std::uint64_t> second = RunDeterministicScript();
    Expect(first == second, "identical command streams produce identical per-tick checksums");
    Expect(!first.empty() && first.back() != 0, "determinism trace contains a final checksum");
    std::cout << "TRACE: deterministic_replay_final_checksum=" << first.back() << '\n';
    Expect(first.back() == ExpectedFinalChecksum, "deterministic replay matches the reviewed golden checksum");
}

void TestInterfaceFlow()
{
    Tempest::Ui::InterfaceState interfaceState;
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Briefing,
        "interface starts on the original Substation 9 briefing");

    const Tempest::Ui::InputEvent begin = interfaceState.HandleKey(Tempest::Ui::KeyEnter);
    Expect(begin.intent == Tempest::Ui::Intent::BeginMatch &&
            interfaceState.GetScreen() == Tempest::Ui::Screen::Playing,
        "confirm starts the match from the briefing");

    interfaceState.HandleKey(interfaceState.KeyFor(Tempest::Ui::Action::Pause));
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Pause,
        "the remappable pause action opens the pause screen");
    interfaceState.HandleKey(interfaceState.KeyFor(Tempest::Ui::Action::OpenSettings));
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Settings &&
            interfaceState.GetSettingsReturnScreen() == Tempest::Ui::Screen::Pause,
        "settings opened during play returns to the safe paused screen");
    interfaceState.HandleKey(Tempest::Ui::KeyEscape);
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Pause,
        "escape returns from settings to pause without advancing simulation");
    interfaceState.HandleKey(interfaceState.KeyFor(Tempest::Ui::Action::Pause));
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Playing,
        "pause action resumes the match");

    interfaceState.SyncOutcome(Tempest::MatchOutcome::Victory);
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Result,
        "a completed simulation opens the in-window result flow");
    const Tempest::Ui::InputEvent restart =
        interfaceState.HandleKey(interfaceState.KeyFor(Tempest::Ui::Action::Restart));
    Expect(restart.intent == Tempest::Ui::Intent::RestartMatch &&
            interfaceState.GetScreen() == Tempest::Ui::Screen::Playing,
        "result flow restarts without returning to the desktop");
}

void TestSettingsBoundsAndRemapping()
{
    Tempest::Ui::InterfaceState interfaceState;
    interfaceState.HandleKey(interfaceState.KeyFor(Tempest::Ui::Action::OpenSettings));
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Settings,
        "briefing exposes essential settings before play");

    for (int index = 0; index < 20; ++index) {
        interfaceState.HandleKey(Tempest::Ui::KeyLeft);
    }
    Expect(interfaceState.GetSettings().cameraSpeedPercent == 50,
        "camera speed has a readable lower bound");
    for (int index = 0; index < 30; ++index) {
        interfaceState.HandleKey(Tempest::Ui::KeyRight);
    }
    Expect(interfaceState.GetSettings().cameraSpeedPercent == 200,
        "camera speed has a stable upper bound");

    interfaceState.HandleKey(Tempest::Ui::KeyDown);
    for (int index = 0; index < 20; ++index) {
        interfaceState.HandleKey(Tempest::Ui::KeyRight);
    }
    Expect(interfaceState.GetSettings().uiScalePercent == 150,
        "UI scale remains within the non-clipping two-column layout range");
    interfaceState.HandleKey(Tempest::Ui::KeyUp);

    for (int row = 0; row < Tempest::Ui::InterfaceState::AdjustableSettingCount; ++row) {
        interfaceState.HandleKey(Tempest::Ui::KeyDown);
    }
    Expect(interfaceState.ActionForSettingsRow(interfaceState.GetSelectedSettingsRow()) ==
            Tempest::Ui::Action::MoveUp,
        "settings navigation reaches the first remappable action");
    interfaceState.HandleKey(Tempest::Ui::KeyEnter);
    Expect(interfaceState.IsCapturingBinding(), "confirm starts an explicit key capture");
    const Tempest::Ui::InputEvent rebound = interfaceState.HandleKey('K');
    Expect(rebound.intent == Tempest::Ui::Intent::BindingChanged &&
            interfaceState.KeyFor(Tempest::Ui::Action::MoveUp) == 'K',
        "a free key can replace a gameplay binding");

    interfaceState.HandleKey(Tempest::Ui::KeyDown);
    interfaceState.HandleKey(Tempest::Ui::KeyEnter);
    const Tempest::Ui::InputEvent collision = interfaceState.HandleKey('K');
    Expect(collision.intent == Tempest::Ui::Intent::BindingRejected &&
            interfaceState.KeyFor(Tempest::Ui::Action::MoveDown) == 'S',
        "duplicate bindings are rejected without losing the prior key");

    bool keyStates[256] = {};
    keyStates['K'] = true;
    Expect(interfaceState.IsActionPressed(Tempest::Ui::Action::MoveUp, keyStates, 256),
        "the runtime input query consumes the remapped key");
}

} // namespace

int main()
{
    TestInitialStateAndPause();
    TestEconomyConstructionAndProduction();
    TestArcPulseRange();
    TestCommandValidationAndChorusTerritoryAi();
    TestVictoryAndDefeat();
    TestDeterministicReplay();
    TestInterfaceFlow();
    TestSettingsBoundsAndRemapping();
    std::cout << "PASS: Project Tempest deterministic simulation tests\n";
    return 0;
}
