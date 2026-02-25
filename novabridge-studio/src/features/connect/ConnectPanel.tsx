import { Input } from "../../ui/components/Input";
import { Button } from "../../ui/components/Button";
import { Card } from "../../ui/components/Card";
import type { PermissionSnapshot } from "../../lib/types";

type ConnectPanelProps = {
  baseUrl: string;
  connected: boolean;
  mode: string;
  permissions?: PermissionSnapshot;
  onBaseUrlChange: (value: string) => void;
  onConnect: () => void;
};

function formatAllowedActions(permissions?: PermissionSnapshot): string {
  const actions = permissions?.executePlan?.allowed_actions;
  if (!Array.isArray(actions) || actions.length === 0) return "n/a";
  return actions.join(", ");
}

export function ConnectPanel({ baseUrl, connected, mode, permissions, onBaseUrlChange, onConnect }: ConnectPanelProps) {
  return (
    <Card className="space-y-3">
      <div className="text-sm text-[var(--muted)]">Connect NovaBridge Studio to local UE endpoint.</div>
      <Input value={baseUrl} onChange={(e) => onBaseUrlChange(e.target.value)} placeholder="http://127.0.0.1:30010" />
      <div className="flex items-center gap-2">
        <Button onClick={onConnect}>Connect</Button>
        <div className="text-xs text-[var(--muted)]">Status: {connected ? `Connected (${mode})` : "Disconnected"}</div>
      </div>

      {connected && permissions ? (
        <div className="rounded-xl border border-white/10 bg-white/5 p-3 text-xs text-[var(--muted)]">
          <div className="mb-2 text-[var(--text)]">Policy Snapshot</div>
          <div>Role: {permissions.role ?? "n/a"}</div>
          <div>ExecutePlan allowed: {permissions.executePlan?.allowed === false ? "no" : "yes"}</div>
          <div>Allowed actions: {formatAllowedActions(permissions)}</div>
          <div>Max steps: {permissions.executePlan?.max_steps ?? "n/a"}</div>
          <div>Spawn allowed: {permissions.spawn?.allowed === false ? "no" : "yes"}</div>
          <div>Spawn per plan: {permissions.spawn?.max_spawn_per_plan ?? "n/a"}</div>
          <div>Execute/min: {permissions.executePlan?.max_requests_per_minute ?? "n/a"}</div>
          <div>Spawn/min: {permissions.spawn?.max_requests_per_minute ?? "n/a"}</div>
        </div>
      ) : null}

      <div className="rounded-xl border border-white/10 bg-black/20 p-3 text-xs text-[var(--muted)]">
        <div className="mb-1 text-[var(--text)]">Runtime Pairing (v0.2 stub)</div>
        <div className="mb-2">UI reserved for `POST /nova/runtime/pair` token exchange in a later release.</div>
        <div className="grid grid-cols-1 gap-2 md:grid-cols-2">
          <Input value="" readOnly placeholder="Pairing code (coming soon)" />
          <Input value="" readOnly placeholder="Runtime token (coming soon)" />
        </div>
      </div>
    </Card>
  );
}
