import type { Plan } from "./types";

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isNumberTuple(value: unknown): value is [number, number, number] {
  if (!Array.isArray(value) || value.length !== 3) return false;
  return value.every((entry) => typeof entry === "number" && Number.isFinite(entry));
}

function isTransform(value: unknown): boolean {
  if (!isRecord(value)) return false;
  if (value.location !== undefined && !isNumberTuple(value.location)) return false;
  if (value.rotation !== undefined && !isNumberTuple(value.rotation)) return false;
  if (value.scale !== undefined && !isNumberTuple(value.scale)) return false;
  return true;
}

function isSpawnStep(step: Record<string, unknown>): boolean {
  if (step.action !== "spawn" || !isRecord(step.params)) return false;
  if (typeof step.params.type !== "string" || step.params.type.trim().length === 0) return false;
  if (step.params.label !== undefined && typeof step.params.label !== "string") return false;
  if (step.params.transform !== undefined && !isTransform(step.params.transform)) return false;
  if (step.params.props !== undefined && !isRecord(step.params.props)) return false;
  return true;
}

function isDeleteStep(step: Record<string, unknown>): boolean {
  if (step.action !== "delete" || !isRecord(step.params)) return false;
  return typeof step.params.name === "string" && step.params.name.trim().length > 0;
}

function isSetStep(step: Record<string, unknown>): boolean {
  if (step.action !== "set" || !isRecord(step.params)) return false;
  if (typeof step.params.target !== "string" || step.params.target.trim().length === 0) return false;
  return isRecord(step.params.props);
}

function isScreenshotStep(step: Record<string, unknown>): boolean {
  if (step.action !== "screenshot" || !isRecord(step.params)) return false;
  const width = step.params.width;
  const height = step.params.height;
  const format = step.params.format;
  if (width !== undefined && (typeof width !== "number" || !Number.isFinite(width) || width <= 0)) return false;
  if (height !== undefined && (typeof height !== "number" || !Number.isFinite(height) || height <= 0)) return false;
  if (format !== undefined && format !== "png" && format !== "raw") return false;
  return true;
}

function isPlanStep(value: unknown): boolean {
  if (!isRecord(value) || typeof value.action !== "string") return false;
  return isSpawnStep(value) || isDeleteStep(value) || isSetStep(value) || isScreenshotStep(value);
}

export function isPlan(value: unknown): value is Plan {
  if (!isRecord(value)) return false;
  if (typeof value.plan_id !== "string" || value.plan_id.trim().length === 0) return false;
  if (value.mode !== "editor" && value.mode !== "runtime") return false;
  if (!Array.isArray(value.steps)) return false;
  return value.steps.every((step) => isPlanStep(step));
}
