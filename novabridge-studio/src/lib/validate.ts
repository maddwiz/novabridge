import type { Plan } from "./types";

export function isPlan(value: unknown): value is Plan {
  if (!value || typeof value !== "object") return false;
  const plan = value as Partial<Plan>;
  return typeof plan.plan_id === "string" && (plan.mode === "editor" || plan.mode === "runtime") && Array.isArray(plan.steps);
}
