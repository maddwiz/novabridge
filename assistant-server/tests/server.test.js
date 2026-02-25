"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");

const { createServer } = require("../server");

async function withServer(deps, run) {
  const server = createServer(deps);
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const address = server.address();
  const baseUrl = `http://127.0.0.1:${address.port}`;

  try {
    await run(baseUrl);
  } finally {
    await new Promise((resolve) => server.close(resolve));
  }
}

async function postJson(baseUrl, path, payload) {
  return fetch(`${baseUrl}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
}

test("/assistant/plan rejects empty prompt", async () => {
  const deps = {
    async generatePlan() {
      return { status: "ok" };
    },
    async executePlan() {
      return { status: "ok" };
    },
    async getCatalogSnapshot() {
      return { status: "ok" };
    },
    async getHealthSnapshot() {
      return { status: "ok" };
    }
  };

  await withServer(deps, async (baseUrl) => {
    const res = await postJson(baseUrl, "/assistant/plan", { prompt: "   " });
    assert.equal(res.status, 400);
    const payload = await res.json();
    assert.equal(payload.status, "error");
  });
});

test("/assistant/execute blocks high-risk plans without allow_high_risk", async () => {
  let executeCalls = 0;
  const deps = {
    async generatePlan() {
      return { status: "ok" };
    },
    async executePlan() {
      executeCalls += 1;
      return { status: "ok" };
    },
    async getCatalogSnapshot() {
      return { status: "ok" };
    },
    async getHealthSnapshot() {
      return { status: "ok" };
    }
  };

  await withServer(deps, async (baseUrl) => {
    const res = await postJson(baseUrl, "/assistant/execute", {
      plan: { plan_id: "p1", mode: "editor", steps: [{ action: "delete", params: { name: "A" } }] },
      risk: { highest_risk: "high" }
    });

    assert.equal(res.status, 409);
    const payload = await res.json();
    assert.equal(payload.status, "blocked");
    assert.equal(executeCalls, 0);
  });
});

test("/assistant/execute allows high-risk plans with explicit approval", async () => {
  let executeCalls = 0;
  const deps = {
    async generatePlan() {
      return { status: "ok" };
    },
    async executePlan(plan) {
      executeCalls += 1;
      return { status: "ok", echoed_plan_id: plan.plan_id };
    },
    async getCatalogSnapshot() {
      return { status: "ok" };
    },
    async getHealthSnapshot() {
      return { status: "ok" };
    }
  };

  await withServer(deps, async (baseUrl) => {
    const res = await postJson(baseUrl, "/assistant/execute", {
      plan: { plan_id: "p2", mode: "editor", steps: [{ action: "delete", params: { name: "A" } }] },
      risk: { highest_risk: "high" },
      allow_high_risk: true
    });

    assert.equal(res.status, 200);
    const payload = await res.json();
    assert.equal(payload.status, "ok");
    assert.equal(payload.echoed_plan_id, "p2");
    assert.equal(executeCalls, 1);
  });
});

test("/assistant/health proxies dependency snapshot", async () => {
  const deps = {
    async generatePlan() {
      return { status: "ok" };
    },
    async executePlan() {
      return { status: "ok" };
    },
    async getCatalogSnapshot() {
      return { status: "ok" };
    },
    async getHealthSnapshot() {
      return { status: "ok", assistant_provider: "mock" };
    }
  };

  await withServer(deps, async (baseUrl) => {
    const res = await fetch(`${baseUrl}/assistant/health`);
    assert.equal(res.status, 200);
    const payload = await res.json();
    assert.equal(payload.status, "ok");
    assert.equal(payload.assistant_provider, "mock");
  });
});
