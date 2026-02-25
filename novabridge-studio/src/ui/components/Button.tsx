import type { ButtonHTMLAttributes, PropsWithChildren } from "react";

type ButtonProps = PropsWithChildren<ButtonHTMLAttributes<HTMLButtonElement>> & {
  tone?: "primary" | "ghost" | "danger";
};

export function Button({ tone = "primary", className, children, ...rest }: ButtonProps) {
  const toneClass =
    tone === "primary"
      ? "bg-gradient-to-r from-[var(--accent-a)] to-[var(--accent-b)] text-white shadow-glow"
      : tone === "danger"
        ? "bg-[var(--danger)]/20 border border-[var(--danger)]/40 text-red-100"
        : "bg-white/5 border border-white/10 text-[var(--text)]";

  return (
    <button
      className={`rounded-xl px-3 py-2 text-sm transition hover:-translate-y-0.5 hover:brightness-110 disabled:opacity-40 ${toneClass} ${className ?? ""}`}
      {...rest}
    >
      {children}
    </button>
  );
}
