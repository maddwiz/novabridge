const { execFile } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');
const http = require('http');
const https = require('https');
const { Type } = require('@sinclair/typebox');

const BLENDER_BIN = process.env.NOVABRIDGE_BLENDER_PATH
  || (process.platform === 'win32'
    ? 'C:\\Program Files\\Blender Foundation\\Blender 4.0\\blender.exe'
    : process.platform === 'darwin'
      ? '/Applications/Blender.app/Contents/MacOS/Blender'
      : '/usr/bin/blender');
const EXPORT_DIR = process.env.NOVABRIDGE_EXPORT_DIR || path.join(os.tmpdir(), 'novabridge-exports');
const SCRIPT_DIR_CANDIDATES = [
  process.env.NOVABRIDGE_SCRIPTS_DIR,
  path.resolve(__dirname, 'scripts'),
  path.resolve(__dirname, '../../../blender/scripts'),
].filter(Boolean);
const SCRIPTS_DIR = SCRIPT_DIR_CANDIDATES.find((dir) => fs.existsSync(dir)) || SCRIPT_DIR_CANDIDATES[0];
const OUTPUT_DIR = process.env.NOVABRIDGE_OUTPUT_DIR || path.join(EXPORT_DIR, 'output');
const UE5_PORT_RAW = Number.parseInt(process.env.NOVABRIDGE_PORT || '30010', 10);
const UE5_PORT = Number.isFinite(UE5_PORT_RAW) && UE5_PORT_RAW > 0 ? UE5_PORT_RAW : 30010;
const UE5_HOST = process.env.NOVABRIDGE_HOST || 'localhost';
const UE5_API_KEY = process.env.NOVABRIDGE_API_KEY || '';
const UE5_IMPORT_SCALE_RAW = Number.parseFloat(process.env.NOVABRIDGE_IMPORT_SCALE || '100');
const UE5_IMPORT_SCALE = Number.isFinite(UE5_IMPORT_SCALE_RAW) && UE5_IMPORT_SCALE_RAW > 0 ? UE5_IMPORT_SCALE_RAW : 100;

// Ensure export dir exists
if (!fs.existsSync(EXPORT_DIR)) fs.mkdirSync(EXPORT_DIR, { recursive: true });
if (!fs.existsSync(OUTPUT_DIR)) fs.mkdirSync(OUTPUT_DIR, { recursive: true });

function ue5Request(method, urlPath, body) {
  return new Promise((resolve, reject) => {
    const bodyStr = body ? JSON.stringify(body) : null;
    const options = {
      hostname: UE5_HOST, port: UE5_PORT, path: urlPath, method,
      headers: { 'Content-Type': 'application/json' },
      timeout: 120000,
    };
    if (UE5_API_KEY) options.headers['X-API-Key'] = UE5_API_KEY;
    if (bodyStr) options.headers['Content-Length'] = Buffer.byteLength(bodyStr);
    const req = http.request(options, (res) => {
      let data = '';
      res.on('data', (chunk) => data += chunk);
      res.on('end', () => {
        try { resolve(JSON.parse(data)); }
        catch (e) { resolve({ raw: data }); }
      });
    });
    req.on('error', (err) => reject(err));
    req.on('timeout', () => { req.destroy(); reject(new Error('UE5 request timed out')); });
    if (bodyStr) req.write(bodyStr);
    req.end();
  });
}

function runBlender(args, timeoutMs = 120000) {
  return new Promise((resolve, reject) => {
    execFile(BLENDER_BIN, args, {
      timeout: timeoutMs,
      maxBuffer: 10 * 1024 * 1024,
      env: { ...process.env, DISPLAY: '' },
    }, (err, stdout, stderr) => {
      if (err && err.killed) {
        reject(new Error(`Blender timed out after ${timeoutMs/1000}s`));
      } else if (err) {
        reject(new Error(`Blender error: ${err.message}\nstderr: ${stderr}`));
      } else {
        resolve({ stdout, stderr });
      }
    });
  });
}

function json(data) {
  return { content: [{ type: 'text', text: JSON.stringify(data, null, 2) }], details: data };
}

function downloadFile(url, destPath) {
  return new Promise((resolve, reject) => {
    const client = url.startsWith('https') ? https : http;
    const doRequest = (reqUrl, redirects = 0) => {
      if (redirects > 5) return reject(new Error('Too many redirects'));
      const httpMod = reqUrl.startsWith('https') ? https : http;
      httpMod.get(reqUrl, { timeout: 60000 }, (res) => {
        if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
          return doRequest(res.headers.location, redirects + 1);
        }
        if (res.statusCode !== 200) {
          return reject(new Error(`Download failed: HTTP ${res.statusCode}`));
        }
        const file = fs.createWriteStream(destPath);
        res.pipe(file);
        file.on('finish', () => { file.close(); resolve(destPath); });
        file.on('error', (err) => { fs.unlink(destPath, () => {}); reject(err); });
      }).on('error', reject);
    };
    doRequest(url);
  });
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function httpJsonRequest(url, method = 'GET', body = null, headers = {}) {
  return new Promise((resolve, reject) => {
    const target = new URL(url);
    const client = target.protocol === 'https:' ? https : http;
    const bodyStr = body ? JSON.stringify(body) : null;
    const req = client.request({
      protocol: target.protocol,
      hostname: target.hostname,
      port: target.port || (target.protocol === 'https:' ? 443 : 80),
      path: `${target.pathname}${target.search}`,
      method,
      headers: {
        'Content-Type': 'application/json',
        ...(bodyStr ? { 'Content-Length': Buffer.byteLength(bodyStr) } : {}),
        ...headers,
      },
      timeout: 60000,
    }, (res) => {
      let raw = '';
      res.on('data', (chunk) => raw += chunk);
      res.on('end', () => {
        if (res.statusCode < 200 || res.statusCode >= 300) {
          return reject(new Error(`HTTP ${res.statusCode}: ${raw.slice(0, 500)}`));
        }
        try {
          resolve(JSON.parse(raw));
        } catch (e) {
          reject(new Error(`Invalid JSON response: ${raw.slice(0, 500)}`));
        }
      });
    });
    req.on('error', reject);
    req.on('timeout', () => { req.destroy(); reject(new Error(`Request timeout: ${url}`)); });
    if (bodyStr) req.write(bodyStr);
    req.end();
  });
}

// OBJ export code for Blender (used by export and pipeline tools)
function objExportCode(outPath) {
  const normalizedOutPath = outPath.replace(/\\/g, '/');
  return `
import bpy

# Remove non-mesh objects and flatten artifacts (e.g., MB-Lab ground plane).
for obj in list(bpy.data.objects):
    if obj.type != 'MESH':
        bpy.data.objects.remove(obj, do_unlink=True)
        continue
    dims = obj.dimensions
    if dims.z < 0.01 and dims.x > 1.0 and dims.y > 1.0:
        bpy.data.objects.remove(obj, do_unlink=True)

# Apply all modifiers before export
for obj in bpy.data.objects:
    if obj.type == 'MESH':
        bpy.context.view_layer.objects.active = obj
        for mod in obj.modifiers:
            try:
                bpy.ops.object.modifier_apply(modifier=mod.name)
            except:
                pass

# Export as OBJ with normals and UVs
bpy.ops.wm.obj_export(
    filepath="${normalizedOutPath}",
    export_selected_objects=False,
    export_uv=True,
    export_normals=True,
    export_materials=False,
    forward_axis='NEGATIVE_Y',
    up_axis='Z',
    global_scale=1.0,
)
print(f"[Nova] Exported OBJ: {sum(1 for o in bpy.data.objects if o.type == 'MESH')} meshes")
`;
}

const plugin = {
  id: 'nova-blender',
  name: 'Nova Blender Bridge',
  description: 'Blender 3D modeling tools - create meshes, sculpt, export, and import into UE5',

  register(api) {

    // ─── Run a Blender Python script ───
    api.registerTool({
      name: 'blender_run',
      label: 'Blender Run Script',
      description: `Run a Python script in headless Blender. Provide either 'script_path' (path to .py file) or 'script' (inline Python code). The script runs in Blender's Python environment with full access to the bpy API. Use this for creating meshes, sculpting, modifiers, materials, UV unwrapping, etc. Returns stdout/stderr from Blender.`,
      parameters: Type.Object({
        script_path: Type.Optional(Type.String({ description: 'Path to a .py script file to run' })),
        script: Type.Optional(Type.String({ description: 'Inline Python code to run in Blender' })),
        blend_file: Type.Optional(Type.String({ description: 'Open this .blend file before running the script' })),
        args: Type.Optional(Type.Array(Type.String(), { description: 'Extra arguments passed after -- (accessible via sys.argv)' })),
      }),
      async execute(_id, params) {
        try {
          const blenderArgs = ['--background'];
          if (params.blend_file) blenderArgs.push(params.blend_file);

          let tmpScript = null;
          if (params.script_path) {
            blenderArgs.push('--python', params.script_path);
          } else if (params.script) {
            tmpScript = path.join(EXPORT_DIR, `inline_${Date.now()}.py`);
            fs.writeFileSync(tmpScript, params.script);
            blenderArgs.push('--python', tmpScript);
          } else {
            return json({ error: 'Provide either script_path or script (inline Python code)' });
          }

          if (params.args && params.args.length > 0) {
            blenderArgs.push('--');
            blenderArgs.push(...params.args);
          }

          const { stdout, stderr } = await runBlender(blenderArgs);
          if (tmpScript) try { fs.unlinkSync(tmpScript); } catch(e) {}

          return json({
            status: 'ok',
            stdout: stdout.split('\n').filter(l => !l.startsWith('Blender ') && l.trim()).join('\n'),
            stderr: stderr ? stderr.split('\n').filter(l => l.trim()).slice(-20).join('\n') : '',
          });
        } catch (err) {
          return json({ error: err.message });
        }
      },
    });

    // ─── List available scripts ───
    api.registerTool({
      name: 'blender_list_scripts',
      label: 'Blender List Scripts',
      description: 'List available Blender Python scripts in the workspace scripts directory.',
      parameters: Type.Object({}),
      async execute(_id, _params) {
        try {
          const scripts = [];
          const scanDir = (dir) => {
            if (!fs.existsSync(dir)) return;
            for (const f of fs.readdirSync(dir)) {
              const full = path.join(dir, f);
              if (fs.statSync(full).isDirectory()) scanDir(full);
              else if (f.endsWith('.py')) scripts.push(full);
            }
          };
          scanDir(SCRIPTS_DIR);
          const outputs = [];
          if (fs.existsSync(OUTPUT_DIR)) {
            for (const f of fs.readdirSync(OUTPUT_DIR)) {
              if (f.endsWith('.blend') || f.endsWith('.obj') || f.endsWith('.glb')) {
                outputs.push(path.join(OUTPUT_DIR, f));
              }
            }
          }
          return json({ scripts, output_files: outputs });
        } catch (err) {
          return json({ error: err.message });
        }
      },
    });

    // ─── Export .blend to OBJ ───
    api.registerTool({
      name: 'blender_export',
      label: 'Blender Export OBJ',
      description: 'Export a Blender scene to OBJ format (compatible with UE5 import). Provide a blend_file, or a script to create geometry, or both. Returns the path to the exported OBJ file.',
      parameters: Type.Object({
        blend_file: Type.Optional(Type.String({ description: 'Path to .blend file to export' })),
        script: Type.Optional(Type.String({ description: 'Inline Python to run before exporting' })),
        script_path: Type.Optional(Type.String({ description: 'Path to .py script to run before exporting' })),
        output_name: Type.Optional(Type.String({ description: 'Output filename (default: export_<timestamp>.obj)' })),
      }),
      async execute(_id, params) {
        try {
          const outName = params.output_name || `export_${Date.now()}.obj`;
          const outPath = path.join(EXPORT_DIR, outName);

          let fullScript = '';
          if (params.script) {
            fullScript = params.script;
          } else if (params.script_path) {
            fullScript = fs.readFileSync(params.script_path, 'utf8');
          }
          fullScript += '\n' + objExportCode(outPath);

          const tmpExport = path.join(EXPORT_DIR, `export_script_${Date.now()}.py`);
          fs.writeFileSync(tmpExport, fullScript);

          const blenderArgs = ['--background'];
          if (params.blend_file) blenderArgs.push(params.blend_file);
          blenderArgs.push('--python', tmpExport);

          const { stdout, stderr } = await runBlender(blenderArgs, 180000);
          try { fs.unlinkSync(tmpExport); } catch(e) {}

          if (!fs.existsSync(outPath)) {
            return json({ error: 'OBJ export failed - file not created', stdout: stdout.slice(-2000), stderr: stderr ? stderr.slice(-1000) : '' });
          }

          const stats = fs.statSync(outPath);
          return json({
            status: 'ok',
            obj_path: outPath,
            size_bytes: stats.size,
            stdout: stdout.split('\n').filter(l => l.includes('[Nova]')).join('\n'),
          });
        } catch (err) {
          return json({ error: err.message });
        }
      },
    });

    // ─── Full pipeline: Blender → OBJ → UE5 ───
    api.registerTool({
      name: 'blender_to_ue5',
      label: 'Blender to UE5',
      description: `Full pipeline: Run a Blender Python script to create/modify a mesh, export to OBJ, and automatically import into UE5 as a StaticMesh. This is the RECOMMENDED way to create complex 3D models (human heads, bodies, organic shapes, detailed props). The script should create geometry using Blender's bpy API — subdivision surfaces, sculpt, modifiers, proportional editing, etc. The resulting mesh appears in UE5 Content Browser at /Game/<asset_name>. After import, use ue5_scene_spawn + ue5_set_property to place it in the scene.`,
      parameters: Type.Object({
        script: Type.Optional(Type.String({ description: 'Inline Blender Python script that creates the geometry' })),
        script_path: Type.Optional(Type.String({ description: 'Path to .py script file' })),
        blend_file: Type.Optional(Type.String({ description: 'Path to existing .blend file to export' })),
        asset_name: Type.String({ description: 'Name for the UE5 asset (e.g. HumanoidHead, NovaBody)' }),
        ue5_path: Type.Optional(Type.String({ description: 'UE5 content path (default: /Game)' })),
      }),
      async execute(_id, params) {
        try {
          const objName = `${params.asset_name}.obj`;
          const objPath = path.join(EXPORT_DIR, objName);

          // Build combined script: user script + OBJ export
          let fullScript = '';
          if (params.script) {
            fullScript = params.script;
          } else if (params.script_path) {
            fullScript = fs.readFileSync(params.script_path, 'utf8');
          }
          fullScript += '\n' + objExportCode(objPath);

          const tmpScript = path.join(EXPORT_DIR, `pipeline_${Date.now()}.py`);
          fs.writeFileSync(tmpScript, fullScript);

          const blenderArgs = ['--background'];
          if (params.blend_file) blenderArgs.push(params.blend_file);
          blenderArgs.push('--python', tmpScript);

          // Step 1: Run Blender → export OBJ
          const { stdout, stderr } = await runBlender(blenderArgs, 180000);
          try { fs.unlinkSync(tmpScript); } catch(e) {}

          if (!fs.existsSync(objPath)) {
            return json({
              error: 'Blender did not produce OBJ file',
              stdout: stdout.split('\n').slice(-30).join('\n'),
              stderr: stderr ? stderr.split('\n').slice(-20).join('\n') : '',
            });
          }

          const objSize = fs.statSync(objPath).size;

          // Step 2: Import OBJ into UE5 via NovaBridge
          const ue5Result = await ue5Request('POST', '/nova/asset/import', {
            file_path: objPath,
            asset_name: params.asset_name,
            destination: params.ue5_path || '/Game',
            scale: UE5_IMPORT_SCALE,
          });

          return json({
            status: ue5Result.status || 'ok',
            blender: {
              stdout: stdout.split('\n').filter(l => l.includes('[Nova]')).join('\n'),
              obj_path: objPath,
              obj_size: objSize,
            },
            ue5_import: ue5Result,
            next_steps: `Mesh imported as /Game/${params.asset_name}. Use ue5_scene_spawn to place a StaticMeshActor, then ue5_set_property with 'StaticMeshComponent0.StaticMesh' = '/Game/${params.asset_name}.${params.asset_name}' to assign the mesh.`,
          });
        } catch (err) {
          return json({ error: err.message });
        }
      },
    });

    // ─── Download 3D model from URL ───
    api.registerTool({
      name: 'model_download',
      label: 'Download 3D Model',
      description: `Download a 3D model file from a URL. Optionally convert to OBJ via Blender and auto-import into UE5. Use with free model libraries like Sketchfab, TurboSquid, CGTrader. Supports OBJ (direct import), GLB/GLTF/BLEND/FBX (converted via Blender).`,
      parameters: Type.Object({
        url: Type.String({ description: 'URL to download the 3D model from' }),
        filename: Type.Optional(Type.String({ description: 'Save as filename (auto-detected from URL if omitted)' })),
        import_to_ue5: Type.Optional(Type.Boolean({ description: 'Auto-import into UE5 after download (default: false)' })),
        asset_name: Type.Optional(Type.String({ description: 'UE5 asset name (required if import_to_ue5 is true)' })),
      }),
      async execute(_id, params) {
        try {
          let filename = params.filename;
          if (!filename) {
            const urlPath = new URL(params.url).pathname;
            filename = path.basename(urlPath) || `model_${Date.now()}`;
          }
          const destPath = path.join(EXPORT_DIR, filename);

          await downloadFile(params.url, destPath);
          const stats = fs.statSync(destPath);
          const ext = path.extname(filename).toLowerCase();

          const result = {
            status: 'ok',
            file_path: destPath,
            size_bytes: stats.size,
            format: ext,
          };

          if (params.import_to_ue5 && params.asset_name) {
            let objPath = destPath;

            // Convert to OBJ if not already OBJ
            if (ext !== '.obj') {
              objPath = path.join(EXPORT_DIR, `${params.asset_name}.obj`);
              let importLine = '';
              if (ext === '.glb' || ext === '.gltf') importLine = `bpy.ops.import_scene.gltf(filepath="${destPath.replace(/\\/g, '/')}")`;
              else if (ext === '.fbx') importLine = `bpy.ops.import_scene.fbx(filepath="${destPath.replace(/\\/g, '/')}")`;
              else if (ext === '.blend') importLine = ''; // blend_file arg handles it

              const convertScript = `
import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
${importLine}
` + objExportCode(objPath);

              const tmpScript = path.join(EXPORT_DIR, `convert_${Date.now()}.py`);
              fs.writeFileSync(tmpScript, convertScript);

              const blenderArgs = ['--background'];
              if (ext === '.blend') blenderArgs.push(destPath);
              blenderArgs.push('--python', tmpScript);

              await runBlender(blenderArgs);
              try { fs.unlinkSync(tmpScript); } catch(e) {}
              result.converted_to = objPath;
            }

            // Import OBJ to UE5
            const ue5Result = await ue5Request('POST', '/nova/asset/import', {
              file_path: objPath,
              asset_name: params.asset_name,
              destination: '/Game',
              scale: UE5_IMPORT_SCALE,
            });
            result.ue5_import = ue5Result;
          }

          return json(result);
        } catch (err) {
          return json({ error: err.message });
        }
      },
    });

    // ─── Generate 3D model from prompt via provider API ───
    api.registerTool({
      name: 'ai_generate_3d',
      label: 'AI Generate 3D',
      description: 'Generate a 3D model from text prompt using Meshy/Luma/Tripo, download it, and optionally import to UE5.',
      parameters: Type.Object({
        prompt: Type.String({ description: 'Text description of the model to generate' }),
        provider: Type.Optional(Type.String({ description: 'meshy | luma | tripo (default: meshy)' })),
        asset_name: Type.Optional(Type.String({ description: 'Asset name for local file and UE5 import' })),
        import_to_ue5: Type.Optional(Type.Boolean({ description: 'Import generated model into UE5 (default true)' })),
        style: Type.Optional(Type.String({ description: 'Style hint (provider-specific)' })),
      }),
      async execute(_id, params) {
        const provider = (params.provider || 'meshy').toLowerCase();
        const importToUe5 = params.import_to_ue5 !== false;
        const assetName = params.asset_name || `ai_model_${Date.now()}`;

        try {
          let modelUrl = '';
          let meta = {};

          if (provider === 'meshy') {
            const apiKey = process.env.MESHY_API_KEY || '';
            if (!apiKey) return json({ error: 'Set MESHY_API_KEY for provider=meshy' });

            const created = await httpJsonRequest(
              'https://api.meshy.ai/v2/text-to-3d',
              'POST',
              {
                mode: 'preview',
                prompt: params.prompt,
                art_style: params.style || 'realistic',
              },
              { Authorization: `Bearer ${apiKey}` },
            );

            const taskId = created.result || created.id || created.task_id;
            if (!taskId) return json({ error: 'Meshy did not return a task id', raw: created });

            let status = 'PENDING';
            let poll = {};
            for (let i = 0; i < 60; i++) {
              await sleep(5000);
              poll = await httpJsonRequest(
                `https://api.meshy.ai/v2/text-to-3d/${taskId}`,
                'GET',
                null,
                { Authorization: `Bearer ${apiKey}` },
              );
              status = (poll.status || '').toUpperCase();
              if (status === 'SUCCEEDED' || status === 'FAILED') break;
            }
            if (status !== 'SUCCEEDED') {
              return json({ error: `Meshy generation failed with status=${status || 'unknown'}`, details: poll });
            }
            modelUrl = (poll.model_urls && (poll.model_urls.glb || poll.model_urls.obj || poll.model_urls.fbx)) || '';
            meta = { task_id: taskId, status };
          } else {
            return json({ error: `Provider '${provider}' not implemented yet. Supported now: meshy` });
          }

          if (!modelUrl) {
            return json({ error: 'No downloadable model URL returned by provider', provider, meta });
          }

          const urlExt = path.extname(new URL(modelUrl).pathname).toLowerCase() || '.glb';
          const downloadPath = path.join(EXPORT_DIR, `${assetName}${urlExt}`);
          await downloadFile(modelUrl, downloadPath);

          const result = {
            status: 'ok',
            provider,
            prompt: params.prompt,
            file_path: downloadPath,
            source_url: modelUrl,
            ...meta,
          };

          if (importToUe5) {
            let importPath = downloadPath;
            const ext = path.extname(importPath).toLowerCase();
            if (ext !== '.obj') {
              const objPath = path.join(EXPORT_DIR, `${assetName}.obj`);
              let importLine = '';
              if (ext === '.glb' || ext === '.gltf') importLine = `bpy.ops.import_scene.gltf(filepath="${importPath.replace(/\\/g, '/')}")`;
              else if (ext === '.fbx') importLine = `bpy.ops.import_scene.fbx(filepath="${importPath.replace(/\\/g, '/')}")`;
              const convertScript = `
import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
${importLine}
` + objExportCode(objPath);
              const tmpScript = path.join(EXPORT_DIR, `ai_convert_${Date.now()}.py`);
              fs.writeFileSync(tmpScript, convertScript);
              await runBlender(['--background', '--python', tmpScript], 240000);
              try { fs.unlinkSync(tmpScript); } catch (e) {}
              importPath = objPath;
              result.converted_to = objPath;
            }

            const ue5Import = await ue5Request('POST', '/nova/asset/import', {
              file_path: importPath,
              asset_name: assetName,
              destination: '/Game',
              scale: UE5_IMPORT_SCALE,
            });
            result.ue5_import = ue5Import;
          }

          return json(result);
        } catch (err) {
          return json({ error: err.message, provider, prompt: params.prompt });
        }
      },
    });

    // ─── Search free 3D model sites ───
    api.registerTool({
      name: 'model_search',
      label: 'Search 3D Models',
      description: `Search for free 3D models on Sketchfab. Returns model names, URLs, and info. Use this to find professional-quality meshes (characters, heads, bodies, props, environments) instead of creating from scratch. After finding a model, visit the URL to download, or use model_download with a direct file URL.`,
      parameters: Type.Object({
        query: Type.String({ description: 'Search query (e.g. "female head", "humanoid body", "sci-fi helmet")' }),
        max_results: Type.Optional(Type.Number({ description: 'Max results to return (default: 10)' })),
      }),
      async execute(_id, params) {
        try {
          const maxResults = params.max_results || 10;
          const query = encodeURIComponent(params.query);
          const url = `https://api.sketchfab.com/v3/search?type=models&q=${query}&downloadable=true&sort_by=-likeCount&count=${maxResults}`;

          const data = await new Promise((resolve, reject) => {
            https.get(url, { timeout: 15000 }, (res) => {
              let body = '';
              res.on('data', (chunk) => body += chunk);
              res.on('end', () => {
                try { resolve(JSON.parse(body)); }
                catch(e) { reject(new Error('Failed to parse Sketchfab response')); }
              });
            }).on('error', reject);
          });

          if (!data.results) {
            return json({ error: 'No results from Sketchfab', raw: data });
          }

          const models = data.results.map(m => ({
            name: m.name,
            url: m.viewerUrl,
            author: m.user?.displayName,
            likes: m.likeCount,
            faces: m.faceCount,
            vertices: m.vertexCount,
            license: m.license?.label,
            download_note: 'Sketchfab downloads require a free account + API token. Visit the URL to download manually, or use model_download with a direct file URL.',
          }));

          return json({
            query: params.query,
            count: models.length,
            models,
            tip: 'To use a model: 1) Visit the URL and download OBJ, 2) Use model_download with the file URL, or 3) Use blender_to_ue5 to create your own.',
          });
        } catch (err) {
          return json({ error: err.message });
        }
      },
    });

  },
};

module.exports = plugin;
module.exports.default = plugin;
