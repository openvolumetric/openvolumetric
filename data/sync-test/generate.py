#!/usr/bin/env python3
"""Generate an unambiguous OpenVolumetric audio/visual sync sequence."""

from __future__ import annotations

import argparse
import math
from pathlib import Path
import shutil
import struct
import subprocess
import wave


FRAME_RATE = 30
DURATION_SECONDS = 10
FRAME_COUNT = FRAME_RATE * DURATION_SECONDS
SAMPLE_RATE = 48_000
PULSE_FRAMES = 3
CLICK_SECONDS = 0.020
FIRST_CLICK_FREQUENCY = 500.0
CLICK_FREQUENCY = 1_000.0


def write_cube(path: Path, scale: float) -> None:
    vertices = (
        (-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
        (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1),
    )
    uvs = ((0, 0), (1, 0), (1, 1), (0, 1))
    faces = (
        (1, 2, 3, 4), (5, 8, 7, 6), (1, 5, 6, 2),
        (2, 6, 7, 3), (3, 7, 8, 4), (5, 1, 4, 8),
    )
    normals = (
        (0, 0, 1), (0, 0, -1), (0, 1, 0),
        (-1, 0, 0), (0, -1, 0), (1, 0, 0),
    )
    lines = [
        "# Constant-topology cube; scale pulses with the audio click.",
        f"o SyncCube_{path.stem}",
    ]
    # Expand cube corners per face. This preserves distinct normal/UV seams
    # and gives temporal position updates exactly the same ordered point set
    # as the full Draco reference mesh.
    lines.extend(
        f"v {vertices[index - 1][0] * scale:.6f} "
        f"{vertices[index - 1][1] * scale:.6f} "
        f"{vertices[index - 1][2] * scale:.6f}"
        for face in faces
        for index in face
    )
    lines.extend(f"vt {u:.6f} {v:.6f}" for u, v in uvs)
    lines.extend(f"vn {x:.6f} {y:.6f} {z:.6f}" for x, y, z in normals)
    for normal in range(1, len(faces) + 1):
        base = (normal - 1) * 4
        lines.append(
            f"f {base + 1}/1/{normal} {base + 2}/2/{normal} "
            f"{base + 3}/3/{normal} {base + 4}/4/{normal}"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_audio(path: Path) -> None:
    total_samples = DURATION_SECONDS * SAMPLE_RATE
    click_samples = round(CLICK_SECONDS * SAMPLE_RATE)
    frames = bytearray()
    for sample in range(total_samples):
        within_second = sample % SAMPLE_RATE
        if within_second < click_samples:
            frequency = (
                FIRST_CLICK_FREQUENCY
                if sample < SAMPLE_RATE
                else CLICK_FREQUENCY
            )
            envelope = 1.0 - within_second / click_samples
            value = 0.85 * envelope * math.sin(
                2.0 * math.pi * frequency * within_second / SAMPLE_RATE
            )
        else:
            value = 0.0
        pcm = max(-32768, min(32767, round(value * 32767)))
        frames.extend(struct.pack("<hh", pcm, pcm))
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(frames)


def find_font() -> str | None:
    candidates = (
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    )
    return next((candidate for candidate in candidates if Path(candidate).is_file()), None)


def supports_filter(ffmpeg: str, name: str) -> bool:
    result = subprocess.run(
        [ffmpeg, "-hide_banner", "-filters"],
        check=True,
        capture_output=True,
        text=True,
    )
    return any(
        line.split()[1:2] == [name]
        for line in result.stdout.splitlines()
        if line.strip()
    )


def write_textures(ffmpeg: str, output_pattern: Path) -> None:
    flash = f"lt(mod(n\\,{FRAME_RATE})\\,{PULSE_FRAMES})"
    filters = [
        f"drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:enable='{flash}'",
        f"drawbox=x=80:y=80:w=352:h=352:color=red@0.85:t=18:enable='{flash}'",
    ]
    font = find_font()
    if font and supports_filter(ffmpeg, "drawtext"):
        filters.append(
            "drawtext="
            f"fontfile={font}:"
            "text='SYNC FRAME %{eif\\:n\\:d\\:6}':"
            "fontcolor=yellow:fontsize=30:x=(w-text_w)/2:y=h-55"
        )
    subprocess.run(
        [
            ffmpeg,
            "-hide_banner",
            "-loglevel", "error",
            "-y",
            "-f", "lavfi",
            "-i",
            f"color=c=black:s=512x512:r={FRAME_RATE}:d={DURATION_SECONDS}",
            "-vf", ",".join(filters),
            "-frames:v", str(FRAME_COUNT),
            "-start_number", "0",
            "-q:v", "2",
            str(output_pattern),
        ],
        check=True,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--ffmpeg",
        default=None,
        help="FFmpeg executable; defaults to the repository build or PATH.",
    )
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parent
    geometry = root / "geometry"
    texture = root / "texture"
    geometry.mkdir(parents=True, exist_ok=True)
    texture.mkdir(parents=True, exist_ok=True)

    repository_ffmpeg = (
        root.parent.parent /
        "OpenVolumetricNative/build/ffmpeg/bin/ffmpeg"
    )
    ffmpeg = (
        arguments.ffmpeg or
        (str(repository_ffmpeg) if repository_ffmpeg.is_file() else None) or
        shutil.which("ffmpeg")
    )
    if not ffmpeg:
        raise SystemExit("FFmpeg was not found; pass --ffmpeg /path/to/ffmpeg.")

    for frame in range(FRAME_COUNT):
        pulse = frame % FRAME_RATE < PULSE_FRAMES
        write_cube(
            geometry / f"{frame:06d}.obj",
            1.25 if pulse else 1.0,
        )
    write_audio(root / "click.wav")
    write_textures(ffmpeg, texture / "%06d.jpg")
    print(
        f"Generated {FRAME_COUNT} frames at {FRAME_RATE} fps "
        f"({DURATION_SECONDS} seconds) in {root}"
    )


if __name__ == "__main__":
    main()
