/*
** Project Tempest
** Copyright 2026 Project Tempest contributors
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Tempest::Evidence {

class Recorder {
public:
    bool Begin(const std::filesystem::path &directory, const std::string &sessionId, std::uint64_t startedUnixMs);
    bool IsEnabled() const;

    void RecordEvent(std::uint64_t elapsedMs, const std::string &name, const std::string &detail = {});
    void RecordFocus(std::uint64_t elapsedMs, bool active);
    void RecordResolution(std::uint64_t elapsedMs, int width, int height);
    void RecordRestart(std::uint64_t elapsedMs);
    void RecordOutcome(std::uint64_t elapsedMs, const std::string &outcome);
    void RecordFrame(
        std::uint64_t elapsedMs,
        double frameMs,
        std::uint64_t simulationTick,
        int width,
        int height,
        bool active,
        std::uint64_t workingSetBytes);
    bool Finish(std::uint64_t elapsedMs, int exitCode, bool cleanShutdown);

    const std::filesystem::path &TracePath() const;
    const std::filesystem::path &SummaryPath() const;

private:
    struct FrameWindow {
        std::uint64_t startMs = 0;
        std::uint64_t endMs = 0;
        std::uint64_t frames = 0;
        std::uint64_t activeFrames = 0;
        double averageMs = 0.0;
        double minimumMs = 0.0;
        double p95Ms = 0.0;
        double p99Ms = 0.0;
        double maximumMs = 0.0;
        std::uint64_t lastSimulationTick = 0;
        int width = 0;
        int height = 0;
        std::uint64_t workingSetBytes = 0;
    };

    void WriteTraceLine(const std::string &line);
    void FlushFrameWindow();
    static std::string EscapeJson(const std::string &value);
    static double Percentile(std::vector<double> values, double percentile);
    double HistogramPercentile(double percentile) const;

    bool m_enabled = false;
    std::ofstream m_trace;
    std::filesystem::path m_tracePath;
    std::filesystem::path m_summaryPath;
    std::string m_sessionId;
    std::uint64_t m_startedUnixMs = 0;
    std::uint64_t m_frameCount = 0;
    double m_totalFrameMs = 0.0;
    double m_minimumFrameMs = 0.0;
    double m_maximumFrameMs = 0.0;
    std::vector<std::uint64_t> m_frameHistogram;
    std::vector<double> m_currentWindowFrameTimes;
    std::uint64_t m_currentWindowStartMs = 0;
    std::uint64_t m_currentWindowEndMs = 0;
    std::uint64_t m_currentWindowActiveFrames = 0;
    std::uint64_t m_currentWindowLastTick = 0;
    int m_currentWindowWidth = 0;
    int m_currentWindowHeight = 0;
    std::uint64_t m_currentWindowWorkingSetBytes = 0;
    std::vector<FrameWindow> m_frameWindows;
    std::uint64_t m_frameWindowsDropped = 0;
    std::uint64_t m_workingSetStartBytes = 0;
    std::uint64_t m_workingSetPeakBytes = 0;
    std::uint64_t m_workingSetEndBytes = 0;
    std::uint64_t m_focusLosses = 0;
    std::uint64_t m_restarts = 0;
    std::vector<std::pair<int, int>> m_resolutions;
    std::vector<std::string> m_outcomes;
};

} // namespace Tempest::Evidence
