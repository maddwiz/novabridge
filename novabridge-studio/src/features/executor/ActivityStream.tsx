import type { ActivityLog } from "../../lib/types";
import { Card } from "../../ui/components/Card";

type ActivityStreamProps = {
  logs: ActivityLog[];
  mode: string;
  baseUrl: string;
  routeCount?: number;
  eventsWsUrl?: string;
  eventSocketState?: "idle" | "connecting" | "connected" | "error";
};

export function ActivityStream({ logs, mode, baseUrl, routeCount, eventsWsUrl, eventSocketState = "idle" }: ActivityStreamProps) {
  return (
    <Card className="flex h-full flex-col gap-3 overflow-hidden">
      <div className="text-sm text-[var(--muted)]">Activity</div>
      <div className="rounded-xl border border-white/10 bg-black/30 p-2 text-xs text-[var(--muted)]">
        <div>URL: {baseUrl}</div>
        <div>Mode: {mode}</div>
        <div>Routes: {routeCount ?? "n/a"}</div>
        <div>Events: {eventsWsUrl ?? "n/a"}</div>
        <div>Event Socket: {eventSocketState}</div>
      </div>
      <div className="panel-scroll flex-1 space-y-2 overflow-auto rounded-xl border border-white/10 bg-black/40 p-2">
        {logs.length === 0 ? <div className="text-[var(--muted)]">No activity yet.</div> : null}
        {logs.map((entry) => (
          <div key={entry.id} className={`rounded-lg border p-2 ${entry.level === "error" ? "border-red-400/40 bg-red-500/10" : "border-white/10 bg-white/5"}`}>
            <div className="font-mono text-[10px] text-[var(--muted)]">{entry.ts}</div>
            <div className="text-[11px]">
              {entry.route} {entry.status ? `(${entry.status})` : ""}
            </div>
            <div className="text-xs">{entry.message}</div>
          </div>
        ))}
      </div>
    </Card>
  );
}
