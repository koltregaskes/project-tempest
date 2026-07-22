/*
** Project Tempest
** Copyright 2026 Project Tempest contributors
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "TempestRuntimeEvidence.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace Tempest::Evidence {

bool Recorder::Begin(
    const std::filesystem::path &directory,
    const std::string &sessionId,
    std::uint64_t startedUnixMs)
{
    if (sessionId.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    m_sessionId = sessionId;
    m_startedUnixMs = startedUnixMs;
    m_tracePath = directory / ("project-tempest-runtime-" + sessionId + ".jsonl");
    m_summaryPath = directory / ("project-tempest-runtime-" + sessionId + "-summary.json");
    m_trace.open(m_tracePath, std::ios::binary | std::ios::trunc);
    if (!m_trace) {
        return false;
    }

    m_enabled = true;
    m_frameHistogram.assign(10001, 0);
    m_currentWindowFrameTimes.reserve(256);
    std::ostringstream line;
    line << "{\"schema_version\":1,\"type\":\"session_start\",\"session_id\":\""
         << EscapeJson(m_sessionId) << "\",\"started_unix_ms\":" << m_startedUnixMs << "}";
    WriteTraceLine(line.str());
    m_trace.flush();
    return true;
}

bool Recorder::IsEnabled() const
{
    return m_enabled;
}

void Recorder::RecordEvent(std::uint64_t elapsedMs, const std::string &name, const std::string &detail)
{
    if (!m_enabled) {
        return;
    }
    std::ostringstream line;
    line << "{\"schema_version\":1,\"type\":\"event\",\"elapsed_ms\":" << elapsedMs
         << ",\"name\":\"" << EscapeJson(name) << "\"";
    if (!detail.empty()) {
        line << ",\"detail\":\"" << EscapeJson(detail) << "\"";
    }
    line << "}";
    WriteTraceLine(line.str());
    m_trace.flush();
}

void Recorder::RecordFocus(std::uint64_t elapsedMs, bool active)
{
    if (!m_enabled) {
        return;
    }
    if (!active) {
        ++m_focusLosses;
    }
    RecordEvent(elapsedMs, active ? "focus_gained" : "focus_lost");
}

void Recorder::RecordResolution(std::uint64_t elapsedMs, int width, int height)
{
    if (!m_enabled) {
        return;
    }
    const std::pair<int, int> resolution { width, height };
    if (std::find(m_resolutions.begin(), m_resolutions.end(), resolution) == m_resolutions.end()) {
        m_resolutions.push_back(resolution);
    }
    RecordEvent(elapsedMs, "resolution", std::to_string(width) + "x" + std::to_string(height));
}

void Recorder::RecordRestart(std::uint64_t elapsedMs)
{
    if (!m_enabled) {
        return;
    }
    ++m_restarts;
    RecordEvent(elapsedMs, "restart");
}

void Recorder::RecordOutcome(std::uint64_t elapsedMs, const std::string &outcome)
{
    if (!m_enabled) {
        return;
    }
    m_outcomes.push_back(outcome);
    RecordEvent(elapsedMs, "outcome", outcome);
}

void Recorder::RecordFrame(
    std::uint64_t elapsedMs,
    double frameMs,
    std::uint64_t simulationTick,
    int width,
    int height,
    bool active,
    std::uint64_t workingSetBytes)
{
    if (!m_enabled || !std::isfinite(frameMs) || frameMs < 0.0) {
        return;
    }

    if (!m_currentWindowFrameTimes.empty() && elapsedMs - m_currentWindowStartMs >= 1000) {
        FlushFrameWindow();
    }
    if (m_currentWindowFrameTimes.empty()) {
        m_currentWindowStartMs = elapsedMs;
    }

    ++m_frameCount;
    m_totalFrameMs += frameMs;
    if (m_frameCount == 1) {
        m_minimumFrameMs = frameMs;
        m_maximumFrameMs = frameMs;
    } else {
        m_minimumFrameMs = std::min(m_minimumFrameMs, frameMs);
        m_maximumFrameMs = std::max(m_maximumFrameMs, frameMs);
    }
    const std::size_t histogramIndex = std::min(
        static_cast<std::size_t>(std::floor(frameMs * 10.0)),
        m_frameHistogram.size() - 1);
    ++m_frameHistogram[histogramIndex];
    m_currentWindowFrameTimes.push_back(frameMs);
    m_currentWindowEndMs = elapsedMs;
    m_currentWindowActiveFrames += active ? 1 : 0;
    m_currentWindowLastTick = simulationTick;
    m_currentWindowWidth = width;
    m_currentWindowHeight = height;
    if (workingSetBytes != 0) {
        if (m_workingSetStartBytes == 0) {
            m_workingSetStartBytes = workingSetBytes;
        }
        m_workingSetEndBytes = workingSetBytes;
        m_workingSetPeakBytes = std::max(m_workingSetPeakBytes, workingSetBytes);
        m_currentWindowWorkingSetBytes = workingSetBytes;
    }
}

bool Recorder::Finish(std::uint64_t elapsedMs, int exitCode, bool cleanShutdown)
{
    if (!m_enabled) {
        return false;
    }

    FlushFrameWindow();
    for (const FrameWindow &window : m_frameWindows) {
        std::ostringstream line;
        line << std::fixed << std::setprecision(4)
             << "{\"schema_version\":1,\"type\":\"frame_window\",\"start_ms\":" << window.startMs
             << ",\"end_ms\":" << window.endMs << ",\"frames\":" << window.frames
             << ",\"active_frames\":" << window.activeFrames << ",\"frame_ms\":{\"average\":"
             << window.averageMs << ",\"min\":" << window.minimumMs << ",\"p95\":" << window.p95Ms
             << ",\"p99\":" << window.p99Ms << ",\"max\":" << window.maximumMs
             << "},\"last_simulation_tick\":" << window.lastSimulationTick << ",\"width\":" << window.width
             << ",\"height\":" << window.height << ",\"working_set_bytes\":"
             << window.workingSetBytes << "}";
        WriteTraceLine(line.str());
    }

    std::ostringstream end;
    end << "{\"schema_version\":1,\"type\":\"session_end\",\"elapsed_ms\":" << elapsedMs
        << ",\"exit_code\":" << exitCode << ",\"clean_shutdown\":"
        << (cleanShutdown ? "true" : "false") << "}";
    WriteTraceLine(end.str());
    m_trace.flush();
    m_trace.close();

    const double average = m_frameCount == 0 ? 0.0 : m_totalFrameMs / static_cast<double>(m_frameCount);
    const double p50 = HistogramPercentile(0.50);
    const double p95 = HistogramPercentile(0.95);
    const double p99 = HistogramPercentile(0.99);
    const double minimum = m_frameCount == 0 ? 0.0 : m_minimumFrameMs;
    const double maximum = m_frameCount == 0 ? 0.0 : m_maximumFrameMs;

    const std::filesystem::path temporary = m_summaryPath.string() + ".tmp";
    std::ofstream summary(temporary, std::ios::binary | std::ios::trunc);
    if (!summary) {
        m_enabled = false;
        return false;
    }
    summary << std::fixed << std::setprecision(4)
            << "{\n"
            << "  \"schema_version\": 1,\n"
            << "  \"mode\": \"user_initiated_runtime_evidence\",\n"
            << "  \"manual_playthrough_claimed\": false,\n"
            << "  \"session_id\": \"" << EscapeJson(m_sessionId) << "\",\n"
            << "  \"started_unix_ms\": " << m_startedUnixMs << ",\n"
            << "  \"duration_ms\": " << elapsedMs << ",\n"
            << "  \"exit_code\": " << exitCode << ",\n"
            << "  \"clean_shutdown\": " << (cleanShutdown ? "true" : "false") << ",\n"
            << "  \"frames\": " << m_frameCount << ",\n"
            << "  \"frame_windows\": " << m_frameWindows.size() << ",\n"
            << "  \"frame_windows_dropped\": " << m_frameWindowsDropped << ",\n"
            << "  \"percentile_resolution_ms\": 0.1,\n"
            << "  \"frame_ms\": {\"min\": " << minimum << ", \"average\": " << average
            << ", \"p50\": " << p50 << ", \"p95\": " << p95 << ", \"p99\": " << p99
            << ", \"max\": " << maximum << "},\n"
            << "  \"working_set_bytes\": {\"start\": " << m_workingSetStartBytes
            << ", \"end\": " << m_workingSetEndBytes << ", \"peak\": " << m_workingSetPeakBytes << "},\n"
            << "  \"focus_losses\": " << m_focusLosses << ",\n"
            << "  \"restarts\": " << m_restarts << ",\n"
            << "  \"resolutions\": [";
    for (std::size_t index = 0; index < m_resolutions.size(); ++index) {
        if (index != 0) {
            summary << ", ";
        }
        summary << "\"" << m_resolutions[index].first << "x" << m_resolutions[index].second << "\"";
    }
    summary << "],\n  \"outcomes\": [";
    for (std::size_t index = 0; index < m_outcomes.size(); ++index) {
        if (index != 0) {
            summary << ", ";
        }
        summary << "\"" << EscapeJson(m_outcomes[index]) << "\"";
    }
    summary << "],\n  \"trace_file\": \"" << EscapeJson(m_tracePath.filename().string()) << "\"\n}\n";
    summary.close();
    if (!summary) {
        m_enabled = false;
        return false;
    }

    std::error_code error;
    std::filesystem::rename(temporary, m_summaryPath, error);
    m_enabled = false;
    return !error;
}

const std::filesystem::path &Recorder::TracePath() const
{
    return m_tracePath;
}

const std::filesystem::path &Recorder::SummaryPath() const
{
    return m_summaryPath;
}

void Recorder::WriteTraceLine(const std::string &line)
{
    if (m_trace) {
        m_trace << line << '\n';
    }
}

void Recorder::FlushFrameWindow()
{
    if (m_currentWindowFrameTimes.empty()) {
        return;
    }
    const double total = std::accumulate(
        m_currentWindowFrameTimes.begin(),
        m_currentWindowFrameTimes.end(),
        0.0);
    const FrameWindow window {
        m_currentWindowStartMs,
        m_currentWindowEndMs,
        static_cast<std::uint64_t>(m_currentWindowFrameTimes.size()),
        m_currentWindowActiveFrames,
        total / static_cast<double>(m_currentWindowFrameTimes.size()),
        *std::min_element(m_currentWindowFrameTimes.begin(), m_currentWindowFrameTimes.end()),
        Percentile(m_currentWindowFrameTimes, 0.95),
        Percentile(m_currentWindowFrameTimes, 0.99),
        *std::max_element(m_currentWindowFrameTimes.begin(), m_currentWindowFrameTimes.end()),
        m_currentWindowLastTick,
        m_currentWindowWidth,
        m_currentWindowHeight,
        m_currentWindowWorkingSetBytes,
    };
    constexpr std::size_t maximumFrameWindows = 7200;
    if (m_frameWindows.size() < maximumFrameWindows) {
        m_frameWindows.push_back(window);
    } else {
        ++m_frameWindowsDropped;
    }
    m_currentWindowFrameTimes.clear();
    m_currentWindowActiveFrames = 0;
    m_currentWindowWorkingSetBytes = 0;
}

std::string Recorder::EscapeJson(const std::string &value)
{
    std::ostringstream escaped;
    for (const unsigned char character : value) {
        switch (character) {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (character < 0x20) {
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(character) << std::dec << std::setfill(' ');
                } else {
                    escaped << static_cast<char>(character);
                }
        }
    }
    return escaped.str();
}

double Recorder::Percentile(std::vector<double> values, double percentile)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double bounded = std::clamp(percentile, 0.0, 1.0);
    const std::size_t rank = bounded == 0.0
        ? 0
        : static_cast<std::size_t>(std::ceil(bounded * static_cast<double>(values.size()))) - 1;
    return values[std::min(rank, values.size() - 1)];
}

double Recorder::HistogramPercentile(double percentile) const
{
    if (m_frameCount == 0 || m_frameHistogram.empty()) {
        return 0.0;
    }
    const double bounded = std::clamp(percentile, 0.0, 1.0);
    const std::uint64_t rank = bounded == 0.0
        ? 1
        : static_cast<std::uint64_t>(std::ceil(bounded * static_cast<double>(m_frameCount)));
    std::uint64_t cumulative = 0;
    for (std::size_t index = 0; index < m_frameHistogram.size(); ++index) {
        cumulative += m_frameHistogram[index];
        if (cumulative >= rank) {
            return static_cast<double>(index) / 10.0;
        }
    }
    return static_cast<double>(m_frameHistogram.size() - 1) / 10.0;
}

} // namespace Tempest::Evidence
