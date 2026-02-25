import type { ExecutePlanResponse, ExecutePlanStepResult, Plan, PlanStep } from "../../lib/types";
import { HttpError, httpJson } from "../../lib/http";

type FallbackStepResult = {
  action: string;
  status: "success" | "error";
  message: string;
  object_id?: string;
};

function makeNovaJsonHeaders(apiKey?: string): HeadersInit {
  const headers: Record<string, string> = { "Content-Type": "application/json" };
  if (apiKey?.trim()) {
    headers["X-API-Key"] = apiKey.trim();
  }
  return headers;
}

function makeNovaHeaders(apiKey?: string): HeadersInit | undefined {
  if (!apiKey?.trim()) return undefined;
  return { "X-API-Key": apiKey.trim() };
}

function toValueString(value: unknown): string {
  if (typeof value === "string") return value;
  if (typeof value === "number" || typeof value === "boolean") return String(value);
  return JSON.stringify(value);
}

function extractActorNameFromSpawnResponse(data: unknown, fallbackLabel?: string): string {
  if (typeof data === "object" && data !== null && "name" in data && typeof (data as { name?: unknown }).name === "string") {
    return (data as { name: string }).name;
  }
  if (fallbackLabel?.trim()) return fallbackLabel.trim();
  return "";
}

async function setActorProperties(
  baseUrl: string,
  actorName: string,
  props: Record<string, unknown>,
  apiKey?: string,
  propertyPrefix = ""
): Promise<void> {
  const entries = Object.entries(props);
  for (const [key, rawValue] of entries) {
    const property = propertyPrefix ? `${propertyPrefix}.${key}` : key;
    await httpJson(`${baseUrl}/nova/scene/set-property`, {
      method: "POST",
      headers: makeNovaJsonHeaders(apiKey),
      body: JSON.stringify({
        name: actorName,
        property,
        value: toValueString(rawValue)
      })
    });
  }
}

async function fallbackStep(baseUrl: string, step: PlanStep, apiKey?: string): Promise<FallbackStepResult> {
  if (step.action === "spawn") {
    const location = step.params.transform?.location;
    const rotation = step.params.transform?.rotation;

    const spawnPayload: Record<string, unknown> = {
      class: step.params.type,
      label: step.params.label
    };
    if (location) {
      spawnPayload.x = location[0];
      spawnPayload.y = location[1];
      spawnPayload.z = location[2];
    }
    if (rotation) {
      spawnPayload.pitch = rotation[0];
      spawnPayload.yaw = rotation[1];
      spawnPayload.roll = rotation[2];
    }

    const spawnData = (await httpJson<unknown>(`${baseUrl}/nova/scene/spawn`, {
      method: "POST",
      headers: makeNovaJsonHeaders(apiKey),
      body: JSON.stringify(spawnPayload)
    })).data;

    const actorName = extractActorNameFromSpawnResponse(spawnData, step.params.label);
    if (actorName && step.params.props && Object.keys(step.params.props).length > 0) {
      await setActorProperties(baseUrl, actorName, step.params.props, apiKey);
    }

    return {
      action: "spawn",
      status: "success",
      message: actorName ? `Spawned ${actorName}` : "Spawned actor",
      object_id: actorName || undefined
    };
  }

  if (step.action === "delete") {
    await httpJson(`${baseUrl}/nova/scene/delete`, {
      method: "POST",
      headers: makeNovaJsonHeaders(apiKey),
      body: JSON.stringify({ name: step.params.name })
    });
    return {
      action: "delete",
      status: "success",
      message: `Deleted ${step.params.name}`
    };
  }

  if (step.action === "set") {
    const target = step.params.target.trim();
    if (!target) {
      return { action: "set", status: "error", message: "Empty set target" };
    }

    const firstDot = target.indexOf(".");
    const actorName = firstDot >= 0 ? target.slice(0, firstDot) : target;
    const propertyPrefix = firstDot >= 0 ? target.slice(firstDot + 1) : "";
    await setActorProperties(baseUrl, actorName, step.params.props, apiKey, propertyPrefix);
    return {
      action: "set",
      status: "success",
      message: `Updated ${actorName}`
    };
  }

  if (step.action === "screenshot") {
    const width = step.params.width ?? 1280;
    const height = step.params.height ?? 720;
    const format = step.params.format ?? "raw";
    const res = await fetch(`${baseUrl}/nova/viewport/screenshot?format=${format}&width=${width}&height=${height}`, {
      method: "GET",
      headers: makeNovaHeaders(apiKey)
    });

    if (!res.ok) {
      const bodyText = await res.text();
      throw new HttpError(res.status, bodyText);
    }

    const bytes = (await res.arrayBuffer()).byteLength;
    return {
      action: "screenshot",
      status: "success",
      message: `Captured ${format} screenshot (${width}x${height}, ${bytes} bytes)`
    };
  }

  const unsupportedAction = (step as { action: string }).action;
  return {
    action: unsupportedAction,
    status: "error",
    message: `No fallback for action ${unsupportedAction}`
  };
}

function normalizeExecutePlanResponse(data: unknown, fallback = false): ExecutePlanResponse {
  const safe = (typeof data === "object" && data !== null ? data : {}) as Record<string, unknown>;
  const rawResults = Array.isArray(safe.results) ? safe.results : [];

  const results: ExecutePlanStepResult[] = rawResults
    .map((item): ExecutePlanStepResult | null => {
      if (typeof item !== "object" || item === null) return null;
      const row = item as Record<string, unknown>;
      const status = row.status === "success" ? "success" : "error";
      return {
        step: typeof row.step === "number" ? row.step : -1,
        action: typeof row.action === "string" ? row.action : "unknown",
        status,
        message: typeof row.message === "string" ? row.message : status === "success" ? "ok" : "error",
        object_id: typeof row.object_id === "string" ? row.object_id : undefined
      };
    })
    .filter((value): value is ExecutePlanStepResult => value !== null);

  const stepCount = typeof safe.step_count === "number" ? safe.step_count : results.length;
  const successCount = typeof safe.success_count === "number"
    ? safe.success_count
    : results.filter((entry) => entry.status === "success").length;
  const errorCount = typeof safe.error_count === "number"
    ? safe.error_count
    : results.filter((entry) => entry.status === "error").length;

  return {
    status: typeof safe.status === "string" ? safe.status : "ok",
    plan_id: typeof safe.plan_id === "string" ? safe.plan_id : undefined,
    mode: safe.mode === "editor" || safe.mode === "runtime" ? safe.mode : undefined,
    role: typeof safe.role === "string" ? safe.role : undefined,
    results,
    step_count: stepCount,
    success_count: successCount,
    error_count: errorCount,
    fallback
  };
}

export async function executePlan(baseUrl: string, plan: Plan, apiKey?: string): Promise<ExecutePlanResponse> {
  try {
    const data = (await httpJson<unknown>(`${baseUrl}/nova/executePlan`, {
      method: "POST",
      headers: makeNovaJsonHeaders(apiKey),
      body: JSON.stringify(plan)
    })).data;
    return normalizeExecutePlanResponse(data, false);
  } catch (error) {
    if (!(error instanceof HttpError) || error.status !== 404) {
      throw error;
    }

    const perStep: ExecutePlanStepResult[] = [];
    for (let index = 0; index < plan.steps.length; index += 1) {
      const step = plan.steps[index];
      try {
        const result = await fallbackStep(baseUrl, step, apiKey);
        perStep.push({
          step: index,
          action: step.action,
          status: result.status,
          message: result.message,
          object_id: result.object_id
        });
      } catch (stepError) {
        const message = stepError instanceof Error ? stepError.message : "Fallback step failed";
        perStep.push({
          step: index,
          action: step.action,
          status: "error",
          message
        });
      }
    }

    const successCount = perStep.filter((entry) => entry.status === "success").length;
    const errorCount = perStep.filter((entry) => entry.status === "error").length;
    return {
      status: "ok",
      plan_id: plan.plan_id,
      mode: plan.mode,
      results: perStep,
      step_count: perStep.length,
      success_count: successCount,
      error_count: errorCount,
      fallback: true
    };
  }
}
