#!/usr/bin/env python3
"""Serve OpenVolumetric packages with deterministic network impairment.

The server supports HEAD and byte-range GET requests required by FFmpeg. A
JSON scenario changes bandwidth, latency, jitter, outages, and deterministic
HTTP failures as elapsed test time advances. It has no third-party dependency.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
from pathlib import Path
import threading
import time
import uuid
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit


DEFAULT_SCENARIO = {
    "phases": [
        {"start_seconds": 0, "name": "high", "bandwidth_mbps": 80},
        {"start_seconds": 30, "name": "constrained", "bandwidth_mbps": 32},
        {"start_seconds": 60, "name": "outage", "outage": True},
        {"start_seconds": 65, "name": "recovery", "bandwidth_mbps": 40,
         "latency_ms": 80, "jitter_ms": 20,
         "failure_every_requests": 11},
        {"start_seconds": 85, "name": "high-restored", "bandwidth_mbps": 80},
    ]
}


class Scenario:
    """Thread-safe scenario clock and deterministic request sequence."""

    def __init__(self, contents: dict, log_path: Path | None):
        phases = contents.get("phases")
        if not isinstance(phases, list) or not phases:
            raise ValueError("Scenario requires a non-empty 'phases' array.")
        self.phases = sorted(phases, key=lambda item: float(item["start_seconds"]))
        if float(self.phases[0]["start_seconds"]) != 0.0:
            raise ValueError("The first scenario phase must start at 0 seconds.")
        self.log_path = log_path
        self.lock = threading.Lock()
        self.started_at = time.monotonic()
        self.request_count = 0
        self.next_delivery_time = self.started_at
        self.server_session = uuid.uuid4().hex
        self.scenario_run = 1

    def reset(self) -> None:
        with self.lock:
            self.started_at = time.monotonic()
            self.request_count = 0
            self.next_delivery_time = self.started_at
            self.scenario_run += 1

    def snapshot(self) -> tuple[float, int, dict, str, int]:
        with self.lock:
            elapsed = time.monotonic() - self.started_at
            self.request_count += 1
            request_number = self.request_count
            scenario_run = self.scenario_run
        active = self.phases[0]
        for phase in self.phases:
            if float(phase["start_seconds"]) > elapsed:
                break
            active = phase
        return (elapsed, request_number, active, self.server_session,
                scenario_run)

    def status(self) -> dict:
        with self.lock:
            elapsed = time.monotonic() - self.started_at
            request_count = self.request_count
        active = self.phases[0]
        for phase in self.phases:
            if float(phase["start_seconds"]) > elapsed:
                break
            active = phase
        return {"elapsed_seconds": elapsed, "request_count": request_count,
                "server_session": self.server_session,
                "scenario_run": self.scenario_run, "phase": active}

    def log(self, record: dict) -> None:
        if self.log_path is None:
            return
        line = json.dumps(record, separators=(",", ":"), sort_keys=True) + "\n"
        with self.lock:
            self.log_path.parent.mkdir(parents=True, exist_ok=True)
            with self.log_path.open("a", encoding="utf-8") as output:
                output.write(line)

    def throttle(self, byte_count: int, bandwidth_mbps: float,
                 scenario_run: int) -> None:
        """Reserve aggregate server bandwidth and wait for delivery time."""
        if bandwidth_mbps <= 0.0 or byte_count <= 0:
            return
        duration = byte_count * 8.0 / (bandwidth_mbps * 1_000_000.0)
        with self.lock:
            # Requests from before /reset may still be unwinding. They must
            # not reserve delivery time in the new deterministic run.
            if scenario_run != self.scenario_run:
                return
            now = time.monotonic()
            delivery = max(now, self.next_delivery_time) + duration
            self.next_delivery_time = delivery
        delay = delivery - time.monotonic()
        if delay > 0.0:
            time.sleep(delay)


class AdaptiveRequestHandler(BaseHTTPRequestHandler):
    """Range-capable handler controlled by the process-wide scenario."""

    server_version = "OpenVolumetricAdaptiveTest/1.0"
    protocol_version = "HTTP/1.1"

    def handle(self) -> None:
        """Treat normal player disconnects as request cancellation.

        FFmpeg closes idle keep-alive sockets whenever it seeks, changes
        representation, or cancels a prepared decoder. Python's base handler
        otherwise reports the resulting TCP reset as an uncaught thread
        exception even though the listening server remains healthy.
        """
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def end_headers(self) -> None:
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Range")
        self.end_headers()

    def do_HEAD(self) -> None:  # noqa: N802
        self._serve(send_body=False)

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/__openvolumetric/status":
            self._json_response(self.server.scenario.status())
            return
        if self.path == "/__openvolumetric/reset":
            self.server.scenario.reset()
            self._json_response(self.server.scenario.status())
            return
        self._serve(send_body=True)

    def _json_response(self, value: dict) -> None:
        payload = (json.dumps(value, indent=2) + "\n").encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _resolve_path(self) -> Path | None:
        relative = unquote(urlsplit(self.path).path).lstrip("/")
        candidate = (self.server.root / relative).resolve()
        try:
            candidate.relative_to(self.server.root)
        except ValueError:
            return None
        return candidate if candidate.is_file() else None

    def _serve(self, send_body: bool) -> None:
        started = time.monotonic()
        (elapsed, request_number, phase, server_session,
         scenario_run) = self.server.scenario.snapshot()
        path = self._resolve_path()
        status = HTTPStatus.OK
        bytes_sent = 0
        range_start = None
        range_end = None
        try:
            failure_every = int(phase.get("failure_every_requests", 0))
            if bool(phase.get("outage", False)):
                self.send_error(HTTPStatus.SERVICE_UNAVAILABLE,
                                "Scripted OpenVolumetric outage")
                status = HTTPStatus.SERVICE_UNAVAILABLE
                return
            if failure_every > 0 and request_number % failure_every == 0:
                status = HTTPStatus.SERVICE_UNAVAILABLE
                self.send_error(status, "Scripted deterministic request failure")
                return
            if path is None:
                status = HTTPStatus.NOT_FOUND
                self.send_error(status)
                return

            size = path.stat().st_size
            first, last = self._parse_range(size)
            range_start = first
            range_end = last
            if first is None:
                status = HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE
                self.send_response(status)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if first != 0 or last != size - 1:
                status = HTTPStatus.PARTIAL_CONTENT
            length = last - first + 1
            self._delay(phase, request_number)
            self.send_response(status)
            self.send_header("Content-Type",
                             mimetypes.guess_type(path.name)[0] or
                             "application/octet-stream")
            self.send_header("Content-Length", str(length))
            if status == HTTPStatus.PARTIAL_CONTENT:
                self.send_header("Content-Range", f"bytes {first}-{last}/{size}")
            self.end_headers()
            if not send_body:
                return

            with path.open("rb") as source:
                source.seek(first)
                remaining = length
                while remaining > 0:
                    chunk = source.read(min(64 * 1024, remaining))
                    if not chunk:
                        break
                    bandwidth = float(phase.get("bandwidth_mbps", 0.0))
                    self.server.scenario.throttle(
                        len(chunk), bandwidth, scenario_run)
                    self.wfile.write(chunk)
                    bytes_sent += len(chunk)
                    remaining -= len(chunk)
        except (BrokenPipeError, ConnectionResetError):
            status = 499
        finally:
            self.server.scenario.log({
                "bytes_sent": bytes_sent,
                "duration_seconds": time.monotonic() - started,
                "elapsed_seconds": elapsed,
                "method": self.command,
                "path": urlsplit(self.path).path,
                "phase": phase.get("name", "unnamed"),
                "range": self.headers.get("Range", ""),
                "range_end": range_end,
                "range_start": range_start,
                "request": request_number,
                "scenario_run": scenario_run,
                "server_session": server_session,
                "status": int(status),
            })

    def _parse_range(self, size: int) -> tuple[int | None, int | None]:
        value = self.headers.get("Range")
        if not value:
            return 0, size - 1
        if not value.startswith("bytes=") or "," in value:
            return None, None
        start_text, end_text = value[6:].split("-", 1)
        try:
            if not start_text:
                suffix = int(end_text)
                return max(0, size - suffix), size - 1
            first = int(start_text)
            last = int(end_text) if end_text else size - 1
        except ValueError:
            return None, None
        if first < 0 or first >= size or last < first:
            return None, None
        return first, min(last, size - 1)

    @staticmethod
    def _delay(phase: dict, request_number: int) -> None:
        latency = float(phase.get("latency_ms", 0.0))
        jitter = float(phase.get("jitter_ms", 0.0))
        # A deterministic five-step waveform is repeatable across test runs.
        jitter_scale = ((request_number % 5) - 2) / 2.0
        delay_ms = max(0.0, latency + jitter * jitter_scale)
        if delay_ms > 0.0:
            time.sleep(delay_ms / 1000.0)

    def log_message(self, format_string: str, *args: object) -> None:
        if self.server.verbose:
            super().log_message(format_string, *args)


class AdaptiveTestServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], root: Path,
                 scenario: Scenario, verbose: bool):
        super().__init__(address, AdaptiveRequestHandler)
        self.root = root
        self.scenario = scenario
        self.verbose = verbose


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="Directory exposed over HTTP")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--scenario", type=Path,
                        help="JSON phase description; defaults to a built-in ramp")
    parser.add_argument("--log", type=Path,
                        help="Optional JSONL request log")
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    root = arguments.root.expanduser().resolve()
    if not root.is_dir():
        raise SystemExit(f"Server root is not a directory: {root}")
    contents = DEFAULT_SCENARIO
    if arguments.scenario:
        contents = json.loads(arguments.scenario.read_text(encoding="utf-8"))
    scenario = Scenario(contents, arguments.log)
    server = AdaptiveTestServer(
        (arguments.bind, arguments.port), root, scenario, arguments.verbose)
    print(f"Serving {root} at http://{arguments.bind}:{arguments.port}/")
    print("Scenario status: /__openvolumetric/status")
    print("Reset scenario:  /__openvolumetric/reset")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
