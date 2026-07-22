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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

void Expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string ReadAll(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "project-tempest-runtime-evidence-tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    Tempest::Evidence::Recorder recorder;
    Expect(recorder.Begin(root, "fixture", 1234), "evidence recorder starts in an explicit directory");
    recorder.RecordResolution(1, 1920, 1080);
    recorder.RecordFocus(2, false);
    recorder.RecordFocus(3, true);
    recorder.RecordRestart(4);
    recorder.RecordOutcome(5, "victory");
    for (int index = 0; index < 70; ++index) {
        recorder.RecordResolution(6, 1000 + index, 700 + index);
        recorder.RecordOutcome(7, "bounded-outcome-" + std::to_string(index));
    }
    recorder.RecordFrame(10, 10.0, 1, 1920, 1080, true, 100);
    recorder.RecordFrame(20, 20.0, 2, 1920, 1080, true, 120);
    recorder.RecordFrame(30, 30.0, 3, 1920, 1080, true, 110);
    Expect(recorder.Finish(40, 0, true), "evidence recorder writes a clean summary");

    const std::string trace = ReadAll(recorder.TracePath());
    const std::string summary = ReadAll(recorder.SummaryPath());
    Expect(trace.find("\"type\":\"session_start\"") != std::string::npos, "trace records session start");
    Expect(trace.find("\"name\":\"focus_lost\"") != std::string::npos, "trace records focus loss");
    Expect(trace.find("\"type\":\"frame_window\"") != std::string::npos,
        "trace serializes bounded frame-time windows");
    Expect(trace.find("\"type\":\"session_end\"") != std::string::npos, "trace records clean session end");
    Expect(summary.find("\"manual_playthrough_claimed\": false") != std::string::npos,
        "summary does not turn instrumentation into a playthrough claim");
    Expect(summary.find("\"average\": 20.0000") != std::string::npos, "summary calculates average frame time");
    Expect(summary.find("\"p95\": 30.0000") != std::string::npos, "summary calculates nearest-rank p95");
    Expect(summary.find("\"percentile_resolution_ms\": 0.1") != std::string::npos,
        "summary discloses histogram percentile precision");
    Expect(summary.find("\"focus_losses\": 1") != std::string::npos, "summary counts focus losses");
    Expect(summary.find("\"restarts\": 1") != std::string::npos, "summary counts restarts");
    Expect(summary.find("\"start\": 100") != std::string::npos, "summary records initial working set");
    Expect(summary.find("\"end\": 110") != std::string::npos, "summary records final working set");
    Expect(summary.find("\"peak\": 120") != std::string::npos, "summary records peak working set");
    Expect(summary.find("\"1920x1080\"") != std::string::npos, "summary records tested resolution");
    Expect(summary.find("\"victory\"") != std::string::npos, "summary records terminal outcome");
    Expect(summary.find("\"resolution_entries_dropped\": 7") != std::string::npos,
        "summary proves the unique-resolution list is capped");
    Expect(summary.find("\"outcome_entries_dropped\": 7") != std::string::npos,
        "summary proves the outcome list is capped");

    std::filesystem::remove_all(root, error);
    std::cout << "PASS: Project Tempest runtime evidence contract\n";
    return 0;
}
