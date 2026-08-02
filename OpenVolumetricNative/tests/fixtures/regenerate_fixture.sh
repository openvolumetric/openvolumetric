#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <ffmpeg-executable> <fixture-generator>" >&2
  exit 2
fi

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work_directory=$(mktemp -d "${TMPDIR:-/tmp}/openvolumetric-fixture.XXXXXX")
media_path="$work_directory/media.mp4"
silent_media_path="$work_directory/media-no-audio.mp4"
fragment_media_path="$work_directory/media-fragment.mp4"

trap 'rm -rf "$work_directory"' EXIT HUP INT TERM

"$1" -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=16x16:rate=4:duration=1" \
  -f lavfi -i "sine=frequency=880:sample_rate=48000:duration=1" \
  -frames:v 4 -c:v libx264 -preset ultrafast -tune zerolatency -crf 30 \
  -pix_fmt yuv420p -g 1 -bf 0 -x264-params "slices=4" \
  -c:a aac -b:a 64k -shortest -movflags +faststart \
  "$media_path"

"$1" -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=16x16:rate=4:duration=1" \
  -frames:v 4 -c:v libx264 -preset ultrafast -tune zerolatency -crf 30 \
  -pix_fmt yuv420p -g 1 -bf 0 -x264-params "slices=4" \
  -an -movflags +faststart "$silent_media_path"

# A single one-second GOP gives the fragmented packer exactly one aligned
# texture/geometry random-access boundary for this one-second fixture.
"$1" -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=16x16:rate=4:duration=1" \
  -frames:v 4 -c:v libx264 -preset ultrafast -tune zerolatency -crf 30 \
  -pix_fmt yuv420p -g 4 -keyint_min 4 -sc_threshold 0 -bf 0 \
  -x264-params "slices=4" -an \
  -movflags +faststart "$fragment_media_path"

"$2" "$media_path" "$work_directory/temporal" \
  "$script_directory/openvolumetric-test.mp4" temporal
"$2" "$media_path" "$work_directory/independent" \
  "$script_directory/openvolumetric-independent.mp4" independent
"$2" "$media_path" "$work_directory/corrupt" \
  "$script_directory/openvolumetric-corrupt-update.mp4" corrupt
"$2" "$silent_media_path" "$work_directory/no-audio" \
  "$script_directory/openvolumetric-no-audio.mp4" temporal
"$2" "$fragment_media_path" "$work_directory/fragmented" \
  "$script_directory/openvolumetric-fragmented.mp4" temporal 1

adaptive_directory="$script_directory/adaptive"
mkdir -p "$adaptive_directory"
cp "$script_directory/openvolumetric-fragmented.mp4" \
  "$adaptive_directory/low.mp4"
cp "$script_directory/openvolumetric-fragmented.mp4" \
  "$adaptive_directory/high.mp4"
cat > "$adaptive_directory/manifest.json" <<'EOF'
{
  "format": "openvolumetric-adaptive",
  "version": 1,
  "presentation_id": "native-test",
  "duration_seconds": 1.0,
  "segment_duration_seconds": 1.0,
  "has_audio": false,
  "segments": [
    {"number": 0, "start_seconds": 0.0, "duration_seconds": 1.0}
  ],
  "representations": [
    {
      "id": "low", "resource_uri": "low.mp4",
      "compatibility_group": "test", "bandwidth": 100000,
      "texture": {"codec": "avc1", "width": 16, "height": 16, "bitrate": 50000},
      "geometry": {"codec": "vvge-v2", "position_quantization_bits": 11,
        "bitrate": 50000, "temporal_compression": true}
    },
    {
      "id": "high", "resource_uri": "high.mp4",
      "compatibility_group": "test", "bandwidth": 200000,
      "texture": {"codec": "avc1", "width": 16, "height": 16, "bitrate": 100000},
      "geometry": {"codec": "vvge-v2", "position_quantization_bits": 14,
        "bitrate": 100000, "temporal_compression": true}
    }
  ]
}
EOF

echo "Fixture family written to $script_directory"
