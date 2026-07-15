#pragma once

#include "TempestSimulation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Tempest::Ui {

constexpr std::uint16_t KeyEnter = 0x0D;
constexpr std::uint16_t KeyEscape = 0x1B;
constexpr std::uint16_t KeySpace = 0x20;
constexpr std::uint16_t KeyLeft = 0x25;
constexpr std::uint16_t KeyUp = 0x26;
constexpr std::uint16_t KeyRight = 0x27;
constexpr std::uint16_t KeyDown = 0x28;

enum class Screen : std::uint8_t {
    Briefing,
    Playing,
    Pause,
    Settings,
    Result,
};

enum class Action : std::uint8_t {
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    BuildRelay,
    ProduceCourier,
    ArcPulse,
    Pause,
    OpenSettings,
    Restart,
    Count,
};

enum class Intent : std::uint8_t {
    None,
    BeginMatch,
    GameplayAction,
    RestartMatch,
    ExitRequested,
    SettingsChanged,
    BindingChanged,
    BindingRejected,
};

struct InputEvent {
    Intent intent = Intent::None;
    Action action = Action::MoveUp;
};

struct Settings {
    std::int32_t cameraSpeedPercent = 100;
    std::int32_t uiScalePercent = 100;
    std::int32_t masterVolume = 80;
    std::int32_t musicVolume = 65;
    std::int32_t effectsVolume = 85;
    bool edgeScroll = true;
    bool reducedMotion = false;
    bool reducedFlashes = true;
    bool colourIndependentCues = true;
};

class InterfaceState {
public:
    static constexpr std::int32_t AdjustableSettingCount = 9;
    static constexpr std::int32_t RemappableActionCount = static_cast<std::int32_t>(Action::Count);

    InterfaceState();

    void ResetForBoot();
    InputEvent HandleKey(std::uint16_t key);
    void SyncOutcome(MatchOutcome outcome);

    Screen GetScreen() const { return m_screen; }
    Screen GetSettingsReturnScreen() const { return m_settingsReturnScreen; }
    bool AllowsGameplayInput() const { return m_screen == Screen::Playing; }
    bool AdvancesSimulation() const { return m_screen == Screen::Playing; }
    bool IsCapturingBinding() const { return m_capturingBinding; }
    std::int32_t GetSelectedSettingsRow() const { return m_selectedSettingsRow; }
    std::int32_t GetSettingsRowCount() const { return AdjustableSettingCount + RemappableActionCount; }
    const Settings &GetSettings() const { return m_settings; }

    std::uint16_t KeyFor(Action action) const;
    Action ActionForSettingsRow(std::int32_t row) const;
    bool IsActionPressed(Action action, const bool *keyStates, std::size_t keyStateCount) const;

    static const char *ActionName(Action action);
    static const char *ScreenName(Screen screen);

private:
    Screen m_screen = Screen::Briefing;
    Screen m_settingsReturnScreen = Screen::Briefing;
    Settings m_settings;
    std::array<std::uint16_t, static_cast<std::size_t>(Action::Count)> m_bindings {};
    std::int32_t m_selectedSettingsRow = 0;
    bool m_capturingBinding = false;

    Action FindAction(std::uint16_t key) const;
    bool HasAction(std::uint16_t key) const;
    InputEvent HandleSettingsKey(std::uint16_t key);
    InputEvent AdjustSelectedSetting(std::int32_t direction);
    bool TryRebind(Action action, std::uint16_t key);
    void OpenSettings(Screen returnScreen);
};

} // namespace Tempest::Ui
