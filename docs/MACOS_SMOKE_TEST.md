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

