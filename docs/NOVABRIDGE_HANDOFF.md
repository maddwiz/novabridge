# NovaBridge System - Complete Handoff Document

**Date:** 2026-02-10
**Author:** Claude Code (Opus 4.6), working with Desmond (@maddwiz)
**Purpose:** Full handoff for any developer or AI agent to continue building on the NovaBridge system

---

## 1. WHAT IS NOVABRIDGE?

NovaBridge is a **3-layer system** that gives AI agents full programmatic control over Unreal Engine 5 via HTTP:

```
AI Agent (any LLM)
    → OpenClaw Extensions (JS, wraps HTTP calls into tool-use format)
        → NovaBridge Plugin (C++, runs inside UE5 Editor, exposes 29 HTTP endpoints)
            → Unreal Engine 5 (scene, assets, materials, viewport, blueprints)
```

Additionally, a **Blender bridge** enables procedural mesh generation:
```
AI Agent → OpenClaw Blender Extension → Blender (Python/MB-Lab) → OBJ export → NovaBridge import → UE5
```

**Key value prop:** An AI can receive a text message like "build me a character" and autonomously generate a 3D body in Blender, import it into UE5, place it in a scene, set up lighting, and take a screenshot — all without human intervention.

---

## 2. COMPONENT INVENTORY

### 2a. NovaBridge UE5 Plugin (C++ — the core)

**Location on this machine:** `/home/nova/UnrealEngine/Engine/Plugins/Experimental/NovaBridge/`

**Files:**
| File | Lines | Purpose |
|------|-------|---------|
| `NovaBridge.uplugin` | 30 | Plugin descriptor, platforms: LinuxArm64, Linux, Win64, Mac |
| `Source/NovaBridge/NovaBridge.Build.cs` | 42 | Build config, module dependencies |
| `Source/NovaBridge/Public/NovaBridgeModule.h` | 88 | Header: class FNovaBridgeModule, all handler declarations |
| `Source/NovaBridge/Private/NovaBridgeModule.cpp` | 2149 | Implementation: all 29 HTTP route handlers |

**Threading model:** Every handler dispatches work to `ENamedThreads::GameThread` via `AsyncTask()`. The HTTP server runs on a background thread. All UE5 API calls happen on the game thread.

**Important:** POST request bodies require null-termination. The handler adds `\0` manually because UE5's HTTP server body bytes are NOT null-terminated.

**Dependencies (Build.cs):**
- Public: Core, CoreUObject, Engine, HTTPServer, Json, JsonUtilities
- Private: UnrealEd, EditorScriptingUtilities, Slate/SlateCore, InputCore, LevelEditor, MeshDescription, StaticMeshDescription, MeshConversion, RawMesh, RenderCore, RHI, ImageWrapper, AssetRegistry, AssetTools, MaterialEditor, Kismet, KismetCompiler, BlueprintGraph

**All 29 HTTP Routes:**

| Route | Method | What it does |
|-------|--------|-------------|
| `/nova/health` | GET | Health check, returns status + route count |
| `/nova/scene/list` | GET | List all actors in current level with transforms |
| `/nova/scene/spawn` | POST | Spawn actor by class (StaticMeshActor, PointLight, etc.) |
| `/nova/scene/delete` | POST | Delete actor by name |
| `/nova/scene/transform` | POST | Set actor location/rotation/scale |
| `/nova/scene/get` | GET/POST | Get actor details: properties, components, set_property hints |
| `/nova/scene/set-property` | POST | Set any property via `ComponentName.PropertyName` syntax |
| `/nova/asset/list` | GET/POST | List assets in Content Browser |
| `/nova/asset/create` | POST | Create new asset |
| `/nova/asset/duplicate` | POST | Duplicate existing asset |
| `/nova/asset/delete` | POST | Delete asset |
| `/nova/asset/rename` | POST | Rename asset |
| `/nova/asset/info` | GET/POST | Get asset metadata |
| `/nova/asset/import` | POST | Import OBJ/FBX file into Content Browser |
| `/nova/mesh/create` | POST | Create static mesh from vertex/triangle data |
| `/nova/mesh/get` | GET/POST | Get mesh geometry info |
| `/nova/mesh/primitive` | POST | Create primitive shapes (box, sphere, etc.) |
| `/nova/material/create` | POST | Create material asset |
| `/nova/material/set-param` | POST | Set material parameters (color, roughness, etc.) |
| `/nova/material/get` | GET/POST | Get material properties |
| `/nova/material/create-instance` | POST | Create material instance from parent |
| `/nova/viewport/screenshot` | GET | Capture viewport as base64 PNG in JSON |
| `/nova/viewport/camera/set` | POST | Set SceneCapture camera position/rotation/FOV |
| `/nova/viewport/camera/get` | GET | Get current camera transform |
| `/nova/blueprint/create` | POST | Create Blueprint asset |
| `/nova/blueprint/add-component` | POST | Add component to Blueprint |
| `/nova/blueprint/compile` | POST | Compile Blueprint |
| `/nova/build/lighting` | POST | Build lighting for level |
| `/nova/exec/command` | POST | Execute arbitrary UE5 console command |

**Key implementation details:**
- Uses UE5's built-in HTTPServer module (same as Web Remote Control)
- Default port: 30010
- SceneCapture2D actor named `NovaBridge_SceneCapture` is auto-created and protected from deletion
- `scene/get` returns `set_property_prefix` hints per component to help AI know how to set properties
- `set-property` uses `ComponentName.PropertyName` syntax (e.g., `LightComponent0.Intensity`)
- Material assignment: `ComponentName.Material` or `ComponentName.Material[N]` for multi-slot
- `asset/import` reads OBJ files from the local filesystem and creates StaticMesh assets

### 2b. OpenClaw nova-ue5-bridge Extension (JS — UE5 API wrapper)

**Location:** `/home/nova/.openclaw/extensions/nova-ue5-bridge/`

**Files:**
| File | Lines | Purpose |
|------|-------|---------|
| `openclaw.plugin.json` | 11 | Plugin manifest |
| `index.js` | 440 | All UE5 tools for AI agent |

**Tools registered:**
| Tool Name | Description |
|-----------|------------|
| `ue5_health` | Check if UE5 editor is running |
| `ue5_scene_list` | List all actors in scene |
| `ue5_scene_spawn` | Spawn actor (with friendly class names) |
| `ue5_scene_delete` | Delete actor |
| `ue5_scene_transform` | Move/rotate/scale actor |
| `ue5_scene_get` | Get actor properties and components |
| `ue5_scene_set_property` | Set property on actor component |
| `ue5_scene_clear` | Clear all actors except protected ones |
| `ue5_asset_list` | List Content Browser assets |
| `ue5_asset_import` | Import OBJ/FBX into UE5 |
| `ue5_material_create` | Create material |
| `ue5_material_set_param` | Set material parameter |
| `ue5_viewport_screenshot` | Take viewport screenshot |
| `ue5_viewport_camera` | Get/set camera |
| `ue5_exec` | Execute console command |

This extension wraps each NovaBridge HTTP endpoint into an OpenClaw tool-use format that LLMs can call. It handles the HTTP connection, Content-Length header, JSON parsing, and error formatting.

**Note:** The extension actually registers **29 tools** (1:1 mapping to all 29 routes), including blueprint create/add-component/compile, build_lighting, and exec_command.

**Special behavior:** `ue5_viewport_screenshot` saves the base64 image to `/home/nova/.openclaw/workspace/screenshots/viewport-<timestamp>.png` and returns both a text content block (with metadata) AND an image content block (so the LLM can see the screenshot visually).

### 2c. OpenClaw nova-blender Extension (JS — Blender pipeline)

**Location:** `/home/nova/.openclaw/extensions/nova-blender/`

**Files:**
| File | Lines | Purpose |
|------|-------|---------|
| `openclaw.plugin.json` | 11 | Plugin manifest |
| `index.js` | 457 | All Blender tools for AI agent |

**Tools registered:**
| Tool Name | Description |
|-----------|------------|
| `blender_run` | Run arbitrary Blender Python script |
| `blender_list_scripts` | List available pre-built scripts |
| `blender_export` | Export Blender scene to OBJ |
| `blender_to_ue5` | **Full pipeline:** Blender Python → OBJ export → UE5 import |
| `model_download` | Download 3D model from URL, optionally import to UE5 |
| `model_search` | Search Sketchfab for free 3D models |

**Key implementation details:**
- Runs Blender in `--background` mode (headless)
- Converts any format (GLB/GLTF/FBX/BLEND) to OBJ via Blender for UE5 import
- `blender_to_ue5` is the recommended tool — handles the full pipeline in one call
- Exports dir: `/home/nova/.openclaw/workspace/exports/`
- Scripts dir: `/home/nova/.openclaw/workspace/embodiment/avatar/blender/scripts/`

### 2d. Blender Scripts

**Location:** `/home/nova/.openclaw/workspace/embodiment/avatar/blender/scripts/`

| Script | Lines | Purpose |
|--------|-------|---------|
| `generate-mblab-female.py` | 72 | Generate parametric female body using MB-Lab addon |

**MB-Lab templates available:**
- `human_female_base`, `human_male_base`, `anime_female`, `anime_male`
- Characters: `f_af01` (African), `f_as01` (Asian), `f_ca01` (Caucasian), `f_la01` (Latin)

---

## 3. CURRENT STATE & KNOWN ISSUES

### What works:
- All 29 NovaBridge routes functional on LinuxArm64
- Blender → OBJ → UE5 pipeline working
- MB-Lab character generation working
- AI can autonomously create scenes, spawn actors, set properties, take screenshots
- SceneCapture camera for viewport screenshots

### Known issues to fix:

1. **Scale mismatch on import**: MB-Lab exports in Blender units (meters). When imported to UE5, the mesh is extremely small and needs ~100x scaling. The FBX export in `generate-mblab-female.py` uses `FBX_SCALE_ALL` but OBJ import path doesn't normalize scale. **Fix:** Add a scale factor to `asset/import` endpoint, or normalize in the Blender export script.

2. **Ground plane in MB-Lab export**: The `generate-mblab-female.py` script doesn't delete the ground plane before export. This creates a large flat surface underneath the body. **Fix:** Add `bpy.ops.mesh.select_all()` + delete non-character objects before export.

3. **No FBX import support in NovaBridge**: The `asset/import` handler currently only supports OBJ files (runtime check: "FBX SDK unavailable on ARM64"). FBX would preserve skeleton/armature data needed for animation. **Fix:** On x86 Windows/Mac/Linux where FBX SDK is available, add FBX import using UFbxFactory in C++. The ARM64 restriction is a runtime check that can be made conditional per-platform.

4. **Screenshot returns JSON with base64**: The viewport screenshot returns `{"image": "<base64>"}` instead of raw PNG. This works but is inefficient for large images. **Fix (optional):** Add a `?format=raw` query parameter option.

5. **SceneCapture vs Editor viewport**: The `show` console commands (BSP, Grid, etc.) don't affect the SceneCapture2D used for screenshots. The SceneCapture has its own ShowFlags. **Fix:** Expose SceneCapture ShowFlags in the `viewport/camera/set` endpoint.

6. **No project auto-load**: When UE5 starts without a project specified, it opens the project browser and gets stuck in headless mode. The service file should specify a project. **Fix:** Create a default project and add its path to the ExecStart command.

7. **Property setting uses internal names**: `set-property` requires knowing the exact component name (e.g., `LightComponent0` not `PointLightComponent0`). The `scene/get` response includes `set_property_prefix` hints but the AI still gets confused. **Fix:** Add fuzzy matching or alias lookup.

---

## 4. ARCHITECTURE FOR CROSS-PLATFORM

The system is already designed to be portable:

### NovaBridge Plugin (C++)
- `.uplugin` already lists `Win64`, `Mac`, `Linux`, `LinuxArm64` in PlatformAllowList
- All dependencies are standard UE5 modules (no platform-specific code)
- Should compile on any platform UE5 supports without changes
- **To build:** Just drop the `NovaBridge/` folder into any UE5 project's `Plugins/` directory and build

### OpenClaw Extensions (JS)
- Pure JavaScript, no native dependencies
- HTTP client calls work on any OS
- Blender subprocess calls use `child_process.spawn` which works on Win/Mac/Linux
- Only platform concern: Blender binary path (currently `/usr/bin/blender`, needs to be configurable)

### Blender Scripts (Python)
- Pure Python using bpy API
- MB-Lab is a Blender addon — works on any platform Blender runs on
- No platform-specific code

### What needs to change for cross-platform:
1. **Blender path**: Hardcoded as `/usr/bin/blender`. Make configurable via environment variable or config file
2. **Export paths**: Currently `/home/nova/.openclaw/workspace/exports/`. Use OS temp directory or configurable path
3. **NovaBridge port**: Hardcoded to 30010. Already fine but should be configurable
4. **File paths in asset/import**: Uses Unix paths. UE5 internally normalizes, but the extension code should handle Windows backslashes

---

## 5. HOW TO SET UP FROM SCRATCH

### Prerequisites:
- Unreal Engine 5.1+ (source build or installed)
- Blender 4.0+ with MB-Lab addon
- Node.js 18+ (for OpenClaw or any HTTP-capable AI framework)

### Step 1: Install NovaBridge Plugin
```bash
# Copy plugin to your UE5 project
cp -r NovaBridge/ /path/to/YourProject/Plugins/NovaBridge/

# Or for engine-level install:
cp -r NovaBridge/ /path/to/UnrealEngine/Engine/Plugins/Experimental/NovaBridge/

# Rebuild the project
# The plugin auto-enables via EnabledByDefault: true
```

### Step 2: Launch UE5 with a project
```bash
# Must specify a project for headless use:
/path/to/UnrealEditor /path/to/YourProject.uproject -vulkan -RenderOffScreen -nosplash -nosound -unattended -nopause

# On Windows:
UnrealEditor.exe "C:\Projects\MyProject.uproject" -dx12 -RenderOffScreen -nosplash -nosound -unattended -nopause

# On Mac:
/path/to/UnrealEditor /path/to/MyProject.uproject -metal -RenderOffScreen -nosplash -nosound -unattended -nopause
```

### Step 3: Verify NovaBridge is running
```bash
curl http://localhost:30010/nova/health
# Expected: {"status":"ok","engine":"UnrealEngine","port":30010,"routes":29}
```

### Step 4: Set up Blender scripts
```bash
# Install MB-Lab addon in Blender
# Place generate-mblab-female.py in accessible location
# Configure blender binary path in extension config
```

### Step 5: Connect AI agent
```bash
# With OpenClaw:
# 1. Copy extensions to ~/.openclaw/extensions/
# 2. Enable in openclaw.json: "nova-ue5-bridge": {"enabled": true}, "nova-blender": {"enabled": true}
# 3. Restart gateway

# With any other AI framework:
# Just make HTTP calls to localhost:30010/nova/* endpoints
# The NovaBridge HTTP API works with any client — curl, Python requests, etc.
```

---

## 6. GITHUB REPO STRUCTURE (RECOMMENDED)

```
novabridge/
├── README.md                              # Setup guide, screenshots, demo
├── LICENSE                                # Choose: MIT, Apache 2.0, or proprietary
├── .gitignore
│
├── NovaBridge/                            # UE5 Plugin (the core product)
│   ├── NovaBridge.uplugin
│   └── Source/
│       └── NovaBridge/
│           ├── NovaBridge.Build.cs
│           ├── Public/
│           │   └── NovaBridgeModule.h
│           └── Private/
│               └── NovaBridgeModule.cpp
│
├── extensions/                            # AI agent integrations
│   ├── openclaw/                          # OpenClaw-specific wrappers
│   │   ├── nova-ue5-bridge/
│   │   │   ├── openclaw.plugin.json
│   │   │   └── index.js
│   │   └── nova-blender/
│   │       ├── openclaw.plugin.json
│   │       └── index.js
│   ├── langchain/                         # (future) LangChain tool definitions
│   ├── mcp/                               # (future) Model Context Protocol server
│   └── python-sdk/                        # (future) Pure Python SDK for HTTP API
│
├── blender/                               # Blender integration scripts
│   └── scripts/
│       └── generate-mblab-female.py
│
├── docs/                                  # Documentation
│   ├── API.md                             # Full HTTP API reference
│   ├── SETUP_WINDOWS.md
│   ├── SETUP_MAC.md
│   ├── SETUP_LINUX.md
│   └── ARCHITECTURE.md
│
└── examples/                              # Example scripts
    ├── curl/                              # curl command examples
    ├── python/                            # Python requests examples
    └── demo-scene/                        # Demo: build a scene from scratch
```

---

## 7. PRODUCT / MONETIZATION NOTES

### What you could sell:
1. **UE5 Marketplace Plugin** — NovaBridge as a standalone plugin ($50-200)
   - Requires packaging for UE5 Marketplace format
   - Epic takes 12% cut
   - Audience: developers wanting AI-controlled UE5

2. **SaaS API** — Hosted UE5 rendering with NovaBridge API
   - Users send HTTP calls, you run UE5 in the cloud
   - Higher revenue but requires infrastructure

3. **AI Framework Integration** — SDKs for popular AI frameworks
   - OpenClaw extension (done)
   - LangChain tools
   - MCP (Model Context Protocol) server
   - CrewAI / AutoGen integration

### Competitive positioning:
- **Existing solutions:** UE5's own Web Remote Control plugin exists but is limited (no mesh creation, no asset import, no Blender pipeline)
- **Your advantage:** Full scene creation pipeline including procedural mesh generation from AI, Blender integration, and MB-Lab body generation
- **Name suggestion for product:** Consider renaming from "NovaBridge" to something generic (e.g., "UnrealAI Bridge", "AI Scene Director") for broader market appeal

### Cross-platform priority:
1. **Windows first** — biggest UE5 developer market
2. **Mac second** — growing UE5 support on Apple Silicon
3. **Linux** — already working (your current setup)

---

## 8. WHAT THE NEXT AGENT SHOULD DO

### Immediate fixes (Day 1):
1. Fix the scale mismatch — add scale normalization to OBJ import
2. Remove ground plane from MB-Lab exports
3. Create a default UE5 project for headless operation
4. Make Blender path configurable

### Short-term improvements (Week 1):
1. Add FBX import support (preserves armature/skeleton for animation)
2. Add `show` flags control on SceneCapture for clean screenshots
3. Add texture/UV support to the material pipeline
4. Test and fix compilation on Win64 and Mac
5. Write the API documentation

### Medium-term (Month 1):
1. Create a standalone UE5 project plugin (not engine plugin)
2. Add animation support (play/stop/blend animations)
3. Add Sequencer control (cinematic camera moves)
4. Build MCP server for Claude Desktop / Claude Code integration
5. Add a Python SDK (simple HTTP wrapper)
6. Package for UE5 Marketplace submission

### Long-term vision:
1. Real-time streaming (pixel streaming integration)
2. Multi-user support (multiple AI agents in same scene)
3. Procedural world generation
4. Physics simulation control
5. Game logic / behavior tree creation via API

---

## 9. RUNNING ON THIS MACHINE

### Services:
```bash
# UE5 Editor
systemctl --user restart nova-ue5-editor.service   # Port 30010

# OpenClaw Gateway (handles WhatsApp → AI → UE5 pipeline)
systemctl --user restart openclaw-gateway.service   # Port 18789

# NovaSpine Memory (C3/Ae)
systemctl --user restart nova-memory.service        # Port 8420
```

### Quick test:
```bash
# Health check
curl http://localhost:30010/nova/health

# List scene
curl http://localhost:30010/nova/scene/list

# Spawn a light
curl -X POST http://localhost:30010/nova/scene/spawn \
  -H "Content-Type: application/json" \
  -d '{"class":"PointLight","label":"TestLight","location":{"x":0,"y":0,"z":200}}'

# Take screenshot
curl http://localhost:30010/nova/viewport/screenshot | python3 -c "
import sys, json, base64
data = json.load(sys.stdin)
with open('screenshot.png','wb') as f:
    f.write(base64.b64decode(data['image']))
"
```

### Build UE5 (ARM64):
```bash
cd /home/nova/UnrealEngine && \
UE_USE_SYSTEM_DOTNET=1 DOTNET_ROLL_FORWARD=LatestMajor LINUX_ROOT=linux-toolchain \
dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll \
UnrealEditor LinuxArm64 Development -NoBuildUHT -SkipPreBuildTargets
```

---

## 10. FILE LOCATIONS SUMMARY

| Component | Path |
|-----------|------|
| NovaBridge Plugin | `/home/nova/UnrealEngine/Engine/Plugins/Experimental/NovaBridge/` |
| UE5 Bridge Extension | `/home/nova/.openclaw/extensions/nova-ue5-bridge/` |
| Blender Extension | `/home/nova/.openclaw/extensions/nova-blender/` |
| MB-Lab Script | `/home/nova/.openclaw/workspace/embodiment/avatar/blender/scripts/generate-mblab-female.py` |
| OpenClaw Config | `/home/nova/.openclaw/openclaw.json` |
| UE5 Service | `/home/nova/.config/systemd/user/nova-ue5-editor.service` |
| UE5 Logs | `/home/nova/UnrealEngine/Engine/Saved/Logs/Unreal.log` |
| Blender Binary | `/usr/bin/blender` (v4.0.2) |
| Export Cache | `/home/nova/.openclaw/workspace/exports/` |

---

---

## 11. KEY IMPLEMENTATION DETAILS FOR THE NEXT AGENT

### OBJ Import Parser (in NovaBridgeModule.cpp)
- Custom OBJ parser handles `v`, `vt`, `vn`, and `f` lines
- Supports n-gon triangulation by fan
- Coordinate conversion: OBJ Y is negated, UV V is flipped (1-V)
- Meshes built using `FMeshDescription` + `BuildFromMeshDescriptions`

### Scene Spawn Class Shortcuts
Spawn supports these friendly class names (no need for full class paths):
- `StaticMeshActor`, `PointLight`, `DirectionalLight`, `SpotLight`
- `CameraActor`, `PlayerStart`, `SkyLight`
- `ExponentialHeightFog`, `PostProcessVolume`
- Also accepts full class paths like `/Script/Engine.PointLight`

### Mesh Primitives
`mesh/primitive` supports: `cube`/`box` (24 tris), `plane` (2 tris), `sphere` (16 rings x 24 segments), `cylinder` (24 segments)

### Material System
- `material/create` optionally wires a `UMaterialExpressionConstant4Vector` (base color) into the material graph
- `material/set-param` works on `UMaterialInstanceConstant` only (not base materials)
- Supports `scalar` and `vector` parameter types

### Blender Extension Constants
- `BLENDER_BIN = '/usr/bin/blender'` (needs to be configurable for cross-platform)
- `EXPORT_DIR = '/tmp/nova-blender-exports'`
- `SCRIPTS_DIR = '/home/nova/.openclaw/workspace/embodiment/avatar/blender/scripts'`
- Blender runs with `DISPLAY: ''` (headless)

### The blender_to_ue5 Pipeline (step by step)
1. Takes user-provided `script` (inline Python) or `script_path` (file path)
2. Appends OBJ export code that: applies all modifiers, exports OBJ with UVs/normals, forward=-Y, up=Z
3. Runs `blender --background [blend_file] --python <combined_script>` (180s timeout)
4. POST to `localhost:30010/nova/asset/import` with the OBJ file path
5. Returns result with `next_steps` hint: use `ue5_scene_spawn` + `ue5_set_property` to place the mesh

### Installed Blender Addons (on this machine)
Both at `/home/nova/.config/blender/4.0/scripts/addons/`:
- **MB-Lab** — parametric human body generator (18K+ vertices, armature, face topology, UV maps)
- **CharMorph** — character morphing, posing, hair, finalization

---

*Total codebase: ~3,248 lines across 6 source files. This is a small, focused system.*
*The entire thing can be understood by a new developer in a few hours.*
