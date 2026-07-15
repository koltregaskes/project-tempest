#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Tempest::Audio {

enum class Cue : std::uint8_t {
    UiConfirm,
    Select,
    Command,
    ArcPulse,
    Alert,
    Count,
};

struct WaveAsset {
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::vector<std::uint8_t> pcm;
};

struct MusicLayerGains {
    float base = 1.0F;
    float pressure = 0.0F;
    float crisis = 0.0F;
};

bool ParsePcmWave(
    const std::vector<std::uint8_t> &bytes,
    WaveAsset &asset,
    std::string &error);
bool LoadPcmWave(
    const std::filesystem::path &path,
    WaveAsset &asset,
    std::string &error);
float VolumeGain(std::int32_t masterPercent, std::int32_t channelPercent);
MusicLayerGains MusicGainsForPressure(float pressure);

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    AudioSystem(const AudioSystem &) = delete;
    AudioSystem &operator=(const AudioSystem &) = delete;

    bool Initialize(const std::filesystem::path &assetDirectory, std::string &error);
    void SetVolumes(
        std::int32_t masterPercent,
        std::int32_t musicPercent,
        std::int32_t effectsPercent);
    void StartMusic();
    void SetMusicPressure(float pressure);
    void SetSuspended(bool suspended);
    void Play(Cue cue);
    void Shutdown();
    bool IsReady() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Tempest::Audio
