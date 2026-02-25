import { httpJson } from "../../lib/http";
import type { CapsResponse, EventsResponse, HealthResponse } from "../../lib/types";

function makeNovaHeaders(apiKey?: string): HeadersInit | undefined {
  if (!apiKey || !apiKey.trim()) return undefined;
  return { "X-API-Key": apiKey.trim() };
}

export async function fetchHealth(baseUrl: string, apiKey?: string): Promise<HealthResponse> {
  const { data } = await httpJson<HealthResponse>(`${baseUrl}/nova/health`, { headers: makeNovaHeaders(apiKey) });
  return data;
}

export async function fetchCaps(baseUrl: string, apiKey?: string): Promise<CapsResponse | null> {
  try {
    const { data } = await httpJson<CapsResponse>(`${baseUrl}/nova/caps`, { headers: makeNovaHeaders(apiKey) });
    return data;
  } catch {
    return null;
  }
}

export async function fetchEvents(baseUrl: string, apiKey?: string): Promise<EventsResponse | null> {
  try {
    const { data } = await httpJson<EventsResponse>(`${baseUrl}/nova/events`, { headers: makeNovaHeaders(apiKey) });
    return data;
  } catch {
    return null;
  }
}
