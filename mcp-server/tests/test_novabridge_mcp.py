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
        self.last_execute_plan = None
        self.last_runtime_pair = None
        self.last_undo = None
        self.last_assistant_plan = None
        self.last_assistant_execute = None

    def spawn(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        self.last_spawn = {"args": args, "kwargs": kwargs}
        return {"status": "ok", "action": "spawn"}

    def execute_plan(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        self.last_execute_plan = {"args": args, "kwargs": kwargs}
        return {"status": "ok", "action": "executePlan"}

    def runtime_pair(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        self.last_runtime_pair = {"args": args, "kwargs": kwargs}
        return {"status": "ok", "action": "runtime.pair"}

    def undo(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        self.last_undo = {"args": args, "kwargs": kwargs}
        return {"status": "ok", "action": "undo"}

    def assistant_health(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        return {"status": "ok", "service": "assistant"}

    def assistant_catalog(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        return {"status": "ok", "catalog": []}

    def assistant_plan(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        self.last_assistant_plan = {"args": args, "kwargs": kwargs}
        return {"status": "ok", "plan": {"steps": []}}

    def assistant_execute(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        self.last_assistant_execute = {"args": args, "kwargs": kwargs}
        return {"status": "ok", "result": "executed"}


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

    def test_execute_plan_tool_forwards_plan_payload(self) -> None:
        stub = _StubClient()
        original_client = self.mod.client
        self.mod.client = stub
        try:
            result = self.mod.ue5_execute_plan(
                [{"action": "spawn", "params": {"type": "PointLight"}}],
                plan_id="mcp-plan",
                role="automation",
            )
        finally:
            self.mod.client = original_client

        self.assertEqual(result["status"], "ok")
        self.assertIsNotNone(stub.last_execute_plan)
        self.assertEqual(stub.last_execute_plan["kwargs"]["plan_id"], "mcp-plan")
        self.assertEqual(stub.last_execute_plan["kwargs"]["role"], "automation")

    def test_runtime_pair_and_undo_tools_forward_arguments(self) -> None:
        stub = _StubClient()
        original_client = self.mod.client
        self.mod.client = stub
        try:
            pair_result = self.mod.ue5_runtime_pair("123456", role="admin")
            undo_result = self.mod.ue5_undo(role="automation")
        finally:
            self.mod.client = original_client

        self.assertEqual(pair_result["status"], "ok")
        self.assertEqual(undo_result["status"], "ok")
        self.assertIsNotNone(stub.last_runtime_pair)
        self.assertEqual(stub.last_runtime_pair["kwargs"]["code"], "123456")
        self.assertEqual(stub.last_runtime_pair["kwargs"]["role"], "admin")
        self.assertIsNotNone(stub.last_undo)
        self.assertEqual(stub.last_undo["kwargs"]["role"], "automation")

    def test_assistant_tools_forward_arguments(self) -> None:
        stub = _StubClient()
        original_client = self.mod.client
        self.mod.client = stub
        try:
            health = self.mod.ue5_assistant_health()
            catalog = self.mod.ue5_assistant_catalog()
            plan_result = self.mod.ue5_assistant_plan("spawn a light", mode="editor")
            execute_result = self.mod.ue5_assistant_execute(
                {"plan_id": "p1", "mode": "editor", "steps": []},
                allow_high_risk=True,
                risk={"highest_risk": "high"},
            )
        finally:
            self.mod.client = original_client

        self.assertEqual(health["status"], "ok")
        self.assertEqual(catalog["status"], "ok")
        self.assertEqual(plan_result["status"], "ok")
        self.assertEqual(execute_result["status"], "ok")
        self.assertIsNotNone(stub.last_assistant_plan)
        self.assertEqual(stub.last_assistant_plan["kwargs"]["prompt"], "spawn a light")
        self.assertEqual(stub.last_assistant_plan["kwargs"]["mode"], "editor")
        self.assertIsNotNone(stub.last_assistant_execute)
        self.assertEqual(stub.last_assistant_execute["kwargs"]["allow_high_risk"], True)


if __name__ == "__main__":
    unittest.main()
