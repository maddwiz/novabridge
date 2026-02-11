# Build Status

## Last Updated

- Date: 2026-02-11
- Environment: Linux ARM64 host (`aarch64`)

## Completed

- Linux ARM64: `UnrealEditor LinuxArm64 Development -SkipPreBuildTargets` completed successfully.
- Runtime validation passed on ARM64:
  - `/nova/health` returns 30 routes.
  - `/nova/project/info` returns loaded project path.
  - raw screenshot and CORS behavior verified.

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
