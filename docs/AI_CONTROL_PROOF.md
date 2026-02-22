# AI Control Proof (macOS)

This file documents concrete proof that NovaBridge was actively controlling UE5 Editor from API calls, not just showing a static render.

## Passing Gate Run

- Commit: `dc80d66d1cb98d8f4e34c8e71504649159f5232b`
- Artifact folder: `artifacts-mac/run-20260221-183743-finalgate/`
- Required API path prefix verified: `/nova/*`

## Evidence Chain

1. Plugin route binding on startup (UE log):
   - `artifacts-mac/run-20260221-183743-finalgate/bind-evidence.txt`
   - Contains:
     - `Created new HttpListener on 127.0.0.1:30010`
     - `NovaBridge server listening on 127.0.0.1:30010 ... with 53 API routes`

2. Health endpoint responded from NovaBridge:
   - `artifacts-mac/run-20260221-183743-finalgate/health.json`
   - `status: ok`, `port: 30010`, `routes: 53`

3. Spawn command created a new actor:
   - `artifacts-mac/run-20260221-183743-finalgate/spawn.json`
   - `label: LaunchSmokeLight`, class `PointLight`

4. Delete command removed the spawned actor:
   - `artifacts-mac/run-20260221-183743-finalgate/delete.json`
   - `status: ok`

5. Visual screenshot from passing mac smoke:
   - `docs/images/mac-smoke-launchproof.png`

![macOS AI control proof](images/mac-smoke-launchproof.png)

## Endpoints Tested

- `GET /nova/health`
- `POST /nova/scene/spawn`
- `POST /nova/scene/delete`
