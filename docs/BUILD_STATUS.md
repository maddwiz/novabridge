# Build Status

## Last Updated

- Date: 2026-02-18
- Environments:
  - Linux ARM64 host (`aarch64`) validation previously completed.
  - macOS native validation completed on `MacBookPro17,1` (Apple M1, 8 GB RAM), macOS `15.6.1` (`24G90`), Xcode `26.2` (`17C52`), Unreal Engine `5.6.1-44394996`.

## macOS Validation (Completed)

### Toolchain discovery and setup

- UE editor binary:
  - `/Users/Shared/Epic Games/UE_5.6/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor`
- Xcode and command line tools verified:
  - `xcode-select -p`
  - `xcodebuild -version`
- Required one-time fix on this Mac before Unreal launch:
  - `xcodebuild -downloadComponent MetalToolchain`

### Source plugin smoke (project copy + `Plugins/NovaBridge`)

- Run directory:
  - `artifacts-mac/run-20260218-112826`
- Source test project:
  - `artifacts-mac/run-20260218-112826/MacSmokeSource/NovaBridgeDefault.uproject`
- Build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="<...>/MacSmokeSource/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Runtime launch command:
  - `UnrealEditor <...>/MacSmokeSource/NovaBridgeDefault.uproject -metal -RenderOffScreen -nosplash -nosound -unattended -nopause -NovaBridgePort=30010 -stdout -FullStdOutLogOutput -log`

Passed runtime checks on port `30010`:
- `GET /nova/health` => `status=ok`, `version=0.9.0`, `routes=30`
- `POST /nova/scene/spawn` => created `PointLight_0` (`MacSmokeLight`)
- `POST /nova/asset/import` with local OBJ + `scale=100` => `status=ok`, asset `/Game/MacSmokeMesh.MacSmokeMesh`
- `GET /nova/viewport/screenshot?format=raw` => PNG bytes (`89 50 4E 47 ...`)
- `POST /nova/scene/delete` cleanup => `status=ok`

Evidence:
- `artifacts-mac/run-20260218-112826/source-validation/health.pretty.json`
- `artifacts-mac/run-20260218-112826/source-validation/spawn.pretty.json`
- `artifacts-mac/run-20260218-112826/source-validation/import.pretty.json`
- `artifacts-mac/run-20260218-112826/source-validation/delete.pretty.json`
- `artifacts-mac/run-20260218-112826/source-validation/screenshot-source.png`
- `artifacts-mac/run-20260218-112826/source-validation/screenshot-magic.txt`
- `artifacts-mac/run-20260218-112826/source-validation/unreal-source.log`

### Golden Path on macOS (Completed)

- Script used (mac-compatible updates applied):
  - `examples/curl/golden_path.sh`
- Command:
  - `NOVABRIDGE_HOST=127.0.0.1 NOVABRIDGE_PORT=30010 bash examples/curl/golden_path.sh`
- Result:
  - Full `[1/6]` through `[6/6]` pass
  - Screenshot output valid PNG

Evidence:
- `artifacts-mac/run-20260218-112826/source-validation/golden-path.txt`
- `artifacts-mac/run-20260218-112826/source-validation/golden-path-screenshot.png`

### Packaging and packaged-plugin retest (Completed)

- Packaging command:
  - `./scripts/package_release.sh v0.9.0`
- Package output:
  - `dist/NovaBridge-v0.9.0.zip`

Packaged-plugin second project validation:
- Second clean project:
  - `artifacts-mac/run-20260218-112826/MacSmokePackaged/NovaBridgeDefault.uproject`
- Packaged plugin installed from:
  - `artifacts-mac/run-20260218-112826/packaged-validation/unzipped/NovaBridge-v0.9.0/NovaBridge`
- Build command:
  - `Build.sh UnrealEditor Mac Development -Project="<...>/MacSmokePackaged/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Runtime launch command:
  - `UnrealEditor <...>/MacSmokePackaged/NovaBridgeDefault.uproject -metal -RenderOffScreen -nosplash -nosound -unattended -nopause -NovaBridgePort=30011 -stdout -FullStdOutLogOutput -log`

Passed runtime checks on port `30011`:
- `GET /nova/health` => `status=ok`, `version=0.9.0`, `routes=30`
- `POST /nova/scene/spawn` => created `MacPackagedLight`
- `POST /nova/asset/import` with OBJ + `scale=100` => `status=ok`
- `GET /nova/viewport/screenshot?format=raw` => valid PNG bytes
- `POST /nova/scene/delete` cleanup => `status=ok`

Package hygiene:
- Zip content scanned and verified no `Intermediate/`, `Saved/`, `.log`, or `docs/NOVABRIDGE_HANDOFF.md`.

Evidence:
- `artifacts-mac/run-20260218-112826/packaged-validation/build-packaged.log`
- `artifacts-mac/run-20260218-112826/packaged-validation/health.pretty.json`
- `artifacts-mac/run-20260218-112826/packaged-validation/import.pretty.json`
- `artifacts-mac/run-20260218-112826/packaged-validation/screenshot-packaged.png`
- `artifacts-mac/run-20260218-112826/packaged-validation/screenshot-magic.txt`
- `artifacts-mac/run-20260218-112826/packaged-validation/zip-contents.txt`

## Linux ARM64 Validation (Previously Completed)

- Linux ARM64 clean rebuild completed successfully:
  - `UnrealEditor LinuxArm64 Development -Clean`
  - `UnrealEditor LinuxArm64 Development -SkipPreBuildTargets`
- Runtime validation passed on ARM64:
  - Golden path smoke script succeeded.
  - `nova-ue5-editor.service` active on port `30010`.
  - `/nova/health` returns `status: ok`, `version: 0.9.0`, and 30 routes.
  - `/nova/project/info` returns loaded project fields.
  - `/nova/viewport/screenshot?format=raw` returns `image/png`.
  - `OPTIONS /nova/scene/list` returns expected CORS headers.
  - `POST /nova/asset/import` with OBJ + `scale` succeeds.
  - `POST /nova/scene/set-property` alias path succeeds.
  - Optional API key mode validated (`401` without key, `200` with key).

## Outstanding

- Linux x86_64 native smoke remains blocked on ARM host dependency (`TextureFormatOodle ... liboo2texlinux64.2.9.6.so`).
- Windows Win64 native smoke remains pending native Windows validation host.
