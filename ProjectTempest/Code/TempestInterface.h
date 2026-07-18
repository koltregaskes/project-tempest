#pragma once

#include "TempestSimulation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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
    // TheSuperHackers @feature koltregaskes 15/07/2026 Add remappable Arc Sentry construction input.
    BuildArcSentry,
    ProduceFabricator,
    ProduceCourier,
    ProduceLancer,
    ProduceCoilCarrier,
    ArcPulse,
    // TheSuperHackers @feature koltregaskes 15/07/2026 Add remappable Grid Link Scan and Emergency Overcharge input.
    GridLinkScan,
    EmergencyOvercharge,
    Pause,
    OpenSettings,
    Restart,
    PrimarySelect,
    ContextCommand,
    Count,
};

enum class InputDevice : std::uint8_t {
    Keyboard,
    Mouse,
};

enum class MouseButton : std::uint16_t {
    Left = 1,
    Right = 2,
    Middle = 3,
    Extra1 = 4,
    Extra2 = 5,
};

struct InputBinding {
    InputDevice device = InputDevice::Keyboard;
    std::uint16_t code = 0;

    bool operator==(const InputBinding &) const = default;
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
    InputEvent HandleMouseButton(MouseButton button);
    void SyncOutcome(MatchOutcome outcome);

    Screen GetScreen() const { return m_screen; }
    Screen GetSettingsReturnScreen() const { return m_settingsReturnScreen; }
    bool AllowsGameplayInput() const { return m_screen == Screen::Playing; }
    bool AdvancesSimulation() const { return m_screen == Screen::Playing; }
    bool IsCapturingBinding() const { return m_capturingBinding; }
    std::int32_t GetSelectedSettingsRow() const { return m_selectedSettingsRow; }
    std::int32_t GetSettingsRowCount() const { return AdjustableSettingCount + RemappableActionCount; }
    const Settings &GetSettings() const { return m_settings; }

    InputBinding BindingFor(Action action) const;
    Action ActionForSettingsRow(std::int32_t row) const;
    bool IsActionPressed(
        Action action,
        const bool *keyStates,
        std::size_t keyStateCount,
        const bool *mouseStates,
        std::size_t mouseStateCount) const;

    std::string SerializeConfiguration() const;
    bool LoadConfiguration(std::string_view content);

    static const char *ActionName(Action action);
    static const char *ScreenName(Screen screen);

private:
    Screen m_screen = Screen::Briefing;
    Screen m_settingsReturnScreen = Screen::Briefing;
    Settings m_settings;
    std::array<InputBinding, static_cast<std::size_t>(Action::Count)> m_bindings {};
    std::int32_t m_selectedSettingsRow = 0;
    bool m_capturingBinding = false;

    InputEvent HandleInput(InputBinding input);
    Action FindAction(InputBinding input) const;
    bool HasAction(InputBinding input) const;
    InputEvent HandleSettingsInput(InputBinding input);
    InputEvent AdjustSelectedSetting(std::int32_t direction);
    bool TryRebind(Action action, InputBinding input);
    void OpenSettings(Screen returnScreen);
};

} // namespace Tempest::Ui
