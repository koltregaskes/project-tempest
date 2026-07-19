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

Colour Apply(Colour input, const Settings &settings);
const char *ModeName(ColourVisionMode mode);

} // namespace Tempest::Accessibility
