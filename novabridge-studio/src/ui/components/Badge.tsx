import type { PropsWithChildren } from "react";

export function Badge({ children }: PropsWithChildren) {
  return <span className="rounded-full border border-white/15 bg-white/5 px-2 py-1 text-xs text-[var(--muted)]">{children}</span>;
}
