import { Input } from "../../ui/components/Input";
import { Button } from "../../ui/components/Button";
import { Card } from "../../ui/components/Card";

type ConnectPanelProps = {
  baseUrl: string;
  connected: boolean;
  mode: string;
  onBaseUrlChange: (value: string) => void;
  onConnect: () => void;
};

export function ConnectPanel({ baseUrl, connected, mode, onBaseUrlChange, onConnect }: ConnectPanelProps) {
  return (
    <Card className="space-y-3">
      <div className="text-sm text-[var(--muted)]">Connect NovaBridge Studio to local UE endpoint.</div>
      <Input value={baseUrl} onChange={(e) => onBaseUrlChange(e.target.value)} placeholder="http://127.0.0.1:30010" />
      <div className="flex items-center gap-2">
        <Button onClick={onConnect}>Connect</Button>
        <div className="text-xs text-[var(--muted)]">Status: {connected ? `Connected (${mode})` : "Disconnected"}</div>
      </div>
    </Card>
  );
}
