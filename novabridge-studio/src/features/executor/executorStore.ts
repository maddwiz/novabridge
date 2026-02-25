import { loadJson, saveJson } from "../../lib/storage";
import type { ActivityLog, Plan } from "../../lib/types";

const KEY = "novabridge-studio-executor";

export type ExecutorState = {
  activity: ActivityLog[];
  lastPlan: Plan | null;
  lastRun: unknown;
};

const defaults: ExecutorState = {
  activity: [],
  lastPlan: null,
  lastRun: null
};

export function loadExecutorState(): ExecutorState {
  return loadJson(KEY, defaults);
}

export function saveExecutorState(state: ExecutorState): void {
  saveJson(KEY, state);
}
