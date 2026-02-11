# Build Status

## Last Updated

- Date: 2026-02-11
- Environment: Linux ARM64 host (`aarch64`)

## Completed

- Linux ARM64 clean rebuild completed successfully:
  - `UnrealEditor LinuxArm64 Development -Clean`
  - `UnrealEditor LinuxArm64 Development -SkipPreBuildTargets`
- Runtime validation passed on ARM64:
  - `nova-ue5-editor.service` is active and serving on port `30010`.
  - `/nova/health` returns `status: ok` with 30 routes.
  - `/nova/project/info` returns loaded project fields.
  - `/nova/viewport/screenshot?format=raw` returns `image/png`.
  - `OPTIONS /nova/scene/list` returns expected CORS headers.
  - `POST /nova/asset/import` with OBJ + `scale` succeeds.
  - `POST /nova/scene/set-property` alias path (`PointLightComponent0.Intensity`) succeeds.
- Packaging validation passed:
  - `scripts/package_release.sh` generated `dist/NovaBridge-v1.0.0.zip`.
  - Zip contains plugin/demo/SDK/MCP/docs/examples.
  - Zip scan found no `Intermediate/`, `Saved/`, crash, or `.log` artifacts.
- SDK/Integration smoke:
  - Python SDK `NovaBridge(host='127.0.0.1', port=30010).health()` succeeded.
  - MCP server dependency installed in isolated venv and tool registry lists 12 tools.

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
