#include "TempestInterface.h"

#include <algorithm>

namespace Tempest::Ui {
namespace {

constexpr std::array<Action, InterfaceState::RemappableActionCount> RemappableActions = {
    Action::MoveUp,
    Action::MoveDown,
    Action::MoveLeft,
    Action::MoveRight,
    Action::BuildRelay,
    Action::ProduceCourier,
    Action::ArcPulse,
    Action::Pause,
    Action::OpenSettings,
    Action::Restart,
};

std::size_t ToIndex(Action action)
{
    return static_cast<std::size_t>(action);
}

void AdjustBounded(std::int32_t &value, std::int32_t direction, std::int32_t step, std::int32_t minimum, std::int32_t maximum)
{
    value = std::clamp(value + (direction * step), minimum, maximum);
}

} // namespace

InterfaceState::InterfaceState()
{
    m_bindings[ToIndex(Action::MoveUp)] = 'W';
    m_bindings[ToIndex(Action::MoveDown)] = 'S';
    m_bindings[ToIndex(Action::MoveLeft)] = 'A';
    m_bindings[ToIndex(Action::MoveRight)] = 'D';
    m_bindings[ToIndex(Action::BuildRelay)] = 'B';
    m_bindings[ToIndex(Action::ProduceCourier)] = 'U';
    m_bindings[ToIndex(Action::ArcPulse)] = 'F';
    m_bindings[ToIndex(Action::Pause)] = KeySpace;
    m_bindings[ToIndex(Action::OpenSettings)] = 'O';
    m_bindings[ToIndex(Action::Restart)] = 'R';
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
    if (m_screen == Screen::Settings) {
        return HandleSettingsKey(key);
    }

    const Action action = FindAction(key);
    const bool mapped = HasAction(key);
    switch (m_screen) {
        case Screen::Briefing:
            if (key == KeyEnter) {
                m_screen = Screen::Playing;
                return { Intent::BeginMatch, Action::MoveUp };
            }
            if (mapped && action == Action::OpenSettings) {
                OpenSettings(Screen::Briefing);
            } else if (key == KeyEscape) {
                return { Intent::ExitRequested, Action::MoveUp };
            }
            break;
        case Screen::Playing:
            if (key == KeyEscape) {
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
            if (key == KeyEscape || (mapped && action == Action::Pause)) {
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
            } else if (key == KeyEscape) {
                return { Intent::ExitRequested, Action::MoveUp };
            }
            break;
        case Screen::Settings:
            break;
    }
    return {};
}

InputEvent InterfaceState::HandleSettingsKey(std::uint16_t key)
{
    if (m_capturingBinding) {
        if (key == KeyEscape) {
            m_capturingBinding = false;
            return {};
        }
        const Action action = ActionForSettingsRow(m_selectedSettingsRow);
        m_capturingBinding = false;
        return TryRebind(action, key)
            ? InputEvent { Intent::BindingChanged, action }
            : InputEvent { Intent::BindingRejected, action };
    }

    if (key == KeyEscape) {
        m_screen = m_settingsReturnScreen;
        return {};
    }
    if (key == KeyUp) {
        m_selectedSettingsRow = (m_selectedSettingsRow + GetSettingsRowCount() - 1) % GetSettingsRowCount();
        return {};
    }
    if (key == KeyDown) {
        m_selectedSettingsRow = (m_selectedSettingsRow + 1) % GetSettingsRowCount();
        return {};
    }
    if (key == KeyLeft) {
        return AdjustSelectedSetting(-1);
    }
    if (key == KeyRight) {
        return AdjustSelectedSetting(1);
    }
    if (key == KeyEnter) {
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

bool InterfaceState::TryRebind(Action action, std::uint16_t key)
{
    if (key == 0 || key == KeyEnter || key == KeyEscape) {
        return false;
    }
    for (std::size_t index = 0; index < m_bindings.size(); ++index) {
        if (index != ToIndex(action) && m_bindings[index] == key) {
            return false;
        }
    }
    m_bindings[ToIndex(action)] = key;
    return true;
}

void InterfaceState::SyncOutcome(MatchOutcome outcome)
{
    if (outcome != MatchOutcome::InProgress && m_screen != Screen::Result && m_screen != Screen::Settings) {
        m_screen = Screen::Result;
        m_capturingBinding = false;
    }
}

std::uint16_t InterfaceState::KeyFor(Action action) const
{
    return m_bindings[ToIndex(action)];
}

Action InterfaceState::ActionForSettingsRow(std::int32_t row) const
{
    const std::int32_t actionRow = std::clamp(row - AdjustableSettingCount, 0, RemappableActionCount - 1);
    return RemappableActions[static_cast<std::size_t>(actionRow)];
}

bool InterfaceState::IsActionPressed(Action action, const bool *keyStates, std::size_t keyStateCount) const
{
    const std::uint16_t key = KeyFor(action);
    return keyStates && key < keyStateCount && keyStates[key];
}

Action InterfaceState::FindAction(std::uint16_t key) const
{
    for (std::size_t index = 0; index < m_bindings.size(); ++index) {
        if (m_bindings[index] == key) {
            return static_cast<Action>(index);
        }
    }
    return Action::MoveUp;
}

bool InterfaceState::HasAction(std::uint16_t key) const
{
    return std::find(m_bindings.begin(), m_bindings.end(), key) != m_bindings.end();
}

const char *InterfaceState::ActionName(Action action)
{
    switch (action) {
        case Action::MoveUp: return "Pan camera up";
        case Action::MoveDown: return "Pan camera down";
        case Action::MoveLeft: return "Pan camera left";
        case Action::MoveRight: return "Pan camera right";
        case Action::BuildRelay: return "Build grid relay";
        case Action::ProduceCourier: return "Produce Courier";
        case Action::ArcPulse: return "Arc Pulse";
        case Action::Pause: return "Pause";
        case Action::OpenSettings: return "Settings";
        case Action::Restart: return "Restart match";
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
