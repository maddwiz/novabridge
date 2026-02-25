"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");

const {
  generatePlan,
  executePlan,
  __internal
} = require("../assistant_engine");

const ENV_KEYS = [
  "NOVABRIDGE_HOST",
  "NOVABRIDGE_PORT",
  "NOVABRIDGE_API_KEY",
  "NOVABRIDGE_ASSISTANT_PROVIDER"
];

function saveEnv() {
  const snapshot = {};
  for (const key of ENV_KEYS) {
    snapshot[key] = process.env[key];
  }
  return snapshot;
}

function restoreEnv(snapshot) {
  for (const key of ENV_KEYS) {
    if (snapshot[key] === undefined) {
      delete process.env[key];
    } else {
      process.env[key] = snapshot[key];
    }
  }
}

test("extractFirstJsonObject can recover plan JSON from mixed text", () => {
  const parsed = __internal.extractFirstJsonObject(
    "Planner output: {\"plan_id\":\"p1\",\"mode\":\"editor\",\"steps\":[]} done"
  );

  assert.equal(parsed.plan_id, "p1");
  assert.equal(parsed.mode, "editor");
  assert.deepEqual(parsed.steps, []);
});

test("generatePlan falls back to mock plan when NovaBridge is unreachable", async () => {
  const env = saveEnv();
  process.env.NOVABRIDGE_ASSISTANT_PROVIDER = "mock";

  __internal.setFetchImplementation(async () => {
    throw new Error("offline");
  });

  try {
    const result = await generatePlan({
      prompt: "spawn a light and take a screenshot",
      mode: "editor"
    });

    assert.equal(result.status, "ok");
    assert.equal(result.source, "mock");
    assert.equal(result.plan.mode, "editor");
    assert.ok(Array.isArray(result.plan.steps));
    assert.ok(result.plan.steps.length >= 1);
  } finally {
    __internal.resetFetchImplementation();
    restoreEnv(env);
  }
});

test("executePlan posts to /nova/executePlan with API key header", async () => {
  const env = saveEnv();
  process.env.NOVABRIDGE_HOST = "127.0.0.1";
  process.env.NOVABRIDGE_PORT = "30125";
  process.env.NOVABRIDGE_API_KEY = "k_assistant";

  const calls = [];
  __internal.setFetchImplementation(async (url, init) => {
    calls.push({ url, init });
    return {
      ok: true,
      text: async () => JSON.stringify({ status: "ok", accepted: true })
    };
  });

  try {
    const plan = {
      plan_id: "assistant-plan-1",
      mode: "editor",
      steps: [{ action: "spawn", params: { type: "PointLight" } }]
    };

    const result = await executePlan(plan);
    assert.equal(result.status, "ok");
    assert.equal(calls.length, 1);

    const call = calls[0];
    assert.equal(call.url, "http://127.0.0.1:30125/nova/executePlan");
    assert.equal(call.init.method, "POST");
    assert.equal(call.init.headers["X-API-Key"], "k_assistant");

    const body = JSON.parse(call.init.body);
    assert.equal(body.plan_id, "assistant-plan-1");
    assert.equal(body.steps.length, 1);
  } finally {
    __internal.resetFetchImplementation();
    restoreEnv(env);
  }
});
