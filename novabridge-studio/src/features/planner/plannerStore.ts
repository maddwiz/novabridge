import { loadJson, saveJson } from "../../lib/storage";
import type { Plan } from "../../lib/types";

const KEY = "novabridge-studio-planner";

export type PlannerState = {
  prompt: string;
  plan: Plan | null;
  showJson: boolean;
};

const defaults: PlannerState = {
  prompt: "",
  plan: null,
  showJson: false
};

export function loadPlannerState(): PlannerState {
  return loadJson(KEY, defaults);
}

export function savePlannerState(state: PlannerState): void {
  saveJson(KEY, state);
}
