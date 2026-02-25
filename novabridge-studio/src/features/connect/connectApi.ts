import { httpJson } from "../../lib/http";
import type { CapsResponse, HealthResponse } from "../../lib/types";

export async function fetchHealth(baseUrl: string): Promise<HealthResponse> {
  const { data } = await httpJson<HealthResponse>(`${baseUrl}/nova/health`);
  return data;
}

export async function fetchCaps(baseUrl: string): Promise<CapsResponse | null> {
  try {
    const { data } = await httpJson<CapsResponse>(`${baseUrl}/nova/caps`);
    return data;
  } catch {
    return null;
  }
}
