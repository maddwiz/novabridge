import type { Plan } from "../../../lib/types";
import {
  buildPlannerSystemPrompt,
  buildPlannerUserPrompt,
  extractJsonPlan,
  type ProviderAdapter,
  type ProviderInput
} from "./custom";

export function makeOllamaAdapter(host: string, model: string): ProviderAdapter {
  return {
    id: "ollama",
    name: "Ollama",
    async generatePlan(input: ProviderInput): Promise<Plan> {
      const res = await fetch(`${host}/api/chat`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          model,
          stream: false,
          messages: [
            {
              role: "system",
              content: buildPlannerSystemPrompt()
            },
            {
              role: "user",
              content: buildPlannerUserPrompt(input)
            }
          ]
        })
      });

      if (!res.ok) {
        throw new Error(`Ollama error: ${res.status} ${await res.text()}`);
      }
      const json = (await res.json()) as { message?: { content?: string } };
      return extractJsonPlan(json.message?.content ?? "");
    }
  };
}
