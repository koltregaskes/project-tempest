#include "TempestAudio.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>

namespace Tempest::Audio {
namespace {

std::uint16_t ReadU16(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::uint32_t ReadU32(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

bool HasTag(const std::vector<std::uint8_t> &bytes, std::size_t offset, const char *tag)
{
    return bytes[offset] == static_cast<std::uint8_t>(tag[0]) &&
        bytes[offset + 1] == static_cast<std::uint8_t>(tag[1]) &&
        bytes[offset + 2] == static_cast<std::uint8_t>(tag[2]) &&
        bytes[offset + 3] == static_cast<std::uint8_t>(tag[3]);
}

} // namespace

bool ParsePcmWave(
    const std::vector<std::uint8_t> &bytes,
    WaveAsset &asset,
    std::string &error)
{
    WaveAsset parsed;
    if (bytes.size() < 12 || !HasTag(bytes, 0, "RIFF") || !HasTag(bytes, 8, "WAVE")) {
        error = "missing RIFF/WAVE header";
        return false;
    }
    const std::uint32_t riffSize = ReadU32(bytes, 4);
    if (riffSize > bytes.size() - 8 || static_cast<std::size_t>(riffSize) + 8 != bytes.size()) {
        error = "RIFF size does not match the file";
        return false;
    }

    bool foundFormat = false;
    bool foundData = false;
    std::size_t offset = 12;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 8) {
            error = "truncated WAV chunk header";
            return false;
        }
        const std::uint32_t chunkSize = ReadU32(bytes, offset + 4);
        const std::size_t dataOffset = offset + 8;
        if (chunkSize > bytes.size() - dataOffset) {
            error = "WAV chunk extends past the file";
            return false;
        }

        if (HasTag(bytes, offset, "fmt ")) {
            if (foundFormat || chunkSize < 16) {
                error = foundFormat ? "duplicate fmt chunk" : "fmt chunk is too small";
                return false;
            }
            const std::uint16_t format = ReadU16(bytes, dataOffset);
            parsed.channels = ReadU16(bytes, dataOffset + 2);
            parsed.sampleRate = ReadU32(bytes, dataOffset + 4);
            const std::uint32_t byteRate = ReadU32(bytes, dataOffset + 8);
            const std::uint16_t blockAlign = ReadU16(bytes, dataOffset + 12);
            parsed.bitsPerSample = ReadU16(bytes, dataOffset + 14);
            const std::uint32_t expectedBlockAlign =
                static_cast<std::uint32_t>(parsed.channels) * parsed.bitsPerSample / 8U;
            if (format != 1 || parsed.channels != 2 || parsed.sampleRate != 48'000 ||
                parsed.bitsPerSample != 16 || blockAlign != expectedBlockAlign ||
                byteRate != parsed.sampleRate * expectedBlockAlign) {
                error = "audio must be stereo 48 kHz PCM16 with a consistent format header";
                return false;
            }
            foundFormat = true;
        } else if (HasTag(bytes, offset, "data")) {
            if (foundData || chunkSize == 0) {
                error = foundData ? "duplicate data chunk" : "audio data is empty";
                return false;
            }
            parsed.pcm.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + chunkSize));
            foundData = true;
        }

        const std::size_t paddedSize = static_cast<std::size_t>(chunkSize) + (chunkSize & 1U);
        if (paddedSize > std::numeric_limits<std::size_t>::max() - dataOffset) {
            error = "WAV chunk size overflow";
            return false;
        }
        offset = dataOffset + paddedSize;
        if (offset > bytes.size()) {
            error = "missing WAV chunk padding";
            return false;
        }
    }

    if (!foundFormat || !foundData) {
        error = "WAV requires one fmt chunk and one data chunk";
        return false;
    }
    const std::size_t blockAlign = static_cast<std::size_t>(parsed.channels) * parsed.bitsPerSample / 8U;
    if (parsed.pcm.size() % blockAlign != 0) {
        error = "audio data is not aligned to complete sample frames";
        return false;
    }
    asset = std::move(parsed);
    error.clear();
    return true;
}

bool LoadPcmWave(
    const std::filesystem::path &path,
    WaveAsset &asset,
    std::string &error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "unable to open " + path.string();
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        error = "invalid file length for " + path.string();
        return false;
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "unable to read " + path.string();
        return false;
    }
    if (!ParsePcmWave(bytes, asset, error)) {
        error = path.filename().string() + ": " + error;
        return false;
    }
    return true;
}

float VolumeGain(std::int32_t masterPercent, std::int32_t channelPercent)
{
    const float master = static_cast<float>(std::clamp(masterPercent, 0, 100)) / 100.0F;
    const float channel = static_cast<float>(std::clamp(channelPercent, 0, 100)) / 100.0F;
    return master * channel;
}

MusicLayerGains MusicGainsForPressure(float pressure)
{
    const float value = std::clamp(pressure, 0.0F, 1.0F);
    const auto smoothStep = [](float lower, float upper, float input) {
        const float scaled = std::clamp((input - lower) / (upper - lower), 0.0F, 1.0F);
        return scaled * scaled * (3.0F - (2.0F * scaled));
    };
    return {
        0.82F + (0.18F * (1.0F - value)),
        smoothStep(0.18F, 0.72F, value),
        smoothStep(0.62F, 1.0F, value),
    };
}

} // namespace Tempest::Audio
