#include "TempestInterface.h"
#include "TempestSimulation.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t FnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;

struct ScenarioResult {
    std::string name;
    Tempest::MatchOutcome outcome = Tempest::MatchOutcome::InProgress;
    std::uint64_t ticks = 0;
    std::uint64_t finalChecksum = 0;
    std::uint64_t traceChecksum = FnvOffsetBasis;
    std::uint32_t chorusWave = 0;
    std::int32_t salvage = 0;
    std::int32_t abilityCharge = 0;
    std::size_t livingFreegridUnits = 0;
    std::size_t livingChorusUnits = 0;
    bool resultFlow = false;
    bool restartFlow = false;
};

[[noreturn]] void Fail(const std::string &message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        Fail(message);
    }
}

Tempest::Command MakeCommand(
    std::uint64_t executeTick,
    Tempest::CommandKind kind,
    std::uint32_t actorId = 0,
    std::uint32_t targetId = 0,
    Tempest::Point point = {},
    Tempest::UnitKind unitKind = Tempest::UnitKind::CourierScout)
{
    Tempest::Command command {};
    command.executeTick = executeTick;
    command.kind = kind;
    command.actorId = actorId;
    command.targetId = targetId;
    command.point = point;
    command.unitKind = unitKind;
    return command;
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

std::vector<std::uint32_t> FindLivingFreegridUnits(const Tempest::MatchState &state)
{
    std::vector<std::uint32_t> ids;
    for (const Tempest::Unit &unit : state.units) {
        if (unit.faction == Tempest::Faction::Freegrid && unit.alive) {
            ids.push_back(unit.id);
        }
    }
    return ids;
}

std::size_t CountLivingUnits(const Tempest::MatchState &state, Tempest::Faction faction)
{
    std::size_t count = 0;
    for (const Tempest::Unit &unit : state.units) {
        if (unit.faction == faction && unit.alive) {
            ++count;
        }
    }
    return count;
}

void HashChecksum(std::uint64_t &trace, std::uint64_t checksum)
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        trace ^= (checksum >> shift) & 0xFFULL;
        trace *= FnvPrime;
    }
}

void StepAndTrace(Tempest::Simulation &simulation, ScenarioResult &result)
{
    simulation.Step();
    HashChecksum(result.traceChecksum, simulation.Checksum());
}

void BeginFreshLaunch(Tempest::Simulation &simulation, Tempest::Ui::InterfaceState &interfaceState)
{
    Expect(simulation.GetState().tick == 0 &&
            simulation.GetState().outcome == Tempest::MatchOutcome::InProgress,
        "a headless acceptance launch starts from pristine Substation 9 state");
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Briefing,
        "a headless acceptance launch starts on the briefing");
    const Tempest::Ui::InputEvent begin = interfaceState.HandleKey(Tempest::Ui::KeyEnter);
    Expect(begin.intent == Tempest::Ui::Intent::BeginMatch &&
            interfaceState.GetScreen() == Tempest::Ui::Screen::Playing,
        "the briefing begins the headless match through the real interface state machine");
}

void FinishResultFlow(
    Tempest::Simulation &simulation,
    Tempest::Ui::InterfaceState &interfaceState,
    ScenarioResult &result)
{
    interfaceState.SyncOutcome(simulation.GetState().outcome);
    result.resultFlow = interfaceState.GetScreen() == Tempest::Ui::Screen::Result;
    Expect(result.resultFlow, "a terminal simulation outcome opens the result screen");

    const Tempest::Ui::InputEvent restart =
        interfaceState.HandleKey(interfaceState.BindingFor(Tempest::Ui::Action::Restart).code);
    Expect(restart.intent == Tempest::Ui::Intent::RestartMatch &&
            interfaceState.GetScreen() == Tempest::Ui::Screen::Playing,
        "the result screen emits an in-process restart intent");
    simulation.Reset();
    result.restartFlow = simulation.GetState().tick == 0 &&
        simulation.GetState().outcome == Tempest::MatchOutcome::InProgress;
    Expect(result.restartFlow, "the result restart restores pristine simulation state");
}

ScenarioResult RunFreegridVictory(const std::string &name)
{
    Tempest::Simulation simulation;
    Tempest::Ui::InterfaceState interfaceState;
    ScenarioResult result;
    result.name = name;
    BeginFreshLaunch(simulation, interfaceState);

    const std::uint32_t fabricatorBay =
        FindBuilding(simulation.GetState(), Tempest::BuildingKind::FabricatorBay);
    const std::uint32_t chorusSpire =
        FindBuilding(simulation.GetState(), Tempest::BuildingKind::ChorusSpire);
    Expect(fabricatorBay != 0 && chorusSpire != 0,
        "the victory scenario locates both faction anchors");

    for (int index = 0; index < 3; ++index) {
        simulation.Submit(MakeCommand(
            simulation.GetState().tick,
            Tempest::CommandKind::ProduceUnit,
            fabricatorBay,
            0,
            {},
            Tempest::UnitKind::CourierScout));
    }

    for (int tick = 0; tick < Tempest::TicksPerSecond * 10; ++tick) {
        StepAndTrace(simulation, result);
    }

    const std::vector<std::uint32_t> strikeForce = FindLivingFreegridUnits(simulation.GetState());
    Expect(strikeForce.size() >= 4,
        "the victory scenario completes enough production for a combined strike force");
    for (const std::uint32_t unitId : strikeForce) {
        simulation.Submit(MakeCommand(
            simulation.GetState().tick,
            Tempest::CommandKind::Attack,
            unitId,
            chorusSpire));
    }

    constexpr int MaximumMatchTicks = Tempest::TicksPerSecond * 180;
    for (int tick = 0;
         tick < MaximumMatchTicks && simulation.GetState().outcome == Tempest::MatchOutcome::InProgress;
         ++tick) {
        StepAndTrace(simulation, result);
    }

    Expect(simulation.GetState().outcome == Tempest::MatchOutcome::Victory,
        "the scripted Freegrid full-match path reaches victory before its safety bound");
    result.outcome = simulation.GetState().outcome;
    result.ticks = simulation.GetState().tick;
    result.finalChecksum = simulation.Checksum();
    result.chorusWave = simulation.GetState().chorusWave;
    result.salvage = simulation.GetState().salvage;
    result.abilityCharge = simulation.GetState().abilityCharge;
    result.livingFreegridUnits = CountLivingUnits(simulation.GetState(), Tempest::Faction::Freegrid);
    result.livingChorusUnits = CountLivingUnits(simulation.GetState(), Tempest::Faction::Chorus);
    FinishResultFlow(simulation, interfaceState, result);
    return result;
}

ScenarioResult RunChorusDefeat(const std::string &name)
{
    Tempest::Simulation simulation;
    Tempest::Ui::InterfaceState interfaceState;
    ScenarioResult result;
    result.name = name;
    BeginFreshLaunch(simulation, interfaceState);

    constexpr int MaximumMatchTicks = Tempest::TicksPerSecond * 300;
    for (int tick = 0;
         tick < MaximumMatchTicks && simulation.GetState().outcome == Tempest::MatchOutcome::InProgress;
         ++tick) {
        StepAndTrace(simulation, result);
    }

    Expect(simulation.GetState().outcome == Tempest::MatchOutcome::Defeat,
        "the unassisted Chorus AI reaches defeat for Freegrid before its safety bound");
    result.outcome = simulation.GetState().outcome;
    result.ticks = simulation.GetState().tick;
    result.finalChecksum = simulation.Checksum();
    result.chorusWave = simulation.GetState().chorusWave;
    result.salvage = simulation.GetState().salvage;
    result.abilityCharge = simulation.GetState().abilityCharge;
    result.livingFreegridUnits = CountLivingUnits(simulation.GetState(), Tempest::Faction::Freegrid);
    result.livingChorusUnits = CountLivingUnits(simulation.GetState(), Tempest::Faction::Chorus);
    FinishResultFlow(simulation, interfaceState, result);
    return result;
}

const char *OutcomeName(Tempest::MatchOutcome outcome)
{
    switch (outcome) {
        case Tempest::MatchOutcome::Victory:
            return "victory";
        case Tempest::MatchOutcome::Defeat:
            return "defeat";
        case Tempest::MatchOutcome::InProgress:
            return "in_progress";
    }
    return "unknown";
}

std::string JsonFor(const std::vector<ScenarioResult> &results)
{
    std::ostringstream json;
    json << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"mode\": \"headless_deterministic_acceptance\",\n"
         << "  \"manual_playthrough_claimed\": false,\n"
         << "  \"fresh_launches\": " << results.size() << ",\n"
         << "  \"scenarios\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const ScenarioResult &result = results[index];
        json << "    {\n"
             << "      \"name\": \"" << result.name << "\",\n"
             << "      \"outcome\": \"" << OutcomeName(result.outcome) << "\",\n"
             << "      \"ticks\": " << result.ticks << ",\n"
             << "      \"final_checksum\": \"" << result.finalChecksum << "\",\n"
             << "      \"trace_checksum\": \"" << result.traceChecksum << "\",\n"
             << "      \"chorus_wave\": " << result.chorusWave << ",\n"
             << "      \"salvage\": " << result.salvage << ",\n"
             << "      \"ability_charge\": " << result.abilityCharge << ",\n"
             << "      \"living_freegrid_units\": " << result.livingFreegridUnits << ",\n"
             << "      \"living_chorus_units\": " << result.livingChorusUnits << ",\n"
             << "      \"result_flow\": " << (result.resultFlow ? "true" : "false") << ",\n"
             << "      \"restart_flow\": " << (result.restartFlow ? "true" : "false") << "\n"
             << "    }" << (index + 1 == results.size() ? "\n" : ",\n");
    }
    json << "  ]\n"
         << "}\n";
    return json.str();
}

} // namespace

int main(int argc, char **argv)
{
    std::string outputPath;
    if (argc == 3 && std::string(argv[1]) == "--output") {
        outputPath = argv[2];
    } else if (argc != 1) {
        Fail("usage: project_tempest_headless_acceptance [--output <path>]");
    }

    const ScenarioResult firstVictory = RunFreegridVictory("freegrid_victory_a");
    const ScenarioResult chorusDefeat = RunChorusDefeat("chorus_defeat");
    const ScenarioResult secondVictory = RunFreegridVictory("freegrid_victory_b");
    Expect(firstVictory.finalChecksum == secondVictory.finalChecksum &&
            firstVictory.traceChecksum == secondVictory.traceChecksum &&
            firstVictory.ticks == secondVictory.ticks,
        "repeated fresh-launch victory scenarios are deterministic");

    const std::string json = JsonFor({ firstVictory, chorusDefeat, secondVictory });
    if (!outputPath.empty()) {
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        Expect(static_cast<bool>(output), "the requested acceptance report can be opened");
        output.write(json.data(), static_cast<std::streamsize>(json.size()));
        Expect(static_cast<bool>(output), "the requested acceptance report is written completely");
    }
    std::cout << json;
    std::cout << "PASS: Project Tempest headless full-match acceptance\n";
    return 0;
}
