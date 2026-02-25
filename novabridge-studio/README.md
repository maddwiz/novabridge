# NovaBridge Studio v0.1.0

NovaBridge Studio is a desktop control surface for NovaBridge (UE5 plugin).

## What It Does

- Connects to a local NovaBridge instance (`/nova/health`, `/nova/caps`)
- Lets you choose an AI provider (OpenAI, Anthropic, Ollama, Custom)
- Generates a strict JSON plan from a prompt
- Previews plan steps before execution
- Executes via `POST /nova/executePlan`
- Falls back to endpoint calls if `executePlan` is not available:
  - `POST /nova/scene/spawn`
  - `POST /nova/scene/delete`
  - `GET /nova/viewport/screenshot`
- Shows console-style activity stream and errors

## Expected NovaBridge Endpoints

- `GET /nova/health`
- `GET /nova/caps` (optional but recommended)
- `POST /nova/executePlan` (preferred)

Fallback-compatible:
- `POST /nova/scene/spawn`
- `POST /nova/scene/delete`
- `GET /nova/viewport/screenshot`

## Run (Dev)

```bash
pnpm install
pnpm dev
```

Desktop shell (Tauri):

```bash
pnpm tauri dev
```

## Build

```bash
pnpm build
pnpm tauri build
```

## Provider Setup

Use **Settings** tab:

- OpenAI: API key + model
- Anthropic: API key + model
- Ollama: host + model
- Custom: endpoint + optional header key/value

Dev mode includes **Mock Provider** toggle for offline testing.

## Security Note

Use localhost endpoints by default (`http://127.0.0.1:30010`).
Do not expose NovaBridge without explicit auth/network controls.

## Screenshot Placeholder

- `docs/screenshot-placeholder.png`
