import type { Plan } from "../../lib/types";
import { isPlan } from "../../lib/validate";

export function parsePlan(value: unknown): Plan {
  if (!isPlan(value)) {
    throw new Error("Model response did not match Plan schema (plan_id/mode/steps with valid action params are required).");
  }
  return value;
}
