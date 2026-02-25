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

export type ActivityLog = {
  id: string;
  ts: string;
  level: "info" | "error";
  route: string;
  status?: number;
  message: string;
};
