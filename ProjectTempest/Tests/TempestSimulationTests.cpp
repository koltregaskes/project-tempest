#include "TempestSimulation.h"
#include "TempestAudio.h"
#include "TempestInterface.h"

#include <cmath>
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

void AppendU16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void AppendTag(std::vector<std::uint8_t> &bytes, const char *tag)
{
    bytes.insert(bytes.end(), tag, tag + 4);
}

std::vector<std::uint8_t> MakePcmWave()
{
    constexpr std::uint32_t dataSize = 8;
    std::vector<std::uint8_t> bytes;
    AppendTag(bytes, "RIFF");
    AppendU32(bytes, 36 + dataSize);
    AppendTag(bytes, "WAVE");
    AppendTag(bytes, "fmt ");
    AppendU32(bytes, 16);
    AppendU16(bytes, 1);
    AppendU16(bytes, 2);
    AppendU32(bytes, 48'000);
    AppendU32(bytes, 192'000);
    AppendU16(bytes, 4);
    AppendU16(bytes, 16);
    AppendTag(bytes, "data");
    AppendU32(bytes, dataSize);
    bytes.insert(bytes.end(), dataSize, 0);
    return bytes;
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

const Tempest::Unit *FindUnitById(const Tempest::MatchState &state, std::uint32_t id)
{
    for (const Tempest::Unit &unit : state.units) {
        if (unit.id == id) {
            return &unit;
        }
    }
    return nullptr;
}

std::uint32_t FindDrone(const Tempest::MatchState &state)
{
    for (const Tempest::Unit &unit : state.units) {
        if (unit.faction == Tempest::Faction::Chorus && unit.kind == Tempest::UnitKind::ChorusDrone && unit.alive) {
            return unit.id;
        }
    }
    return 0;
}

bool IsWithin(const Tempest::Point &left, const Tempest::Point &right, std::int32_t range)
{
    const std::int64_t dx = static_cast<std::int64_t>(left.x) - right.x;
    const std::int64_t dy = static_cast<std::int64_t>(left.y) - right.y;
    return (dx * dx) + (dy * dy) <= static_cast<std::int64_t>(range) * range;
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

void TestLethalArcPulseResolvesBeforeActions()
{
    Tempest::Simulation simulation;
    const std::uint32_t workshopId = FindBuilding(simulation.GetState(), Tempest::BuildingKind::Workshop);
    simulation.Submit(MakeCommand(0, Tempest::CommandKind::ProduceCourier, workshopId));
    simulation.Step(Tempest::TicksPerSecond * 3 + 1);

    const std::vector<std::uint32_t> couriers = FindCouriers(simulation.GetState());
    const std::uint32_t droneId = FindDrone(simulation.GetState());
    Expect(couriers.size() == 2 && droneId != 0, "lethal Arc Pulse fixture has two casters and one drone");
    const Tempest::Unit *drone = FindUnitById(simulation.GetState(), droneId);
    Expect(drone != nullptr, "lethal Arc Pulse fixture can inspect its drone");
    const Tempest::Point meetingPoint = drone->position;
    for (const std::uint32_t courierId : couriers) {
        simulation.Submit(MakeCommand(
            simulation.GetState().tick,
            Tempest::CommandKind::Move,
            courierId,
            0,
            meetingPoint));
    }

    bool armed = false;
    for (int tick = 0; tick < 500; ++tick) {
        simulation.Step();
        drone = FindUnitById(simulation.GetState(), droneId);
        if (!drone || !drone->alive) {
            continue;
        }
        const Tempest::Unit *firstCaster = FindUnitById(simulation.GetState(), couriers[0]);
        const Tempest::Unit *secondCaster = FindUnitById(simulation.GetState(), couriers[1]);
        if (firstCaster && secondCaster && firstCaster->alive && secondCaster->alive &&
            IsWithin(firstCaster->position, drone->position, 3200) &&
            IsWithin(secondCaster->position, drone->position, 3200)) {
            armed = true;
            break;
        }
    }
    Expect(armed, "lethal Arc Pulse fixture moves both casters within pulse range");

    drone = FindUnitById(simulation.GetState(), droneId);
    Expect(drone != nullptr, "drone remains inspectable before lethal damage");
    const Tempest::Point pulsePoint = drone->position;
    const Tempest::Point dronePositionBeforePulse = drone->position;
    std::vector<std::int32_t> chorusCaptureBeforePulse;
    for (const Tempest::ControlNode &node : simulation.GetState().nodes) {
        chorusCaptureBeforePulse.push_back(node.chorusCaptureTicks);
    }

    simulation.Submit(MakeCommand(
        simulation.GetState().tick,
        Tempest::CommandKind::ArcPulse,
        couriers[0],
        0,
        pulsePoint));
    simulation.Submit(MakeCommand(
        simulation.GetState().tick,
        Tempest::CommandKind::ArcPulse,
        couriers[1],
        0,
        pulsePoint));
    simulation.Step();

    drone = FindUnitById(simulation.GetState(), droneId);
    Expect(drone && !drone->alive && drone->hitPoints == 0 &&
            drone->order == Tempest::OrderKind::Idle && drone->targetId == 0,
        "lethal Arc Pulse immediately clears the drone's live action state");
    Expect(drone && drone->position.x == dronePositionBeforePulse.x &&
            drone->position.y == dronePositionBeforePulse.y,
        "a drone killed by Arc Pulse cannot move later in the same tick");
    for (std::size_t index = 0; index < simulation.GetState().nodes.size(); ++index) {
        Expect(simulation.GetState().nodes[index].chorusCaptureTicks == chorusCaptureBeforePulse[index],
            "a drone killed by Arc Pulse cannot advance capture progress in the same tick");
    }
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
    const Tempest::Ui::InputEvent select =
        interfaceState.HandleMouseButton(Tempest::Ui::MouseButton::Left);
    Expect(select.intent == Tempest::Ui::Intent::GameplayAction &&
            select.action == Tempest::Ui::Action::PrimarySelect,
        "the default primary mouse binding enters the gameplay action stream");

    interfaceState.HandleKey(interfaceState.BindingFor(Tempest::Ui::Action::Pause).code);
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Pause,
        "the remappable pause action opens the pause screen");
    interfaceState.HandleKey(interfaceState.BindingFor(Tempest::Ui::Action::OpenSettings).code);
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Settings &&
            interfaceState.GetSettingsReturnScreen() == Tempest::Ui::Screen::Pause,
        "settings opened during play returns to the safe paused screen");
    interfaceState.HandleKey(Tempest::Ui::KeyEscape);
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Pause,
        "escape returns from settings to pause without advancing simulation");
    interfaceState.HandleKey(interfaceState.BindingFor(Tempest::Ui::Action::Pause).code);
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Playing,
        "pause action resumes the match");

    interfaceState.SyncOutcome(Tempest::MatchOutcome::Victory);
    Expect(interfaceState.GetScreen() == Tempest::Ui::Screen::Result,
        "a completed simulation opens the in-window result flow");
    const Tempest::Ui::InputEvent restart =
        interfaceState.HandleKey(interfaceState.BindingFor(Tempest::Ui::Action::Restart).code);
    Expect(restart.intent == Tempest::Ui::Intent::RestartMatch &&
            interfaceState.GetScreen() == Tempest::Ui::Screen::Playing,
        "result flow restarts without returning to the desktop");
}

void TestSettingsBoundsAndRemapping()
{
    Tempest::Ui::InterfaceState interfaceState;
    interfaceState.HandleKey(interfaceState.BindingFor(Tempest::Ui::Action::OpenSettings).code);
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
            interfaceState.BindingFor(Tempest::Ui::Action::MoveUp) ==
                Tempest::Ui::InputBinding { Tempest::Ui::InputDevice::Keyboard, 'K' },
        "a free key can replace a gameplay binding");

    interfaceState.HandleKey(Tempest::Ui::KeyDown);
    interfaceState.HandleKey(Tempest::Ui::KeyEnter);
    const Tempest::Ui::InputEvent collision = interfaceState.HandleKey('K');
    Expect(collision.intent == Tempest::Ui::Intent::BindingRejected &&
            interfaceState.BindingFor(Tempest::Ui::Action::MoveDown) ==
                Tempest::Ui::InputBinding { Tempest::Ui::InputDevice::Keyboard, 'S' },
        "duplicate bindings are rejected without losing the prior key");

    bool keyStates[256] = {};
    bool mouseStates[6] = {};
    keyStates['K'] = true;
    Expect(interfaceState.IsActionPressed(Tempest::Ui::Action::MoveUp, keyStates, 256, mouseStates, 6),
        "the runtime input query consumes the remapped key");

    for (int row = 0; row < 9; ++row) {
        interfaceState.HandleKey(Tempest::Ui::KeyDown);
    }
    Expect(interfaceState.ActionForSettingsRow(interfaceState.GetSelectedSettingsRow()) ==
            Tempest::Ui::Action::PrimarySelect,
        "settings navigation reaches the primary mouse action");
    interfaceState.HandleKey(Tempest::Ui::KeyEnter);
    const Tempest::Ui::InputEvent mouseRebound =
        interfaceState.HandleMouseButton(Tempest::Ui::MouseButton::Middle);
    Expect(mouseRebound.intent == Tempest::Ui::Intent::BindingChanged &&
            interfaceState.BindingFor(Tempest::Ui::Action::PrimarySelect) ==
                Tempest::Ui::InputBinding {
                    Tempest::Ui::InputDevice::Mouse,
                    static_cast<std::uint16_t>(Tempest::Ui::MouseButton::Middle) },
        "primary selection can be rebound to a mouse button");
}

void TestConfigurationPersistence()
{
    Tempest::Ui::InterfaceState source;
    source.HandleKey(source.BindingFor(Tempest::Ui::Action::OpenSettings).code);
    source.HandleKey(Tempest::Ui::KeyRight);
    for (int row = 0; row < Tempest::Ui::InterfaceState::AdjustableSettingCount; ++row) {
        source.HandleKey(Tempest::Ui::KeyDown);
    }
    source.HandleKey(Tempest::Ui::KeyEnter);
    source.HandleKey('K');

    const std::string saved = source.SerializeConfiguration();
    Tempest::Ui::InterfaceState restored;
    Expect(restored.LoadConfiguration(saved), "a complete versioned settings file loads");
    Expect(restored.GetSettings().cameraSpeedPercent == 110,
        "persisted numeric settings survive a round trip");
    Expect(restored.BindingFor(Tempest::Ui::Action::MoveUp) ==
            Tempest::Ui::InputBinding { Tempest::Ui::InputDevice::Keyboard, 'K' },
        "persisted control bindings survive a round trip");
    Expect(restored.SerializeConfiguration() == saved,
        "configuration serialization is deterministic");

    const std::string pristine = restored.SerializeConfiguration();
    std::string corrupt = saved;
    const std::size_t cameraValue = corrupt.find("camera_speed_percent=110");
    Expect(cameraValue != std::string::npos, "round-trip fixture contains the adjusted camera speed");
    corrupt.replace(cameraValue, std::string("camera_speed_percent=110").size(), "camera_speed_percent=999");
    Expect(!restored.LoadConfiguration(corrupt), "out-of-range persisted settings are rejected");
    Expect(restored.SerializeConfiguration() == pristine,
        "a rejected settings file cannot partially mutate live settings");
}

void TestAudioContract()
{
    std::vector<std::uint8_t> bytes = MakePcmWave();
    Tempest::Audio::WaveAsset asset;
    std::string error;
    Expect(Tempest::Audio::ParsePcmWave(bytes, asset, error),
        "a valid stereo 48 kHz PCM16 WAV parses");
    Expect(asset.channels == 2 && asset.sampleRate == 48'000 &&
            asset.bitsPerSample == 16 && asset.pcm.size() == 8,
        "the WAV parser preserves the runtime format and sample payload");

    const Tempest::Audio::WaveAsset pristine = asset;
    bytes[24] = 0x44;
    bytes[25] = 0xAC;
    bytes[26] = 0;
    bytes[27] = 0;
    bytes[28] = 0x10;
    bytes[29] = 0xB1;
    bytes[30] = 0x02;
    bytes[31] = 0;
    Expect(!Tempest::Audio::ParsePcmWave(bytes, asset, error),
        "a non-48 kHz WAV is rejected");
    Expect(asset.channels == pristine.channels && asset.sampleRate == pristine.sampleRate &&
            asset.bitsPerSample == pristine.bitsPerSample && asset.pcm == pristine.pcm,
        "a rejected WAV cannot partially mutate the destination asset");

    bytes = MakePcmWave();
    bytes.pop_back();
    Expect(!Tempest::Audio::ParsePcmWave(bytes, asset, error),
        "a truncated WAV is rejected");
    Expect(std::abs(Tempest::Audio::VolumeGain(80, 65) - 0.52F) < 0.0001F,
        "master and channel volume controls multiply predictably");
    Expect(Tempest::Audio::VolumeGain(-10, 150) == 0.0F &&
            Tempest::Audio::VolumeGain(150, 150) == 1.0F,
        "volume gain clamps both controls to the supported range");
    const Tempest::Audio::MusicLayerGains calm = Tempest::Audio::MusicGainsForPressure(0.0F);
    const Tempest::Audio::MusicLayerGains pressured = Tempest::Audio::MusicGainsForPressure(0.7F);
    const Tempest::Audio::MusicLayerGains crisis = Tempest::Audio::MusicGainsForPressure(1.0F);
    Expect(calm.base == 1.0F && calm.pressure == 0.0F && calm.crisis == 0.0F,
        "calm music uses only the base stem");
    Expect(pressured.pressure > calm.pressure && pressured.crisis > calm.crisis &&
            crisis.pressure == 1.0F && crisis.crisis == 1.0F,
        "music layers increase monotonically with match pressure");
}

} // namespace

int main()
{
    TestInitialStateAndPause();
    TestEconomyConstructionAndProduction();
    TestArcPulseRange();
    TestLethalArcPulseResolvesBeforeActions();
    TestCommandValidationAndChorusTerritoryAi();
    TestVictoryAndDefeat();
    TestDeterministicReplay();
    TestInterfaceFlow();
    TestSettingsBoundsAndRemapping();
    TestConfigurationPersistence();
    TestAudioContract();
    std::cout << "PASS: Project Tempest deterministic simulation tests\n";
    return 0;
}
