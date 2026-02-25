# NovaBridge Quick Start (v1.0.2)

## 1) Setup

One-click (no terminal):

- macOS/Linux: double-click `NovaBridge-OneClick.command`
- Windows: double-click `NovaBridge-OneClick.bat`
- Optional config: copy `novabridge.env.example` -> `novabridge.env`

Terminal setup:

macOS/Linux:

```bash
./scripts/setup.sh
```

Windows:

```powershell
./scripts/setup_win.ps1
```

## 2) Health Check

```bash
curl http://127.0.0.1:30010/nova/health
curl http://127.0.0.1:30010/nova/caps
```

## 3) Spawn Test

```bash
curl -X POST http://127.0.0.1:30010/nova/scene/spawn \
  -H "Content-Type: application/json" \
  -d '{"class":"PointLight","label":"LaunchSmokeLight","x":0,"y":0,"z":260}'
```

## 4) Execute Plan Test

```bash
curl -X POST http://127.0.0.1:30010/nova/executePlan \
  -H "Content-Type: application/json" \
  -d '{"plan_id":"quickstart","steps":[{"action":"spawn","params":{"type":"PointLight","label":"QS_Light","x":100,"y":0,"z":250}}]}'
```

## 5) Optional Runtime Check

```bash
YourGame -NovaBridgeRuntime=1 -NovaBridgeRuntimePort=30020
curl -X POST http://127.0.0.1:30020/nova/runtime/pair -H "Content-Type: application/json" -d '{"code":"123456"}'
```

## 6) Package Release Bundle

```bash
./scripts/package_release.sh 1.0.2
```

## 7) Docker Harness

```bash
docker run --rm -p 8080:8080 ghcr.io/maddwiz/novabridge:latest
```
