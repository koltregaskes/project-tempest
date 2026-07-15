#!/usr/bin/env python3
"""Generate Project Tempest's original deterministic PCM audio pack."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import wave
from pathlib import Path


SAMPLE_RATE = 48_000
CHANNELS = 2
SAMPLE_WIDTH = 2


def clamp(value: float) -> float:
    return max(-1.0, min(1.0, value))


def smooth_attack_release(time: float, duration: float, attack: float, release: float) -> float:
    attack_gain = min(1.0, time / max(attack, 1.0 / SAMPLE_RATE))
    release_gain = min(1.0, (duration - time) / max(release, 1.0 / SAMPLE_RATE))
    return max(0.0, min(attack_gain, release_gain))


class Noise:
    def __init__(self, seed: int) -> None:
        self.state = seed & 0xFFFFFFFF

    def next(self) -> float:
        self.state = (1664525 * self.state + 1013904223) & 0xFFFFFFFF
        return ((self.state / 0xFFFFFFFF) * 2.0) - 1.0


def write_wave(path: Path, duration: float, sample) -> None:
    frame_count = round(duration * SAMPLE_RATE)
    frames = bytearray(frame_count * CHANNELS * SAMPLE_WIDTH)
    offset = 0
    for index in range(frame_count):
        time = index / SAMPLE_RATE
        left, right = sample(time, index)
        struct.pack_into(
            "<hh",
            frames,
            offset,
            round(clamp(left) * 32767.0),
            round(clamp(right) * 32767.0),
        )
        offset += 4
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(CHANNELS)
        output.setsampwidth(SAMPLE_WIDTH)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(frames)


def music_base_sample(duration: float):
    beat_seconds = 0.6

    def sample(time: float, _index: int) -> tuple[float, float]:
        beat_phase = (time % beat_seconds) / beat_seconds
        breath = 0.65 + 0.35 * math.sin(math.tau * (1.0 / 4.0) * time) ** 2
        bass = breath * (0.18 * math.sin(math.tau * 55.0 * time))
        bass += 0.045 * math.sin(math.tau * 110.0 * time)
        grid = 0.025 * math.sin(math.tau * 330.0 * time) * (
            0.45 + 0.55 * math.sin(math.tau * (1.0 / 2.4) * time) ** 2
        )
        relay = 0.018 * math.sin(math.tau * 220.0 * time) * math.exp(-beat_phase * 8.0)
        pan = 0.18 * math.sin(math.tau * (1.0 / duration) * time)
        mono = bass + grid + relay
        return mono * (1.0 - pan), mono * (1.0 + pan)

    return sample


def music_pressure_sample(duration: float):
    noise = Noise(0x50524553)
    beat_seconds = 0.6
    notes = (110.0, 130.8128, 146.8324, 164.8138, 146.8324, 130.8128, 98.0, 110.0)

    def sample(time: float, _index: int) -> tuple[float, float]:
        beat_phase = (time % beat_seconds) / beat_seconds
        beat_index = int(time / beat_seconds)
        note = notes[beat_index % len(notes)]
        note_envelope = math.sin(math.pi * beat_phase) ** 2
        pulse = math.exp(-beat_phase * 13.0)
        arpeggio = note_envelope * (
            0.105 * math.sin(math.tau * note * time)
            + 0.04 * math.sin(math.tau * note * 2.0 * time)
        )
        percussion = pulse * noise.next() * 0.06
        pan = 0.22 * math.sin(math.tau * (1.0 / duration) * time)
        mono = arpeggio + percussion
        return mono * (1.0 - pan), mono * (1.0 + pan)

    return sample


def music_crisis_sample(duration: float):
    noise = Noise(0x43524953)
    subdivision = 0.15

    def sample(time: float, _index: int) -> tuple[float, float]:
        phase = (time % subdivision) / subdivision
        tick = math.exp(-phase * 18.0) * noise.next() * 0.045
        alarm_envelope = 0.45 + 0.55 * math.sin(math.tau * 0.5 * time) ** 2
        alarm = alarm_envelope * (
            0.065 * math.sin(math.tau * 440.0 * time)
            + 0.025 * math.sin(math.tau * 660.0 * time)
        )
        pan = 0.28 * math.sin(math.tau * (2.0 / duration) * time)
        mono = tick + alarm
        return mono * (1.0 - pan), mono * (1.0 + pan)

    return sample


def chirp_sample(duration: float, start_hz: float, end_hz: float, seed: int, noise_level: float = 0.0):
    noise = Noise(seed)

    def sample(time: float, _index: int) -> tuple[float, float]:
        progress = time / duration
        frequency = start_hz + ((end_hz - start_hz) * progress)
        phase = math.tau * (start_hz * time + 0.5 * (end_hz - start_hz) * time * progress)
        envelope = smooth_attack_release(time, duration, 0.008, duration * 0.45)
        tone = math.sin(phase) * 0.34
        harmonic = math.sin(phase * 2.0) * 0.09
        grit = noise.next() * noise_level * (1.0 - progress)
        pan = math.sin(math.tau * frequency * time * 0.007) * 0.08
        value = (tone + harmonic + grit) * envelope
        return value * (1.0 - pan), value * (1.0 + pan)

    return sample


def command_sample(duration: float):
    noise = Noise(0xC011A4D)

    def sample(time: float, _index: int) -> tuple[float, float]:
        value = 0.0
        for onset, frequency in ((0.0, 310.0), (0.075, 465.0)):
            local = time - onset
            if 0.0 <= local < 0.105:
                envelope = smooth_attack_release(local, 0.105, 0.003, 0.08)
                value += envelope * (
                    0.28 * math.sin(math.tau * frequency * local)
                    + 0.04 * noise.next()
                )
        return value * 0.96, value

    return sample


def arc_pulse_sample(duration: float):
    noise = Noise(0xA2C0F11E)

    def sample(time: float, _index: int) -> tuple[float, float]:
        progress = time / duration
        envelope = smooth_attack_release(time, duration, 0.012, 0.34)
        sweep = 185.0 - (125.0 * progress)
        phase = math.tau * (185.0 * time - 62.5 * time * progress)
        body = 0.36 * math.sin(phase) + 0.13 * math.sin(phase * 2.02)
        crackle = noise.next() * 0.15 * math.exp(-progress * 5.0)
        ring = 0.08 * math.sin(math.tau * (720.0 + 40.0 * math.sin(math.tau * 8.0 * time)) * time)
        value = (body + crackle + ring) * envelope
        width = 0.12 * math.sin(math.tau * sweep * time)
        return value * (1.0 - width), value * (1.0 + width)

    return sample


def alert_sample(duration: float):
    def sample(time: float, _index: int) -> tuple[float, float]:
        envelope = smooth_attack_release(time, duration, 0.01, 0.12)
        modulation = math.sin(math.tau * 4.0 * time)
        frequency = 430.0 + (75.0 * modulation)
        value = envelope * (
            0.27 * math.sin(math.tau * frequency * time)
            + 0.08 * math.sin(math.tau * frequency * 1.5 * time)
        )
        return value, value * 0.92

    return sample


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Repository-shaped output root used for deterministic staging.",
    )
    args = parser.parse_args()
    audio_root = args.output_root / "ProjectTempest" / "Content" / "Audio"
    assets = (
        ("pt_music_substation.wav", 12.0, music_base_sample(12.0)),
        ("pt_music_pressure.wav", 12.0, music_pressure_sample(12.0)),
        ("pt_music_crisis.wav", 12.0, music_crisis_sample(12.0)),
        ("pt_ui_confirm.wav", 0.24, chirp_sample(0.24, 420.0, 780.0, 0xC0F1A4)),
        ("pt_select.wav", 0.14, chirp_sample(0.14, 620.0, 980.0, 0x5E1EC7)),
        ("pt_command.wav", 0.22, command_sample(0.22)),
        ("pt_arc_pulse.wav", 0.72, arc_pulse_sample(0.72)),
        ("pt_alert.wav", 0.55, alert_sample(0.55)),
    )
    report = []
    for filename, duration, generator in assets:
        path = audio_root / filename
        write_wave(path, duration, generator)
        report.append(
            {
                "path": path.relative_to(args.output_root).as_posix(),
                "duration_seconds": duration,
                "sample_rate": SAMPLE_RATE,
                "channels": CHANNELS,
                "sample_width_bits": SAMPLE_WIDTH * 8,
                "sha256": sha256(path),
            }
        )
    print(json.dumps({"generator": "create-tempest-audio.py", "assets": report}, indent=2))


if __name__ == "__main__":
    main()
