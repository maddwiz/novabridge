# NovaBridge Studio v0.1.0

NovaBridge Studio is a desktop control surface for NovaBridge (UE5 plugin).

## What It Does

- Connects to a local NovaBridge instance (`/nova/health`, `/nova/caps`)
- Supports optional NovaBridge API key forwarding (`X-API-Key`)
- Lets you choose an AI provider (OpenAI, Anthropic, Ollama, Custom)
- Generates a strict JSON plan from a prompt
- Previews plan steps before execution
- Performs permission preflight using `/nova/caps` `permissions` (when available) to block disallowed actions before execution
- Shows current policy snapshot in Connect and inline policy-block reasons in Plan Preview
- Executes via `POST /nova/executePlan`
- Shows per-step execution outcomes in the activity stream
- Falls back to endpoint calls if `executePlan` is not available:
  - `POST /nova/scene/spawn`
  - `POST /nova/scene/delete`
  - `POST /nova/scene/set-property`
  - `GET /nova/viewport/screenshot`
- Includes runtime pairing UI placeholders for v0.2 (`/nova/runtime/pair` flow not yet active)
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

## Tests

```bash
pnpm test
```

## Provider Setup

Use **Settings** tab:

- NovaBridge API key (optional, forwarded to NovaBridge HTTP endpoints)
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
