# NovaBridge Demo Video Script (2:00)

## 0:00-0:15 - Hook
Voiceover:
"What if an AI could build Unreal Engine 5 scenes for you from plain language?"

On screen:
- Title card: `NovaBridge: AI Control for Unreal Engine 5`
- Quick flashes of generated UE scenes and character imports.

## 0:15-0:45 - Prompt to Action
Voiceover:
"Using NovaBridge, any AI client can drive UE5 through a simple HTTP and tool API."

On screen:
- Show prompt in Claude/Desktop style UI:
  - `Build a simple cinematic scene with warm lighting and a hero prop.`
- Show live tool calls:
  - `ue5_scene_spawn`
  - `ue5_material_create`
  - `ue5_viewport_camera`

## 0:45-1:15 - Scene Build
Voiceover:
"NovaBridge spawns actors, sets properties, edits camera and captures outputs automatically."

On screen:
- Spawn floor/cube/light.
- Set light intensity and color.
- Camera reposition with `show_flags` cleanup.
- Capture screenshot with `format=raw`.

## 1:15-1:45 - Blender Pipeline
Voiceover:
"For complex geometry, NovaBridge can run Blender scripts, export models, and import directly into UE5."

On screen:
- Run `blender_to_ue5`.
- Show generated mesh imported to `/Game`.
- Spawn imported mesh in scene.

## 1:45-2:00 - CTA
Voiceover:
"NovaBridge gives AI agents real control over UE5 production workflows. Plugin, SDK, MCP server, and examples included."

On screen:
- Final hero shot.
- Text bullets:
  - `30 UE5 API routes`
  - `Python SDK + MCP server`
  - `Blender pipeline included`
- CTA: `Get NovaBridge - docs and setup in repo`.
