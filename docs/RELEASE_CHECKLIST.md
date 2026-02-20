# NovaBridge Release Checklist

## Core Validation

- [x] Golden path demo flow passes (`examples/curl/golden_path.sh`)
- [x] `GET /nova/health` returns `status: ok`
- [x] `GET /nova/health` includes `version`
- [x] `GET /nova/project/info` returns non-empty project fields
- [x] `GET /nova/viewport/screenshot?format=raw` returns `image/png`
- [x] `OPTIONS` preflight returns CORS headers
- [x] `POST /nova/asset/import` with OBJ + `scale` succeeds
- [x] `POST /nova/scene/set-property` alias path works for common components
- [x] Optional API key mode validated (`401` without key, `200` with key)

## Platform Validation

- [x] Linux ARM64 build and runtime smoke tested
- [ ] Linux x86_64 build smoke tested
- [x] Windows Win64 compile smoke tested
- [x] macOS compile smoke tested
- [x] macOS full smoke checklist complete (`docs/MACOS_SMOKE_TEST.md`)

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

- [x] `docs/API.md` up to date with current routes
- [x] `docs/SETUP_*.md` reflect environment variables and project launch rules
- [x] `docs/SMOKE_TEST_CHECKLIST.md` present
- [x] Customer docs included (`INSTALL.md`, `BuyerGuide.md`, `CHANGELOG.md`, `EULA.txt`)
- [x] `demo/VIDEO_SCRIPT.md` available for launch video production
- [x] `site/index.html` available as starter landing page
