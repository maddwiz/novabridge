# Build Status

## Last Updated

- Date: 2026-02-18
- Environment: Linux ARM64 host (`aarch64`)

## Completed

- Linux ARM64 clean rebuild completed successfully:
  - `UnrealEditor LinuxArm64 Development -Clean`
  - `UnrealEditor LinuxArm64 Development -SkipPreBuildTargets`
- Runtime validation passed on ARM64:
  - Golden path smoke script succeeded: `examples/curl/golden_path.sh`.
  - `nova-ue5-editor.service` is active and serving on port `30010`.
  - `/nova/health` returns `status: ok`, `version: 0.9.0`, and 30 routes.
  - `/nova/project/info` returns loaded project fields.
  - `/nova/viewport/screenshot?format=raw` returns `image/png`.
  - `OPTIONS /nova/scene/list` returns expected CORS headers.
  - `POST /nova/asset/import` with OBJ + `scale` succeeds.
  - `POST /nova/scene/set-property` alias path (`PointLightComponent0.Intensity`) succeeds.
  - Optional API key mode validated:
    - no key on protected route returns `401`
    - `X-API-Key` on protected route returns `200`
- Packaging validation passed:
  - `scripts/package_release.sh v0.9.0` generated `dist/NovaBridge-v0.9.0.zip`.
  - Zip contains plugin/demo/SDK/MCP/examples/customer docs.
  - Zip excludes internal handoff/checklist docs and excludes `Intermediate/`, `Saved/`, crash, and `.log` artifacts.
- SDK/Integration smoke:
  - Python SDK `NovaBridge(host='127.0.0.1', port=30010).health()` succeeded.
  - Python SDK API key header support verified in code path.
  - MCP server dependency installed in isolated venv and tool registry lists 12 tools.
  - OpenClaw extension JavaScript syntax checks pass (`node --check`).

## Attempted

- Linux x86_64 module build command:
  - `UnrealEditor Linux Development -SkipPreBuildTargets -Module=NovaBridge`

Result:
- SDK became valid after setting `LINUX_MULTIARCH_ROOT=/home/nova/UnrealEngine/linux-toolchain-v20`.
- Build then failed due missing third-party x86_64 binary dependency on this ARM machine:
  - `TextureFormatOodle ... liboo2texlinux64.2.9.6.so`

## Notes

- This is an environment artifact on ARM hosts, not a NovaBridge source regression.
- Win64 and macOS compilation require native build environments and remain to be executed on their respective platforms.
