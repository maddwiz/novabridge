const http = require('http');
const fs = require('fs');
const path = require('path');
const { Type } = require('@sinclair/typebox');

const UE5_PORT = 30010;
const UE5_HOST = 'localhost';

function ue5Request(method, path, body) {
  return new Promise((resolve, reject) => {
    const bodyStr = body ? JSON.stringify(body) : null;
    const options = {
      hostname: UE5_HOST,
      port: UE5_PORT,
      path: path,
      method: method,
      headers: { 'Content-Type': 'application/json' },
      timeout: 30000,
    };
    if (bodyStr) {
      options.headers['Content-Length'] = Buffer.byteLength(bodyStr);
    }
    const req = http.request(options, (res) => {
      let data = '';
      res.on('data', (chunk) => data += chunk);
      res.on('end', () => {
        try { resolve(JSON.parse(data)); }
        catch (e) { resolve({ raw: data }); }
      });
    });
    req.on('error', (err) => reject(new Error(`UE5 connection failed: ${err.message}. Is UnrealEditor running?`)));
    req.on('timeout', () => { req.destroy(); reject(new Error('UE5 request timed out')); });
    if (bodyStr) req.write(bodyStr);
    req.end();
  });
}

function json(data) {
  return {
    content: [{ type: 'text', text: JSON.stringify(data, null, 2) }],
    details: data,
  };
}

async function run(method, path, params) {
  try {
    const result = await ue5Request(method, path, params);
    return json(result);
  } catch (err) {
    return json({ error: err.message });
  }
}

const plugin = {
  id: 'nova-ue5-bridge',
  name: 'Nova UE5 Bridge',
  description: 'UE5 editor control bridge - gives Nova full programmatic control over Unreal Engine 5',

  register(api) {

    api.registerTool({
      name: 'ue5_health',
      label: 'UE5 Health',
      description: 'Check if UE5 editor is running and responsive',
      parameters: Type.Object({}),
      async execute(_id, _params) { return run('GET', '/nova/health'); },
    });

    api.registerTool({
      name: 'ue5_scene_list',
      label: 'UE5 Scene List',
      description: 'List all actors in the current level. Returns actor names, classes, and transforms.',
      parameters: Type.Object({}),
      async execute(_id, _params) { return run('GET', '/nova/scene/list'); },
    });

    api.registerTool({
      name: 'ue5_scene_spawn',
      label: 'UE5 Spawn Actor',
      description: 'Spawn an actor in the scene. Class can be: StaticMeshActor, PointLight, DirectionalLight, SpotLight, SkyLight, ExponentialHeightFog, PostProcessVolume, CameraActor, PlayerStart, or a full class path.',
      parameters: Type.Object({
        class: Type.String({ description: 'Actor class name (e.g. StaticMeshActor, PointLight)' }),
        x: Type.Number({ description: 'X position' }),
        y: Type.Number({ description: 'Y position' }),
        z: Type.Number({ description: 'Z position' }),
        pitch: Type.Optional(Type.Number({ description: 'Pitch rotation' })),
        yaw: Type.Optional(Type.Number({ description: 'Yaw rotation' })),
        roll: Type.Optional(Type.Number({ description: 'Roll rotation' })),
        label: Type.Optional(Type.String({ description: 'Display label for the actor' })),
      }),
      async execute(_id, params) { return run('POST', '/nova/scene/spawn', params); },
    });

    api.registerTool({
      name: 'ue5_scene_delete',
      label: 'UE5 Delete Actor',
      description: 'Delete an actor from the scene by name or label',
      parameters: Type.Object({
        name: Type.String({ description: 'Actor name or label' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/scene/delete', params); },
    });

    api.registerTool({
      name: 'ue5_scene_transform',
      label: 'UE5 Transform Actor',
      description: 'Set an actor\'s transform (location, rotation, scale). Provide only the fields you want to change.',
      parameters: Type.Object({
        name: Type.String({ description: 'Actor name or label' }),
        location: Type.Optional(Type.Object({
          x: Type.Optional(Type.Number()),
          y: Type.Optional(Type.Number()),
          z: Type.Optional(Type.Number()),
        })),
        rotation: Type.Optional(Type.Object({
          pitch: Type.Optional(Type.Number()),
          yaw: Type.Optional(Type.Number()),
          roll: Type.Optional(Type.Number()),
        })),
        scale: Type.Optional(Type.Object({
          x: Type.Optional(Type.Number()),
          y: Type.Optional(Type.Number()),
          z: Type.Optional(Type.Number()),
        })),
      }),
      async execute(_id, params) { return run('POST', '/nova/scene/transform', params); },
    });

    api.registerTool({
      name: 'ue5_scene_get',
      label: 'UE5 Get Actor',
      description: 'Get detailed info about an actor including all editable properties and components',
      parameters: Type.Object({
        name: Type.String({ description: 'Actor name or label' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/scene/get', params); },
    });

    api.registerTool({
      name: 'ue5_scene_set_property',
      label: 'UE5 Set Property',
      description: 'Set a property on an actor or component. For component properties use ComponentName.PropertyName syntax (e.g. "LightComponent0.Intensity", "StaticMeshComponent0.StaticMesh"). Call ue5_scene_get first to see component names and properties.',
      parameters: Type.Object({
        name: Type.String({ description: 'Actor name or label' }),
        property: Type.String({ description: 'Property name' }),
        value: Type.String({ description: 'Property value as string (UE5 text format)' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/scene/set-property', params); },
    });

    // Assets
    api.registerTool({
      name: 'ue5_asset_list',
      label: 'UE5 List Assets',
      description: 'List assets in a content path. Defaults to /Game.',
      parameters: Type.Object({
        path: Type.Optional(Type.String({ description: 'Content path (default: /Game)' })),
      }),
      async execute(_id, params) { return run('POST', '/nova/asset/list', params); },
    });

    api.registerTool({
      name: 'ue5_asset_create',
      label: 'UE5 Create Asset',
      description: 'Create a new asset. Supported types: Material, StaticMesh.',
      parameters: Type.Object({
        type: Type.String({ description: 'Asset type (Material, StaticMesh)' }),
        name: Type.String({ description: 'Asset name' }),
        path: Type.Optional(Type.String({ description: 'Content path (default: /Game)' })),
      }),
      async execute(_id, params) { return run('POST', '/nova/asset/create', params); },
    });

    api.registerTool({
      name: 'ue5_asset_duplicate',
      label: 'UE5 Duplicate Asset',
      description: 'Duplicate an existing asset',
      parameters: Type.Object({
        source: Type.String({ description: 'Source asset path' }),
        destination: Type.String({ description: 'Destination asset path' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/asset/duplicate', params); },
    });

    api.registerTool({
      name: 'ue5_asset_delete',
      label: 'UE5 Delete Asset',
      description: 'Delete an asset',
      parameters: Type.Object({
        path: Type.String({ description: 'Asset path to delete' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/asset/delete', params); },
    });

    api.registerTool({
      name: 'ue5_asset_rename',
      label: 'UE5 Rename Asset',
      description: 'Rename or move an asset',
      parameters: Type.Object({
        source: Type.String({ description: 'Current asset path' }),
        destination: Type.String({ description: 'New asset path' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/asset/rename', params); },
    });

    api.registerTool({
      name: 'ue5_asset_info',
      label: 'UE5 Asset Info',
      description: 'Get metadata about an asset',
      parameters: Type.Object({
        path: Type.String({ description: 'Asset path' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/asset/info', params); },
    });

    api.registerTool({
      name: 'ue5_asset_import',
      label: 'UE5 Import Asset',
      description: 'Import an external file (FBX, OBJ, PNG, etc.) into UE5 Content Browser. The file must exist on disk. Use blender_to_ue5 for the full Blender→UE5 pipeline, or use this directly for files already on disk.',
      parameters: Type.Object({
        file_path: Type.String({ description: 'Absolute path to the file on disk (e.g. /tmp/model.fbx)' }),
        asset_name: Type.Optional(Type.String({ description: 'Name for the imported asset' })),
        destination: Type.Optional(Type.String({ description: 'UE5 content path (default: /Game)' })),
      }),
      async execute(_id, params) { return run('POST', '/nova/asset/import', params); },
    });

    // Mesh
    api.registerTool({
      name: 'ue5_mesh_create',
      label: 'UE5 Create Mesh',
      description: 'Create a static mesh from vertices and triangles. Vertices: [{x,y,z}]. Triangles: [{i0,i1,i2}]. Optional per-vertex: u,v (UVs), nx,ny,nz (normals).',
      parameters: Type.Object({
        name: Type.String({ description: 'Mesh asset name' }),
        path: Type.Optional(Type.String({ description: 'Content path (default: /Game)' })),
        vertices: Type.Array(Type.Object({
          x: Type.Number(), y: Type.Number(), z: Type.Number(),
          u: Type.Optional(Type.Number()), v: Type.Optional(Type.Number()),
          nx: Type.Optional(Type.Number()), ny: Type.Optional(Type.Number()), nz: Type.Optional(Type.Number()),
        })),
        triangles: Type.Array(Type.Object({
          i0: Type.Number(), i1: Type.Number(), i2: Type.Number(),
        })),
      }),
      async execute(_id, params) { return run('POST', '/nova/mesh/create', params); },
    });

    api.registerTool({
      name: 'ue5_mesh_get',
      label: 'UE5 Get Mesh',
      description: 'Get info about a static mesh (vertex/triangle counts, LODs)',
      parameters: Type.Object({
        path: Type.String({ description: 'Mesh asset path' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/mesh/get', params); },
    });

    api.registerTool({
      name: 'ue5_mesh_primitive',
      label: 'UE5 Primitive Mesh',
      description: 'Generate a primitive mesh. Supported types: cube, box, plane, sphere, cylinder.',
      parameters: Type.Object({
        type: Type.String({ description: 'Primitive type: cube, box, plane' }),
        name: Type.Optional(Type.String({ description: 'Asset name (defaults to type name)' })),
        path: Type.Optional(Type.String({ description: 'Content path (default: /Game)' })),
        size: Type.Optional(Type.Number({ description: 'Size in units (default: 100)' })),
      }),
      async execute(_id, params) { return run('POST', '/nova/mesh/primitive', params); },
    });

    // Material
    api.registerTool({
      name: 'ue5_material_create',
      label: 'UE5 Create Material',
      description: 'Create a new material. Optionally set base color with {r,g,b,a} in 0-1 range.',
      parameters: Type.Object({
        name: Type.String({ description: 'Material name' }),
        path: Type.Optional(Type.String({ description: 'Content path (default: /Game)' })),
        color: Type.Optional(Type.Object({
          r: Type.Number({ description: 'Red 0-1' }),
          g: Type.Number({ description: 'Green 0-1' }),
          b: Type.Number({ description: 'Blue 0-1' }),
          a: Type.Optional(Type.Number({ description: 'Alpha 0-1' })),
        })),
      }),
      async execute(_id, params) { return run('POST', '/nova/material/create', params); },
    });

    api.registerTool({
      name: 'ue5_material_set_param',
      label: 'UE5 Set Material Param',
      description: 'Set a parameter on a material instance. Type: scalar (number) or vector ({r,g,b,a}).',
      parameters: Type.Object({
        path: Type.String({ description: 'Material instance path' }),
        param: Type.String({ description: 'Parameter name' }),
        type: Type.Optional(Type.String({ description: 'Parameter type: scalar or vector' })),
        value: Type.Unknown({ description: 'Parameter value (number for scalar, {r,g,b,a} for vector)' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/material/set-param', params); },
    });

    api.registerTool({
      name: 'ue5_material_get',
      label: 'UE5 Get Material',
      description: 'Get material info and parameters',
      parameters: Type.Object({
        path: Type.String({ description: 'Material path' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/material/get', params); },
    });

    api.registerTool({
      name: 'ue5_material_create_instance',
      label: 'UE5 Material Instance',
      description: 'Create a material instance from a parent material',
      parameters: Type.Object({
        parent: Type.String({ description: 'Parent material path' }),
        name: Type.String({ description: 'Instance name' }),
        path: Type.Optional(Type.String({ description: 'Content path (default: /Game)' })),
      }),
      async execute(_id, params) { return run('POST', '/nova/material/create-instance', params); },
    });

    // Viewport
    api.registerTool({
      name: 'ue5_viewport_screenshot',
      label: 'UE5 Screenshot',
      description: 'Capture a screenshot of the editor viewport. Saves PNG to workspace/screenshots/ and returns the file path. Use to SEE what you are building.',
      parameters: Type.Object({}),
      async execute(_id, _params) {
        try {
          const result = await ue5Request('GET', '/nova/viewport/screenshot');
          if (result.image) {
            const dir = '/home/nova/.openclaw/workspace/screenshots';
            if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
            const ts = new Date().toISOString().replace(/[:.]/g, '-');
            const filename = `viewport-${ts}.png`;
            const filepath = path.join(dir, filename);
            fs.writeFileSync(filepath, Buffer.from(result.image, 'base64'));
            const sizeKB = Math.round(fs.statSync(filepath).size / 1024);
            const resolution = result.width ? `${result.width}x${result.height}` : '1280x720';
            return {
              content: [
                { type: 'text', text: JSON.stringify({ status: 'ok', file: filepath, filename, size_kb: sizeKB, resolution, message: `Screenshot captured (${sizeKB}KB, ${resolution}). The image is shown below — describe what you see and evaluate your work.` }, null, 2) },
                { type: 'image', data: result.image, mimeType: 'image/png' },
              ],
            };
          }
          return json(result);
        } catch (err) {
          return json({ error: err.message });
        }
      },
    });

    api.registerTool({
      name: 'ue5_viewport_set_camera',
      label: 'UE5 Set Camera',
      description: 'Set the editor viewport camera position and rotation',
      parameters: Type.Object({
        location: Type.Optional(Type.Object({
          x: Type.Optional(Type.Number()),
          y: Type.Optional(Type.Number()),
          z: Type.Optional(Type.Number()),
        })),
        rotation: Type.Optional(Type.Object({
          pitch: Type.Optional(Type.Number()),
          yaw: Type.Optional(Type.Number()),
          roll: Type.Optional(Type.Number()),
        })),
      }),
      async execute(_id, params) { return run('POST', '/nova/viewport/camera/set', params); },
    });

    api.registerTool({
      name: 'ue5_viewport_get_camera',
      label: 'UE5 Get Camera',
      description: 'Get the current editor viewport camera position and rotation',
      parameters: Type.Object({}),
      async execute(_id, _params) { return run('GET', '/nova/viewport/camera/get'); },
    });

    // Blueprint
    api.registerTool({
      name: 'ue5_blueprint_create',
      label: 'UE5 Create Blueprint',
      description: 'Create a new Blueprint class. Parent class defaults to Actor.',
      parameters: Type.Object({
        name: Type.String({ description: 'Blueprint name' }),
        path: Type.Optional(Type.String({ description: 'Content path (default: /Game)' })),
        parent_class: Type.Optional(Type.String({ description: 'Parent class (default: Actor)' })),
      }),
      async execute(_id, params) { return run('POST', '/nova/blueprint/create', params); },
    });

    api.registerTool({
      name: 'ue5_blueprint_add_component',
      label: 'UE5 Add Component',
      description: 'Add a component to a Blueprint. Common: StaticMeshComponent, SkeletalMeshComponent, PointLightComponent, SceneComponent.',
      parameters: Type.Object({
        blueprint: Type.String({ description: 'Blueprint asset path' }),
        component_class: Type.String({ description: 'Component class name' }),
        component_name: Type.Optional(Type.String({ description: 'Component name' })),
      }),
      async execute(_id, params) { return run('POST', '/nova/blueprint/add-component', params); },
    });

    api.registerTool({
      name: 'ue5_blueprint_compile',
      label: 'UE5 Compile Blueprint',
      description: 'Compile a Blueprint',
      parameters: Type.Object({
        blueprint: Type.String({ description: 'Blueprint asset path' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/blueprint/compile', params); },
    });

    // Build
    api.registerTool({
      name: 'ue5_build_lighting',
      label: 'UE5 Build Lighting',
      description: 'Build lighting for the current level',
      parameters: Type.Object({}),
      async execute(_id, _params) { return run('POST', '/nova/build/lighting', {}); },
    });

    api.registerTool({
      name: 'ue5_exec_command',
      label: 'UE5 Console Command',
      description: 'Execute a UE5 console command (e.g. "SAVE ALL", "MAP REBUILD")',
      parameters: Type.Object({
        command: Type.String({ description: 'Console command to execute' }),
      }),
      async execute(_id, params) { return run('POST', '/nova/exec/command', params); },
    });
  }
};

module.exports = plugin;
module.exports.default = plugin;
