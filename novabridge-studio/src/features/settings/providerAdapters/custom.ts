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

export function buildPlannerSystemPrompt(): string {
  return [
    "You are NovaBridge Studio's planning engine.",
    "Return one JSON object only. No markdown. No prose.",
    "Schema:",
    "{",
    '  "plan_id": "string",',
    '  "mode": "editor" | "runtime",',
    '  "steps": [',
    "    {",
    '      "action": "spawn",',
    '      "params": {',
    '        "type": "string",',
    '        "label": "string(optional)",',
    '        "transform": {',
    '          "location": [number, number, number](optional),',
    '          "rotation": [number, number, number](optional),',
    '          "scale": [number, number, number](optional)',
    "        },",
    '        "props": { "key": "value" }(optional)',
    "      }",
    "    },",
    '    { "action": "delete", "params": { "name": "string" } },',
    '    { "action": "set", "params": { "target": "string", "props": { "key": "value" } } },',
    '    { "action": "screenshot", "params": { "width": number(optional), "height": number(optional), "format": "png" | "raw"(optional) } }',
    "  ]",
    "}",
    "Only use actions valid for the current capabilities/policy input.",
    "If a requested action is disallowed, choose the closest allowed action and continue.",
    "Keep plans short and executable.",
  ].join("\n");
}

export function buildPlannerUserPrompt(input: ProviderInput): string {
  return [
    `mode=${input.mode}`,
    "capabilities_and_permissions_json=",
    input.capsText,
    "user_prompt=",
    input.prompt
  ].join("\n");
}

function findFirstJsonObject(text: string): string | null {
  const source = text.trim();
  if (!source) return null;

  for (let start = 0; start < source.length; start += 1) {
    if (source[start] !== "{") continue;

    let depth = 0;
    let inString = false;
    let escaped = false;

    for (let index = start; index < source.length; index += 1) {
      const char = source[index];

      if (inString) {
        if (escaped) {
          escaped = false;
          continue;
        }
        if (char === "\\") {
          escaped = true;
          continue;
        }
        if (char === "\"") {
          inString = false;
        }
        continue;
      }

      if (char === "\"") {
        inString = true;
        continue;
      }
      if (char === "{") {
        depth += 1;
        continue;
      }
      if (char === "}") {
        depth -= 1;
        if (depth === 0) {
          return source.slice(start, index + 1);
        }
      }
    }
  }
  return null;
}

export function extractJsonPlan(text: string): Plan {
  const withoutFence = text
    .trim()
    .replace(/^```(?:json)?\s*/i, "")
    .replace(/\s*```$/, "");

  try {
    return JSON.parse(withoutFence) as Plan;
  } catch {
    const candidate = findFirstJsonObject(withoutFence);
    if (!candidate) {
      throw new Error("Provider did not return a JSON object.");
    }
    return JSON.parse(candidate) as Plan;
  }
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
          system: buildPlannerSystemPrompt(),
          user: buildPlannerUserPrompt(input),
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
