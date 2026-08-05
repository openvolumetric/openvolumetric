#!/usr/bin/env python3
"""Stage an installed OpenVolumetric SDK and static dependencies for Unreal."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


MAC_LIBRARIES = (
    "libOpenVolumetricCore.a", "libOpenVolumetricAuthoringCore.a",
    "libavformat.a", "libavcodec.a", "libswresample.a", "libavutil.a",
    "libdraco.a", "libcurl.a", "libssl.a", "libcrypto.a", "libz.a",
)
WINDOWS_LIBRARIES = (
    ("OpenVolumetricCore.lib", "OpenVolumetricCore.lib"),
    ("OpenVolumetricAuthoringCore.lib", "OpenVolumetricAuthoringCore.lib"),
    ("avformat.lib", "avformat.lib"),
    ("avcodec.lib", "avcodec.lib"),
    ("swresample.lib", "swresample.lib"),
    ("avutil.lib", "avutil.lib"),
    ("draco.lib", "draco.lib"),
    ("libcurl.lib", "libcurl.lib"),
    # The static vcpkg zlib archive is named zs.lib. Keep the staged name
    # consumed by the Unreal module stable.
    ("zlib.lib", "zs.lib"),
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path,
                        help="CMake install prefix containing include/OpenVolumetric")
    parser.add_argument("--dependencies", required=True, type=Path,
                        help="vcpkg triplet prefix containing static libraries")
    parser.add_argument("--platform", required=True, choices=("Mac", "Win64"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("Unreal/Plugins/OpenVolumetric/Source/ThirdParty/OpenVolumetricSDK"),
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    source_headers = arguments.sdk / "include" / "OpenVolumetric"
    if not source_headers.is_dir():
        raise SystemExit(f"Installed SDK headers not found: {source_headers}")

    output_headers = arguments.output / "include" / "OpenVolumetric"
    output_libraries = arguments.output / "lib" / arguments.platform
    if output_headers.exists():
        shutil.rmtree(output_headers)
    if output_libraries.exists():
        shutil.rmtree(output_libraries)
    output_headers.parent.mkdir(parents=True, exist_ok=True)
    output_libraries.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source_headers, output_headers, dirs_exist_ok=True)

    libraries = (
        tuple((filename, filename) for filename in MAC_LIBRARIES)
        if arguments.platform == "Mac" else WINDOWS_LIBRARIES
    )
    for staged_filename, source_filename in libraries:
        sdk_archive = arguments.sdk / "lib" / source_filename
        dependency_archive = arguments.dependencies / "lib" / source_filename
        source = sdk_archive if sdk_archive.is_file() else dependency_archive
        if not source.is_file():
            raise SystemExit(
                f"Required Unreal SDK archive not found: {source_filename}")
        shutil.copy2(source, output_libraries / staged_filename)

    print(f"Staged OpenVolumetric Unreal SDK at {arguments.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
