"use strict";

const { buildCommandCatalog, summarizePlanRisk } = require("./command_catalog");

const DEFAULT_PLAN = {
  mode: "editor",
  steps: [
    {
      action: "spawn",
      params: {
        type: "PointLight",
        label: "AssistantLight"
      }
    }
  ]
};

let fetchImplementation = (...args) => fetch(...args);

function setFetchImplementation(fetchImpl) {
  if (typeof fetchImpl !== "function") {
    throw new Error("fetch implementation must be a function");
  }
  fetchImplementation = fetchImpl;
}

function resetFetchImplementation() {
  fetchImplementation = (...args) => fetch(...args);
}

function getConfig() {
  const host = process.env.NOVABRIDGE_HOST || "127.0.0.1";
  const port = Number.parseInt(process.env.NOVABRIDGE_PORT || "30010", 10);
  const provider = (process.env.NOVABRIDGE_ASSISTANT_PROVIDER || "mock").toLowerCase();

  return {
    host,
    port: Number.isFinite(port) && port > 0 ? port : 30010,
    provider,
    apiKey: process.env.NOVABRIDGE_API_KEY || "",
    openaiApiKey: process.env.OPENAI_API_KEY || "",
    openaiModel: process.env.NOVABRIDGE_ASSISTANT_OPENAI_MODEL || "gpt-4o-mini",
    anthropicApiKey: process.env.ANTHROPIC_API_KEY || "",
    anthropicModel: process.env.NOVABRIDGE_ASSISTANT_ANTHROPIC_MODEL || "claude-3-5-sonnet-latest",
    ollamaHost: (process.env.OLLAMA_HOST || "http://127.0.0.1:11434").replace(/\/+$/, ""),
    ollamaModel: process.env.NOVABRIDGE_ASSISTANT_OLLAMA_MODEL || "llama3.1",
    customUrl: (process.env.NOVABRIDGE_ASSISTANT_CUSTOM_URL || "").trim(),
    customHeaderKey: (process.env.NOVABRIDGE_ASSISTANT_CUSTOM_HEADER_KEY || "").trim(),
    customHeaderValue: process.env.NOVABRIDGE_ASSISTANT_CUSTOM_HEADER_VALUE || ""
  };
}

function getNovaBaseUrl(config = getConfig()) {
  return `http://${config.host}:${config.port}/nova`;
}

function buildNovaHeaders(config = getConfig()) {
  const headers = {
    "Content-Type": "application/json"
  };
  if (config.apiKey) {
    headers["X-API-Key"] = config.apiKey;
  }
  return headers;
}

async function novaRequest(method, route, body, config = getConfig()) {
  const url = `${getNovaBaseUrl(config)}${route}`;
  const init = {
    method,
    headers: buildNovaHeaders(config)
  };
  if (body !== undefined && body !== null) {
    init.body = JSON.stringify(body);
  }

  const res = await fetchImplementation(url, init);
  const text = await res.text();
  let parsed = {};
  if (text) {
    try {
      parsed = JSON.parse(text);
    } catch (_error) {
      parsed = { raw: text };
    }
  }
  if (!res.ok) {
    const detail = typeof parsed === "object" ? JSON.stringify(parsed) : text;
    throw new Error(`NovaBridge HTTP ${res.status}: ${detail}`);
  }
  return parsed;
}

function safeJsonParse(text) {
  try {
    return JSON.parse(text);
  } catch (_error) {
    return null;
  }
}

function extractFirstJsonObject(text) {
  const source = typeof text === "string" ? text.trim() : "";
  if (!source) {
    return null;
  }

  const direct = safeJsonParse(source);
  if (direct && typeof direct === "object") {
    return direct;
  }

  for (let start = 0; start < source.length; start += 1) {
    if (source[start] !== "{") {
      continue;
    }
    let depth = 0;
    let inString = false;
    let escaped = false;
    for (let i = start; i < source.length; i += 1) {
      const ch = source[i];
      if (inString) {
        if (escaped) {
          escaped = false;
          continue;
        }
        if (ch === "\\") {
          escaped = true;
          continue;
        }
        if (ch === "\"") {
          inString = false;
        }
        continue;
      }
      if (ch === "\"") {
        inString = true;
        continue;
      }
      if (ch === "{") {
        depth += 1;
        continue;
      }
      if (ch === "}") {
        depth -= 1;
        if (depth === 0) {
          const candidate = source.slice(start, i + 1);
          const parsed = safeJsonParse(candidate);
          if (parsed && typeof parsed === "object") {
            return parsed;
          }
        }
      }
    }
  }
  return null;
}

function makeMockPlan(prompt, mode, sceneActors) {
  const text = (prompt || "").toLowerCase();
  const steps = [];
  const existingActor = Array.isArray(sceneActors) && sceneActors.length > 0 ? sceneActors[0] : null;

  if (text.includes("delete") && existingActor && typeof existingActor.label === "string") {
    steps.push({
      action: "delete",
      params: { name: existingActor.label }
    });
  }

  if (text.includes("light")) {
    steps.push({
      action: "spawn",
      params: {
        type: "PointLight",
        label: "AssistantPointLight",
        transform: {
          location: [0, 0, 240]
        }
      }
    });
  }

  if (text.includes("screenshot") || text.includes("capture")) {
    steps.push({
      action: "screenshot",
      params: {
        width: 1280,
        height: 720,
        format: "raw"
      }
    });
  }

  if (steps.length === 0) {
    return {
      plan_id: `assistant-${Date.now()}`,
      mode,
      steps: DEFAULT_PLAN.steps
    };
  }

  return {
    plan_id: `assistant-${Date.now()}`,
    mode,
    steps
  };
}

function normalizePlanShape(candidate, mode, fallbackPrompt, sceneActors) {
  if (!candidate || typeof candidate !== "object") {
    return makeMockPlan(fallbackPrompt, mode, sceneActors);
  }

  const plan = candidate;
  const steps = Array.isArray(plan.steps) ? plan.steps : [];
  const normalized = {
    plan_id: typeof plan.plan_id === "string" && plan.plan_id.trim() ? plan.plan_id : `assistant-${Date.now()}`,
    mode: plan.mode === "runtime" ? "runtime" : mode,
    steps: []
  };

  for (const step of steps) {
    if (!step || typeof step !== "object" || typeof step.action !== "string" || typeof step.params !== "object") {
      continue;
    }
    normalized.steps.push(step);
  }

  if (normalized.steps.length === 0) {
    return makeMockPlan(fallbackPrompt, mode, sceneActors);
  }
  return normalized;
}

function buildPlannerSystemPrompt() {
  return [
    "You are NovaBridge Assistant planner.",
    "Return JSON only. No markdown. No prose.",
    "Schema:",
    "{",
    '  "plan_id": "string",',
    '  "mode": "editor" | "runtime",',
    '  "steps": [',
    '    { "action": "spawn", "params": { "type": "string", "label": "string(optional)", "transform": { "location": [number,number,number] } } },',
    '    { "action": "delete", "params": { "name": "string" } },',
    '    { "action": "set", "params": { "target": "string", "props": { "key": "value" } } },',
    '    { "action": "screenshot", "params": { "width": number(optional), "height": number(optional), "format": "png" | "raw"(optional) } }',
    "  ]",
    "}",
    "Only emit actions that appear in allowed capabilities/policy."
  ].join("\n");
}

function buildPlannerUserPrompt(input) {
  return [
    `mode=${input.mode}`,
    `prompt=${input.prompt}`,
    "capabilities_and_policy_json=",
    JSON.stringify(input.caps, null, 2),
    "scene_context_json=",
    JSON.stringify(input.sceneContext, null, 2),
    "risk_catalog_json=",
    JSON.stringify(input.catalog, null, 2)
  ].join("\n");
}

async function callOpenAiPlan(systemPrompt, userPrompt, config) {
  if (!config.openaiApiKey) {
    throw new Error("Missing OPENAI_API_KEY");
  }
  const res = await fetchImplementation("https://api.openai.com/v1/chat/completions", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${config.openaiApiKey}`
    },
    body: JSON.stringify({
      model: config.openaiModel,
      temperature: 0.2,
      messages: [
        { role: "system", content: systemPrompt },
        { role: "user", content: userPrompt }
      ]
    })
  });
  if (!res.ok) {
    throw new Error(`OpenAI error ${res.status}: ${await res.text()}`);
  }
  const payload = await res.json();
  const content = payload?.choices?.[0]?.message?.content || "";
  return extractFirstJsonObject(content);
}

async function callAnthropicPlan(systemPrompt, userPrompt, config) {
  if (!config.anthropicApiKey) {
    throw new Error("Missing ANTHROPIC_API_KEY");
  }
  const res = await fetchImplementation("https://api.anthropic.com/v1/messages", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "x-api-key": config.anthropicApiKey,
      "anthropic-version": "2023-06-01"
    },
    body: JSON.stringify({
      model: config.anthropicModel,
      max_tokens: 1000,
      system: systemPrompt,
      messages: [{ role: "user", content: userPrompt }]
    })
  });
  if (!res.ok) {
    throw new Error(`Anthropic error ${res.status}: ${await res.text()}`);
  }
  const payload = await res.json();
  const content = payload?.content?.[0]?.text || "";
  return extractFirstJsonObject(content);
}

async function callOllamaPlan(systemPrompt, userPrompt, config) {
  const res = await fetchImplementation(`${config.ollamaHost}/api/chat`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      model: config.ollamaModel,
      stream: false,
      messages: [
        { role: "system", content: systemPrompt },
        { role: "user", content: userPrompt }
      ]
    })
  });
  if (!res.ok) {
    throw new Error(`Ollama error ${res.status}: ${await res.text()}`);
  }
  const payload = await res.json();
  return extractFirstJsonObject(payload?.message?.content || "");
}

async function callCustomPlan(systemPrompt, userPrompt, config) {
  if (!config.customUrl) {
    throw new Error("Missing NOVABRIDGE_ASSISTANT_CUSTOM_URL");
  }
  const headers = {
    "Content-Type": "application/json"
  };
  if (config.customHeaderKey) {
    headers[config.customHeaderKey] = config.customHeaderValue;
  }
  const res = await fetchImplementation(config.customUrl, {
    method: "POST",
    headers,
    body: JSON.stringify({
      system: systemPrompt,
      user: userPrompt,
      output: "json"
    })
  });
  if (!res.ok) {
    throw new Error(`Custom provider error ${res.status}: ${await res.text()}`);
  }
  return extractFirstJsonObject(await res.text());
}

async function fetchCapabilities(config) {
  try {
    const caps = await novaRequest("GET", "/caps", null, config);
    return caps && typeof caps === "object" ? caps : {};
  } catch (_error) {
    return {};
  }
}

async function fetchSceneContext(config) {
  try {
    const scene = await novaRequest("GET", "/scene/list", null, config);
    const actors = Array.isArray(scene.actors) ? scene.actors : [];
    return {
      actor_count: typeof scene.count === "number" ? scene.count : actors.length,
      level: typeof scene.level === "string" ? scene.level : "",
      sample_actors: actors.slice(0, 30)
    };
  } catch (_error) {
    return {
      actor_count: 0,
      level: "",
      sample_actors: []
    };
  }
}

async function generatePlan(input) {
  const config = getConfig();
  const mode = input && input.mode === "runtime" ? "runtime" : "editor";
  const prompt = input && typeof input.prompt === "string" ? input.prompt : "";

  const caps = await fetchCapabilities(config);
  const sceneContext = await fetchSceneContext(config);
  const capabilities = Array.isArray(caps.capabilities) ? caps.capabilities : [];
  const catalog = buildCommandCatalog(capabilities);

  const systemPrompt = buildPlannerSystemPrompt();
  const userPrompt = buildPlannerUserPrompt({
    mode,
    prompt,
    caps,
    sceneContext,
    catalog
  });

  let providerResult = null;
  let source = "mock";
  try {
    if (config.provider === "openai") {
      providerResult = await callOpenAiPlan(systemPrompt, userPrompt, config);
      source = "openai";
    } else if (config.provider === "anthropic") {
      providerResult = await callAnthropicPlan(systemPrompt, userPrompt, config);
      source = "anthropic";
    } else if (config.provider === "ollama") {
      providerResult = await callOllamaPlan(systemPrompt, userPrompt, config);
      source = "ollama";
    } else if (config.provider === "custom") {
      providerResult = await callCustomPlan(systemPrompt, userPrompt, config);
      source = "custom";
    }
  } catch (_error) {
    providerResult = null;
    source = "mock";
  }

  const normalizedPlan = normalizePlanShape(providerResult, mode, prompt, sceneContext.sample_actors);
  const risk = summarizePlanRisk(normalizedPlan.steps);
  return {
    status: "ok",
    source,
    mode,
    plan: normalizedPlan,
    risk,
    catalog,
    scene_context: sceneContext
  };
}

async function executePlan(plan) {
  const config = getConfig();
  const body = plan && typeof plan === "object" ? plan : DEFAULT_PLAN;
  const result = await novaRequest("POST", "/executePlan", body, config);
  return {
    status: "ok",
    result
  };
}

async function getCatalogSnapshot() {
  const config = getConfig();
  const caps = await fetchCapabilities(config);
  const capabilities = Array.isArray(caps.capabilities) ? caps.capabilities : [];
  return {
    status: "ok",
    mode: caps.mode || "unknown",
    catalog: buildCommandCatalog(capabilities)
  };
}

async function getHealthSnapshot() {
  const config = getConfig();
  try {
    const health = await novaRequest("GET", "/health", null, config);
    return {
      status: "ok",
      assistant_provider: config.provider,
      nova: health
    };
  } catch (error) {
    return {
      status: "degraded",
      assistant_provider: config.provider,
      error: error instanceof Error ? error.message : String(error)
    };
  }
}

module.exports = {
  generatePlan,
  executePlan,
  getCatalogSnapshot,
  getHealthSnapshot,
  __internal: {
    getConfig,
    getNovaBaseUrl,
    buildNovaHeaders,
    safeJsonParse,
    extractFirstJsonObject,
    makeMockPlan,
    normalizePlanShape,
    buildPlannerSystemPrompt,
    buildPlannerUserPrompt,
    setFetchImplementation,
    resetFetchImplementation
  }
};
