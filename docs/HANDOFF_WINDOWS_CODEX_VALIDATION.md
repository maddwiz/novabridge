# NovaBridge v1.0.1 Handoff - Windows Codex Validation

Use this handoff on a Windows Codex machine to run ship-gate validation for NovaBridge `v1.0.1`.

## Objective

Validate NovaBridge on Windows (UE 5.7.x preferred) and confirm:
- core checks pass
- editor endpoints work in headless mode
- package artifact is valid
- no regressions from `v1.0.0`

## Environment Assumptions

- Windows 11 x64
- Unreal Engine 5.7.x installed
- Visual Studio Build Tools 2022 installed
- Python 3.11+ and Node 20+
- GitHub CLI (`gh`) optional for CI/release checks

## Handoff Prompt (Paste Into Windows Codex)

```text
You are validating NovaBridge v1.0.1 on Windows.

Repository:
- https://github.com/maddwiz/novabridge

Strict requirements:
1) Do not change API behavior unless a clear bug is found.
2) Keep localhost-first + token/role security posture intact.
3) Report findings with severity and exact file references.
4) If all checks pass, say explicitly "Windows ship gate passed."

Steps:
1. Clone/open repo and checkout tag v1.0.1.
2. Run fast checks:
   - python scripts/ci/validate_novabridge_cpp.py
   - python -m unittest discover -s python-sdk/tests -p "test_*.py"
   - python -m unittest discover -s mcp-server/tests -p "test_*.py"
   - node --test assistant-server/tests/*.test.js
3. Follow docs/WINDOWS_SMOKE_TEST.md and execute headless editor smoke validation.
4. Verify minimum HTTP surface:
   - GET /nova/health
   - GET /nova/caps
   - POST /nova/executePlan (spawn + delete plan)
   - GET /nova/events
5. Package release:
   - pwsh scripts/package_release_win.ps1 -Version 1.0.1
6. Confirm zip hygiene:
   - No Intermediate/, Saved/, logs, or private/internal docs in package.
7. Return a final report:
   - pass/fail per step
   - commands run
   - endpoint outputs summary
   - any blocker or risk
```

## Expected Result

- All fast checks pass.
- Windows smoke checks pass with no regressions.
- Release zip is generated and clean.
- Final decision: `GO` or `NO-GO`.
