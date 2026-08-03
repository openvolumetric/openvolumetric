#!/usr/bin/env python3
"""Serve an OpenVolumetric package or media file over range-capable HTTP.

This is the normal, unimpaired package server. Use adaptive_test_server.py
when deterministic bandwidth, latency, outage, or request-failure phases are
required. Both servers default to all interfaces on port 8000.
"""

from __future__ import annotations

import argparse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import mimetypes
from pathlib import Path
import socket
from urllib.parse import unquote, urlsplit


COPY_CHUNK_BYTES = 1024 * 1024


def resolve_display_host(bind_address: str) -> str:
    """Return the address clients should use to reach this host.

    A wildcard bind accepts traffic on every interface but is not itself a
    usable client destination. A UDP route lookup obtains the address of the
    currently preferred interface without sending any network traffic.
    """
    if bind_address not in ("", "0.0.0.0"):
        return bind_address
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route_socket:
            route_socket.connect(("192.0.2.1", 9))
            return str(route_socket.getsockname()[0])
    except OSError:
        return "<host-address>"


class PackageRequestHandler(BaseHTTPRequestHandler):
    """Serve files below the configured package root without path traversal."""

    server_version = "OpenVolumetricPackageServer/1.0"
    protocol_version = "HTTP/1.1"

    def handle(self) -> None:
        """Treat player seek/cancellation disconnects as normal operation."""
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
        self._serve(send_body=True)

    def _resolve_path(self) -> Path | None:
        relative = unquote(urlsplit(self.path).path).lstrip("/")
        if self.server.single_file is not None:
            if relative in ("", self.server.single_file.name):
                return self.server.single_file
            return None
        relative = relative or "manifest.json"
        candidate = (self.server.root / relative).resolve()
        try:
            candidate.relative_to(self.server.root)
        except ValueError:
            return None
        return candidate if candidate.is_file() else None

    def _serve(self, send_body: bool) -> None:
        path = self._resolve_path()
        if path is None:
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        size = path.stat().st_size
        first, last = self._parse_range(size)
        if first is None:
            self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
            self.send_header("Content-Range", f"bytes */{size}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        partial = first != 0 or last != size - 1
        status = HTTPStatus.PARTIAL_CONTENT if partial else HTTPStatus.OK
        length = last - first + 1
        self.send_response(status)
        self.send_header(
            "Content-Type",
            mimetypes.guess_type(path.name)[0] or "application/octet-stream",
        )
        self.send_header("Content-Length", str(length))
        if partial:
            self.send_header("Content-Range", f"bytes {first}-{last}/{size}")
        self.end_headers()
        if not send_body:
            return

        try:
            with path.open("rb") as source:
                source.seek(first)
                remaining = length
                while remaining > 0:
                    chunk = source.read(min(COPY_CHUNK_BYTES, remaining))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    remaining -= len(chunk)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def _parse_range(self, size: int) -> tuple[int | None, int | None]:
        value = self.headers.get("Range")
        if not value:
            return 0, size - 1
        if not value.startswith("bytes=") or "," in value:
            return None, None
        try:
            start_text, end_text = value[6:].split("-", 1)
            if not start_text:
                suffix = int(end_text)
                if suffix <= 0:
                    return None, None
                return max(0, size - suffix), size - 1
            first = int(start_text)
            last = int(end_text) if end_text else size - 1
        except ValueError:
            return None, None
        if first < 0 or first >= size or last < first:
            return None, None
        return first, min(last, size - 1)

    def log_message(self, format_string: str, *args: object) -> None:
        if self.server.verbose:
            super().log_message(format_string, *args)


class PackageServer(ThreadingHTTPServer):
    """Threaded package server carrying immutable process configuration."""

    daemon_threads = True

    def __init__(self, address: tuple[str, int], root: Path, verbose: bool,
                 single_file: Path | None = None):
        super().__init__(address, PackageRequestHandler)
        self.root = root
        self.verbose = verbose
        self.single_file = single_file


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "source",
        type=Path,
        help="Adaptive package directory or a single OpenVolumetric media file",
    )
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    source = arguments.source.expanduser().resolve()
    single_file = source if source.is_file() else None
    if single_file is not None:
        root = single_file.parent
    elif source.is_dir():
        root = source
        if not (root / "manifest.json").is_file():
            raise SystemExit(f"Package does not contain manifest.json: {root}")
    else:
        raise SystemExit(f"Source does not exist: {source}")

    server = PackageServer(
        (arguments.bind, arguments.port), root, arguments.verbose, single_file)
    display_host = resolve_display_host(arguments.bind)
    if single_file is not None:
        print(
            f"Serving file {single_file} at "
            f"http://{display_host}:{arguments.port}/{single_file.name}"
        )
        print(f"Root URL: http://{display_host}:{arguments.port}/")
    else:
        print(
            f"Serving package {root} at "
            f"http://{display_host}:{arguments.port}/"
        )
        print(
            f"Manifest URL: "
            f"http://{display_host}:{arguments.port}/manifest.json"
        )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
