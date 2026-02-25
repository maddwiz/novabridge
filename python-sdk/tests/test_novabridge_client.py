from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch


SDK_ROOT = Path(__file__).resolve().parents[1]
if str(SDK_ROOT) not in sys.path:
    sys.path.insert(0, str(SDK_ROOT))

from novabridge import NovaBridge  # noqa: E402


class _FakeResponse:
    def __init__(self, payload: bytes):
        self._payload = payload

    def read(self) -> bytes:
        return self._payload

    def __enter__(self) -> "_FakeResponse":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        return None


class NovaBridgeClientTests(unittest.TestCase):
    def test_health_request_sends_auth_role_and_runtime_token_headers(self) -> None:
        captured = {}

        def fake_urlopen(req, timeout):  # type: ignore[no-untyped-def]
            captured["req"] = req
            captured["timeout"] = timeout
            return _FakeResponse(json.dumps({"status": "ok"}).encode("utf-8"))

        client = NovaBridge(
            host="127.0.0.1",
            port=30123,
            api_key="k_test",
            role="automation",
            runtime_token="tok_test",
            timeout=17,
        )

        with patch("urllib.request.urlopen", side_effect=fake_urlopen):
            result = client.health()

        self.assertEqual(result["status"], "ok")
        req = captured["req"]
        self.assertEqual(req.get_header("X-api-key"), "k_test")
        self.assertEqual(req.get_header("X-novabridge-role"), "automation")
        self.assertEqual(req.get_header("X-novabridge-token"), "tok_test")
        self.assertEqual(req.full_url, "http://127.0.0.1:30123/nova/health")
        self.assertEqual(captured["timeout"], 17)

    def test_raw_viewport_screenshot_sends_auth_headers(self) -> None:
        captured = {}
        png_bytes = b"\x89PNG\r\n\x1a\nraw-bytes"

        def fake_urlopen(req, timeout):  # type: ignore[no-untyped-def]
            captured["req"] = req
            captured["timeout"] = timeout
            return _FakeResponse(png_bytes)

        client = NovaBridge(
            host="127.0.0.1",
            port=30124,
            api_key="k_raw",
            role="admin",
            runtime_token="tok_raw",
            timeout=23,
        )

        with patch("urllib.request.urlopen", side_effect=fake_urlopen):
            result = client.viewport_screenshot(raw=True, width=640, height=360)

        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["bytes"], len(png_bytes))
        req = captured["req"]
        self.assertEqual(req.get_header("X-api-key"), "k_raw")
        self.assertEqual(req.get_header("X-novabridge-role"), "admin")
        self.assertEqual(req.get_header("X-novabridge-token"), "tok_raw")
        self.assertIn(
            "http://127.0.0.1:30124/nova/viewport/screenshot?width=640&height=360&format=raw",
            req.full_url,
        )
        self.assertEqual(captured["timeout"], 23)


if __name__ == "__main__":
    unittest.main()
