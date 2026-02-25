import json
import sys
from pathlib import Path

from novabridge import NovaBridge

if len(sys.argv) < 2:
    raise SystemExit("usage: python execute_plan_from_file.py <plan.json>")

plan_path = Path(sys.argv[1])
plan = json.loads(plan_path.read_text(encoding="utf-8"))
client = NovaBridge(host="127.0.0.1", port=30010)
print(client.execute_plan(plan.get("steps", []), plan_id=plan.get("plan_id")))
