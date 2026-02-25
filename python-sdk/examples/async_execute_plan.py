import asyncio

from novabridge_async import AsyncNovaBridge


async def main() -> None:
    async with AsyncNovaBridge(host="127.0.0.1", port=30010) as client:
        result = await client.execute_plan(
            [
                {"action": "spawn", "params": {"type": "PointLight", "label": "AsyncLight", "x": 0, "y": 0, "z": 250}},
                {"action": "screenshot", "params": {"inline": False}},
            ],
            plan_id="async-plan",
        )
        print(result)


if __name__ == "__main__":
    asyncio.run(main())
