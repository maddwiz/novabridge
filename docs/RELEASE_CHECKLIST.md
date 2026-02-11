# NovaBridge Release Checklist

## Core Validation

- [x] `GET /nova/health` returns `status: ok`
- [x] `GET /nova/project/info` returns non-empty project fields
- [x] `GET /nova/viewport/screenshot?format=raw` returns `image/png`
- [x] `OPTIONS` preflight returns CORS headers
- [x] `POST /nova/asset/import` with OBJ + `scale` succeeds
- [x] `POST /nova/scene/set-property` alias path works for common components

## Platform Validation

- [x] Linux ARM64 build and runtime smoke tested
- [ ] Linux x86_64 build smoke tested
- [ ] Windows Win64 compile smoke tested
- [ ] macOS compile smoke tested

Reference command outcomes: `docs/BUILD_STATUS.md`.

## Packaging

- [x] `scripts/package_release.sh` generates zip in `dist/`
- [x] Bundle contains plugin, demo project, SDK, MCP server, docs, examples
- [x] No `Intermediate/`, `Saved/`, logs, or crash artifacts in zip

## Integrations

- [ ] OpenClaw extensions load cleanly
- [x] Python SDK imports and executes `health()`
- [x] MCP server starts and lists tools

## Docs and Demo Assets

- [ ] `docs/API.md` up to date with current routes
- [ ] `docs/SETUP_*.md` reflect environment variables and project launch rules
- [ ] `demo/VIDEO_SCRIPT.md` available for launch video production
- [ ] `site/index.html` available as starter landing page
