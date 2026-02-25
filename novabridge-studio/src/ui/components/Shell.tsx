import type { PropsWithChildren } from "react";

export function Shell({ children }: PropsWithChildren) {
  return <div className="mx-auto h-screen max-w-[1600px] p-4 md:p-5">{children}</div>;
}
