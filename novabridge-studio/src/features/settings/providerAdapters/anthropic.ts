import type { Plan } from "../../../lib/types";
import { extractJsonPlan, type ProviderAdapter, type ProviderInput } from "./custom";

export function makeAnthropicAdapter(apiKey: string, model: string): ProviderAdapter {
  return {
    id: "anthropic",
    name: "Anthropic",
    async generatePlan(input: ProviderInput): Promise<Plan> {
      if (!apiKey) throw new Error("Missing Anthropic API key");

      const res = await fetch("https://api.anthropic.com/v1/messages", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "x-api-key": apiKey,
          "anthropic-version": "2023-06-01"
        },
        body: JSON.stringify({
          model,
          max_tokens: 1000,
          system:
            "You are a planner. Output valid JSON only. No prose. Schema: {plan_id:string,mode:'editor'|'runtime',steps:[{action:'spawn'|'delete'|'set'|'screenshot',params:object}]}",
          messages: [
            {
              role: "user",
              content: `mode=${input.mode}\ncapabilities=${input.capsText}\nprompt=${input.prompt}`
            }
          ]
        })
      });

      if (!res.ok) {
        throw new Error(`Anthropic error: ${res.status} ${await res.text()}`);
      }
      const json = (await res.json()) as {
        content?: Array<{ text?: string }>;
      };
      const content = json.content?.[0]?.text ?? "";
      return extractJsonPlan(content);
    }
  };
}
