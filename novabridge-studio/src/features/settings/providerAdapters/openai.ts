import type { Plan } from "../../../lib/types";
import { extractJsonPlan, type ProviderAdapter, type ProviderInput } from "./custom";

export function makeOpenAIAdapter(apiKey: string, model: string): ProviderAdapter {
  return {
    id: "openai",
    name: "OpenAI",
    async generatePlan(input: ProviderInput): Promise<Plan> {
      if (!apiKey) throw new Error("Missing OpenAI API key");

      const res = await fetch("https://api.openai.com/v1/chat/completions", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${apiKey}`
        },
        body: JSON.stringify({
          model,
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
          ],
          temperature: 0.2
        })
      });

      if (!res.ok) {
        throw new Error(`OpenAI error: ${res.status} ${await res.text()}`);
      }
      const json = (await res.json()) as {
        choices?: Array<{ message?: { content?: string } }>;
      };
      const content = json.choices?.[0]?.message?.content ?? "";
      return extractJsonPlan(content);
    }
  };
}
