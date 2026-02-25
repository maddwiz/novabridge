import type { Plan, PlanStep } from "../../lib/types";
import { HttpError, httpJson } from "../../lib/http";

async function fallbackStep(baseUrl: string, step: PlanStep): Promise<unknown> {
  if (step.action === "spawn") {
    return (await httpJson(`${baseUrl}/nova/scene/spawn`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ class: step.params.type, label: step.params.label })
    })).data;
  }
  if (step.action === "delete") {
    return (await httpJson(`${baseUrl}/nova/scene/delete`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: step.params.name })
    })).data;
  }
  if (step.action === "screenshot") {
    const width = step.params.width ?? 1280;
    const height = step.params.height ?? 720;
    return (await httpJson(`${baseUrl}/nova/viewport/screenshot?format=raw&width=${width}&height=${height}`)).data;
  }
  throw new Error(`No fallback for action ${step.action}`);
}

export async function executePlan(baseUrl: string, plan: Plan): Promise<unknown> {
  try {
    return (await httpJson(`${baseUrl}/nova/executePlan`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(plan)
    })).data;
  } catch (error) {
    if (!(error instanceof HttpError) || error.status !== 404) {
      throw error;
    }

    const perStep = [] as unknown[];
    for (const step of plan.steps) {
      perStep.push(await fallbackStep(baseUrl, step));
    }
    return { status: "ok", fallback: true, results: perStep };
  }
}
