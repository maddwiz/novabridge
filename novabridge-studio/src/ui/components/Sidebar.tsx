import { Cpu, Link2, Settings, Sparkles, Wand2 } from "lucide-react";
import type { ReactNode } from "react";
import type { AppRoute } from "../../app/routes";

type SidebarProps = {
  route: AppRoute;
  onSelect: (route: AppRoute) => void;
  connected: boolean;
  mode: string;
};

const items: Array<{ id: AppRoute; label: string; icon: ReactNode }> = [
  { id: "connect", label: "Connect", icon: <Link2 size={16} /> },
  { id: "build", label: "Build", icon: <Sparkles size={16} /> },
  { id: "modify", label: "Modify", icon: <Wand2 size={16} /> },
  { id: "capture", label: "Capture", icon: <Cpu size={16} /> },
  { id: "settings", label: "Settings", icon: <Settings size={16} /> }
];

export function Sidebar({ route, onSelect, connected, mode }: SidebarProps) {
  return (
    <aside className="glass flex h-full flex-col gap-3 rounded-2xl p-3">
      <div className="mb-2 px-2">
        <div className="text-xs uppercase tracking-[0.28em] text-[var(--muted)]">NovaBridge</div>
        <div className="mt-1 text-lg font-semibold">Studio v0.1</div>
      </div>

      <div className="space-y-1">
        {items.map((item) => (
          <button
            key={item.id}
            onClick={() => onSelect(item.id)}
            className={`flex w-full items-center gap-2 rounded-xl px-3 py-2 text-sm transition ${route === item.id ? "bg-white/14" : "hover:bg-white/8"}`}
          >
            {item.icon}
            {item.label}
          </button>
        ))}
      </div>

      <div className="mt-auto rounded-xl border border-white/10 bg-black/20 p-3">
        <div className="flex items-center gap-2 text-xs text-[var(--muted)]">
          <span className="badge-led" style={{ color: connected ? "#54e28e" : "#ff6f8e" }} />
          {connected ? "Connected" : "Disconnected"}
        </div>
        <div className="mt-1 text-xs uppercase tracking-wide text-[var(--muted)]">Mode: {mode}</div>
      </div>
    </aside>
  );
}
