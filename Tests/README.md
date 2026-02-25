# NovaBridge Public Test Surface

NovaBridge includes public Unreal Automation tests in:

- `NovaBridge/Source/NovaBridgeCore/Private/Tests/NovaBridgePlanSchemaTests.cpp`
- `NovaBridge/Source/NovaBridgeCore/Private/Tests/NovaBridgeCorePolicyCapabilityTests.cpp`

## Run in Unreal Editor

```bash
UnrealEditor "/path/to/NovaBridgeDefault.uproject" -ExecCmds="Automation RunTests NovaBridge.Core; Quit" -unattended -nop4 -nosplash -NullRHI
```

## Run in CI (self-hosted macOS)

```bash
scripts/ci/run_automation_tests_mac.sh
```

The automation suite covers:

- execute-plan schema validation
- capability registry behavior
- runtime/editor policy guardrails
- role-based plan permissions
- spawn/action limits
