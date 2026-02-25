export class HttpError extends Error {
  status: number;
  bodyText: string;

  constructor(status: number, bodyText: string) {
    const suffix = bodyText?.trim() ? `: ${bodyText.trim().slice(0, 220)}` : "";
    super(`HTTP ${status}${suffix}`);
    this.status = status;
    this.bodyText = bodyText;
  }
}

export async function httpJson<T>(url: string, init?: RequestInit): Promise<{ status: number; data: T }> {
  const res = await fetch(url, init);
  const text = await res.text();
  if (!res.ok) {
    throw new HttpError(res.status, text);
  }

  let parsed: T;
  try {
    parsed = JSON.parse(text) as T;
  } catch {
    parsed = {} as T;
  }
  return { status: res.status, data: parsed };
}
