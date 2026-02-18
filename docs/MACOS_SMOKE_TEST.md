# macOS Validation Checklist

Run on a real Mac before claiming production macOS support.

1. Install UE 5.3-5.5 and Xcode command line tools.
2. Create a fresh blank UE project.
3. Copy `NovaBridge/` into `Plugins/NovaBridge/`.
4. Build in Xcode and open editor.
5. Verify:
   - `GET /nova/health` returns 200.
   - `POST /nova/scene/spawn` works.
   - `POST /nova/asset/import` works with OBJ.
   - `GET /nova/viewport/screenshot?format=raw` returns PNG.
6. Package with `scripts/package_release.sh v0.9.0`.
7. Re-test using the packaged plugin zip contents on a second clean project.
8. Mark macOS as validated in `docs/RELEASE_CHECKLIST.md`.

## Latest Validation Evidence (2026-02-18)

- Machine: MacBookPro17,1 (Apple M1, 8 GB), macOS 15.6.1 (24G90)
- Toolchain: Xcode 26.2 (17C52), UE 5.6.1 (`/Users/Shared/Epic Games/UE_5.6`)
- Run artifacts: `artifacts-mac/run-20260218-112826/`

Source-plugin project (`MacSmokeSource`) results:
- Build: pass (`Build.sh UnrealEditor Mac Development -Project=<.../MacSmokeSource/NovaBridgeDefault.uproject>`)
- Runtime launch: pass (`-metal -RenderOffScreen -unattended -NovaBridgePort=30010`)
- `GET /nova/health`: pass (`status=ok`, `version=0.9.0`, `routes=30`)
- `POST /nova/scene/spawn`: pass
- `POST /nova/asset/import` with OBJ + `scale=100`: pass
- `GET /nova/viewport/screenshot?format=raw`: pass (valid PNG header)
- Cleanup (`POST /nova/scene/delete`): pass
- Golden path script (`examples/curl/golden_path.sh`): pass

Packaged-plugin project (`MacSmokePackaged`) results:
- Package build: pass (`./scripts/package_release.sh v0.9.0`)
- Zip output: `dist/NovaBridge-v0.9.0.zip`
- Build + runtime retest on port `30011`: pass for health/spawn/import/screenshot/cleanup
- Zip hygiene: verified no `Intermediate/`, `Saved/`, `.log`, or `docs/NOVABRIDGE_HANDOFF.md`
