#include "TempestInterface.h"
#include "TempestSimulation.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define TEMPEST_ISATTY _isatty
#define TEMPEST_FILENO _fileno
#else
#include <unistd.h>
#define TEMPEST_ISATTY isatty
#define TEMPEST_FILENO fileno
#endif

namespace {
namespace Ui = Tempest::Ui;

constexpr std::int32_t GridScanRange = 5000;
constexpr std::uint32_t StatusIntervalTicks = static_cast<std::uint32_t>(Tempest::TicksPerSecond) * 10U;
constexpr std::int32_t MaximumWaitSeconds = 600;

enum class LineResult {
    Continue,
    Quit,
    Failed,
};

struct SessionOptions {
    std::string scriptPath;
    bool expectOutcome = false;
    Tempest::MatchOutcome expected = Tempest::MatchOutcome::InProgress;
    bool strict = false;
};

bool ParseInt(std::string_view text, std::int32_t &value)
{
    if (text.empty()) {
        return false;
    }
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc {} && result.ptr == end;
}

std::string Lower(std::string_view text)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

std::vector<std::string> Tokenize(const std::string &line)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : line) {
        if (ch == '#') {
            break;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(Lower(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(Lower(current));
    }
    return tokens;
}

const char *OutcomeTitle(Tempest::MatchOutcome outcome)
{
    switch (outcome) {
        case Tempest::MatchOutcome::Victory: return "GRID RESTORED";
        case Tempest::MatchOutcome::Defeat: return "DISTRICT LOST";
        case Tempest::MatchOutcome::InProgress: return "IN PROGRESS";
    }
    return "IN PROGRESS";
}

const char *OrderName(Tempest::OrderKind order)
{
    switch (order) {
        case Tempest::OrderKind::Idle: return "idle";
        case Tempest::OrderKind::Move: return "moving";
        case Tempest::OrderKind::Capture: return "capturing";
        case Tempest::OrderKind::Attack: return "attacking";
        case Tempest::OrderKind::Repair: return "repairing";
    }
    return "idle";
}

const char *OwnerName(Tempest::Faction faction)
{
    switch (faction) {
        case Tempest::Faction::Freegrid: return "Freegrid";
        case Tempest::Faction::Chorus: return "Chorus";
        case Tempest::Faction::Neutral: return "neutral";
    }
    return "neutral";
}

std::int64_t DistanceSquared(Tempest::Point left, Tempest::Point right)
{
    const std::int64_t dx = static_cast<std::int64_t>(left.x) - right.x;
    const std::int64_t dy = static_cast<std::int64_t>(left.y) - right.y;
    return (dx * dx) + (dy * dy);
}

class Session {
public:
    explicit Session(SessionOptions options)
        : m_options(std::move(options))
    {
        std::cout << std::unitbuf;
    }

    void PrintIntro() const
    {
        std::cout
            << "Project Tempest\n"
            << "Substation 9\n"
            << "\n"
            << "2089. Chorus has colonised the basin's abandoned control grid.\n"
            << "\n"
            << "You command Freegrid.\n"
            << "Win: destroy the Chorus Spire.\n"
            << "Loss: if the Relay Core falls, the district is lost.\n"
            << "\n"
            << "Salvage pays for construction and production. Captured substations add salvage and ability charge.\n"
            << "\n"
            << "Type begin to start. Type help for commands.\n";
    }

    LineResult HandleLine(const std::string &line)
    {
        const std::vector<std::string> tokens = Tokenize(line);
        if (tokens.empty()) {
            return LineResult::Continue;
        }
        const std::string &command = tokens[0];
        if (command == "quit" || command == "exit") {
            return LineResult::Quit;
        }
        if (command == "help") {
            PrintHelp();
            return LineResult::Continue;
        }
        if (command == "look") {
            PrintLook();
            return LineResult::Continue;
        }
        if (command == "begin") {
            Begin();
            return LineResult::Continue;
        }
        if (command == "pause") {
            Pause();
            return LineResult::Continue;
        }
        if (command == "restart") {
            Restart();
            return LineResult::Continue;
        }
        if (command == "select") {
            return SelectCommand(tokens);
        }
        if (command == "move") {
            return MoveCommand(tokens);
        }
        if (command == "capture") {
            return CaptureCommand(tokens);
        }
        if (command == "attack") {
            return AttackCommand(tokens);
        }
        if (command == "build") {
            return BuildCommand(tokens);
        }
        if (command == "produce") {
            return ProduceCommand(tokens);
        }
        if (command == "repair") {
            return RepairCommand(tokens);
        }
        if (command == "scan") {
            return ScanCommand(tokens);
        }
        if (command == "overcharge") {
            OverchargeCommand();
            return LineResult::Continue;
        }
        if (command == "pulse") {
            return PulseCommand(tokens);
        }
        if (command == "wait") {
            return WaitCommand(tokens);
        }
        std::cout << "Unknown command '" << command << "'. Type help.\n";
        return m_options.strict ? LineResult::Failed : LineResult::Continue;
    }

    int Finish() const
    {
        const Tempest::MatchOutcome outcome = m_simulation.GetState().outcome;
        if (!m_options.expectOutcome) {
            return 0;
        }
        if (outcome == m_options.expected) {
            return 0;
        }
        const BuildingHitPoints hitPoints = ReadObjectiveHitPoints();
        std::cerr << "Expected " << OutcomeTitle(m_options.expected)
                  << " but the match is " << OutcomeTitle(outcome)
                  << " at tick " << m_simulation.GetState().tick
                  << " (core " << hitPoints.core << ", spire " << hitPoints.spire << ").\n";
        return 1;
    }

private:
    struct BuildingHitPoints {
        std::int32_t core = -1;
        std::int32_t spire = -1;
    };

    SessionOptions m_options;
    Tempest::Simulation m_simulation;
    Ui::InterfaceState m_interface;
    std::vector<std::uint32_t> m_selected;
    bool m_resultAnnounced = false;

    const Tempest::MatchState &State() const
    {
        return m_simulation.GetState();
    }

    bool IsSelected(std::uint32_t id) const
    {
        return std::find(m_selected.begin(), m_selected.end(), id) != m_selected.end();
    }

    void PruneSelection()
    {
        m_selected.erase(std::remove_if(m_selected.begin(), m_selected.end(), [this](std::uint32_t id) {
            const Tempest::Unit *unit = FindUnit(id);
            return unit == nullptr || !unit->alive || unit->faction != Tempest::Faction::Freegrid;
        }), m_selected.end());
    }

    void ReplaceSelection(std::vector<std::uint32_t> ids)
    {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        m_selected = std::move(ids);
    }

    void SelectStartingCourier()
    {
        for (const Tempest::Unit &unit : State().units) {
            if (unit.alive && unit.faction == Tempest::Faction::Freegrid && unit.kind == Tempest::UnitKind::CourierScout) {
                ReplaceSelection({ unit.id });
                return;
            }
        }
        m_selected.clear();
    }

    const Tempest::Unit *FindUnit(std::uint32_t id) const
    {
        for (const Tempest::Unit &unit : State().units) {
            if (unit.id == id) {
                return &unit;
            }
        }
        return nullptr;
    }

    const Tempest::Building *FindBuilding(std::uint32_t id) const
    {
        for (const Tempest::Building &building : State().buildings) {
            if (building.id == id) {
                return &building;
            }
        }
        return nullptr;
    }

    const Tempest::ControlNode *FindNode(std::uint32_t id) const
    {
        for (const Tempest::ControlNode &node : State().nodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    }

    std::uint32_t FindBuildingId(Tempest::BuildingKind kind) const
    {
        std::uint32_t found = 0;
        for (const Tempest::Building &building : State().buildings) {
            if (building.kind == kind && building.hitPoints > 0 && (found == 0 || building.id < found)) {
                found = building.id;
            }
        }
        return found;
    }

    std::uint32_t FindLivingUnitId(Tempest::UnitKind kind, Tempest::Faction faction, Tempest::Point from) const
    {
        std::uint32_t found = 0;
        std::int64_t bestDistance = 0;
        for (const Tempest::Unit &unit : State().units) {
            if (!unit.alive || unit.kind != kind || unit.faction != faction) {
                continue;
            }
            const std::int64_t distance = DistanceSquared(unit.position, from);
            if (found == 0 || distance < bestDistance || (distance == bestDistance && unit.id < found)) {
                found = unit.id;
                bestDistance = distance;
            }
        }
        return found;
    }

    std::vector<std::uint32_t> LivingFreegrid(std::optional<Tempest::UnitKind> kind) const
    {
        std::vector<std::uint32_t> ids;
        for (const Tempest::Unit &unit : State().units) {
            if (!unit.alive || unit.faction != Tempest::Faction::Freegrid) {
                continue;
            }
            if (kind.has_value() && unit.kind != *kind) {
                continue;
            }
            ids.push_back(unit.id);
        }
        return ids;
    }

    std::optional<std::uint32_t> ResolveNode(const std::string &token) const
    {
        std::int32_t number = 0;
        if (!ParseInt(token, number) || number <= 0) {
            return std::nullopt;
        }
        const auto requested = static_cast<std::uint32_t>(number);
        if (FindNode(requested) != nullptr) {
            return requested;
        }
        if (requested >= 1 && requested <= State().nodes.size()) {
            return State().nodes[static_cast<std::size_t>(requested - 1)].id;
        }
        return std::nullopt;
    }

    std::optional<Tempest::UnitKind> ParseFreegridKind(const std::string &token, bool &plural) const
    {
        plural = false;
        if (token == "courier" || token == "couriers") {
            plural = token == "couriers";
            return Tempest::UnitKind::CourierScout;
        }
        if (token == "rig" || token == "rigs" || token == "fabricator" || token == "fabricators") {
            plural = token == "rigs" || token == "fabricators";
            return Tempest::UnitKind::FabricatorRig;
        }
        if (token == "lancer" || token == "lancers") {
            plural = token == "lancers";
            return Tempest::UnitKind::LancerCrew;
        }
        if (token == "coil" || token == "coils" || token == "carrier" || token == "carriers") {
            plural = token == "coils" || token == "carriers";
            return Tempest::UnitKind::CoilCarrier;
        }
        return std::nullopt;
    }

    std::optional<Tempest::UnitKind> ParseProduceKind(const std::string &token) const
    {
        bool plural = false;
        return ParseFreegridKind(token, plural);
    }

    std::optional<Tempest::BuildingKind> ParsePlayerStructure(const std::string &token) const
    {
        if (token == "dynamo" || token == "relay") {
            return Tempest::BuildingKind::Dynamo;
        }
        if (token == "sentry") {
            return Tempest::BuildingKind::ArcSentry;
        }
        return std::nullopt;
    }

    std::uint32_t FabricatorActor() const
    {
        for (const std::uint32_t id : m_selected) {
            const Tempest::Unit *unit = FindUnit(id);
            if (unit != nullptr && unit->alive && unit->kind == Tempest::UnitKind::FabricatorRig) {
                return unit->id;
            }
        }
        const std::vector<std::uint32_t> rigs = LivingFreegrid(Tempest::UnitKind::FabricatorRig);
        return rigs.empty() ? 0U : rigs.front();
    }

    bool EnsurePlaying() const
    {
        switch (m_interface.GetScreen()) {
            case Ui::Screen::Briefing:
                std::cout << "Type begin before giving orders.\n";
                return false;
            case Ui::Screen::Pause:
                std::cout << "The match is paused. Type pause to resume.\n";
                return false;
            case Ui::Screen::Result:
                std::cout << "The match is over. Type restart to play again, or quit.\n";
                return false;
            case Ui::Screen::Settings:
                std::cout << "Close settings before giving orders.\n";
                return false;
            case Ui::Screen::Playing:
                return true;
        }
        return false;
    }

    bool Advance(std::uint32_t ticks, bool report)
    {
        if (!m_interface.AdvancesSimulation()) {
            if (report) {
                std::cout << "Time is stopped.\n";
            }
            return State().outcome != Tempest::MatchOutcome::InProgress;
        }

        bool printed = false;
        for (std::uint32_t stepped = 0; stepped < ticks; ++stepped) {
            if (!m_interface.AdvancesSimulation()) {
                break;
            }
            m_simulation.Step();
            const Tempest::MatchOutcome outcome = State().outcome;
            m_interface.SyncOutcome(outcome);
            if (outcome != Tempest::MatchOutcome::InProgress) {
                PrintResult();
                return true;
            }
            if (report && ((stepped + 1) % StatusIntervalTicks) == 0) {
                PrintStatus();
                printed = true;
            }
        }
        if (report && !printed) {
            PrintStatus();
        }
        return false;
    }

    void Submit(Tempest::Command command)
    {
        command.executeTick = State().tick;
        m_simulation.Submit(command);
    }

    Tempest::Command MakeCommand(Tempest::CommandKind kind, std::uint32_t actor, std::uint32_t target = 0) const
    {
        Tempest::Command command;
        command.kind = kind;
        command.actorId = actor;
        command.targetId = target;
        command.executeTick = State().tick;
        return command;
    }

    BuildingHitPoints ReadObjectiveHitPoints() const
    {
        BuildingHitPoints hitPoints;
        for (const Tempest::Building &building : State().buildings) {
            if (building.kind == Tempest::BuildingKind::RelayCore) {
                hitPoints.core = building.hitPoints;
            } else if (building.kind == Tempest::BuildingKind::ChorusSpire) {
                hitPoints.spire = building.hitPoints;
            }
        }
        return hitPoints;
    }

    void PrintHelp() const
    {
        std::cout
            << "Win by destroying the Chorus Spire. Lose if the Relay Core falls.\n"
            << "Commands:\n"
            << "  begin                         start the match\n"
            << "  look                          show the battlefield\n"
            << "  select <id|courier|couriers|rig|rigs|lancer|lancers|coil|coils|all>\n"
            << "  move <x> <y>                  move the selection\n"
            << "  capture <node>                node id, or 1..3 from west to east\n"
            << "  attack <id|spire|nest|pylon|skitter|warden|harrower>\n"
            << "  build <dynamo|sentry> [node]  Fabricator rig restores a structure\n"
            << "  produce <rig|courier|lancer|coil>\n"
            << "  repair <id|core|bay|dynamo|sentry>\n"
            << "  scan <x> <y>                  grid-link scan\n"
            << "  overcharge                    emergency overcharge\n"
            << "  pulse [x y]                   Arc Pulse from the first selected unit\n"
            << "  wait <seconds>\n"
            << "  pause                         pause or resume\n"
            << "  restart                       restart from pause or the result\n"
            << "  help\n"
            << "  quit\n";
    }

    void PrintStatus() const
    {
        const Tempest::MatchState &state = State();
        const BuildingHitPoints hitPoints = ReadObjectiveHitPoints();
        int freegrid = 0;
        int chorus = 0;
        for (const Tempest::Unit &unit : state.units) {
            if (!unit.alive) {
                continue;
            }
            if (unit.faction == Tempest::Faction::Freegrid) {
                ++freegrid;
            } else if (unit.faction == Tempest::Faction::Chorus) {
                ++chorus;
            }
        }
        const std::uint64_t seconds = state.tick / static_cast<std::uint64_t>(Tempest::TicksPerSecond);
        std::cout << "t=" << seconds << "s"
                  << "  salvage=" << state.salvage
                  << "  ability=" << state.abilityCharge
                  << "  core=" << hitPoints.core
                  << "  spire=" << hitPoints.spire
                  << "  freegrid=" << freegrid
                  << "  chorus=" << chorus << '\n';
    }

    void PrintMap() const
    {
        constexpr int columns = 41;
        constexpr int rows = 17;
        struct Cell {
            char glyph = '.';
            int priority = 0;
        };
        std::vector<Cell> cells(static_cast<std::size_t>(columns * rows));
        std::int32_t minX = 0;
        std::int32_t maxX = 1;
        std::int32_t minY = 0;
        std::int32_t maxY = 1;
        bool any = false;
        const auto include = [&](Tempest::Point point) {
            if (!any) {
                minX = maxX = point.x;
                minY = maxY = point.y;
                any = true;
                return;
            }
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        };
        for (const Tempest::Unit &unit : State().units) {
            if (unit.alive) {
                include(unit.position);
            }
        }
        for (const Tempest::Building &building : State().buildings) {
            if (building.hitPoints > 0) {
                include(building.position);
            }
        }
        for (const Tempest::ControlNode &node : State().nodes) {
            include(node.position);
        }
        const std::int32_t spanX = std::max(1, maxX - minX);
        const std::int32_t spanY = std::max(1, maxY - minY);
        const auto plot = [&](Tempest::Point point, char glyph, int priority) {
            const int column = static_cast<int>((static_cast<std::int64_t>(point.x - minX) * (columns - 1)) / spanX);
            const int rowFromSouth = static_cast<int>((static_cast<std::int64_t>(point.y - minY) * (rows - 1)) / spanY);
            const int row = (rows - 1) - rowFromSouth;
            Cell &cell = cells[static_cast<std::size_t>((row * columns) + column)];
            if (priority >= cell.priority) {
                cell.glyph = glyph;
                cell.priority = priority;
            }
        };
        for (const Tempest::ControlNode &node : State().nodes) {
            const char glyph = node.owner == Tempest::Faction::Freegrid
                ? 'o'
                : (node.owner == Tempest::Faction::Chorus ? 'x' : '+');
            plot(node.position, glyph, 1);
        }
        for (const Tempest::Building &building : State().buildings) {
            if (building.hitPoints <= 0) {
                continue;
            }
            char glyph = 'B';
            switch (building.kind) {
                case Tempest::BuildingKind::RelayCore: glyph = 'R'; break;
                case Tempest::BuildingKind::FabricatorBay: glyph = 'B'; break;
                case Tempest::BuildingKind::Dynamo: glyph = 'D'; break;
                case Tempest::BuildingKind::ArcSentry: glyph = 'A'; break;
                case Tempest::BuildingKind::MachineNest: glyph = 'N'; break;
                case Tempest::BuildingKind::SignalPylon: glyph = 'Y'; break;
                case Tempest::BuildingKind::ChorusSpire: glyph = 'S'; break;
                case Tempest::BuildingKind::Count: break;
            }
            plot(building.position, glyph, 2);
        }
        for (const Tempest::Unit &unit : State().units) {
            if (!unit.alive) {
                continue;
            }
            const bool selected = IsSelected(unit.id);
            const char glyph = selected ? '*' : (unit.faction == Tempest::Faction::Freegrid ? 'f' : 'c');
            plot(unit.position, glyph, selected ? 4 : 3);
        }

        std::cout << "Map (+Y north, Freegrid starts southwest, Chorus Spire is S)\n";
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                std::cout << cells[static_cast<std::size_t>((row * columns) + column)].glyph;
            }
            std::cout << '\n';
        }
        std::cout << "* selected  f Freegrid  c Chorus  R core  B bay  D dynamo  A sentry\n"
                  << "N nest  Y pylon  S spire  + neutral node  o your node  x Chorus node\n";
    }

    void PrintEntities() const
    {
        std::cout << "Freegrid\n";
        for (const Tempest::Unit &unit : State().units) {
            if (!unit.alive || unit.faction != Tempest::Faction::Freegrid) {
                continue;
            }
            std::cout << "  #" << unit.id << ' ' << Tempest::GetUnitDefinition(unit.kind).displayName
                      << "  HP " << unit.hitPoints << '/' << unit.maximumHitPoints
                      << "  (" << unit.position.x << ", " << unit.position.y << ")  "
                      << OrderName(unit.order);
            if (IsSelected(unit.id)) {
                std::cout << "  [selected]";
            }
            std::cout << '\n';
        }
        std::cout << "Chorus\n";
        for (const Tempest::Unit &unit : State().units) {
            if (!unit.alive || unit.faction != Tempest::Faction::Chorus) {
                continue;
            }
            std::cout << "  #" << unit.id << ' ' << Tempest::GetUnitDefinition(unit.kind).displayName
                      << "  HP " << unit.hitPoints << '/' << unit.maximumHitPoints
                      << "  (" << unit.position.x << ", " << unit.position.y << ")  "
                      << OrderName(unit.order) << '\n';
        }
        std::cout << "Structures\n";
        for (const Tempest::Building &building : State().buildings) {
            if (building.hitPoints <= 0) {
                continue;
            }
            std::cout << "  #" << building.id << ' ' << Tempest::GetBuildingDefinition(building.kind).displayName
                      << "  [" << (building.faction == Tempest::Faction::Freegrid ? 'F' : 'C') << "]"
                      << "  HP " << building.hitPoints << '/' << building.maximumHitPoints
                      << "  (" << building.position.x << ", " << building.position.y << ')';
            if (!building.complete) {
                std::cout << "  building " << building.remainingBuildTicks / Tempest::TicksPerSecond << "s";
            }
            std::cout << '\n';
        }
        std::cout << "Substations\n";
        std::uint32_t ordinal = 1;
        for (const Tempest::ControlNode &node : State().nodes) {
            std::cout << "  " << ordinal << "  #" << node.id << ' ' << OwnerName(node.owner)
                      << "  (" << node.position.x << ", " << node.position.y << ")\n";
            ++ordinal;
        }
        if (!State().production.empty()) {
            std::cout << "Production\n";
            for (const Tempest::ProductionOrder &order : State().production) {
                const std::int32_t seconds = (order.remainingTicks + Tempest::TicksPerSecond - 1) / Tempest::TicksPerSecond;
                std::cout << "  " << Tempest::GetUnitDefinition(order.kind).displayName << "  " << seconds << "s\n";
            }
        }
    }

    void PrintLook()
    {
        PruneSelection();
        const Tempest::MatchState &state = State();
        std::cout << "\n" << Ui::InterfaceState::ScreenName(m_interface.GetScreen())
                  << "  t=" << (state.tick / static_cast<std::uint64_t>(Tempest::TicksPerSecond)) << "s"
                  << "  salvage=" << state.salvage
                  << "  ability=" << state.abilityCharge
                  << "  capacity=" << m_simulation.UsedFreegridCapacity() << '/' << m_simulation.FreegridCapacity()
                  << '\n';
        std::cout << "Objective: destroy the Chorus Spire. Loss: the Relay Core falls.\n";
        PrintMap();
        PrintEntities();
    }

    void PrintResult()
    {
        if (m_resultAnnounced) {
            return;
        }
        m_resultAnnounced = true;
        const Tempest::MatchState &state = State();
        const bool victory = state.outcome == Tempest::MatchOutcome::Victory;
        const int linked = static_cast<int>(std::count_if(state.nodes.begin(), state.nodes.end(), [](const Tempest::ControlNode &node) {
            return node.owner == Tempest::Faction::Freegrid;
        }));
        std::cout << '\n' << OutcomeTitle(state.outcome) << '\n'
                  << (victory
                          ? "You severed the Chorus Spire and returned the grid to Freegrid control."
                          : "The Relay Core fell before the Chorus signal could be isolated.")
                  << "\n\nDecisive state: " << linked << " of " << state.nodes.size()
                  << " substations linked, " << state.salvage << " salvage banked, "
                  << state.abilityCharge << "% ability charge.\n"
                  << "Type restart to play again, or quit to leave.\n";
    }

    void Begin()
    {
        if (m_interface.GetScreen() != Ui::Screen::Briefing) {
            std::cout << "The match has already started.\n";
            return;
        }
        const Ui::InputEvent event = m_interface.HandleKey(Ui::KeyEnter);
        if (event.intent != Ui::Intent::BeginMatch) {
            std::cout << "The link could not be established.\n";
            return;
        }
        SelectStartingCourier();
        std::cout << "Link established. Substation 9 is live.\n";
        PrintLook();
    }

    void Pause()
    {
        const Ui::Screen screen = m_interface.GetScreen();
        if (screen != Ui::Screen::Playing && screen != Ui::Screen::Pause) {
            std::cout << "Pause is available during the match.\n";
            return;
        }
        m_interface.HandleKey(m_interface.BindingFor(Ui::Action::Pause).code);
        if (m_interface.GetScreen() == Ui::Screen::Pause) {
            std::cout << "NETWORK PAUSED\nSimulation time is stopped. Type pause to resume, restart to begin again, or quit.\n";
        } else {
            std::cout << "Match resumed.\n";
        }
    }

    void Restart()
    {
        if (m_interface.GetScreen() == Ui::Screen::Briefing) {
            std::cout << "The match has not started.\n";
            return;
        }
        if (m_interface.GetScreen() == Ui::Screen::Playing) {
            m_interface.HandleKey(m_interface.BindingFor(Ui::Action::Pause).code);
        }
        const Ui::InputEvent event = m_interface.HandleKey(m_interface.BindingFor(Ui::Action::Restart).code);
        if (event.intent != Ui::Intent::RestartMatch) {
            std::cout << "Restart is available from pause or the result.\n";
            return;
        }
        m_simulation.Reset();
        m_resultAnnounced = false;
        SelectStartingCourier();
        std::cout << "Substation 9 restarted.\n";
        PrintLook();
    }

    LineResult SelectCommand(const std::vector<std::string> &tokens)
    {
        if (tokens.size() != 2) {
            std::cout << "Usage: select <id|courier|couriers|rig|rigs|lancer|lancers|coil|coils|all>\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        PruneSelection();
        if (tokens[1] == "all") {
            ReplaceSelection(LivingFreegrid(std::nullopt));
        } else {
            std::int32_t number = 0;
            if (ParseInt(tokens[1], number) && number > 0) {
                const auto id = static_cast<std::uint32_t>(number);
                const Tempest::Unit *unit = FindUnit(id);
                if (unit == nullptr || !unit->alive || unit->faction != Tempest::Faction::Freegrid) {
                    std::cout << "No living Freegrid unit #" << id << ".\n";
                    return LineResult::Continue;
                }
                ReplaceSelection({ id });
            } else {
                bool plural = false;
                const std::optional<Tempest::UnitKind> kind = ParseFreegridKind(tokens[1], plural);
                if (!kind.has_value()) {
                    std::cout << "Unknown selection '" << tokens[1] << "'.\n";
                    return m_options.strict ? LineResult::Failed : LineResult::Continue;
                }
                std::vector<std::uint32_t> ids = LivingFreegrid(kind);
                if (!plural && !ids.empty()) {
                    ids.resize(1);
                }
                ReplaceSelection(std::move(ids));
            }
        }
        PruneSelection();
        if (m_selected.empty()) {
            std::cout << "Nothing selected.\n";
            return LineResult::Continue;
        }
        std::cout << "Selected";
        for (const std::uint32_t id : m_selected) {
            const Tempest::Unit *unit = FindUnit(id);
            if (unit != nullptr) {
                std::cout << "  #" << unit->id << ' ' << Tempest::GetUnitDefinition(unit->kind).displayName;
            }
        }
        std::cout << '\n';
        return LineResult::Continue;
    }

    std::vector<std::uint32_t> SelectedActors()
    {
        PruneSelection();
        if (m_selected.empty()) {
            std::cout << "Select a Freegrid unit first.\n";
        }
        return m_selected;
    }

    LineResult MoveCommand(const std::vector<std::string> &tokens)
    {
        if (!EnsurePlaying()) {
            return LineResult::Continue;
        }
        if (tokens.size() != 3) {
            std::cout << "Usage: move <x> <y>\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        std::int32_t x = 0;
        std::int32_t y = 0;
        if (!ParseInt(tokens[1], x) || !ParseInt(tokens[2], y)) {
            std::cout << "Move coordinates must be whole numbers.\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        const std::vector<std::uint32_t> actors = SelectedActors();
        if (actors.empty()) {
            return LineResult::Continue;
        }
        for (const std::uint32_t id : actors) {
            Tempest::Command command = MakeCommand(Tempest::CommandKind::Move, id);
            command.point = { x, y };
            Submit(command);
        }
        if (Advance(1, false)) {
            return LineResult::Continue;
        }
        std::cout << "Moving " << actors.size() << " unit" << (actors.size() == 1 ? "" : "s")
                  << " to (" << x << ", " << y << ").\n";
        return LineResult::Continue;
    }

    LineResult CaptureCommand(const std::vector<std::string> &tokens)
    {
        if (!EnsurePlaying()) {
            return LineResult::Continue;
        }
        if (tokens.size() != 2) {
            std::cout << "Usage: capture <node>\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        const std::optional<std::uint32_t> nodeId = ResolveNode(tokens[1]);
        if (!nodeId.has_value()) {
            std::cout << "No substation '" << tokens[1] << "'. Use look for ids 1..3.\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        const std::vector<std::uint32_t> actors = SelectedActors();
        if (actors.empty()) {
            return LineResult::Continue;
        }
        for (const std::uint32_t id : actors) {
            Submit(MakeCommand(Tempest::CommandKind::Capture, id, *nodeId));
        }
        if (Advance(1, false)) {
            return LineResult::Continue;
        }
        int capturing = 0;
        for (const std::uint32_t id : actors) {
            const Tempest::Unit *unit = FindUnit(id);
            if (unit != nullptr && unit->order == Tempest::OrderKind::Capture && unit->targetId == *nodeId) {
                ++capturing;
            }
        }
        if (capturing == 0) {
            std::cout << "The selection could not capture that substation.\n";
            return LineResult::Continue;
        }
        std::cout << "Capturing substation #" << *nodeId << " with " << capturing << " unit"
                  << (capturing == 1 ? "" : "s") << ".\n";
        return LineResult::Continue;
    }

    std::optional<std::uint32_t> ResolveAttackTarget(const std::string &token) const
    {
        std::int32_t number = 0;
        if (ParseInt(token, number) && number > 0) {
            const auto id = static_cast<std::uint32_t>(number);
            const Tempest::Unit *unit = FindUnit(id);
            const Tempest::Building *building = FindBuilding(id);
            if ((unit != nullptr && unit->alive) || (building != nullptr && building->hitPoints > 0)) {
                return id;
            }
            return std::nullopt;
        }
        if (token == "spire") {
            return FindBuildingId(Tempest::BuildingKind::ChorusSpire);
        }
        if (token == "nest") {
            return FindBuildingId(Tempest::BuildingKind::MachineNest);
        }
        if (token == "pylon") {
            return FindBuildingId(Tempest::BuildingKind::SignalPylon);
        }
        Tempest::Point from {};
        if (!m_selected.empty()) {
            if (const Tempest::Unit *selected = FindUnit(m_selected.front())) {
                from = selected->position;
            }
        }
        if (token == "skitter") {
            return FindLivingUnitId(Tempest::UnitKind::Skitter, Tempest::Faction::Chorus, from);
        }
        if (token == "warden") {
            return FindLivingUnitId(Tempest::UnitKind::Warden, Tempest::Faction::Chorus, from);
        }
        if (token == "harrower") {
            return FindLivingUnitId(Tempest::UnitKind::Harrower, Tempest::Faction::Chorus, from);
        }
        return std::nullopt;
    }

    std::string DescribeTarget(std::uint32_t id) const
    {
        if (const Tempest::Unit *unit = FindUnit(id)) {
            return std::string(Tempest::GetUnitDefinition(unit->kind).displayName) + " #" + std::to_string(unit->id);
        }
        if (const Tempest::Building *building = FindBuilding(id)) {
            return std::string(Tempest::GetBuildingDefinition(building->kind).displayName) + " #" + std::to_string(building->id);
        }
        return "target #" + std::to_string(id);
    }

    LineResult AttackCommand(const std::vector<std::string> &tokens)
    {
        if (!EnsurePlaying()) {
            return LineResult::Continue;
        }
        if (tokens.size() != 2) {
            std::cout << "Usage: attack <id|spire|nest|pylon|skitter|warden|harrower>\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        const std::optional<std::uint32_t> target = ResolveAttackTarget(tokens[1]);
        if (!target.has_value() || *target == 0) {
            std::cout << "No attack target '" << tokens[1] << "'.\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        const std::vector<std::uint32_t> actors = SelectedActors();
        if (actors.empty()) {
            return LineResult::Continue;
        }
        for (const std::uint32_t id : actors) {
            Submit(MakeCommand(Tempest::CommandKind::Attack, id, *target));
        }
        const std::string targetName = DescribeTarget(*target);
        if (Advance(1, false)) {
            return LineResult::Continue;
        }
        int attacking = 0;
        for (const std::uint32_t id : actors) {
            const Tempest::Unit *unit = FindUnit(id);
            if (unit != nullptr && unit->order == Tempest::OrderKind::Attack && unit->targetId == *target) {
                ++attacking;
            }
        }
        if (attacking == 0) {
            std::cout << "The selection could not attack " << targetName << ".\n";
            return LineResult::Continue;
        }
        std::cout << attacking << " unit" << (attacking == 1 ? "" : "s") << " attacking " << targetName << ".\n";
        return LineResult::Continue;
    }

    LineResult BuildCommand(const std::vector<std::string> &tokens)
    {
        if (!EnsurePlaying()) {
            return LineResult::Continue;
        }
        if (tokens.size() < 2 || tokens.size() > 3) {
            std::cout << "Usage: build <dynamo|sentry> [node]\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        const std::optional<Tempest::BuildingKind> kind = ParsePlayerStructure(tokens[1]);
        if (!kind.has_value()) {
            std::cout << "Build dynamo or sentry.\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        std::uint32_t nodeId = 0;
        if (tokens.size() == 3) {
            const std::optional<std::uint32_t> resolved = ResolveNode(tokens[2]);
            if (!resolved.has_value()) {
                std::cout << "No substation '" << tokens[2] << "'.\n";
                return m_options.strict ? LineResult::Failed : LineResult::Continue;
            }
            nodeId = *resolved;
        } else {
            for (const Tempest::ControlNode &node : State().nodes) {
                if (node.owner == Tempest::Faction::Freegrid) {
                    nodeId = node.id;
                    break;
                }
            }
        }
        const std::uint32_t actor = FabricatorActor();
        const Tempest::BuildingDefinition &definition = Tempest::GetBuildingDefinition(*kind);
        if (actor == 0) {
            std::cout << "A living Fabricator rig is required to build.\n";
            return LineResult::Continue;
        }
        if (nodeId == 0) {
            std::cout << "Capture a substation before building a " << definition.displayName << ".\n";
            return LineResult::Continue;
        }
        const Tempest::ControlNode *node = FindNode(nodeId);
        if (node == nullptr || node->owner != Tempest::Faction::Freegrid) {
            std::cout << "Substation #" << nodeId << " is not yours yet. Capture it first.\n";
            return LineResult::Continue;
        }
        if (State().salvage < definition.salvageCost) {
            std::cout << definition.displayName << " costs " << definition.salvageCost
                      << " salvage. You have " << State().salvage << ".\n";
            return LineResult::Continue;
        }
        if (!m_simulation.CanBuildStructure(actor, nodeId, *kind)) {
            std::cout << "A " << definition.displayName << " is already at that substation.\n";
            return LineResult::Continue;
        }
        Tempest::Command command = MakeCommand(Tempest::CommandKind::BuildStructure, actor, nodeId);
        command.buildingKind = *kind;
        Submit(command);
        if (Advance(1, false)) {
            return LineResult::Continue;
        }
        std::cout << "Building " << definition.displayName << " at substation #" << nodeId << ".\n";
        return LineResult::Continue;
    }

    LineResult ProduceCommand(const std::vector<std::string> &tokens)
    {
        if (!EnsurePlaying()) {
            return LineResult::Continue;
        }
        if (tokens.size() != 2) {
            std::cout << "Usage: produce <rig|courier|lancer|coil>\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        const std::optional<Tempest::UnitKind> kind = ParseProduceKind(tokens[1]);
        if (!kind.has_value()) {
            std::cout << "Produce a rig, courier, lancer, or coil.\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        const std::uint32_t bayId = FindBuildingId(Tempest::BuildingKind::FabricatorBay);
        const Tempest::UnitDefinition &definition = Tempest::GetUnitDefinition(*kind);
        if (bayId == 0 || !m_simulation.CanProduceUnit(bayId, *kind)) {
            std::cout << "Cannot queue " << definition.displayName
                      << ". It costs " << definition.salvageCost << " salvage and "
                      << definition.capacityCost << " capacity ("
                      << m_simulation.UsedFreegridCapacity() << '/' << m_simulation.FreegridCapacity()
                      << " used, " << State().salvage << " salvage).\n";
            return LineResult::Continue;
        }
        Tempest::Command command = MakeCommand(Tempest::CommandKind::ProduceUnit, bayId);
        command.unitKind = *kind;
        Submit(command);
        if (Advance(1, false)) {
            return LineResult::Continue;
        }
        std::cout << "Queued " << definition.displayName << " (" << definition.salvageCost << " salvage).\n";
        return LineResult::Continue;
    }

    std::optional<std::uint32_t> ResolveRepairTarget(const std::string &token) const
    {
        std::int32_t number = 0;
        if (ParseInt(token, number) && number > 0) {
            return static_cast<std::uint32_t>(number);
        }
        if (token == "core") {
            return FindBuildingId(Tempest::BuildingKind::RelayCore);
        }
        if (token == "bay") {
            return FindBuildingId(Tempest::BuildingKind::FabricatorBay);
        }
        if (token == "dynamo" || token == "relay") {
            return FindBuildingId(Tempest::BuildingKind::Dynamo);
        }
        if (token == "sentry") {
            return FindBuildingId(Tempest::BuildingKind::ArcSentry);
        }
        return std::nullopt;
    }

    LineResult RepairCommand(const std::vector<std::string> &tokens)
    {
        if (!EnsurePlaying()) {
            return LineResult::Continue;
        }
        if (tokens.size() != 2) {
            std::cout << "Usage: repair <id|core|bay|dynamo|sentry>\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        const std::optional<std::uint32_t> target = ResolveRepairTarget(tokens[1]);
        if (!target.has_value() || *target == 0) {
            std::cout << "No repair target '" << tokens[1] << "'.\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        PruneSelection();
        std::vector<std::uint32_t> rigs;
        for (const std::uint32_t id : m_selected) {
            const Tempest::Unit *unit = FindUnit(id);
            if (unit != nullptr && unit->kind == Tempest::UnitKind::FabricatorRig) {
                rigs.push_back(id);
            }
        }
        if (rigs.empty()) {
            const std::uint32_t actor = FabricatorActor();
            if (actor != 0) {
                rigs.push_back(actor);
            }
        }
        if (rigs.empty()) {
            std::cout << "A Fabricator rig is required to repair.\n";
            return LineResult::Continue;
        }
        for (const std::uint32_t id : rigs) {
            Submit(MakeCommand(Tempest::CommandKind::Repair, id, *target));
        }
        if (Advance(1, false)) {
            return LineResult::Continue;
        }
        const Tempest::Unit *rig = FindUnit(rigs.front());
        if (rig == nullptr || rig->order != Tempest::OrderKind::Repair) {
            std::cout << "The rig could not repair " << DescribeTarget(*target) << ". The target must be a damaged Freegrid unit or structure.\n";
            return LineResult::Continue;
        }
        std::cout << "Repairing " << DescribeTarget(*target) << ".\n";
        return LineResult::Continue;
    }

    int ScanContacts() const
    {
        const Tempest::MatchState &state = State();
        const std::size_t scanIndex = static_cast<std::size_t>(Tempest::AbilityKind::GridLinkScan);
        if (state.abilityDurationTicks[scanIndex] <= 0) {
            return 0;
        }
        int contacts = 0;
        for (const Tempest::Unit &unit : state.units) {
            if (unit.alive && unit.faction == Tempest::Faction::Chorus &&
                DistanceSquared(unit.position, state.scanCenter) <= static_cast<std::int64_t>(GridScanRange) * GridScanRange) {
                ++contacts;
            }
        }
        for (const Tempest::Building &building : state.buildings) {
            if (building.hitPoints > 0 && building.faction == Tempest::Faction::Chorus &&
                DistanceSquared(building.position, state.scanCenter) <= static_cast<std::int64_t>(GridScanRange) * GridScanRange) {
                ++contacts;
            }
        }
        return contacts;
    }

    LineResult ScanCommand(const std::vector<std::string> &tokens)
    {
        if (!EnsurePlaying()) {
            return LineResult::Continue;
        }
        if (tokens.size() != 3) {
            std::cout << "Usage: scan <x> <y>\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        std::int32_t x = 0;
        std::int32_t y = 0;
        if (!ParseInt(tokens[1], x) || !ParseInt(tokens[2], y)) {
            std::cout << "Scan coordinates must be whole numbers.\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        if (!m_simulation.CanActivateAbility(Tempest::AbilityKind::GridLinkScan)) {
            std::cout << "Grid-link scan is not ready. It costs "
                      << Tempest::GetAbilityDefinition(Tempest::AbilityKind::GridLinkScan).abilityChargeCost
                      << " ability charge.\n";
            return LineResult::Continue;
        }
        Tempest::Command command = MakeCommand(Tempest::CommandKind::ActivateAbility, 0);
        command.abilityKind = Tempest::AbilityKind::GridLinkScan;
        command.point = { x, y };
        Submit(command);
        if (Advance(1, false)) {
            return LineResult::Continue;
        }
        std::cout << "Grid-link scan at (" << x << ", " << y << ") reports " << ScanContacts() << " Chorus contacts.\n";
        return LineResult::Continue;
    }

    void OverchargeCommand()
    {
        if (!EnsurePlaying()) {
            return;
        }
        if (!m_simulation.CanActivateAbility(Tempest::AbilityKind::EmergencyOvercharge)) {
            std::cout << "Emergency overcharge is not ready. It costs "
                      << Tempest::GetAbilityDefinition(Tempest::AbilityKind::EmergencyOvercharge).abilityChargeCost
                      << " ability charge.\n";
            return;
        }
        Tempest::Command command = MakeCommand(Tempest::CommandKind::ActivateAbility, 0);
        command.abilityKind = Tempest::AbilityKind::EmergencyOvercharge;
        Submit(command);
        if (Advance(1, false)) {
            return;
        }
        std::cout << "Emergency overcharge is active.\n";
    }

    LineResult PulseCommand(const std::vector<std::string> &tokens)
    {
        if (!EnsurePlaying()) {
            return LineResult::Continue;
        }
        if (tokens.size() != 1 && tokens.size() != 3) {
            std::cout << "Usage: pulse [x y]\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        PruneSelection();
        if (m_selected.empty()) {
            std::cout << "Select a Freegrid unit before Arc Pulse.\n";
            return LineResult::Continue;
        }
        const Tempest::Unit *caster = FindUnit(m_selected.front());
        if (caster == nullptr) {
            std::cout << "Select a Freegrid unit before Arc Pulse.\n";
            return LineResult::Continue;
        }
        Tempest::Point point = caster->position;
        if (tokens.size() == 3) {
            std::int32_t x = 0;
            std::int32_t y = 0;
            if (!ParseInt(tokens[1], x) || !ParseInt(tokens[2], y)) {
                std::cout << "Pulse coordinates must be whole numbers.\n";
                return m_options.strict ? LineResult::Failed : LineResult::Continue;
            }
            point = { x, y };
        }
        const std::int32_t chargeBefore = State().abilityCharge;
        Tempest::Command command = MakeCommand(Tempest::CommandKind::ArcPulse, caster->id);
        command.point = point;
        Submit(command);
        if (Advance(1, false)) {
            return LineResult::Continue;
        }
        if (State().abilityCharge < chargeBefore) {
            std::cout << Tempest::GetUnitDefinition(caster->kind).displayName << " #" << caster->id
                      << " fired Arc Pulse at (" << point.x << ", " << point.y << ").\n";
        } else {
            std::cout << "Arc Pulse did not fire. It costs 25 ability charge and the point must be in range of the caster.\n";
        }
        return LineResult::Continue;
    }

    LineResult WaitCommand(const std::vector<std::string> &tokens)
    {
        if (!EnsurePlaying()) {
            return LineResult::Continue;
        }
        if (tokens.size() != 2) {
            std::cout << "Usage: wait <seconds>\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        std::int32_t seconds = 0;
        if (!ParseInt(tokens[1], seconds) || seconds <= 0 || seconds > MaximumWaitSeconds) {
            std::cout << "Wait for 1 to " << MaximumWaitSeconds << " seconds.\n";
            return m_options.strict ? LineResult::Failed : LineResult::Continue;
        }
        Advance(static_cast<std::uint32_t>(seconds) * static_cast<std::uint32_t>(Tempest::TicksPerSecond), true);
        return LineResult::Continue;
    }
};

void PrintUsage()
{
    std::cerr << "Usage: project_tempest_session [--script file] [--expect victory|defeat]\n";
}

bool ParseArguments(int argc, char **argv, SessionOptions &options)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            PrintUsage();
            return false;
        }
        if (argument == "--script") {
            if (index + 1 >= argc) {
                PrintUsage();
                return false;
            }
            options.scriptPath = argv[++index];
            options.strict = true;
            continue;
        }
        if (argument == "--expect") {
            if (index + 1 >= argc) {
                PrintUsage();
                return false;
            }
            const std::string expected = Lower(argv[++index]);
            options.expectOutcome = true;
            options.strict = true;
            if (expected == "victory") {
                options.expected = Tempest::MatchOutcome::Victory;
            } else if (expected == "defeat") {
                options.expected = Tempest::MatchOutcome::Defeat;
            } else {
                PrintUsage();
                return false;
            }
            continue;
        }
        PrintUsage();
        return false;
    }
    return true;
}

int RunLines(Session &session, std::istream &input, bool announceLines)
{
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (announceLines && !Tokenize(line).empty()) {
            std::cout << "> " << line << '\n';
        }
        const LineResult result = session.HandleLine(line);
        if (result == LineResult::Quit) {
            return session.Finish();
        }
        if (result == LineResult::Failed) {
            std::cerr << "Stopped on line " << lineNumber << ".\n";
            return 1;
        }
    }
    return session.Finish();
}

} // namespace

int main(int argc, char **argv)
{
    SessionOptions options;
    if (!ParseArguments(argc, argv, options)) {
        return argc > 1 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h") ? 0 : 2;
    }

    Session session(options);
    session.PrintIntro();
    if (!options.scriptPath.empty()) {
        std::ifstream script(options.scriptPath);
        if (!script) {
            std::cerr << "Could not read " << options.scriptPath << ".\n";
            return 2;
        }
        return RunLines(session, script, true);
    }

    if (TEMPEST_ISATTY(TEMPEST_FILENO(stdin))) {
        std::string line;
        while (true) {
            std::cout << "> ";
            if (!std::getline(std::cin, line)) {
                std::cout << '\n';
                break;
            }
            const LineResult result = session.HandleLine(line);
            if (result == LineResult::Quit) {
                break;
            }
        }
        return session.Finish();
    }
    return RunLines(session, std::cin, false);
}
