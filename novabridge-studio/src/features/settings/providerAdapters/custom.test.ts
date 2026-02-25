import { describe, expect, it } from "vitest";
import { buildPlannerSystemPrompt, buildPlannerUserPrompt, extractJsonPlan } from "./custom";

describe("custom provider adapter helpers", () => {
  it("extracts plan JSON from fenced output", () => {
    const content = [
      "```json",
      '{"plan_id":"mock","mode":"editor","steps":[{"action":"spawn","params":{"type":"PointLight","label":"L1"}}]}',
      "```"
    ].join("\n");

    const parsed = extractJsonPlan(content);
    expect(parsed.plan_id).toBe("mock");
    expect(parsed.steps[0]?.action).toBe("spawn");
  });

  it("extracts first JSON object from mixed text", () => {
    const content = [
      "Here is your plan:",
      '{"plan_id":"p2","mode":"runtime","steps":[{"action":"delete","params":{"name":"OldLight"}}]}',
      "Execute now."
    ].join("\n");

    const parsed = extractJsonPlan(content);
    expect(parsed.plan_id).toBe("p2");
    expect(parsed.mode).toBe("runtime");
  });

  it("throws when no JSON exists", () => {
    expect(() => extractJsonPlan("no json here")).toThrow("Provider did not return a JSON object.");
  });

  it("builds non-empty planner prompts", () => {
    const system = buildPlannerSystemPrompt();
    const user = buildPlannerUserPrompt({
      mode: "editor",
      capsText: '{"capabilities":[]}',
      prompt: "spawn a light"
    });

    expect(system.length).toBeGreaterThan(20);
    expect(user).toContain("spawn a light");
    expect(user).toContain("capabilities_and_permissions_json");
  });
});

