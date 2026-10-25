/*
** Project Tempest presentation interpolation
** Copyright 2026 Project Tempest contributors
** GPL-3.0-or-later
*/

#pragma once

#include "TempestSimulation.h"

namespace Tempest {

struct PresentationPoint {
    float x = 0.0F;
    float y = 0.0F;
};

// Alpha 0 is the previous fixed step and alpha 1 is the current fixed step.
// The frame accumulator is the time since that step was consumed, so the
// rendered sample lags the authoritative state by at most one tick.
inline double PresentationAlpha(double accumulatorSeconds, double tickSeconds)
{
    if (!(tickSeconds > 0.0)) {
        return 1.0;
    }
    double alpha = accumulatorSeconds / tickSeconds;
    if (alpha < 0.0) {
        alpha = 0.0;
    }
    if (alpha > 1.0) {
        alpha = 1.0;
    }
    return alpha;
}

inline PresentationPoint InterpolatePresentationPoint(Point previous, Point current, double alpha)
{
    if (alpha < 0.0) {
        alpha = 0.0;
    }
    if (alpha > 1.0) {
        alpha = 1.0;
    }
    const float blend = static_cast<float>(alpha);
    return {
        static_cast<float>(previous.x) +
            (static_cast<float>(current.x) - static_cast<float>(previous.x)) * blend,
        static_cast<float>(previous.y) +
            (static_cast<float>(current.y) - static_cast<float>(previous.y)) * blend,
    };
}

} // namespace Tempest
