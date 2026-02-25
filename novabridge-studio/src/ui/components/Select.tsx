import type { SelectHTMLAttributes } from "react";

export function Select(props: SelectHTMLAttributes<HTMLSelectElement>) {
  return (
    <select
      {...props}
      className={`w-full rounded-xl border border-white/15 bg-[#0b1537]/70 px-3 py-2 text-sm text-[var(--text)] outline-none focus:border-[var(--accent-b)] ${props.className ?? ""}`}
    />
  );
}
