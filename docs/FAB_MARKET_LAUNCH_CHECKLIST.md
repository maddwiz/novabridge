# Fab Market Launch Checklist (NovaBridge)

Use this checklist to publish NovaBridge on Fab with current repo state.

## 1) Business + Account

- [ ] Publisher profile completed (tax + payout set).
- [ ] Seller display name, support email, and legal entity details finalized.
- [ ] Refund/support policy confirmed for marketplace customers.

## 2) License + Legal

- [x] `LICENSE` updated to proprietary commercial notice.
- [x] `EULA.txt` updated with Fab-priority clause and direct-sale fallback terms.
- [x] `SUPPORT.md` updated to separate marketplace vs direct refund paths.
- [ ] Legal review of final terms in your jurisdiction (recommended before launch).

## 3) Technical Release Gate

- [x] Windows Win64 validated (latest run in `docs/WINDOWS_SMOKE_TEST.md`).
- [x] macOS validated (latest run in `docs/BUILD_STATUS.md`).
- [x] Linux ARM64 validated (see `docs/BUILD_STATUS.md`).
- [ ] Linux x86_64 validated (currently pending).
- [x] Release checklist complete except Linux x86_64 (`docs/RELEASE_CHECKLIST.md`).

## 4) Package Assembly

- [ ] Build release zip:
  - `pwsh scripts/package_release_win.ps1 -Version 0.9.0` (Windows)
  - or `./scripts/package_release.sh 0.9.0` (Linux/macOS)
- [ ] Verify zip hygiene:
  - no `Intermediate/`, `Saved/`, `DerivedDataCache/`, logs, crash dumps.
- [ ] Verify docs included in zip:
  - `README.md`, `INSTALL.md`, `BuyerGuide.md`, `SUPPORT.md`, `EULA.txt`, `CHANGELOG.md`.

## 5) Listing Content Pack

- [ ] Title + short description finalized.
- [ ] Long description includes:
  - what it does (HTTP control for UE5 editor),
  - supported platforms,
  - Early Access disclosure,
  - known limitation: Linux x86_64 pending validation.
- [ ] Media prepared:
  - icon/thumbnail,
  - screenshots (health, spawn/import flow, viewport result),
  - short demo video/GIF.
- [ ] Documentation link set (repo docs or docs microsite).

## 6) Pricing + Tiers (Fab)

- [ ] Configure Fab Standard License tiers:
  - Personal
  - Professional
- [ ] Set launch pricing for both tiers.
- [ ] Confirm discount strategy (launch promo yes/no).

## 7) Platform Positioning (Recommended Copy)

Use this supported-platform statement in listing text:

> Validated: Windows Win64, macOS, Linux ARM64.  
> In validation: Linux x86_64.  
> Release tier: Early Access (`v0.9.0`).

## 8) Launch-Day Operations

- [ ] Publish listing and verify it is searchable.
- [ ] Smoke-test customer install using the published package.
- [ ] Monitor support inbox + marketplace Q&A for first 72 hours.
- [ ] Prepare hotfix branch for urgent install/runtime issues.
