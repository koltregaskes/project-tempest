/*
** Project Tempest accessibility colour transform
** Derived from Electronic Arts' Tunable Colorblindness Solution.
** Copyright (c) 2015-2021 Electronic Arts Inc.
** Licensed under the Apache License, Version 2.0.
** Modified for Project Tempest on 18 July 2026.
*/

#pragma once

#include <cstdint>

namespace Tempest::Accessibility {

enum class ColourVisionMode : std::uint8_t {
    Off,
    Protanopia,
    Deuteranopia,
    Tritanopia,
    Count,
};

struct Colour {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 1.0F;
};

struct Settings {
    ColourVisionMode mode = ColourVisionMode::Off;
    std::int32_t strengthPercent = 90;
    std::int32_t brightnessPercent = 0;
    std::int32_t contrastPercent = 0;
};

// GPU-neutral constants for the exact presentation transform. The Direct3D 8
// renderer and the portable test path both consume this contract so the
// shader cannot silently drift from the reference implementation.
struct PresentationParameters {
    float preparationScale = 1.0F;
    float preparationOffset = 0.0F;
    Colour greenTransform { 0.0F, 1.0F, 0.0F, 0.0F };
    Colour blueTransform { 0.0F, 0.0F, 1.0F, 0.0F };
    float strength = 0.0F;
    float outputScale = 1.0F;
    float outputOffset = 0.0F;
};

PresentationParameters BuildPresentationParameters(const Settings &settings);
Colour ApplyPresentationParameters(Colour input, const PresentationParameters &parameters);
Colour Apply(Colour input, const Settings &settings);
const char *ModeName(ColourVisionMode mode);

} // namespace Tempest::Accessibility
