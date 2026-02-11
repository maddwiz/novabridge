# Runner Setup (Mac + Windows)

Use this to connect your MacBook and Windows machine (or cloud Windows VM) as GitHub self-hosted runners.

## Before You Start

1. Repo has CI workflow at `.github/workflows/build-plugin-self-hosted.yml`.
2. CI jobs are guarded by repo variable `ENABLE_SELF_HOSTED_BUILDS`.

Set repo variables in GitHub:

- `ENABLE_SELF_HOSTED_BUILDS=true`
- `UE_ROOT_MAC=/path/to/UnrealEngine` (optional if passed via workflow input)
- `UE_ROOT_WIN=C:\Path\To\UnrealEngine` (optional if passed via workflow input)

## Get Registration Token

In GitHub:

- Repo -> `Settings` -> `Actions` -> `Runners` -> `New self-hosted runner`
- Choose the platform and copy the short-lived registration token.

Token expires quickly (roughly 1 hour), so run setup commands immediately.

## MacBook (M1/M2)

```bash
cd /path/to/novabridge
./scripts/ci/setup_runner_mac.sh \
  --repo-url https://github.com/maddwiz/novabridge \
  --token <MAC_REGISTRATION_TOKEN> \
  --ue-root /Applications/Epic/UE_5.1
```

Default labels applied:

- `self-hosted,macOS,ARM64,unreal`

## Windows Machine / VM

Open PowerShell as Administrator:

```powershell
cd C:\path\to\novabridge
.\scripts\ci\setup_runner_win.ps1 `
  -RepoUrl "https://github.com/maddwiz/novabridge" `
  -Token "<WIN_REGISTRATION_TOKEN>" `
  -UERoot "C:\Program Files\Epic Games\UE_5.1"
```

Default labels applied:

- `self-hosted,Windows,X64,unreal`

## Trigger Build

After both runners are online:

1. GitHub -> `Actions` -> `Build Plugin (Self-Hosted)`.
2. Click `Run workflow`.
3. (Optional) pass `ue_root_mac` / `ue_root_win` for one-off overrides.

Artifacts:

- `NovaBridge-Mac`
- `NovaBridge-Win64`

## Virtual Machines

- Windows cloud VM is fine for Win64 builds.
- macOS builds must run on Apple hardware (local Mac or cloud Mac host).
- Prefer persistent disks/images so Unreal install is not lost between runs.
