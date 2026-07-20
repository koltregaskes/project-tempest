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
constexpr std::uint64_t ExpectedVictoryFinalChecksum = 12952808647802402891ULL;
constexpr std::uint64_t ExpectedVictoryTraceChecksum = 5821017828228671614ULL;
constexpr std::uint64_t ExpectedDefeatFinalChecksum = 16762171224111744054ULL;
constexpr std::uint64_t ExpectedDefeatTraceChecksum = 12235278080197510931ULL;

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
    bool territoryCapture = false;
    bool construction = false;
    bool production = false;
    bool factionAbilities = false;
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
    Tempest::UnitKind unitKind = Tempest::UnitKind::CourierScout,
    Tempest::BuildingKind buildingKind = Tempest::BuildingKind::Dynamo,
    Tempest::AbilityKind abilityKind = Tempest::AbilityKind::GridLinkScan)
{
    Tempest::Command command {};
    command.executeTick = executeTick;
    command.kind = kind;
    command.actorId = actorId;
    command.targetId = targetId;
    command.point = point;
    command.unitKind = unitKind;
    command.buildingKind = buildingKind;
    command.abilityKind = abilityKind;
    return command;
}

std::uint32_t FindUnit(const Tempest::MatchState &state, Tempest::UnitKind kind)
{
    for (const Tempest::Unit &unit : state.units) {
        if (unit.kind == kind && unit.alive) {
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

std::size_t OrderFreegridAssault(Tempest::Simulation &simulation, std::uint32_t targetId)
{
    const std::vector<std::uint32_t> units = FindLivingFreegridUnits(simulation.GetState());
    for (const std::uint32_t unitId : units) {
        simulation.Submit(MakeCommand(
            simulation.GetState().tick,
            Tempest::CommandKind::Attack,
            unitId,
            targetId));
    }
    return units.size();
}

std::size_t CountLivingUnits(const Tempest::MatchState &state, Tempest::UnitKind kind)
{
    std::size_t count = 0;
    for (const Tempest::Unit &unit : state.units) {
        if (unit.kind == kind && unit.alive) {
            ++count;
        }
    }
    return count;
}

bool NodeIsOwnedBy(const Tempest::MatchState &state, std::uint32_t nodeId, Tempest::Faction faction)
{
    for (const Tempest::ControlNode &node : state.nodes) {
        if (node.id == nodeId) {
            return node.owner == faction;
        }
    }
    return false;
}

bool HasCompleteBuilding(const Tempest::MatchState &state, Tempest::BuildingKind kind)
{
    for (const Tempest::Building &building : state.buildings) {
        if (building.kind == kind && building.hitPoints > 0 && building.complete) {
            return true;
        }
    }
    return false;
}

std::int32_t BuildingHitPoints(const Tempest::MatchState &state, Tempest::BuildingKind kind)
{
    for (const Tempest::Building &building : state.buildings) {
        if (building.kind == kind) {
            return building.hitPoints;
        }
    }
    return -1;
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

template <typename Predicate>
void StepUntil(
    Tempest::Simulation &simulation,
    ScenarioResult &result,
    int maximumTicks,
    Predicate predicate,
    const std::string &failureMessage)
{
    for (int tick = 0; tick < maximumTicks && !predicate(); ++tick) {
        Expect(simulation.GetState().outcome == Tempest::MatchOutcome::InProgress,
            failureMessage + " before the match reached a terminal state");
        StepAndTrace(simulation, result);
    }
    Expect(predicate(), failureMessage);
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
    const std::uint32_t fabricatorRig = FindUnit(simulation.GetState(), Tempest::UnitKind::FabricatorRig);
    const std::uint32_t courier = FindUnit(simulation.GetState(), Tempest::UnitKind::CourierScout);
    const std::uint32_t firstNode = simulation.GetState().nodes.front().id;
    Expect(fabricatorBay != 0 && chorusSpire != 0 && fabricatorRig != 0 && courier != 0,
        "the victory scenario locates both faction anchors and starting Freegrid units");

    simulation.Submit(MakeCommand(
        simulation.GetState().tick,
        Tempest::CommandKind::Capture,
        courier,
        firstNode));
    StepUntil(
        simulation,
        result,
        Tempest::TicksPerSecond * 20,
        [&simulation, firstNode]() {
            return NodeIsOwnedBy(simulation.GetState(), firstNode, Tempest::Faction::Freegrid);
        },
        "the scripted player captures its first salvage substation");
    result.territoryCapture = true;

    Expect(simulation.CanBuildStructure(fabricatorRig, firstNode, Tempest::BuildingKind::ArcSentry) &&
            simulation.CanProduceUnit(fabricatorBay, Tempest::UnitKind::LancerCrew),
        "captured territory enables the planned defensive construction and production orders");
    simulation.Submit(MakeCommand(
        simulation.GetState().tick,
        Tempest::CommandKind::BuildStructure,
        fabricatorRig,
        firstNode,
        {},
        Tempest::UnitKind::CourierScout,
        Tempest::BuildingKind::ArcSentry));
    simulation.Submit(MakeCommand(
        simulation.GetState().tick,
        Tempest::CommandKind::ProduceUnit,
        fabricatorBay,
        0,
        {},
        Tempest::UnitKind::LancerCrew));

    StepUntil(
        simulation,
        result,
        Tempest::TicksPerSecond * 10,
        [&simulation]() {
            return simulation.GetState().abilityCharge >=
                Tempest::GetAbilityDefinition(Tempest::AbilityKind::GridLinkScan).abilityChargeCost +
                Tempest::GetAbilityDefinition(Tempest::AbilityKind::EmergencyOvercharge).abilityChargeCost;
        },
        "territory income charges both governed faction abilities");
    simulation.Submit(MakeCommand(
        simulation.GetState().tick,
        Tempest::CommandKind::ActivateAbility,
        0,
        0,
        { 14500, 10500 },
        Tempest::UnitKind::CourierScout,
        Tempest::BuildingKind::Dynamo,
        Tempest::AbilityKind::GridLinkScan));
    simulation.Submit(MakeCommand(
        simulation.GetState().tick,
        Tempest::CommandKind::ActivateAbility,
        0,
        0,
        {},
        Tempest::UnitKind::CourierScout,
        Tempest::BuildingKind::Dynamo,
        Tempest::AbilityKind::EmergencyOvercharge));
    StepAndTrace(simulation, result);
    Expect(simulation.GetState().abilityDurationTicks[static_cast<std::size_t>(Tempest::AbilityKind::GridLinkScan)] > 0 &&
            simulation.GetState().abilityDurationTicks[static_cast<std::size_t>(Tempest::AbilityKind::EmergencyOvercharge)] > 0,
        "the scripted player activates scan and emergency overcharge before the assault");
    result.factionAbilities = true;
    Expect(OrderFreegridAssault(simulation, chorusSpire) >= 2,
        "the scripted player launches the opening assault while reinforcements are produced");

    StepUntil(
        simulation,
        result,
        Tempest::TicksPerSecond * 12,
        [&simulation]() {
            return HasCompleteBuilding(simulation.GetState(), Tempest::BuildingKind::ArcSentry) &&
                CountLivingUnits(simulation.GetState(), Tempest::UnitKind::LancerCrew) == 1;
        },
        "the scripted player completes an Arc Sentry and Lancer production cycle");
    result.construction = true;
    result.production = true;
    Expect(OrderFreegridAssault(simulation, chorusSpire) >= 3,
        "the completed Lancer joins the active assault");

    StepUntil(
        simulation,
        result,
        Tempest::TicksPerSecond * 20,
        [&simulation, fabricatorBay]() {
            return simulation.CanProduceUnit(fabricatorBay, Tempest::UnitKind::CourierScout);
        },
        "the captured node generates enough salvage for a second Courier");
    simulation.Submit(MakeCommand(
        simulation.GetState().tick,
        Tempest::CommandKind::ProduceUnit,
        fabricatorBay,
        0,
        {},
        Tempest::UnitKind::CourierScout));
    StepUntil(
        simulation,
        result,
        Tempest::TicksPerSecond * 6,
        [&simulation]() {
            return CountLivingUnits(simulation.GetState(), Tempest::UnitKind::CourierScout) >= 2;
        },
        "the scripted player completes its second Courier production cycle");
    Expect(OrderFreegridAssault(simulation, chorusSpire) >= 4,
        "the second Courier joins the combined original-role strike force");

    constexpr int MaximumMatchTicks = Tempest::TicksPerSecond * 180;
    for (int tick = 0;
         tick < MaximumMatchTicks && simulation.GetState().outcome == Tempest::MatchOutcome::InProgress;
         ++tick) {
        StepAndTrace(simulation, result);
    }

    if (simulation.GetState().outcome != Tempest::MatchOutcome::Victory) {
        std::cerr << "Victory diagnostic: tick=" << simulation.GetState().tick
                  << " outcome=" << static_cast<int>(simulation.GetState().outcome)
                  << " relay_hp=" << BuildingHitPoints(simulation.GetState(), Tempest::BuildingKind::RelayCore)
                  << " spire_hp=" << BuildingHitPoints(simulation.GetState(), Tempest::BuildingKind::ChorusSpire)
                  << " freegrid_units=" << CountLivingUnits(simulation.GetState(), Tempest::Faction::Freegrid)
                  << " chorus_units=" << CountLivingUnits(simulation.GetState(), Tempest::Faction::Chorus)
                  << " chorus_wave=" << simulation.GetState().chorusWave << '\n';
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
             << "      \"territory_capture\": " << (result.territoryCapture ? "true" : "false") << ",\n"
             << "      \"construction\": " << (result.construction ? "true" : "false") << ",\n"
             << "      \"production\": " << (result.production ? "true" : "false") << ",\n"
             << "      \"faction_abilities\": " << (result.factionAbilities ? "true" : "false") << ",\n"
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
    std::cout << "TRACE: victory_ticks=" << firstVictory.ticks
              << " victory_final_checksum=" << firstVictory.finalChecksum
              << " victory_trace_checksum=" << firstVictory.traceChecksum
              << " defeat_ticks=" << chorusDefeat.ticks
              << " defeat_final_checksum=" << chorusDefeat.finalChecksum
              << " defeat_trace_checksum=" << chorusDefeat.traceChecksum << '\n';
    Expect(firstVictory.finalChecksum == ExpectedVictoryFinalChecksum &&
            firstVictory.traceChecksum == ExpectedVictoryTraceChecksum,
        "the Freegrid full-match path matches its reviewed golden checksums");
    Expect(chorusDefeat.finalChecksum == ExpectedDefeatFinalChecksum &&
            chorusDefeat.traceChecksum == ExpectedDefeatTraceChecksum,
        "the Chorus full-match path matches its reviewed golden checksums");

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
