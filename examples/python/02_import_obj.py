#!/usr/bin/env python3
"""Import OBJ and spawn it in scene."""

import argparse
import sys
from pathlib import Path

SDK_DIR = Path(__file__).resolve().parents[2] / "python-sdk"
sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("obj_path", help="Path to OBJ file")
    parser.add_argument("--asset-name", default="ImportedMesh")
    parser.add_argument("--scale", type=float, default=100.0)
    args = parser.parse_args()

    ue5 = NovaBridge()
    result = ue5.import_asset(args.obj_path, asset_name=args.asset_name, scale=args.scale)
    print("import:", result)

    actor = ue5.spawn("StaticMeshActor", label=f"{args.asset_name}_Actor", x=0, y=0, z=100)
    actor_name = actor.get("name") or actor.get("label") or f"{args.asset_name}_Actor"
    ue5.set_property(
        actor_name,
        "StaticMeshComponent0.StaticMesh",
        f"/Game/{args.asset_name}.{args.asset_name}",
    )
    print("spawned actor:", actor_name)


if __name__ == "__main__":
    main()
