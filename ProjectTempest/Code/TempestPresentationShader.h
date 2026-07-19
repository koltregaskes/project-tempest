/*
** Project Tempest accessibility presentation shader
** Copyright 2026 Project Tempest contributors
** GPL-3.0-or-later
*/

#pragma once

namespace Tempest::Presentation {

inline constexpr const char *ShaderSource =
    "ps.1.4\n"
    "texld r0, t0\n"
    "mad r0, r0, c0, c1\n"
    "mov_sat r1.r, r0.r\n"
    "dp3_sat r1.g, r0, c2\n"
    "dp3_sat r1.b, r0, c3\n"
    "mov r1.a, r0.a\n"
    "lrp r0, c4, r1, r0\n"
    "mad_sat r0, r0, c5, c6\n";

} // namespace Tempest::Presentation
