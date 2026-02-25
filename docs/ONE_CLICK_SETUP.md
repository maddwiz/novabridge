# One-Click Setup

NovaBridge supports one-click startup for users who do not want a terminal workflow.

## Files

- `NovaBridge-OneClick.command` (macOS/Linux)
- `NovaBridge-OneClick.bat` (Windows)
- `scripts/one_click.sh`
- `scripts/one_click_win.ps1`
- `novabridge.env.example`

## Basic Flow

1. Copy `novabridge.env.example` to `novabridge.env`.
2. Fill any provider credentials you want to use.
3. Double-click the launcher for your OS.

The launcher will:
- copy/install plugin into the target UE project
- launch Unreal Editor (when engine binary is discoverable)
- start assistant server (if Node.js is available)
- open Studio URL: `http://127.0.0.1:30016/nova/studio`

## Supported AI Modes via One-Click Env

- `NOVABRIDGE_ASSISTANT_PROVIDER=mock`
- `NOVABRIDGE_ASSISTANT_PROVIDER=openai`
- `NOVABRIDGE_ASSISTANT_PROVIDER=anthropic`
- `NOVABRIDGE_ASSISTANT_PROVIDER=ollama`
- `NOVABRIDGE_ASSISTANT_PROVIDER=custom`

Key fields are in `novabridge.env.example`.

## OpenClaw

OpenClaw extensions read `NOVABRIDGE_HOST` / `NOVABRIDGE_PORT` / `NOVABRIDGE_API_KEY`.
The one-click env template includes these so OpenClaw can target the same NovaBridge instance.

## Notes

- Keep `novabridge.env` out of source control (already ignored in `.gitignore`).
- Runtime pairing/token workflows are unchanged.
- If Node.js is not installed, plugin/editor setup still runs; assistant-server startup is skipped.
