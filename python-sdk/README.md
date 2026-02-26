# NovaBridge Python SDK

Python SDK for NovaBridge with:
- sync client (`novabridge.py`)
- async client (`novabridge_async.py`)
- pydantic models (`novabridge_models.py`)
- CLI (`novabridge-cli`)
- assistant/planner convenience methods (`/assistant/*`)

## Install

```bash
cd python-sdk
python -m pip install .
```

## Sync Client

```python
from novabridge import NovaBridge

ue5 = NovaBridge(host="127.0.0.1", port=30010, api_key="replace-if-needed")
print(ue5.health())
print(ue5.caps())

result = ue5.execute_plan(
    [
        {"action": "spawn", "params": {"type": "PointLight", "label": "LaunchSmokeLight", "x": 0, "y": 0, "z": 260}},
        {"action": "screenshot", "params": {"width": 1280, "height": 720}},
    ],
    plan_id="sdk-demo",
    role="automation",
)
print(result)

print(ue5.assistant_health())
print(ue5.assistant_plan("spawn a point light near origin", mode="editor"))
```

## Async Client

```python
import asyncio
from novabridge_async import AsyncNovaBridge

async def main() -> None:
    async with AsyncNovaBridge(host="127.0.0.1", port=30010) as ue5:
        print(await ue5.health())
        print(await ue5.execute_plan([{"action": "spawn", "params": {"type": "PointLight", "label": "AsyncLight"}}]))

asyncio.run(main())
```

## CLI

```bash
novabridge-cli --host 127.0.0.1 --port 30010 health
novabridge-cli caps
novabridge-cli spawn-actor PointLight --label LaunchSmokeLight --x 0 --y 0 --z 260
novabridge-cli execute-plan --plan-file ./examples/plan.json
novabridge-cli screenshot --output viewport.png
novabridge-cli runtime-pair 123456 --plan-role automation --port 30020
novabridge-cli assistant-health
novabridge-cli assistant-plan "build a simple lighting pass" --mode editor
```

## Retry/Error Handling

Sync and async clients include:
- transport retry (`URLError` / `aiohttp.ClientError`)
- retry on `429`, `500`, `502`, `503`, `504`
- exponential backoff (`retry_backoff * 2^attempt`)

Tune with:
- `max_retries`
- `retry_backoff`

## Examples

See `python-sdk/examples/`:
- `health_check.py`
- `spawn_point_light.py`
- `execute_plan_from_file.py`
- `runtime_pair_and_health.py`
- `capture_screenshot.py`
- `bulk_spawn_grid.py`
- `undo_last_action.py`
- `scene_snapshot.py`
- `async_execute_plan.py`

## Tests

```bash
python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'
```
