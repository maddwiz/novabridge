import { useEffect, useMemo, useState } from "react";
import { motion } from "framer-motion";
import { DEFAULT_ROUTE, type AppRoute } from "./routes";
import { Shell } from "../ui/components/Shell";
import { Sidebar } from "../ui/components/Sidebar";
import { Topbar } from "../ui/components/Topbar";
import { Card } from "../ui/components/Card";
import { Button } from "../ui/components/Button";
import { TextArea } from "../ui/components/TextArea";
import { Toast } from "../ui/components/Toast";
import { ConnectPanel } from "../features/connect/ConnectPanel";
import { fetchCaps, fetchHealth } from "../features/connect/connectApi";
import { loadConnectState, saveConnectState } from "../features/connect/connectStore";
import { loadPlannerState, savePlannerState } from "../features/planner/plannerStore";
import { loadExecutorState, saveExecutorState } from "../features/executor/executorStore";
import { loadSettingsState, saveSettingsState } from "../features/settings/settingsStore";
import { promptTemplates } from "../features/planner/templates";
import { generatePlan } from "../features/planner/plannerApi";
import { PlanPreview } from "../features/planner/PlanPreview";
import { validatePlanAgainstPermissions } from "../features/planner/planPolicy";
import { executePlan } from "../features/executor/executorApi";
import { ActivityStream } from "../features/executor/ActivityStream";
import { ProvidersPanel } from "../features/settings/ProvidersPanel";
import { nowIso, summarizePrompt } from "../lib/format";
import type { ActivityLog, Mode } from "../lib/types";

function makeLog(level: "info" | "error", route: string, message: string, status?: number): ActivityLog {
  return {
    id: `${Date.now()}-${Math.random().toString(16).slice(2)}`,
    ts: nowIso(),
    level,
    route,
    status,
    message
  };
}

export function App() {
  const [route, setRoute] = useState<AppRoute>(DEFAULT_ROUTE);
  const [connectState, setConnectState] = useState(loadConnectState);
  const [plannerState, setPlannerState] = useState(loadPlannerState);
  const [executorState, setExecutorState] = useState(loadExecutorState);
  const [settingsState, setSettingsState] = useState(loadSettingsState);
  const [toast, setToast] = useState<{ message: string; kind: "info" | "error" } | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => saveConnectState(connectState), [connectState]);
  useEffect(() => savePlannerState(plannerState), [plannerState]);
  useEffect(() => saveExecutorState(executorState), [executorState]);
  useEffect(() => saveSettingsState(settingsState), [settingsState]);

  const routeCount = useMemo(() => connectState.caps?.length, [connectState.caps]);
  const policyErrors = useMemo(() => {
    if (!plannerState.plan) return [];
    return validatePlanAgainstPermissions(plannerState.plan, connectState.permissions);
  }, [plannerState.plan, connectState.permissions]);

  const pushLog = (entry: ActivityLog) => {
    setExecutorState((prev) => ({ ...prev, activity: [entry, ...prev.activity].slice(0, 200) }));
  };

  const showToast = (message: string, kind: "info" | "error" = "info") => {
    setToast({ message, kind });
    window.setTimeout(() => setToast(null), 2800);
  };

  const onConnect = async () => {
    setBusy(true);
    try {
      const health = await fetchHealth(connectState.baseUrl);
      const caps = await fetchCaps(connectState.baseUrl);
      const mode: Mode = caps?.mode ?? health.mode ?? "unknown";
      setConnectState((prev) => ({
        ...prev,
        connected: health.status === "ok",
        mode,
        version: health.version,
        caps: caps?.capabilities ?? prev.caps,
        permissions: caps?.permissions ?? prev.permissions
      }));
      pushLog(makeLog("info", "/nova/health", `Connected in ${mode} mode`));
      showToast("Connected", "info");
    } catch (error) {
      setConnectState((prev) => ({ ...prev, connected: false, mode: "unknown", permissions: undefined }));
      const message = error instanceof Error ? error.message : "Connect failed";
      pushLog(makeLog("error", "/nova/health", message));
      showToast(message, "error");
    } finally {
      setBusy(false);
    }
  };

  const onGeneratePlan = async () => {
    const prompt = plannerState.prompt.trim();
    if (!prompt) {
      showToast("Prompt is empty", "error");
      return;
    }

    setBusy(true);
    try {
      const mode = connectState.mode === "runtime" ? "runtime" : "editor";
      const capsText = JSON.stringify(
        {
          capabilities: connectState.caps ?? [],
          permissions: connectState.permissions ?? null
        },
        null,
        2
      );
      const plan = await generatePlan({
        prompt,
        mode,
        capsText,
        settings: settingsState
      });

      setPlannerState((prev) => ({ ...prev, plan }));
      pushLog(makeLog("info", "planner.generate", `Plan generated for: ${summarizePrompt(prompt)}`));
      showToast("Plan generated", "info");
    } catch (error) {
      const message = error instanceof Error ? error.message : "Plan generation failed";
      pushLog(makeLog("error", "planner.generate", message));
      showToast(message, "error");
    } finally {
      setBusy(false);
    }
  };

  const onExecutePlan = async () => {
    const plan = plannerState.plan;
    if (!plan) return;

    if (policyErrors.length > 0) {
      const summary = policyErrors[0];
      pushLog(makeLog("error", "/nova/executePlan", summary));
      showToast(summary, "error");
      return;
    }

    setBusy(true);
    try {
      const result = await executePlan(connectState.baseUrl, plan);
      setExecutorState((prev) => ({ ...prev, lastPlan: plan, lastRun: result }));
      pushLog(makeLog("info", "/nova/executePlan", "Plan executed"));
      showToast("Execution complete", "info");
    } catch (error) {
      const message = error instanceof Error ? error.message : "Execute failed";
      pushLog(makeLog("error", "/nova/executePlan", message));
      showToast(message, "error");
    } finally {
      setBusy(false);
    }
  };

  return (
    <Shell>
      <div className="grid h-full grid-cols-1 gap-4 md:grid-cols-[260px_minmax(0,1fr)_380px]">
        <Sidebar route={route} onSelect={setRoute} connected={connectState.connected} mode={connectState.mode} />

        <main className="flex min-h-0 flex-col gap-4">
          <Topbar baseUrl={connectState.baseUrl} version={connectState.version} />

          <motion.div
            key={route}
            initial={{ opacity: 0, y: 6 }}
            animate={{ opacity: 1, y: 0 }}
            className="panel-scroll min-h-0 flex-1 overflow-auto"
          >
            {route === "connect" ? (
              <ConnectPanel
                baseUrl={connectState.baseUrl}
                connected={connectState.connected}
                mode={connectState.mode}
                permissions={connectState.permissions}
                onBaseUrlChange={(value) => setConnectState((prev) => ({ ...prev, baseUrl: value }))}
                onConnect={onConnect}
              />
            ) : null}

            {route === "build" ? (
              plannerState.plan ? (
                <PlanPreview
                  plan={plannerState.plan}
                  showJson={plannerState.showJson}
                  policyErrors={policyErrors}
                  onToggleJson={() => setPlannerState((prev) => ({ ...prev, showJson: !prev.showJson }))}
                  onExecute={onExecutePlan}
                  onBack={() => setPlannerState((prev) => ({ ...prev, plan: null }))}
                />
              ) : (
                <Card className="space-y-4">
                  <div className="text-sm text-[var(--muted)]">Describe what you want built in UE.</div>
                  <TextArea
                    rows={8}
                    value={plannerState.prompt}
                    onChange={(e) => setPlannerState((prev) => ({ ...prev, prompt: e.target.value }))}
                    placeholder="Spawn a point light named LaunchSmokeLight and frame a quick screenshot"
                  />
                  <div className="flex flex-wrap gap-2">
                    {promptTemplates.map((template) => (
                      <button
                        key={template}
                        onClick={() => setPlannerState((prev) => ({ ...prev, prompt: template }))}
                        className="rounded-full border border-white/10 bg-white/5 px-3 py-1 text-xs text-[var(--muted)] hover:bg-white/10"
                      >
                        {template}
                      </button>
                    ))}
                  </div>
                  <Button onClick={onGeneratePlan} disabled={busy}>Generate Plan</Button>
                </Card>
              )
            ) : null}

            {route === "settings" ? <ProvidersPanel settings={settingsState} onChange={setSettingsState} /> : null}

            {route === "modify" ? (
              <Card className="text-sm text-[var(--muted)]">Modify mode is reserved for v0.2.</Card>
            ) : null}

            {route === "capture" ? (
              <Card className="text-sm text-[var(--muted)]">Capture mode is reserved for v0.2.</Card>
            ) : null}
          </motion.div>
        </main>

        <ActivityStream logs={executorState.activity} mode={connectState.mode} baseUrl={connectState.baseUrl} routeCount={routeCount} />
      </div>

      <Toast message={toast?.message ?? ""} kind={toast?.kind ?? "info"} />
    </Shell>
  );
}
