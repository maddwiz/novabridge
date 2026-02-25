import { motion } from "framer-motion";
import type { Plan } from "../../lib/types";
import { Card } from "../../ui/components/Card";
import { Button } from "../../ui/components/Button";

type PlanPreviewProps = {
  plan: Plan;
  showJson: boolean;
  policyErrors?: string[];
  onToggleJson: () => void;
  onExecute: () => void;
  onBack: () => void;
};

export function PlanPreview({ plan, showJson, policyErrors = [], onToggleJson, onExecute, onBack }: PlanPreviewProps) {
  const hasPolicyErrors = policyErrors.length > 0;

  return (
    <Card className="space-y-3">
      <div className="text-sm text-[var(--muted)]">Plan Preview</div>
      <div className="space-y-2">
        {plan.steps.map((step, idx) => (
          <motion.div key={idx} initial={{ opacity: 0, y: 4 }} animate={{ opacity: 1, y: 0 }} className="rounded-xl border border-white/10 bg-white/5 p-3">
            <div className="text-sm font-medium">Step {idx + 1}: {step.action}</div>
            <pre className="mt-1 overflow-x-auto text-xs text-[var(--muted)]">{JSON.stringify(step.params, null, 2)}</pre>
          </motion.div>
        ))}
      </div>

      <div className="flex flex-wrap gap-2">
        <Button onClick={onExecute} disabled={hasPolicyErrors}>Confirm & Execute</Button>
        <Button tone="ghost" onClick={onToggleJson}>{showJson ? "Hide JSON plan" : "Show JSON plan"}</Button>
        <Button tone="ghost" onClick={onBack}>Back</Button>
      </div>

      {hasPolicyErrors ? (
        <div className="rounded-xl border border-red-400/40 bg-red-500/10 p-3 text-xs text-red-100">
          <div className="mb-1 font-medium">Execution blocked by policy</div>
          {policyErrors.map((error, index) => (
            <div key={`${error}-${index}`}>- {error}</div>
          ))}
        </div>
      ) : null}

      {showJson ? <pre className="max-h-64 overflow-auto rounded-xl border border-white/10 bg-black/30 p-3 text-xs">{JSON.stringify(plan, null, 2)}</pre> : null}
    </Card>
  );
}
