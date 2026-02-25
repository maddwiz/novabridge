from __future__ import annotations

import json
import sys
import unittest
import urllib.error
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


class NovaBridgeRetryTests(unittest.TestCase):
    def test_health_retries_on_transport_error(self) -> None:
        calls = {"count": 0}

        def fake_urlopen(req, timeout):  # type: ignore[no-untyped-def]
            calls["count"] += 1
            if calls["count"] < 2:
                raise urllib.error.URLError("temporary failure")
            return _FakeResponse(json.dumps({"status": "ok"}).encode("utf-8"))

        client = NovaBridge(host="127.0.0.1", port=39999, max_retries=2, retry_backoff=0)

        with patch("urllib.request.urlopen", side_effect=fake_urlopen):
            result = client.health()

        self.assertEqual(result["status"], "ok")
        self.assertEqual(calls["count"], 2)

    def test_runtime_pair_role_is_forwarded(self) -> None:
        captured = {}

        def fake_urlopen(req, timeout):  # type: ignore[no-untyped-def]
            captured["body"] = req.data
            return _FakeResponse(json.dumps({"status": "ok", "token": "t1"}).encode("utf-8"))

        client = NovaBridge(host="127.0.0.1", port=39998)
        with patch("urllib.request.urlopen", side_effect=fake_urlopen):
            result = client.runtime_pair("123456", role="automation")

        self.assertEqual(result["status"], "ok")
        self.assertEqual(client.runtime_token, "t1")
        body = json.loads(captured["body"].decode("utf-8"))
        self.assertEqual(body["code"], "123456")
        self.assertEqual(body["role"], "automation")


if __name__ == "__main__":
    unittest.main()
