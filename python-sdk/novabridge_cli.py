"""Command-line interface for NovaBridge SDK."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict, Optional

from novabridge import NovaBridge, NovaBridgeError


def _load_plan(args: argparse.Namespace) -> Dict[str, Any]:
    if args.plan_file:
        raw = Path(args.plan_file).read_text(encoding="utf-8")
        payload = json.loads(raw)
        if not isinstance(payload, dict):
            raise ValueError("Plan file must contain a JSON object")
        return payload

    if args.plan_json:
        payload = json.loads(args.plan_json)
        if not isinstance(payload, dict):
            raise ValueError("--plan-json must be a JSON object")
        return payload

    raise ValueError("Provide --plan-file or --plan-json")


def _client_from_args(args: argparse.Namespace) -> NovaBridge:
    return NovaBridge(
        host=args.host,
        port=args.port,
        timeout=args.timeout,
        api_key=args.api_key,
        role=args.role,
        runtime_token=args.runtime_token,
        max_retries=args.max_retries,
        retry_backoff=args.retry_backoff,
    )


def _print(payload: Any) -> None:
    print(json.dumps(payload, indent=2, sort_keys=True))


def cmd_health(args: argparse.Namespace) -> int:
    _print(_client_from_args(args).health())
    return 0


def cmd_caps(args: argparse.Namespace) -> int:
    _print(_client_from_args(args).caps())
    return 0


def cmd_spawn(args: argparse.Namespace) -> int:
    result = _client_from_args(args).spawn(
        args.actor_class,
        x=args.x,
        y=args.y,
        z=args.z,
        pitch=args.pitch,
        yaw=args.yaw,
        roll=args.roll,
        label=args.label,
    )
    _print(result)
    return 0


def cmd_delete(args: argparse.Namespace) -> int:
    _print(_client_from_args(args).delete_actor(args.name))
    return 0


def cmd_execute_plan(args: argparse.Namespace) -> int:
    client = _client_from_args(args)
    payload = _load_plan(args)
    steps = payload.get("steps", [])
    plan_id: Optional[str] = payload.get("plan_id") or args.plan_id
    role: Optional[str] = args.plan_role
    _print(client.execute_plan(steps, plan_id=plan_id, role=role))
    return 0


def cmd_undo(args: argparse.Namespace) -> int:
    _print(_client_from_args(args).undo(role=args.plan_role))
    return 0


def cmd_pair(args: argparse.Namespace) -> int:
    _print(_client_from_args(args).runtime_pair(args.code, role=args.plan_role))
    return 0


def cmd_screenshot(args: argparse.Namespace) -> int:
    client = _client_from_args(args)
    result = client.viewport_screenshot(
        width=args.width,
        height=args.height,
        raw=args.raw,
        save_path=args.output,
    )
    _print(result)
    return 0


def _base_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="novabridge-cli", description="NovaBridge command-line interface")
    parser.add_argument("--host", default="127.0.0.1", help="NovaBridge host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=30010, help="NovaBridge port (default: 30010)")
    parser.add_argument("--timeout", type=int, default=60, help="HTTP timeout in seconds")
    parser.add_argument("--api-key", default=None, help="Optional NovaBridge API key")
    parser.add_argument("--role", default=None, help="Default role header (admin|automation|read_only)")
    parser.add_argument("--runtime-token", default=None, help="Runtime token header value")
    parser.add_argument("--max-retries", type=int, default=2, help="Network retries for 429/5xx/transport failures")
    parser.add_argument("--retry-backoff", type=float, default=0.25, help="Backoff base seconds")
    return parser


def build_parser() -> argparse.ArgumentParser:
    parser = _base_parser()
    sub = parser.add_subparsers(dest="command", required=True)

    health = sub.add_parser("health", help="GET /nova/health")
    health.set_defaults(func=cmd_health)

    caps = sub.add_parser("caps", help="GET /nova/caps")
    caps.set_defaults(func=cmd_caps)

    spawn = sub.add_parser("spawn-actor", help="POST /nova/scene/spawn")
    spawn.add_argument("actor_class", help="UE actor class (PointLight, StaticMeshActor, etc.)")
    spawn.add_argument("--label", default=None)
    spawn.add_argument("--x", type=float, default=0.0)
    spawn.add_argument("--y", type=float, default=0.0)
    spawn.add_argument("--z", type=float, default=0.0)
    spawn.add_argument("--pitch", type=float, default=0.0)
    spawn.add_argument("--yaw", type=float, default=0.0)
    spawn.add_argument("--roll", type=float, default=0.0)
    spawn.set_defaults(func=cmd_spawn)

    delete = sub.add_parser("delete-actor", help="POST /nova/scene/delete")
    delete.add_argument("name", help="Actor name or label")
    delete.set_defaults(func=cmd_delete)

    plan = sub.add_parser("execute-plan", help="POST /nova/executePlan")
    plan.add_argument("--plan-file", default=None, help="Path to JSON plan file")
    plan.add_argument("--plan-json", default=None, help="Inline JSON plan object")
    plan.add_argument("--plan-id", default=None, help="Override plan_id")
    plan.add_argument("--plan-role", default=None, help="Per-request role override")
    plan.set_defaults(func=cmd_execute_plan)

    undo = sub.add_parser("undo", help="POST /nova/undo")
    undo.add_argument("--plan-role", default=None, help="Per-request role override")
    undo.set_defaults(func=cmd_undo)

    pair = sub.add_parser("runtime-pair", help="POST /nova/runtime/pair")
    pair.add_argument("code", help="Pairing code from runtime logs")
    pair.add_argument("--plan-role", default=None, help="Requested paired role")
    pair.set_defaults(func=cmd_pair)

    screenshot = sub.add_parser("screenshot", help="GET /nova/viewport/screenshot")
    screenshot.add_argument("--width", type=int, default=None)
    screenshot.add_argument("--height", type=int, default=None)
    screenshot.add_argument("--output", default=None, help="Output PNG path")
    screenshot.add_argument("--raw", action="store_true", help="Request raw PNG bytes")
    screenshot.set_defaults(func=cmd_screenshot)

    return parser


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        return int(args.func(args))
    except (NovaBridgeError, ValueError, json.JSONDecodeError) as exc:
        print(json.dumps({"status": "error", "error": str(exc)}, indent=2))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
