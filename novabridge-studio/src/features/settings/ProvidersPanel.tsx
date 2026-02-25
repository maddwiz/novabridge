import { Card } from "../../ui/components/Card";
import { Input } from "../../ui/components/Input";
import { Select } from "../../ui/components/Select";
import type { SettingsState } from "./settingsStore";

type ProvidersPanelProps = {
  settings: SettingsState;
  onChange: (next: SettingsState) => void;
};

export function ProvidersPanel({ settings, onChange }: ProvidersPanelProps) {
  const set = <K extends keyof SettingsState>(key: K, value: SettingsState[K]) => {
    onChange({ ...settings, [key]: value });
  };

  return (
    <Card className="space-y-3">
      <div className="text-sm text-[var(--muted)]">Provider Settings</div>
      <Select value={settings.provider} onChange={(e) => set("provider", e.target.value as SettingsState["provider"])}>
        <option value="openai">OpenAI</option>
        <option value="anthropic">Anthropic</option>
        <option value="ollama">Ollama</option>
        <option value="custom">Custom</option>
      </Select>

      {settings.provider === "openai" ? (
        <>
          <Input placeholder="OpenAI API Key" value={settings.openaiKey} onChange={(e) => set("openaiKey", e.target.value)} />
          <Input placeholder="Model" value={settings.openaiModel} onChange={(e) => set("openaiModel", e.target.value)} />
        </>
      ) : null}

      {settings.provider === "anthropic" ? (
        <>
          <Input placeholder="Anthropic API Key" value={settings.anthropicKey} onChange={(e) => set("anthropicKey", e.target.value)} />
          <Input placeholder="Model" value={settings.anthropicModel} onChange={(e) => set("anthropicModel", e.target.value)} />
        </>
      ) : null}

      {settings.provider === "ollama" ? (
        <>
          <Input placeholder="Ollama Host" value={settings.ollamaHost} onChange={(e) => set("ollamaHost", e.target.value)} />
          <Input placeholder="Model" value={settings.ollamaModel} onChange={(e) => set("ollamaModel", e.target.value)} />
        </>
      ) : null}

      {settings.provider === "custom" ? (
        <>
          <Input placeholder="Endpoint" value={settings.customEndpoint} onChange={(e) => set("customEndpoint", e.target.value)} />
          <Input placeholder="Header Key" value={settings.customHeaderKey} onChange={(e) => set("customHeaderKey", e.target.value)} />
          <Input placeholder="Header Value" value={settings.customHeaderValue} onChange={(e) => set("customHeaderValue", e.target.value)} />
        </>
      ) : null}

      <label className="flex items-center gap-2 text-sm text-[var(--muted)]">
        <input type="checkbox" checked={settings.useMockProvider} onChange={(e) => set("useMockProvider", e.target.checked)} />
        Use Mock Provider in dev
      </label>
    </Card>
  );
}
