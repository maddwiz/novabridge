from __future__ import annotations

import json
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from socketserver import ThreadingMixIn
from typing import Any, Dict, List
from urllib.parse import parse_qs, urlparse


SDK_ROOT = Path(__file__).resolve().parents[1]
if str(SDK_ROOT) not in sys.path:
    sys.path.insert(0, str(SDK_ROOT))

from novabridge import NovaBridge  # noqa: E402


class _ThreadedHttpServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


class _RecordingHandler(BaseHTTPRequestHandler):
    server_version = "NovaBridgeMock/1.0"
    protocol_version = "HTTP/1.1"

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0:
            return b""
        return self.rfile.read(length)

    def _record(self, body: bytes) -> None:
        self.server.records.append(  # type: ignore[attr-defined]
            {
                "method": self.command,
                "path": self.path,
                "headers": {k.lower(): v for k, v in self.headers.items()},
                "body": body,
            }
        )

    def _write_json(self, payload: Dict[str, Any], status: int = 200) -> None:
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self) -> None:  # noqa: N802
        body = self._read_body()
        self._record(body)
        route = urlparse(self.path).path

        if route == "/nova/health":
            self._write_json({"status": "ok"})
            return
        if route == "/nova/viewport/screenshot":
            png = b"\x89PNG\r\n\x1a\nmock-png"
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(png)))
            self.end_headers()
            self.wfile.write(png)
            return
        self._write_json({"error": "not found"}, status=404)

    def do_POST(self) -> None:  # noqa: N802
        body = self._read_body()
        self._record(body)
        route = urlparse(self.path).path

        if route == "/nova/scene/spawn":
            self._write_json({"status": "ok", "name": "MockActor_1"})
            return
        if route == "/nova/executePlan":
            self._write_json({"status": "ok", "results": []})
            return
        self._write_json({"error": "not found"}, status=404)

    def log_message(self, format: str, *args: Any) -> None:  # noqa: A003
        return


class NovaBridgeHeaderIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.server = _ThreadedHttpServer(("127.0.0.1", 0), _RecordingHandler)
        cls.server.records = []  # type: ignore[attr-defined]
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.port = cls.server.server_port

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def setUp(self) -> None:
        self.server.records.clear()  # type: ignore[attr-defined]

    def _find_route(self, method: str, route: str) -> Dict[str, Any]:
        records: List[Dict[str, Any]] = list(self.server.records)  # type: ignore[attr-defined]
        for record in records:
            parsed = urlparse(record["path"])
            if record["method"] == method and parsed.path == route:
                return record
        raise AssertionError(f"Missing request {method} {route}. Seen: {[r['path'] for r in records]}")

    def test_headers_are_forwarded_for_json_and_raw_requests(self) -> None:
        client = NovaBridge(
            host="127.0.0.1",
            port=self.port,
            api_key="k_live",
            role="read_only",
            runtime_token="tok_live",
            timeout=10,
        )

        health = client.health()
        self.assertEqual(health["status"], "ok")

        spawn = client.spawn("PointLight", label="HeaderLight")
        self.assertEqual(spawn["status"], "ok")

        client.execute_plan(
            [{"action": "delete", "params": {"name": "HeaderLight"}}],
            plan_id="p1",
            role="automation",
        )

        screenshot = client.viewport_screenshot(raw=True, width=320, height=200)
        self.assertEqual(screenshot["status"], "ok")
        self.assertEqual(screenshot["format"], "png")

        health_req = self._find_route("GET", "/nova/health")
        spawn_req = self._find_route("POST", "/nova/scene/spawn")
        plan_req = self._find_route("POST", "/nova/executePlan")
        screenshot_req = self._find_route("GET", "/nova/viewport/screenshot")

        for req in [health_req, spawn_req, plan_req, screenshot_req]:
            headers = req["headers"]
            self.assertEqual(headers.get("x-api-key"), "k_live")
            self.assertEqual(headers.get("x-novabridge-token"), "tok_live")

        self.assertEqual(health_req["headers"].get("x-novabridge-role"), "read_only")
        self.assertEqual(spawn_req["headers"].get("x-novabridge-role"), "read_only")
        self.assertEqual(plan_req["headers"].get("x-novabridge-role"), "automation")
        self.assertEqual(screenshot_req["headers"].get("x-novabridge-role"), "read_only")

        self.assertEqual(spawn_req["headers"].get("content-type"), "application/json")
        self.assertGreater(int(spawn_req["headers"].get("content-length", "0")), 0)

        query = parse_qs(urlparse(screenshot_req["path"]).query)
        self.assertEqual(query.get("format"), ["raw"])
        self.assertEqual(query.get("width"), ["320"])
        self.assertEqual(query.get("height"), ["200"])


if __name__ == "__main__":
    unittest.main()
