#include "TempestInterface.h"

#include <algorithm>
#include <charconv>
#include <sstream>

namespace Tempest::Ui {
namespace {

constexpr std::array<Action, InterfaceState::RemappableActionCount> RemappableActions = {
    Action::MoveUp,
    Action::MoveDown,
    Action::MoveLeft,
    Action::MoveRight,
    Action::BuildRelay,
    Action::BuildArcSentry,
    Action::ProduceFabricator,
    Action::ProduceCourier,
    Action::ProduceLancer,
    Action::ProduceCoilCarrier,
    Action::ArcPulse,
    Action::GridLinkScan,
    Action::EmergencyOvercharge,
    Action::Pause,
    Action::OpenSettings,
    Action::Restart,
    Action::PrimarySelect,
    Action::ContextCommand,
};

constexpr std::array<const char *, static_cast<std::size_t>(Action::Count)> ActionConfigNames = {
    "MoveUp",
    "MoveDown",
    "MoveLeft",
    "MoveRight",
    "BuildRelay",
    "BuildArcSentry",
    "ProduceFabricator",
    "ProduceCourier",
    "ProduceLancer",
    "ProduceCoilCarrier",
    "ArcPulse",
    "GridLinkScan",
    "EmergencyOvercharge",
    "Pause",
    "OpenSettings",
    "Restart",
    "PrimarySelect",
    "ContextCommand",
};

std::size_t ToIndex(Action action)
{
    return static_cast<std::size_t>(action);
}

void AdjustBounded(std::int32_t &value, std::int32_t direction, std::int32_t step, std::int32_t minimum, std::int32_t maximum)
{
    value = std::clamp(value + (direction * step), minimum, maximum);
}

InputBinding Keyboard(std::uint16_t key)
{
    return { InputDevice::Keyboard, key };
}

InputBinding Mouse(MouseButton button)
{
    return { InputDevice::Mouse, static_cast<std::uint16_t>(button) };
}

bool IsKeyboard(InputBinding input, std::uint16_t key)
{
    return input.device == InputDevice::Keyboard && input.code == key;
}

bool ParseInteger(std::string_view text, std::int32_t &value)
{
    if (text.empty()) {
        return false;
    }
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc {} && result.ptr == end;
}

bool ParseBoolean(std::string_view text, bool &value)
{
    if (text == "1") {
        value = true;
        return true;
    }
    if (text == "0") {
        value = false;
        return true;
    }
    return false;
}

bool IsValidBinding(InputBinding input)
{
    if (input.device == InputDevice::Keyboard) {
        return input.code > 0 && input.code < 256 && input.code != KeyEnter && input.code != KeyEscape;
    }
    return input.code >= static_cast<std::uint16_t>(MouseButton::Left) &&
        input.code <= static_cast<std::uint16_t>(MouseButton::Extra2);
}

} // namespace

InterfaceState::InterfaceState()
{
    m_bindings[ToIndex(Action::MoveUp)] = Keyboard('W');
    m_bindings[ToIndex(Action::MoveDown)] = Keyboard('S');
    m_bindings[ToIndex(Action::MoveLeft)] = Keyboard('A');
    m_bindings[ToIndex(Action::MoveRight)] = Keyboard('D');
    m_bindings[ToIndex(Action::BuildRelay)] = Keyboard('B');
    // TheSuperHackers @feature koltregaskes 15/07/2026 Bind Arc Sentry construction to a collision-safe default key.
    m_bindings[ToIndex(Action::BuildArcSentry)] = Keyboard('T');
    m_bindings[ToIndex(Action::ProduceFabricator)] = Keyboard('G');
    m_bindings[ToIndex(Action::ProduceCourier)] = Keyboard('U');
    m_bindings[ToIndex(Action::ProduceLancer)] = Keyboard('I');
    m_bindings[ToIndex(Action::ProduceCoilCarrier)] = Keyboard('P');
    m_bindings[ToIndex(Action::ArcPulse)] = Keyboard('F');
    // TheSuperHackers @feature koltregaskes 15/07/2026 Bind faction scan and overcharge abilities to remappable defaults.
    m_bindings[ToIndex(Action::GridLinkScan)] = Keyboard('Q');
    m_bindings[ToIndex(Action::EmergencyOvercharge)] = Keyboard('E');
    m_bindings[ToIndex(Action::Pause)] = Keyboard(KeySpace);
    m_bindings[ToIndex(Action::OpenSettings)] = Keyboard('O');
    m_bindings[ToIndex(Action::Restart)] = Keyboard('R');
    m_bindings[ToIndex(Action::PrimarySelect)] = Mouse(MouseButton::Left);
    m_bindings[ToIndex(Action::ContextCommand)] = Mouse(MouseButton::Right);
    ResetForBoot();
}

void InterfaceState::ResetForBoot()
{
    m_screen = Screen::Briefing;
    m_settingsReturnScreen = Screen::Briefing;
    m_selectedSettingsRow = 0;
    m_capturingBinding = false;
}

void InterfaceState::OpenSettings(Screen returnScreen)
{
    m_settingsReturnScreen = returnScreen;
    m_screen = Screen::Settings;
    m_selectedSettingsRow = 0;
    m_capturingBinding = false;
}

InputEvent InterfaceState::HandleKey(std::uint16_t key)
{
    return HandleInput(Keyboard(key));
}

InputEvent InterfaceState::HandleMouseButton(MouseButton button)
{
    return HandleInput(Mouse(button));
}

InputEvent InterfaceState::HandleInput(InputBinding input)
{
    if (m_screen == Screen::Settings) {
        return HandleSettingsInput(input);
    }

    const Action action = FindAction(input);
    const bool mapped = HasAction(input);
    switch (m_screen) {
        case Screen::Briefing:
            if (IsKeyboard(input, KeyEnter)) {
                m_screen = Screen::Playing;
                return { Intent::BeginMatch, Action::MoveUp };
            }
            if (mapped && action == Action::OpenSettings) {
                OpenSettings(Screen::Briefing);
            } else if (IsKeyboard(input, KeyEscape)) {
                return { Intent::ExitRequested, Action::MoveUp };
            }
            break;
        case Screen::Playing:
            if (IsKeyboard(input, KeyEscape)) {
                m_screen = Screen::Pause;
                break;
            }
            if (!mapped) {
                break;
            }
            if (action == Action::Pause) {
                m_screen = Screen::Pause;
            } else if (action == Action::OpenSettings) {
                OpenSettings(Screen::Pause);
            } else if (action != Action::Restart) {
                return { Intent::GameplayAction, action };
            }
            break;
        case Screen::Pause:
            if (IsKeyboard(input, KeyEscape) || (mapped && action == Action::Pause)) {
                m_screen = Screen::Playing;
            } else if (mapped && action == Action::OpenSettings) {
                OpenSettings(Screen::Pause);
            } else if (mapped && action == Action::Restart) {
                m_screen = Screen::Playing;
                return { Intent::RestartMatch, action };
            }
            break;
        case Screen::Result:
            if (mapped && action == Action::Restart) {
                m_screen = Screen::Playing;
                return { Intent::RestartMatch, action };
            }
            if (mapped && action == Action::OpenSettings) {
                OpenSettings(Screen::Result);
            } else if (IsKeyboard(input, KeyEscape)) {
                return { Intent::ExitRequested, Action::MoveUp };
            }
            break;
        case Screen::Settings:
            break;
    }
    return {};
}

InputEvent InterfaceState::HandleSettingsInput(InputBinding input)
{
    if (m_capturingBinding) {
        if (IsKeyboard(input, KeyEscape)) {
            m_capturingBinding = false;
            return {};
        }
        const Action action = ActionForSettingsRow(m_selectedSettingsRow);
        m_capturingBinding = false;
        return TryRebind(action, input)
            ? InputEvent { Intent::BindingChanged, action }
            : InputEvent { Intent::BindingRejected, action };
    }

    if (IsKeyboard(input, KeyEscape)) {
        m_screen = m_settingsReturnScreen;
        return {};
    }
    if (IsKeyboard(input, KeyUp)) {
        m_selectedSettingsRow = (m_selectedSettingsRow + GetSettingsRowCount() - 1) % GetSettingsRowCount();
        return {};
    }
    if (IsKeyboard(input, KeyDown)) {
        m_selectedSettingsRow = (m_selectedSettingsRow + 1) % GetSettingsRowCount();
        return {};
    }
    if (IsKeyboard(input, KeyLeft)) {
        return AdjustSelectedSetting(-1);
    }
    if (IsKeyboard(input, KeyRight)) {
        return AdjustSelectedSetting(1);
    }
    if (IsKeyboard(input, KeyEnter)) {
        if (m_selectedSettingsRow < AdjustableSettingCount) {
            return AdjustSelectedSetting(1);
        }
        m_capturingBinding = true;
    }
    return {};
}

InputEvent InterfaceState::AdjustSelectedSetting(std::int32_t direction)
{
    if (m_selectedSettingsRow >= AdjustableSettingCount) {
        return {};
    }
    switch (m_selectedSettingsRow) {
        case 0:
            AdjustBounded(m_settings.cameraSpeedPercent, direction, 10, 50, 200);
            break;
        case 1:
            AdjustBounded(m_settings.uiScalePercent, direction, 10, 80, 150);
            break;
        case 2:
            AdjustBounded(m_settings.masterVolume, direction, 5, 0, 100);
            break;
        case 3:
            AdjustBounded(m_settings.musicVolume, direction, 5, 0, 100);
            break;
        case 4:
            AdjustBounded(m_settings.effectsVolume, direction, 5, 0, 100);
            break;
        case 5:
            m_settings.edgeScroll = !m_settings.edgeScroll;
            break;
        case 6:
            m_settings.reducedMotion = !m_settings.reducedMotion;
            break;
        case 7:
            m_settings.reducedFlashes = !m_settings.reducedFlashes;
            break;
        case 8:
            m_settings.colourIndependentCues = !m_settings.colourIndependentCues;
            break;
        default:
            return {};
    }
    return { Intent::SettingsChanged, Action::MoveUp };
}

bool InterfaceState::TryRebind(Action action, InputBinding input)
{
    if (!IsValidBinding(input)) {
        return false;
    }
    for (std::size_t index = 0; index < m_bindings.size(); ++index) {
        if (index != ToIndex(action) && m_bindings[index] == input) {
            return false;
        }
    }
    m_bindings[ToIndex(action)] = input;
    return true;
}

void InterfaceState::SyncOutcome(MatchOutcome outcome)
{
    if (outcome != MatchOutcome::InProgress && m_screen != Screen::Result && m_screen != Screen::Settings) {
        m_screen = Screen::Result;
        m_capturingBinding = false;
    }
}

InputBinding InterfaceState::BindingFor(Action action) const
{
    return m_bindings[ToIndex(action)];
}

Action InterfaceState::ActionForSettingsRow(std::int32_t row) const
{
    const std::int32_t actionRow = std::clamp(row - AdjustableSettingCount, 0, RemappableActionCount - 1);
    return RemappableActions[static_cast<std::size_t>(actionRow)];
}

bool InterfaceState::IsActionPressed(
    Action action,
    const bool *keyStates,
    std::size_t keyStateCount,
    const bool *mouseStates,
    std::size_t mouseStateCount) const
{
    const InputBinding binding = BindingFor(action);
    if (binding.device == InputDevice::Keyboard) {
        return keyStates && binding.code < keyStateCount && keyStates[binding.code];
    }
    return mouseStates && binding.code < mouseStateCount && mouseStates[binding.code];
}

Action InterfaceState::FindAction(InputBinding input) const
{
    for (std::size_t index = 0; index < m_bindings.size(); ++index) {
        if (m_bindings[index] == input) {
            return static_cast<Action>(index);
        }
    }
    return Action::MoveUp;
}

bool InterfaceState::HasAction(InputBinding input) const
{
    return std::find(m_bindings.begin(), m_bindings.end(), input) != m_bindings.end();
}

std::string InterfaceState::SerializeConfiguration() const
{
    std::ostringstream output;
    output << "project_tempest_settings=3\n"
           << "camera_speed_percent=" << m_settings.cameraSpeedPercent << '\n'
           << "ui_scale_percent=" << m_settings.uiScalePercent << '\n'
           << "master_volume=" << m_settings.masterVolume << '\n'
           << "music_volume=" << m_settings.musicVolume << '\n'
           << "effects_volume=" << m_settings.effectsVolume << '\n'
           << "edge_scroll=" << (m_settings.edgeScroll ? 1 : 0) << '\n'
           << "reduced_motion=" << (m_settings.reducedMotion ? 1 : 0) << '\n'
           << "reduced_flashes=" << (m_settings.reducedFlashes ? 1 : 0) << '\n'
           << "colour_independent_cues=" << (m_settings.colourIndependentCues ? 1 : 0) << '\n';
    for (std::size_t index = 0; index < m_bindings.size(); ++index) {
        const InputBinding binding = m_bindings[index];
        output << "binding." << ActionConfigNames[index] << '='
               << (binding.device == InputDevice::Keyboard ? "keyboard:" : "mouse:")
               << binding.code << '\n';
    }
    return output.str();
}

bool InterfaceState::LoadConfiguration(std::string_view content)
{
    Settings settings;
    std::array<InputBinding, static_cast<std::size_t>(Action::Count)> bindings = m_bindings;
    std::array<bool, AdjustableSettingCount> settingSeen {};
    std::array<bool, static_cast<std::size_t>(Action::Count)> bindingSeen {};
    bool versionSeen = false;
    std::int32_t configurationVersion = 0;

    std::size_t lineStart = 0;
    while (lineStart <= content.size()) {
        const std::size_t lineEnd = content.find('\n', lineStart);
        std::string_view line = content.substr(
            lineStart,
            lineEnd == std::string_view::npos ? content.size() - lineStart : lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lineStart = lineEnd == std::string_view::npos ? content.size() + 1 : lineEnd + 1;
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos) {
            return false;
        }
        const std::string_view key = line.substr(0, separator);
        const std::string_view value = line.substr(separator + 1);
        if (key == "project_tempest_settings") {
            if (versionSeen || !ParseInteger(value, configurationVersion) ||
                configurationVersion < 1 || configurationVersion > 3) {
                return false;
            }
            versionSeen = true;
            continue;
        }

        std::int32_t number = 0;
        bool boolean = false;
        std::size_t settingIndex = AdjustableSettingCount;
        if (key == "camera_speed_percent") {
            settingIndex = 0;
            if (!ParseInteger(value, number) || number < 50 || number > 200) return false;
            settings.cameraSpeedPercent = number;
        } else if (key == "ui_scale_percent") {
            settingIndex = 1;
            if (!ParseInteger(value, number) || number < 80 || number > 150) return false;
            settings.uiScalePercent = number;
        } else if (key == "master_volume") {
            settingIndex = 2;
            if (!ParseInteger(value, number) || number < 0 || number > 100) return false;
            settings.masterVolume = number;
        } else if (key == "music_volume") {
            settingIndex = 3;
            if (!ParseInteger(value, number) || number < 0 || number > 100) return false;
            settings.musicVolume = number;
        } else if (key == "effects_volume") {
            settingIndex = 4;
            if (!ParseInteger(value, number) || number < 0 || number > 100) return false;
            settings.effectsVolume = number;
        } else if (key == "edge_scroll") {
            settingIndex = 5;
            if (!ParseBoolean(value, boolean)) return false;
            settings.edgeScroll = boolean;
        } else if (key == "reduced_motion") {
            settingIndex = 6;
            if (!ParseBoolean(value, boolean)) return false;
            settings.reducedMotion = boolean;
        } else if (key == "reduced_flashes") {
            settingIndex = 7;
            if (!ParseBoolean(value, boolean)) return false;
            settings.reducedFlashes = boolean;
        } else if (key == "colour_independent_cues") {
            settingIndex = 8;
            if (!ParseBoolean(value, boolean)) return false;
            settings.colourIndependentCues = boolean;
        }
        if (settingIndex < settingSeen.size()) {
            if (settingSeen[settingIndex]) {
                return false;
            }
            settingSeen[settingIndex] = true;
            continue;
        }

        if (!key.starts_with("binding.")) {
            continue;
        }
        const std::string_view actionName = key.substr(8);
        std::size_t actionIndex = ActionConfigNames.size();
        for (std::size_t index = 0; index < ActionConfigNames.size(); ++index) {
            if (actionName == ActionConfigNames[index]) {
                actionIndex = index;
                break;
            }
        }
        if (actionIndex == ActionConfigNames.size() || bindingSeen[actionIndex]) {
            return false;
        }
        const std::size_t deviceSeparator = value.find(':');
        if (deviceSeparator == std::string_view::npos || !ParseInteger(value.substr(deviceSeparator + 1), number) ||
            number < 0 || number > 65535) {
            return false;
        }
        InputBinding binding;
        const std::string_view device = value.substr(0, deviceSeparator);
        if (device == "keyboard") {
            binding.device = InputDevice::Keyboard;
        } else if (device == "mouse") {
            binding.device = InputDevice::Mouse;
        } else {
            return false;
        }
        binding.code = static_cast<std::uint16_t>(number);
        if (!IsValidBinding(binding)) {
            return false;
        }
        bindings[actionIndex] = binding;
        bindingSeen[actionIndex] = true;
    }

    if (configurationVersion < 3) {
        constexpr std::array<std::uint16_t, 21> MigrationFallbackKeys {
            'G', 'I', 'P', 'T', 'Q', 'E', 'J', 'L', 'Y', 'H', 'N', 'M',
            'V', 'C', 'Z', 'X', '1', '2', '3', '4', '5'
        };
        const auto migrateBinding = [&](Action action) {
            const std::size_t actionIndex = ToIndex(action);
            if (bindingSeen[actionIndex]) {
                return true;
            }
            const auto isUsed = [&](InputBinding candidate) {
                for (std::size_t index = 0; index < bindings.size(); ++index) {
                    if (index != actionIndex && bindingSeen[index] && bindings[index] == candidate) {
                        return true;
                    }
                }
                return false;
            };
            if (isUsed(bindings[actionIndex])) {
                bool assigned = false;
                for (const std::uint16_t key : MigrationFallbackKeys) {
                    const InputBinding candidate = Keyboard(key);
                    if (!isUsed(candidate)) {
                        bindings[actionIndex] = candidate;
                        assigned = true;
                        break;
                    }
                }
                if (!assigned) {
                    return false;
                }
            }
            bindingSeen[actionIndex] = true;
            return true;
        };
        if ((configurationVersion == 1 &&
                (!migrateBinding(Action::ProduceFabricator) ||
                    !migrateBinding(Action::ProduceLancer) ||
                    !migrateBinding(Action::ProduceCoilCarrier))) ||
            !migrateBinding(Action::BuildArcSentry) ||
            !migrateBinding(Action::GridLinkScan) ||
            !migrateBinding(Action::EmergencyOvercharge)) {
            return false;
        }
    }
    if (!versionSeen || std::find(settingSeen.begin(), settingSeen.end(), false) != settingSeen.end() ||
        std::find(bindingSeen.begin(), bindingSeen.end(), false) != bindingSeen.end()) {
        return false;
    }
    for (std::size_t left = 0; left < bindings.size(); ++left) {
        for (std::size_t right = left + 1; right < bindings.size(); ++right) {
            if (bindings[left] == bindings[right]) {
                return false;
            }
        }
    }

    m_settings = settings;
    m_bindings = bindings;
    return true;
}

const char *InterfaceState::ActionName(Action action)
{
    switch (action) {
        case Action::MoveUp: return "Pan camera up";
        case Action::MoveDown: return "Pan camera down";
        case Action::MoveLeft: return "Pan camera left";
        case Action::MoveRight: return "Pan camera right";
        case Action::BuildRelay: return "Restore grid relay";
        case Action::BuildArcSentry: return "Build Arc Sentry";
        case Action::ProduceFabricator: return "Produce Fabricator rig";
        case Action::ProduceCourier: return "Produce Courier scout";
        case Action::ProduceLancer: return "Produce Lancer crew";
        case Action::ProduceCoilCarrier: return "Produce Coil carrier";
        case Action::ArcPulse: return "Arc Pulse";
        case Action::GridLinkScan: return "Grid-link scan";
        case Action::EmergencyOvercharge: return "Emergency overcharge";
        case Action::Pause: return "Pause";
        case Action::OpenSettings: return "Settings";
        case Action::Restart: return "Restart match";
        case Action::PrimarySelect: return "Primary select";
        case Action::ContextCommand: return "Context command";
        case Action::Count: break;
    }
    return "Unknown action";
}

const char *InterfaceState::ScreenName(Screen screen)
{
    switch (screen) {
        case Screen::Briefing: return "Briefing";
        case Screen::Playing: return "Playing";
        case Screen::Pause: return "Paused";
        case Screen::Settings: return "Settings";
        case Screen::Result: return "Result";
    }
    return "Unknown";
}

} // namespace Tempest::Ui
