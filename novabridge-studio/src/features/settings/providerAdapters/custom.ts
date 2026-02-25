import type { Plan } from "../../../lib/types";

export type ProviderInput = {
  prompt: string;
  capsText: string;
  mode: "editor" | "runtime";
};

export interface ProviderAdapter {
  id: string;
  name: string;
  generatePlan(input: ProviderInput): Promise<Plan>;
}

export function extractJsonPlan(text: string): Plan {
  const start = text.indexOf("{");
  const end = text.lastIndexOf("}");
  const slice = start >= 0 && end > start ? text.slice(start, end + 1) : text;
  return JSON.parse(slice) as Plan;
}

export function makeCustomAdapter(endpoint: string, headerKey: string, headerValue: string): ProviderAdapter {
  return {
    id: "custom",
    name: "Custom",
    async generatePlan(input: ProviderInput): Promise<Plan> {
      if (!endpoint) throw new Error("Missing custom endpoint");

      const headers: Record<string, string> = { "Content-Type": "application/json" };
      if (headerKey.trim()) {
        headers[headerKey.trim()] = headerValue;
      }

      const res = await fetch(endpoint, {
        method: "POST",
        headers,
        body: JSON.stringify({
          prompt: input.prompt,
          mode: input.mode,
          capabilities: input.capsText,
          output: "json"
        })
      });

      if (!res.ok) {
        throw new Error(`Custom provider error: ${res.status} ${await res.text()}`);
      }

      return extractJsonPlan(await res.text());
    }
  };
}
