# Adaptive streaming evaluation

OpenVolumetric includes a deterministic HTTP range server and equivalent CSV
metrics recorders in Unity and Unreal. Together they provide repeatable
bandwidth, latency, jitter, outage, and request-failure experiments without
installing a network-emulation package on the host.

## Serve a package without network impairment

For ordinary local-network playback, point the package server directly at the
directory containing `manifest.json`, `low.mp4`, and `high.mp4`:

```bash
python3 tools/package_server.py \
  Unity/Assets/StreamingAssets/Adaptive/bbc_rachel
```

The manifest is available at
`http://<host-address>:8000/manifest.json`. The server supports the same
HTTP/1.1 byte ranges, HEAD, OPTIONS, CORS, disconnect handling, default bind
address (`0.0.0.0`), and default port (`8000`) as the controlled test server.
Use `--port` or `--bind` with either script when those defaults are unsuitable.

The same server can expose one standalone OpenVolumetric MP4 for progressive
HTTP testing:

```bash
python3 tools/package_server.py path/to/video.mp4
```

The file is then available at both `http://<host-address>:8000/` and
`http://<host-address>:8000/video.mp4`.

## Start the controlled test server

From the repository root, serve the directory containing the adaptive package:

```bash
python3 tools/adaptive_test_server.py Unity/Assets/StreamingAssets \
  --port 8000 \
  --scenario tools/adaptive_network_scenario.json \
  --log /tmp/openvolumetric-network.jsonl
```

Use this manifest on the same Mac:

```text
http://127.0.0.1:8000/Adaptive/bbc_rachel/manifest.json
```

For Quest or another LAN device, replace `127.0.0.1` with the Mac's LAN IP.
The server supports `HEAD`, byte-range `GET`, CORS, and concurrent requests.

The supplied scenario runs these phases relative to server startup:

| Time | Phase | Conditions |
| ---: | --- | --- |
| 0–30 s | high | 80 Mbps, 10 ms latency |
| 30–60 s | constrained | 32 Mbps, 50 ms latency, 15 ms jitter |
| 60–65 s | outage | HTTP 503 for every request |
| 65–85 s | lossy recovery | 40 Mbps, 80 ms latency, every eleventh request fails |
| 85 s onward | high restored | 80 Mbps, 10 ms latency |

The jitter uses a deterministic five-request waveform. Request failures use a
fixed interval rather than randomness, making repeated runs comparable. Edit
or copy the JSON to define other phases. Supported fields are
`start_seconds`, `name`, `bandwidth_mbps`, `latency_ms`, `jitter_ms`,
`outage`, and `failure_every_requests`.

The supplied rates assume the current sample package (approximately 25.6 Mbps
Low and 29.0 Mbps High). The constrained phase is deliberately below the High
headroom threshold but above the Low media rate; a rate below Low cannot prove
a sustainable downgrade. Recalculate these values for materially different
packages.

Bandwidth is scheduled across the server as one aggregate limit. Concurrent
range requests from active and preparing decoder sessions therefore share the
configured capacity instead of each receiving the full stated bandwidth.

Inspect or restart the scenario clock without restarting the process:

```text
http://127.0.0.1:8000/__openvolumetric/status
http://127.0.0.1:8000/__openvolumetric/reset
```

The optional JSONL server log records a unique server session and reset run,
phase, request number, path, requested byte range, status,
bytes sent, and request duration. It is the transport-side source for request
failures, delivered bytes, and unused or repeated range analysis.

## Record Unity metrics

On the `OpenVolumetric` component:

1. Enable **Use Adaptive Manifest** and select **Auto** quality.
2. Enable **Live Adaptive Switching**.
3. Enable **Record Adaptive Metrics** under **Adaptive Evaluation**.
4. Optionally change the filename and the default 0.25-second sample interval.

Unity writes beneath `Application.persistentDataPath` and logs the complete
path when recording begins. On Quest this is the application's external files
directory and can be retrieved with `adb`.

## Record Unreal metrics

On `UOpenVolumetricComponent`:

1. Enable **Use Adaptive Manifest**, **Auto**, and live switching.
2. Enable **Record Adaptive Metrics** under **Developer > Evaluation**.
3. Optionally change the filename and sample interval.

Unreal writes the CSV beneath the project's `Saved` directory and reports its
path in the Output Log.

Both engines use the same CSV schema:

- wall and media time;
- playback, input-buffer, and adaptive-switch states;
- active and pending representations;
- smoothed throughput;
- downloaded/cached bytes and HTTP request/recovery counts;
- active and cached fragments;
- cumulative rebuffer count and duration;
- successful and failed switch counts and latest preparation latency;
- presented time, audio/visual clock error, engine frame time, and
  engine-reported memory use; and
- the current player error.

Unity's memory field is its total allocated engine memory; Unreal reports the
process physical-memory figure exposed by `FPlatformMemory`. Treat each as a
within-platform time series rather than directly comparing their absolute
values. Detailed decoder CPU/GPU cost still requires the engine profiler.

## Baseline experiment

Reset the server immediately before each run. Play for at least 120 seconds so
the full scenario and restored-high interval are observed. Do not manually
select quality during an automatic-policy run. Repeat at least three times on
each platform and preserve the scenario JSON, server JSONL, engine CSV, build
revision, package manifest, device model, and network topology with the result.

Success requires a downgrade under constrained delivery, synchronized recovery
after the outage, an eventual upgrade after sustained headroom, no mixed
texture/geometry presentation, no audio discontinuity beyond the documented
rebuffer interval, and bounded cache use.

The automatic Unity baseline passed once on 2 August 2026: High downgraded to
Low, playback recovered through the outage/loss phases, and Low upgraded to
High after restored bandwidth and the ten-second headroom interval. Preserve
this as an implementation validation, not the paper result; the formal result
still requires repeated runs with archived server JSONL and engine CSV files.

The capacity field used by the policy is an EWMA of completed HTTP range
transfer rates, including request latency but excluding time when the bounded
cache is idle. This is distinct from average representation consumption and is
necessary for a Low stream to demonstrate sufficient spare capacity for High.
