from __future__ import annotations

import importlib.util
import pathlib
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "mcp-server" / "novabridge_mcp.py"


class _FakeFastMCP:
    def __init__(self, _name: str):
        self.name = _name

    def tool(self):  # type: ignore[no-untyped-def]
        def decorator(fn):  # type: ignore[no-untyped-def]
            return fn

        return decorator

    def run(self) -> None:
        return None


def _load_module():  # type: ignore[no-untyped-def]
    fake_mcp = types.ModuleType("mcp")
    fake_server = types.ModuleType("mcp.server")
    fake_fastmcp = types.ModuleType("mcp.server.fastmcp")
    fake_fastmcp.FastMCP = _FakeFastMCP

    sys.modules["mcp"] = fake_mcp
    sys.modules["mcp.server"] = fake_server
    sys.modules["mcp.server.fastmcp"] = fake_fastmcp

    spec = importlib.util.spec_from_file_location("novabridge_mcp_test", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Failed to load module spec from {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)  # type: ignore[attr-defined]
    return module


class _StubClient:
    def __init__(self):
        self.last_spawn = None

    def spawn(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        self.last_spawn = {"args": args, "kwargs": kwargs}
        return {"status": "ok", "action": "spawn"}


class NovaBridgeMcpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = _load_module()

    def test_wrap_converts_novabridge_error_to_error_payload(self) -> None:
        NovaBridgeError = self.mod.NovaBridgeError

        def raise_error():  # type: ignore[no-untyped-def]
            raise NovaBridgeError("denied")

        result = self.mod._wrap(raise_error)
        self.assertEqual(result["status"], "error")
        self.assertIn("denied", result["error"])

    def test_spawn_tool_forwards_arguments_to_client(self) -> None:
        stub = _StubClient()
        original_client = self.mod.client
        self.mod.client = stub
        try:
            result = self.mod.ue5_spawn(
                "PointLight",
                label="MCPTestLight",
                x=1.0,
                y=2.0,
                z=3.0,
                pitch=4.0,
                yaw=5.0,
                roll=6.0,
            )
        finally:
            self.mod.client = original_client

        self.assertEqual(result["status"], "ok")
        self.assertIsNotNone(stub.last_spawn)
        self.assertEqual(stub.last_spawn["args"][0], "PointLight")
        self.assertEqual(stub.last_spawn["kwargs"]["label"], "MCPTestLight")
        self.assertEqual(stub.last_spawn["kwargs"]["x"], 1.0)
        self.assertEqual(stub.last_spawn["kwargs"]["roll"], 6.0)


if __name__ == "__main__":
    unittest.main()
