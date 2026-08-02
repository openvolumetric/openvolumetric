#!/usr/bin/env python3
"""Run native HTTP byte-source tests against deterministic local failures."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


RESOURCE_SIZE = 128 * 1024
RESOURCE = bytes(index % 251 for index in range(RESOURCE_SIZE))


class RangeHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    recover_requests = 0

    def log_message(self, _format: str, *_args: object) -> None:
        pass

    def do_HEAD(self) -> None:  # noqa: N802
        self.send_response(200)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(RESOURCE_SIZE))
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/recover.bin":
            type(self).recover_requests += 1
            if type(self).recover_requests <= 2:
                self._empty_error(503)
                return
        elif self.path == "/exhaust.bin":
            self._empty_error(503)
            return

        match = re.fullmatch(r"bytes=(\d+)-(\d+)", self.headers.get("Range", ""))
        if match is None:
            self._empty_error(416)
            return
        start = int(match.group(1))
        end = min(int(match.group(2)), RESOURCE_SIZE - 1)
        if start < 0 or start > end or start >= RESOURCE_SIZE:
            self._empty_error(416)
            return
        if self.path == "/slow.bin" and start >= 64 * 1024:
            time.sleep(2.0)

        payload = RESOURCE[start:end + 1]
        if self.path == "/truncated.bin":
            payload = payload[:max(1, len(payload) // 2)]
        self.send_response(206)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Range", f"bytes {start}-{end}/{RESOURCE_SIZE}")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        try:
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _empty_error(self, status: int) -> None:
        self.send_response(status)
        self.send_header("Content-Length", "0")
        self.end_headers()


def main() -> int:
    if len(sys.argv) != 2:
        print("Expected the native transport-test executable.", file=sys.stderr)
        return 2
    executable = Path(sys.argv[1]).resolve()
    if not executable.is_file():
        print(f"Transport-test executable does not exist: {executable}",
              file=sys.stderr)
        return 2

    server = ThreadingHTTPServer(("127.0.0.1", 0), RangeHandler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    environment = os.environ.copy()
    environment["NO_PROXY"] = "127.0.0.1,localhost"
    environment["no_proxy"] = "127.0.0.1,localhost"
    base_url = f"http://127.0.0.1:{server.server_port}"
    try:
        completed = subprocess.run(
            [str(executable), base_url],
            env=environment,
            timeout=20,
            check=False,
        )
        return completed.returncode
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=2)


if __name__ == "__main__":
    raise SystemExit(main())
