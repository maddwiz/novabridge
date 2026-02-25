import { describe, expect, it } from "vitest";
import { validatePlanAgainstPermissions } from "./planPolicy";
import type { PermissionSnapshot, Plan } from "../../lib/types";

describe("validatePlanAgainstPermissions", () => {
  it("flags actions outside allowed actions list", () => {
    const plan: Plan = {
      plan_id: "plan-1",
      mode: "editor",
      steps: [
        { action: "spawn", params: { type: "PointLight", label: "A" } },
        { action: "screenshot", params: { width: 1280, height: 720, format: "raw" } }
      ]
    };

    const permissions: PermissionSnapshot = {
      executePlan: {
        allowed: true,
        allowed_actions: ["spawn"],
        max_steps: 10
      }
    };

    const errors = validatePlanAgainstPermissions(plan, permissions);
    expect(errors.some((entry) => entry.includes("not allowed"))).toBe(true);
  });

  it("enforces class allow list and spawn limit", () => {
    const plan: Plan = {
      plan_id: "plan-2",
      mode: "editor",
      steps: [
        { action: "spawn", params: { type: "PointLight", label: "A" } },
        { action: "spawn", params: { type: "SpotLight", label: "B" } }
      ]
    };

    const permissions: PermissionSnapshot = {
      spawn: {
        allowed: true,
        classes_unrestricted: false,
        allowedClasses: ["PointLight"],
        max_spawn_per_plan: 1
      },
      executePlan: {
        allowed: true,
        allowed_actions: ["spawn"]
      }
    };

    const errors = validatePlanAgainstPermissions(plan, permissions);
    expect(errors.some((entry) => entry.includes("max_spawn_per_plan"))).toBe(true);
    expect(errors.some((entry) => entry.includes("outside allowedClasses"))).toBe(true);
  });

  it("allows valid plans under permissive policy", () => {
    const plan: Plan = {
      plan_id: "plan-3",
      mode: "editor",
      steps: [
        { action: "spawn", params: { type: "PointLight", label: "A" } },
        { action: "delete", params: { name: "A" } }
      ]
    };

    const permissions: PermissionSnapshot = {
      executePlan: {
        allowed: true,
        allowed_actions: ["spawn", "delete"],
        max_steps: 5
      },
      spawn: {
        allowed: true,
        classes_unrestricted: true,
        max_spawn_per_plan: 5
      }
    };

    const errors = validatePlanAgainstPermissions(plan, permissions);
    expect(errors).toEqual([]);
  });
});

