import { loadJson, saveJson } from "../../lib/storage";
import type { Capability, Mode } from "../../lib/types";

const KEY = "novabridge-studio-connect";

export type ConnectState = {
  baseUrl: string;
  connected: boolean;
  mode: Mode;
  version?: string;
  caps?: Capability[];
};

const defaults: ConnectState = {
  baseUrl: "http://127.0.0.1:30010",
  connected: false,
  mode: "unknown"
};

export function loadConnectState(): ConnectState {
  return loadJson(KEY, defaults);
}

export function saveConnectState(state: ConnectState): void {
  saveJson(KEY, state);
}
