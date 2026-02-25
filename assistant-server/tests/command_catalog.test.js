"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");

const {
  classifyAction,
  buildCommandCatalog,
  summarizePlanRisk
} = require("../command_catalog");

test("classifyAction marks destructive actions as high risk", () => {
  const result = classifyAction("delete");
  assert.equal(result.action, "delete");
  assert.equal(result.risk, "high");
  assert.equal(result.requires_confirmation, true);
});

test("classifyAction defaults unknown actions to medium risk", () => {
  const result = classifyAction("custom.action");
  assert.equal(result.action, "custom.action");
  assert.equal(result.risk, "medium");
  assert.equal(result.requires_confirmation, false);
});

test("buildCommandCatalog merges capability actions and built-in metadata", () => {
  const catalog = buildCommandCatalog([
    { action: "spawn" },
    { action: "Scene.Delete" },
    { action: "custom.op" }
  ]);

  const actions = catalog.map((entry) => entry.action);
  assert.ok(actions.includes("spawn"));
  assert.ok(actions.includes("scene.delete"));
  assert.ok(actions.includes("custom.op"));

  const sorted = [...actions].sort((a, b) => a.localeCompare(b));
  assert.deepEqual(actions, sorted);
});

test("summarizePlanRisk elevates highest risk and confirmation flag", () => {
  const summary = summarizePlanRisk([
    { action: "spawn", params: {} },
    { action: "delete", params: { name: "Actor1" } }
  ]);

  assert.equal(summary.highest_risk, "high");
  assert.equal(summary.requires_confirmation, true);
  assert.equal(summary.steps.length, 2);
  assert.equal(summary.steps[0].risk, "medium");
  assert.equal(summary.steps[1].risk, "high");
});
