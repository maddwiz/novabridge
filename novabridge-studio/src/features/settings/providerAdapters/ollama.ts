import type { Plan } from "../../../lib/types";
import { extractJsonPlan, type ProviderAdapter, type ProviderInput } from "./custom";

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
              content:
                "You are a planner. Output valid JSON only. No prose. Schema: {plan_id:string,mode:'editor'|'runtime',steps:[{action:'spawn'|'delete'|'set'|'screenshot',params:object}]}"
            },
            {
              role: "user",
              content: `mode=${input.mode}\ncapabilities=${input.capsText}\nprompt=${input.prompt}`
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
