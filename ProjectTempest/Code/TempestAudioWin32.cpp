#include "TempestAudio.h"

#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Tempest::Audio {
namespace {

constexpr std::array<const wchar_t *, static_cast<std::size_t>(Cue::Count)> kCueFiles = {
    L"pt_ui_confirm.wav",
    L"pt_select.wav",
    L"pt_command.wav",
    L"pt_arc_pulse.wav",
    L"pt_alert.wav",
};
constexpr std::array<const wchar_t *, 3> kMusicFiles = {
    L"pt_music_substation.wav",
    L"pt_music_pressure.wav",
    L"pt_music_crisis.wav",
};
constexpr UINT32 kMusicStartOperationSet = 1;

std::string HResultMessage(const char *operation, HRESULT result)
{
    std::ostringstream message;
    message << operation << " failed with HRESULT 0x"
            << std::hex << std::uppercase << static_cast<unsigned long>(result);
    return message.str();
}

WAVEFORMATEX MakeFormat(const WaveAsset &asset)
{
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = asset.channels;
    format.nSamplesPerSec = asset.sampleRate;
    format.wBitsPerSample = asset.bitsPerSample;
    format.nBlockAlign = static_cast<WORD>(asset.channels * asset.bitsPerSample / 8U);
    format.nAvgBytesPerSec = asset.sampleRate * format.nBlockAlign;
    return format;
}

} // namespace

struct AudioSystem::Impl {
    IXAudio2 *engine = nullptr;
    IXAudio2MasteringVoice *masteringVoice = nullptr;
    IXAudio2SubmixVoice *musicBus = nullptr;
    IXAudio2SubmixVoice *effectsBus = nullptr;
    std::array<IXAudio2SourceVoice *, kMusicFiles.size()> musicVoices = {};
    std::array<IXAudio2SourceVoice *, static_cast<std::size_t>(Cue::Count)> cueVoices = {};
    std::array<WaveAsset, kMusicFiles.size()> musicLayers;
    std::array<WaveAsset, static_cast<std::size_t>(Cue::Count)> cues;
    std::int32_t masterPercent = 80;
    std::int32_t musicPercent = 65;
    std::int32_t effectsPercent = 85;
    float musicPressure = 0.0F;
    float appliedMusicPressure = -1.0F;
    bool musicStarted = false;
    bool suspended = false;
    bool comReference = false;
};

AudioSystem::AudioSystem() : m_impl(std::make_unique<Impl>())
{
}

AudioSystem::~AudioSystem()
{
    Shutdown();
}

bool AudioSystem::Initialize(const std::filesystem::path &assetDirectory, std::string &error)
{
    Shutdown();
    for (std::size_t index = 0; index < kMusicFiles.size(); ++index) {
        if (!LoadPcmWave(assetDirectory / kMusicFiles[index], m_impl->musicLayers[index], error)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < kCueFiles.size(); ++index) {
        if (!LoadPcmWave(assetDirectory / kCueFiles[index], m_impl->cues[index], error)) {
            return false;
        }
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(comResult)) {
        m_impl->comReference = true;
    } else if (comResult != RPC_E_CHANGED_MODE) {
        error = HResultMessage("CoInitializeEx", comResult);
        return false;
    }

    HRESULT result = XAudio2Create(&m_impl->engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(result)) {
        error = HResultMessage("XAudio2Create", result);
        Shutdown();
        return false;
    }
    result = m_impl->engine->CreateMasteringVoice(&m_impl->masteringVoice);
    if (FAILED(result)) {
        error = HResultMessage("CreateMasteringVoice", result);
        Shutdown();
        return false;
    }
    result = m_impl->engine->CreateSubmixVoice(&m_impl->musicBus, 2, 48'000);
    if (FAILED(result)) {
        error = HResultMessage("CreateSubmixVoice(music)", result);
        Shutdown();
        return false;
    }
    result = m_impl->engine->CreateSubmixVoice(&m_impl->effectsBus, 2, 48'000);
    if (FAILED(result)) {
        error = HResultMessage("CreateSubmixVoice(effects)", result);
        Shutdown();
        return false;
    }

    XAUDIO2_SEND_DESCRIPTOR musicSend = { 0, m_impl->musicBus };
    XAUDIO2_VOICE_SENDS musicSends = { 1, &musicSend };
    for (std::size_t index = 0; index < m_impl->musicLayers.size(); ++index) {
        WAVEFORMATEX musicFormat = MakeFormat(m_impl->musicLayers[index]);
        result = m_impl->engine->CreateSourceVoice(
            &m_impl->musicVoices[index],
            &musicFormat,
            0,
            XAUDIO2_DEFAULT_FREQ_RATIO,
            nullptr,
            &musicSends);
        if (FAILED(result)) {
            error = HResultMessage("CreateSourceVoice(music)", result);
            Shutdown();
            return false;
        }
    }

    XAUDIO2_SEND_DESCRIPTOR effectsSend = { 0, m_impl->effectsBus };
    XAUDIO2_VOICE_SENDS effectsSends = { 1, &effectsSend };
    for (std::size_t index = 0; index < m_impl->cues.size(); ++index) {
        WAVEFORMATEX cueFormat = MakeFormat(m_impl->cues[index]);
        result = m_impl->engine->CreateSourceVoice(
            &m_impl->cueVoices[index],
            &cueFormat,
            0,
            XAUDIO2_DEFAULT_FREQ_RATIO,
            nullptr,
            &effectsSends);
        if (FAILED(result)) {
            error = HResultMessage("CreateSourceVoice(effect)", result);
            Shutdown();
            return false;
        }
    }
    result = m_impl->engine->StartEngine();
    if (FAILED(result)) {
        error = HResultMessage("StartEngine", result);
        Shutdown();
        return false;
    }
    SetVolumes(m_impl->masterPercent, m_impl->musicPercent, m_impl->effectsPercent);
    SetMusicPressure(m_impl->musicPressure);
    error.clear();
    return true;
}

void AudioSystem::SetVolumes(
    std::int32_t masterPercent,
    std::int32_t musicPercent,
    std::int32_t effectsPercent)
{
    m_impl->masterPercent = std::clamp(masterPercent, 0, 100);
    m_impl->musicPercent = std::clamp(musicPercent, 0, 100);
    m_impl->effectsPercent = std::clamp(effectsPercent, 0, 100);
    if (m_impl->musicBus) {
        m_impl->musicBus->SetVolume(VolumeGain(m_impl->masterPercent, m_impl->musicPercent));
    }
    if (m_impl->effectsBus) {
        m_impl->effectsBus->SetVolume(VolumeGain(m_impl->masterPercent, m_impl->effectsPercent));
    }
}

void AudioSystem::StartMusic()
{
    if (!IsReady() || m_impl->musicStarted) {
        return;
    }
    std::array<XAUDIO2_BUFFER, kMusicFiles.size()> buffers = {};
    for (std::size_t index = 0; index < buffers.size(); ++index) {
        buffers[index].Flags = XAUDIO2_END_OF_STREAM;
        buffers[index].AudioBytes = static_cast<UINT32>(m_impl->musicLayers[index].pcm.size());
        buffers[index].pAudioData = m_impl->musicLayers[index].pcm.data();
        buffers[index].LoopCount = XAUDIO2_LOOP_INFINITE;
        if (FAILED(m_impl->musicVoices[index]->SubmitSourceBuffer(&buffers[index]))) {
            for (IXAudio2SourceVoice *voice : m_impl->musicVoices) {
                voice->FlushSourceBuffers();
            }
            return;
        }
    }
    for (IXAudio2SourceVoice *voice : m_impl->musicVoices) {
        if (FAILED(voice->Start(0, kMusicStartOperationSet))) {
            m_impl->engine->CommitChanges(kMusicStartOperationSet);
            for (IXAudio2SourceVoice *startedVoice : m_impl->musicVoices) {
                startedVoice->Stop(0);
                startedVoice->FlushSourceBuffers();
            }
            return;
        }
    }
    if (FAILED(m_impl->engine->CommitChanges(kMusicStartOperationSet))) {
        for (IXAudio2SourceVoice *voice : m_impl->musicVoices) {
            voice->Stop(0);
            voice->FlushSourceBuffers();
        }
        return;
    }
    m_impl->musicStarted = true;
}

void AudioSystem::SetMusicPressure(float pressure)
{
    const float requestedPressure = std::clamp(pressure, 0.0F, 1.0F);
    m_impl->musicPressure = requestedPressure;
    if (m_impl->appliedMusicPressure >= 0.0F &&
        std::abs(requestedPressure - m_impl->appliedMusicPressure) < 0.005F) {
        return;
    }
    const MusicLayerGains gains = MusicGainsForPressure(m_impl->musicPressure);
    const std::array<float, kMusicFiles.size()> values = { gains.base, gains.pressure, gains.crisis };
    for (std::size_t index = 0; index < m_impl->musicVoices.size(); ++index) {
        if (m_impl->musicVoices[index]) {
            m_impl->musicVoices[index]->SetVolume(values[index]);
        }
    }
    m_impl->appliedMusicPressure = requestedPressure;
}

void AudioSystem::SetSuspended(bool suspended)
{
    if (!m_impl->engine || suspended == m_impl->suspended) {
        return;
    }
    if (suspended) {
        m_impl->engine->StopEngine();
        m_impl->suspended = true;
    } else if (SUCCEEDED(m_impl->engine->StartEngine())) {
        m_impl->suspended = false;
    }
}

void AudioSystem::Play(Cue cue)
{
    const std::size_t index = static_cast<std::size_t>(cue);
    if (!IsReady() || index >= m_impl->cueVoices.size()) {
        return;
    }
    IXAudio2SourceVoice *voice = m_impl->cueVoices[index];
    voice->Stop(0);
    voice->FlushSourceBuffers();
    XAUDIO2_BUFFER buffer = {};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(m_impl->cues[index].pcm.size());
    buffer.pAudioData = m_impl->cues[index].pcm.data();
    if (SUCCEEDED(voice->SubmitSourceBuffer(&buffer))) {
        voice->Start(0);
    }
}

void AudioSystem::Shutdown()
{
    if (!m_impl) {
        return;
    }
    for (IXAudio2SourceVoice *&voice : m_impl->musicVoices) {
        if (voice) {
            voice->Stop(0);
            voice->DestroyVoice();
            voice = nullptr;
        }
    }
    for (IXAudio2SourceVoice *&voice : m_impl->cueVoices) {
        if (voice) {
            voice->Stop(0);
            voice->DestroyVoice();
            voice = nullptr;
        }
    }
    if (m_impl->effectsBus) {
        m_impl->effectsBus->DestroyVoice();
        m_impl->effectsBus = nullptr;
    }
    if (m_impl->musicBus) {
        m_impl->musicBus->DestroyVoice();
        m_impl->musicBus = nullptr;
    }
    if (m_impl->masteringVoice) {
        m_impl->masteringVoice->DestroyVoice();
        m_impl->masteringVoice = nullptr;
    }
    if (m_impl->engine) {
        m_impl->engine->StopEngine();
        m_impl->engine->Release();
        m_impl->engine = nullptr;
    }
    if (m_impl->comReference) {
        CoUninitialize();
        m_impl->comReference = false;
    }
    m_impl->musicStarted = false;
    m_impl->suspended = false;
    m_impl->appliedMusicPressure = -1.0F;
    m_impl->musicLayers = {};
    m_impl->cues = {};
}

bool AudioSystem::IsReady() const
{
    return m_impl && m_impl->engine && m_impl->masteringVoice &&
        m_impl->musicBus && m_impl->effectsBus &&
        std::all_of(m_impl->musicVoices.begin(), m_impl->musicVoices.end(), [](const auto *voice) { return voice; }) &&
        std::all_of(m_impl->cueVoices.begin(), m_impl->cueVoices.end(), [](const auto *voice) { return voice; });
}

} // namespace Tempest::Audio
