export function nowIso(): string {
  return new Date().toISOString();
}

export function summarizePrompt(prompt: string): string {
  const trimmed = prompt.trim();
  if (!trimmed) return "No prompt";
  return trimmed.length > 120 ? `${trimmed.slice(0, 117)}...` : trimmed;
}

export function normalizeBaseUrl(url: string): string {
  const trimmed = url.trim();
  if (!trimmed) return "";
  return trimmed.replace(/\/+$/, "");
}
