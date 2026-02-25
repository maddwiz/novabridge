import type { PropsWithChildren } from "react";

type CardProps = PropsWithChildren<{ className?: string }>;

export function Card({ className, children }: CardProps) {
  return <div className={`glass rounded-2xl p-4 ${className ?? ""}`}>{children}</div>;
}
