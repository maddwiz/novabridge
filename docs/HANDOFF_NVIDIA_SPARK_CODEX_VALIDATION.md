# NovaBridge v1.0.1 Handoff - NVIDIA Spark Codex Validation

Use this handoff on an NVIDIA Spark Codex environment (Linux + NVIDIA GPU) to validate NovaBridge cross-platform readiness.

## Objective

Validate NovaBridge on Linux/NVIDIA with two tracks:
- Track A: fast checks + Docker harness (no Unreal required)
- Track B: Unreal Linux build + headless smoke (if UE is available)

## Environment Assumptions

- Ubuntu 22.04+ (or equivalent Linux)
- NVIDIA driver + CUDA stack available
- Docker installed
- Python 3.11+ and Node 20+
- Unreal Engine 5.6/5.7 Linux build optional (for Track B)

## Handoff Prompt (Paste Into NVIDIA Spark Codex)

```text
You are validating NovaBridge v1.0.1 on Linux/NVIDIA (Spark environment).

Repository:
- https://github.com/maddwiz/novabridge

Rules:
1) Keep localhost-first and role/token security behavior unchanged.
2) Do not remove or relax guardrails while testing.
3) Return a structured GO/NO-GO report with evidence.

Steps:
1. Clone/open repo and checkout tag v1.0.1.
2. Run fast checks:
   - python3 scripts/ci/validate_novabridge_cpp.py
   - python3 -m unittest discover -s python-sdk/tests -p "test_*.py"
   - python3 -m unittest discover -s mcp-server/tests -p "test_*.py"
   - node --test assistant-server/tests/*.test.js
3. Track A (Docker harness):
   - docker pull ghcr.io/maddwiz/novabridge:v1.0.1
   - docker run --rm -p 8080:8080 ghcr.io/maddwiz/novabridge:v1.0.1
   - curl http://127.0.0.1:8080/nova/health
   - curl http://127.0.0.1:8080/nova/caps
4. Track B (only if Linux Unreal is installed):
   - export UE_ROOT_LINUX=<path to Unreal root>
   - chmod +x scripts/ci/build_plugin_linux.sh
   - scripts/ci/build_plugin_linux.sh "$PWD" "/tmp/novabridge-linux-artifacts" "$UE_ROOT_LINUX"
   - Launch headless editor with plugin-enabled test project and verify:
     - GET /nova/health
     - GET /nova/caps
     - POST /nova/executePlan (spawn + delete)
     - GET /nova/events
5. Runtime security checks (if runtime build available):
   - confirm unauthenticated runtime requests are rejected
   - confirm localhost-only request policy remains enforced
6. Return final report:
   - Track A result
   - Track B result (or "skipped: UE unavailable")
   - regressions found (if any) with file references
   - final GO/NO-GO recommendation
```

## Expected Result

- Track A passes on all Spark environments.
- Track B passes when UE Linux toolchain is available.
- No regression to policy, security, or endpoint stability.
