import { Badge } from "./Badge";

type TopbarProps = {
  baseUrl: string;
  version?: string;
};

export function Topbar({ baseUrl, version }: TopbarProps) {
  return (
    <div className="glass flex items-center justify-between rounded-2xl px-4 py-3">
      <div>
        <div className="text-sm text-[var(--muted)]">Connected Endpoint</div>
        <div className="font-mono text-sm">{baseUrl}</div>
      </div>
      <Badge>{version ? `NovaBridge ${version}` : "NovaBridge"}</Badge>
    </div>
  );
}
