import { loadJson, saveJson } from "../../lib/storage";
import type { ActivityLog, ExecutePlanResponse, Plan } from "../../lib/types";

const KEY = "novabridge-studio-executor";

export type ExecutorState = {
  activity: ActivityLog[];
  lastPlan: Plan | null;
  lastRun: ExecutePlanResponse | null;
};

const defaults: ExecutorState = {
  activity: [],
  lastPlan: null,
  lastRun: null
};

export function loadExecutorState(): ExecutorState {
  const loaded = loadJson<Partial<ExecutorState>>(KEY, {});
  return { ...defaults, ...loaded };
}

export function saveExecutorState(state: ExecutorState): void {
  saveJson(KEY, state);
}
