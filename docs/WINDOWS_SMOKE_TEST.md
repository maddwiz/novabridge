# Windows Smoke Test (Win64)

## Run Info

- Date: 2026-02-20
- Host: DESKTOP-QNVIB5M
- UE Version: 5.7.3 (`C:\Program Files\Epic Games\UE_5.7`)
- Project: `NovaBridgeDefault.uproject`
- Ports: 30010 (source), 30011 (packaged)

## Source Plugin Validation

1. Build plugin + editor:
   - `Build.bat UnrealEditor Win64 Development -Project="<...>\\NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
2. Launch UE headless:
   - `UnrealEditor.exe "<...>\\NovaBridgeDefault.uproject" -dx12 -RenderOffScreen -nosplash -nosound -unattended -nopause -NovaBridgePort=30010`
3. Smoke checks:
   - `GET /nova/health` -> `status=ok`
   - `POST /nova/scene/spawn` -> created `GoldenPathLight`
   - `POST /nova/mesh/primitive` -> created `WinSmokeCube`
   - `POST /nova/asset/import` (OBJ + `scale=100`) -> `status=ok`
   - `GET /nova/viewport/screenshot?format=raw` -> PNG bytes
   - `POST /nova/scene/delete` -> `status=ok`

Evidence: `artifacts-win/run-20260219-200626/`

## Packaged Plugin Validation

1. Build package:
   - `pwsh scripts/package_release_win.ps1 -Version v0.9.0`
2. Create clean test project and install packaged plugin.
3. Build plugin + editor:
   - `Build.bat UnrealEditor Win64 Development -Project="<...>\\WinSmokePackaged\\NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
4. Launch UE headless:
   - `UnrealEditor.exe "<...>\\WinSmokePackaged\\NovaBridgeDefault.uproject" -dx12 -RenderOffScreen -nosplash -nosound -unattended -nopause -NovaBridgePort=30011`
5. Smoke checks:
   - `GET /nova/health` -> `status=ok`
   - `POST /nova/scene/spawn` -> created `PackagedLight`
   - `POST /nova/asset/import` (OBJ + `scale=100`) -> `status=ok`
   - `GET /nova/viewport/screenshot?format=raw` -> PNG bytes
   - `POST /nova/scene/delete` -> `status=ok`

Evidence: `artifacts-win/run-20260219-200626/packaged-validation-20260219-201939/`
