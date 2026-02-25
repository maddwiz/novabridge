from novabridge import NovaBridge

client = NovaBridge(host="127.0.0.1", port=30010)

steps = []
for x in range(3):
    for y in range(3):
        steps.append(
            {
                "action": "spawn",
                "params": {
                    "type": "PointLight",
                    "label": f"GridLight_{x}_{y}",
                    "x": x * 300,
                    "y": y * 300,
                    "z": 260,
                },
            }
        )

print(client.execute_plan(steps, plan_id="grid-lights"))
