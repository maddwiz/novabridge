# Self-Hosted CI for Mac + Windows + Linux

This repo includes GitHub Actions workflow:

- `.github/workflows/build-plugin-self-hosted.yml`

Bootstrap commands are in:

- `docs/RUNNER_SETUP.md`
- `scripts/ci/setup_runner_mac.sh`
- `scripts/ci/setup_runner_win.ps1`

It builds NovaBridge plugin packages on your own machines:

- macOS ARM64 runner (M1/M2 MacBook)
- Windows x64 runner
- Linux x64 runner (optional)

This can be physical hardware or virtual/cloud computers.

## 1) Prepare Mac Runner (M1/M2)

1. Install Unreal Engine (5.1+).
2. Install GitHub Actions self-hosted runner on the Mac.
3. Add runner labels:
   - `self-hosted`
   - `macOS`
   - `ARM64`
   - `unreal`
4. Set environment variable on the runner host:
   - `UE_ROOT=/path/to/UnrealEngine`

## 2) Prepare Windows Runner

1. Install Unreal Engine (5.1+).
2. Install GitHub Actions self-hosted runner on Windows.
3. Add runner labels:
   - `self-hosted`
   - `Windows`
   - `X64`
   - `unreal`
4. Set environment variable on runner host:
   - `UE_ROOT=C:\Path\To\UnrealEngine`

## 3) Prepare Linux Runner (Optional)

1. Install Unreal Engine (5.1+).
2. Install GitHub Actions self-hosted runner on Linux.
3. Add runner labels:
   - `self-hosted`
   - `Linux`
   - `X64`
   - `unreal`
4. Set environment variable on runner host:
   - `UE_ROOT=/path/to/UnrealEngine`

## 4) Trigger Build

From GitHub Actions:

- Run workflow: `Build Plugin (Self-Hosted)`
- Optional inputs if you need per-run overrides:
  - `ue_root_mac`
  - `ue_root_win`
  - `ue_root_linux`
  - `build_linux` (defaults to false for manual runs)

Artifacts produced:

- `NovaBridge-Mac`
- `NovaBridge-Win64`
- `NovaBridge-Linux` (if Linux job is enabled)

Each artifact includes:

- zipped packaged plugin
- `manifest.txt` with build metadata

## 5) Notes

- This workflow uses your local Unreal installs; no cloud Unreal license setup is required.
- If a job says no runner found, verify labels and runner online status.
- If Unreal path fails, set `UE_ROOT` correctly or pass workflow input overrides.
- Push-triggered Mac/Win jobs are disabled until `ENABLE_SELF_HOSTED_BUILDS=true`.
- Push-triggered Linux job is disabled until `ENABLE_SELF_HOSTED_LINUX_BUILDS=true`.

## 6) Using Virtual Computers

- Windows is straightforward on cloud VMs. Install UE + runner, then add labels.
- macOS builds require Apple hardware backing. In practice this means:
  - your own Mac (local runner), or
  - a cloud Mac service / dedicated Mac instance.
- Once both runners are online, GitHub Actions treats them the same as local machines.
