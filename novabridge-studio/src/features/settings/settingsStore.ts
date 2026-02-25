import { loadJson, saveJson } from "../../lib/storage";

const KEY = "novabridge-studio-settings";

export type ProviderId = "openai" | "anthropic" | "ollama" | "custom";

export type SettingsState = {
  provider: ProviderId;
  openaiKey: string;
  openaiModel: string;
  anthropicKey: string;
  anthropicModel: string;
  ollamaHost: string;
  ollamaModel: string;
  customEndpoint: string;
  customHeaderKey: string;
  customHeaderValue: string;
  useMockProvider: boolean;
};

const defaults: SettingsState = {
  provider: "openai",
  openaiKey: "",
  openaiModel: "gpt-4o-mini",
  anthropicKey: "",
  anthropicModel: "claude-3-5-sonnet-latest",
  ollamaHost: "http://127.0.0.1:11434",
  ollamaModel: "llama3.1",
  customEndpoint: "",
  customHeaderKey: "",
  customHeaderValue: "",
  useMockProvider: true
};

export function loadSettingsState(): SettingsState {
  return loadJson(KEY, defaults);
}

export function saveSettingsState(state: SettingsState): void {
  saveJson(KEY, state);
}
