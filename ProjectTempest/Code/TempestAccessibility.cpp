/*
** Project Tempest accessibility colour transform
** Derived from Electronic Arts' Tunable Colorblindness Solution.
** Copyright (c) 2015-2021 Electronic Arts Inc.
** Licensed under the Apache License, Version 2.0.
** Modified for Project Tempest on 18 July 2026: portable C++ API, bounded settings, final output clamp.
*/

#include "TempestAccessibility.h"

#include <algorithm>

namespace Tempest::Accessibility {
namespace {

struct Vector3 {
    float red;
    float green;
    float blue;
};

float Clamp(float value)
{
    return std::clamp(value, 0.0F, 1.0F);
}

Vector3 RgbToLms(Vector3 colour)
{
    return {
        (17.8824F * colour.red) + (43.5161F * colour.green) + (4.11935F * colour.blue),
        (3.45565F * colour.red) + (27.1554F * colour.green) + (3.86714F * colour.blue),
        (0.0299566F * colour.red) + (0.184309F * colour.green) + (1.46709F * colour.blue),
    };
}

Vector3 LmsToRgb(Vector3 colour)
{
    return {
        (0.0809444479F * colour.red) - (0.130504409F * colour.green) + (0.116721066F * colour.blue),
        (-0.0102485335F * colour.red) + (0.0540193266F * colour.green) - (0.113614708F * colour.blue),
        (-0.000365296938F * colour.red) - (0.00412161469F * colour.green) + (0.693511405F * colour.blue),
    };
}

Vector3 DaltonizeUnclamped(Vector3 input, ColourVisionMode mode)
{
    const Vector3 lms = RgbToLms(input);
    Vector3 weak = lms;
    switch (mode) {
        case ColourVisionMode::Protanopia:
            weak.red = (2.02344F * lms.green) - (2.5281F * lms.blue);
            break;
        case ColourVisionMode::Deuteranopia:
            weak.green = (0.494207F * lms.red) + (1.24827F * lms.blue);
            break;
        case ColourVisionMode::Tritanopia:
            weak.blue = (-0.395913F * lms.red) + (0.801109F * lms.green);
            break;
        default:
            return input;
    }

    const Vector3 simulated = LmsToRgb(weak);
    const Vector3 error {
        input.red - simulated.red,
        input.green - simulated.green,
        input.blue - simulated.blue,
    };
    return {
        input.red,
        input.green + error.green + (0.7F * error.red),
        input.blue + error.blue + (0.7F * error.red),
    };
}

float Dot(Vector3 left, const Colour &right)
{
    return (left.red * right.red) + (left.green * right.green) + (left.blue * right.blue);
}

} // namespace

// TheSuperHackers @feature koltregaskes 19/07/2026 Share one exact CPU/GPU presentation contract.
PresentationParameters BuildPresentationParameters(const Settings &settings)
{
    const float strength = settings.mode == ColourVisionMode::Off
        ? 0.0F
        : static_cast<float>(std::clamp(settings.strengthPercent, 0, 100)) / 100.0F;
    const float brightness = static_cast<float>(std::clamp(settings.brightnessPercent, -10, 10)) / 100.0F;
    const float contrast = static_cast<float>(std::clamp(settings.contrastPercent, -25, 40)) / 100.0F;

    const Vector3 redBasis = DaltonizeUnclamped({ 1.0F, 0.0F, 0.0F }, settings.mode);
    const Vector3 greenBasis = DaltonizeUnclamped({ 0.0F, 1.0F, 0.0F }, settings.mode);
    const Vector3 blueBasis = DaltonizeUnclamped({ 0.0F, 0.0F, 1.0F }, settings.mode);

    PresentationParameters parameters;
    parameters.preparationScale = 1.0F + (strength * 0.112F);
    parameters.preparationOffset = (0.5F * (1.0F - parameters.preparationScale)) - (0.075F * strength);
    parameters.greenTransform = { redBasis.green, greenBasis.green, blueBasis.green, 0.0F };
    parameters.blueTransform = { redBasis.blue, greenBasis.blue, blueBasis.blue, 0.0F };
    parameters.strength = strength;
    parameters.outputScale = 1.0F + contrast;
    parameters.outputOffset = (0.5F * (1.0F - parameters.outputScale)) + brightness + (0.08F * strength);
    return parameters;
}

Colour ApplyPresentationParameters(Colour input, const PresentationParameters &parameters)
{
    const Vector3 original { Clamp(input.red), Clamp(input.green), Clamp(input.blue) };
    const Vector3 prepared {
        (original.red * parameters.preparationScale) + parameters.preparationOffset,
        (original.green * parameters.preparationScale) + parameters.preparationOffset,
        (original.blue * parameters.preparationScale) + parameters.preparationOffset,
    };
    const Vector3 shifted {
        Clamp(prepared.red),
        Clamp(Dot(prepared, parameters.greenTransform)),
        Clamp(Dot(prepared, parameters.blueTransform)),
    };
    const Vector3 mixed {
        (shifted.red * parameters.strength) + (prepared.red * (1.0F - parameters.strength)),
        (shifted.green * parameters.strength) + (prepared.green * (1.0F - parameters.strength)),
        (shifted.blue * parameters.strength) + (prepared.blue * (1.0F - parameters.strength)),
    };
    return {
        Clamp((mixed.red * parameters.outputScale) + parameters.outputOffset),
        Clamp((mixed.green * parameters.outputScale) + parameters.outputOffset),
        Clamp((mixed.blue * parameters.outputScale) + parameters.outputOffset),
        Clamp(input.alpha),
    };
}

// TheSuperHackers @feature koltregaskes 18/07/2026 Add a deterministic tunable colour-vision transform.
Colour Apply(Colour input, const Settings &settings)
{
    const float strength = settings.mode == ColourVisionMode::Off
        ? 0.0F
        : static_cast<float>(std::clamp(settings.strengthPercent, 0, 100)) / 100.0F;
    const float brightness = static_cast<float>(std::clamp(settings.brightnessPercent, -10, 10)) / 100.0F;
    const float contrast = static_cast<float>(std::clamp(settings.contrastPercent, -25, 40)) / 100.0F;

    const Vector3 original { Clamp(input.red), Clamp(input.green), Clamp(input.blue) };
    const Vector3 prepared {
        ((original.red - 0.5F) * (1.0F + (strength * 0.112F))) + 0.5F - (0.075F * strength),
        ((original.green - 0.5F) * (1.0F + (strength * 0.112F))) + 0.5F - (0.075F * strength),
        ((original.blue - 0.5F) * (1.0F + (strength * 0.112F))) + 0.5F - (0.075F * strength),
    };
    const Vector3 transformed = DaltonizeUnclamped(prepared, settings.mode);
    const Vector3 shifted {
        Clamp(transformed.red),
        Clamp(transformed.green),
        Clamp(transformed.blue),
    };
    Vector3 output {
        (shifted.red * strength) + (prepared.red * (1.0F - strength)),
        (shifted.green * strength) + (prepared.green * (1.0F - strength)),
        (shifted.blue * strength) + (prepared.blue * (1.0F - strength)),
    };
    output.red = ((output.red - 0.5F) * (1.0F + contrast)) + 0.5F + brightness + (0.08F * strength);
    output.green = ((output.green - 0.5F) * (1.0F + contrast)) + 0.5F + brightness + (0.08F * strength);
    output.blue = ((output.blue - 0.5F) * (1.0F + contrast)) + 0.5F + brightness + (0.08F * strength);

    return { Clamp(output.red), Clamp(output.green), Clamp(output.blue), Clamp(input.alpha) };
}

const char *ModeName(ColourVisionMode mode)
{
    switch (mode) {
        case ColourVisionMode::Off: return "Off";
        case ColourVisionMode::Protanopia: return "Protanopia";
        case ColourVisionMode::Deuteranopia: return "Deuteranopia";
        case ColourVisionMode::Tritanopia: return "Tritanopia";
        default: return "Off";
    }
}

} // namespace Tempest::Accessibility
