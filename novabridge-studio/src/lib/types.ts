export type Mode = "unknown" | "editor" | "runtime";

export type Transform = {
  location?: [number, number, number];
  rotation?: [number, number, number];
  scale?: [number, number, number];
};

export type PlanStep =
  | { action: "spawn"; params: { type: string; label?: string; transform?: Transform; props?: Record<string, unknown> } }
  | { action: "delete"; params: { name: string } }
  | { action: "set"; params: { target: string; props: Record<string, unknown> } }
  | { action: "screenshot"; params: { width?: number; height?: number; format?: "png" | "raw" } };

export type Plan = {
  plan_id: string;
  mode: "editor" | "runtime";
  steps: PlanStep[];
};

export type Capability = {
  action: string;
  [k: string]: unknown;
};

export type CapsResponse = {
  status: string;
  mode?: "editor" | "runtime";
  capabilities?: Capability[];
};

export type HealthResponse = {
  status: string;
  mode?: "editor" | "runtime";
  version?: string;
};

export type ActivityLog = {
  id: string;
  ts: string;
  level: "info" | "error";
  route: string;
  status?: number;
  message: string;
};
