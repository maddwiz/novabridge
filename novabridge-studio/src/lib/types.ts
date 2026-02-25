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

export type PermissionSnapshot = {
  mode?: "editor" | "runtime";
  role?: string;
  spawn?: {
    allowed?: boolean;
    classes_unrestricted?: boolean;
    allowedClasses?: string[];
    max_spawn_per_plan?: number;
    max_requests_per_minute?: number;
    [k: string]: unknown;
  };
  executePlan?: {
    allowed?: boolean;
    allowed_actions?: string[];
    max_steps?: number;
    max_requests_per_minute?: number;
    [k: string]: unknown;
  };
  route_rate_limits_per_minute?: Record<string, number>;
  [k: string]: unknown;
};

export type CapsResponse = {
  status: string;
  mode?: "editor" | "runtime";
  capabilities?: Capability[];
  permissions?: PermissionSnapshot;
};

export type HealthResponse = {
  status: string;
  mode?: "editor" | "runtime";
  version?: string;
};

export type EventsResponse = {
  status?: string;
  ws_url?: string;
  ws_port?: number;
  clients?: number;
  pending_events?: number;
  supported_types?: string[];
  [k: string]: unknown;
};

export type ActivityLog = {
  id: string;
  ts: string;
  level: "info" | "error";
  route: string;
  status?: number;
  message: string;
};

export type ExecutePlanStepResult = {
  step: number;
  action: string;
  status: "success" | "error";
  message: string;
  object_id?: string;
};

export type ExecutePlanResponse = {
  status: string;
  plan_id?: string;
  mode?: "editor" | "runtime";
  role?: string;
  results: ExecutePlanStepResult[];
  step_count: number;
  success_count: number;
  error_count: number;
  fallback?: boolean;
};
