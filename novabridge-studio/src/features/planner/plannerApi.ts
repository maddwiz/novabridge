import type { Plan } from "../../lib/types";
import type { SettingsState } from "../settings/settingsStore";
import { makeOpenAIAdapter } from "../settings/providerAdapters/openai";
import { makeAnthropicAdapter } from "../settings/providerAdapters/anthropic";
import { makeOllamaAdapter } from "../settings/providerAdapters/ollama";
import { makeCustomAdapter } from "../settings/providerAdapters/custom";
import { parsePlan } from "./planSchema";

export async function generatePlan(input: {
  prompt: string;
  mode: "editor" | "runtime";
  capsText: string;
  settings: SettingsState;
}): Promise<Plan> {
  if (import.meta.env.DEV && input.settings.useMockProvider) {
    return parsePlan({
      plan_id: "mock",
      mode: input.mode,
      steps: [{ action: "spawn", params: { type: "PointLight", label: "LaunchSmokeLight" } }]
    });
  }

  let plan: Plan;
  if (input.settings.provider === "openai") {
    plan = await makeOpenAIAdapter(input.settings.openaiKey, input.settings.openaiModel).generatePlan(input);
  } else if (input.settings.provider === "anthropic") {
    plan = await makeAnthropicAdapter(input.settings.anthropicKey, input.settings.anthropicModel).generatePlan(input);
  } else if (input.settings.provider === "ollama") {
    plan = await makeOllamaAdapter(input.settings.ollamaHost, input.settings.ollamaModel).generatePlan(input);
  } else {
    plan = await makeCustomAdapter(input.settings.customEndpoint, input.settings.customHeaderKey, input.settings.customHeaderValue).generatePlan(input);
  }

  return parsePlan(plan);
}
