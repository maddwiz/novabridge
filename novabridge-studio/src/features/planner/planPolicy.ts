import type { PermissionSnapshot, Plan } from "../../lib/types";

function includesIgnoreCase(values: readonly string[], candidate: string): boolean {
  return values.some((value) => value.toLowerCase() === candidate.toLowerCase());
}

export function validatePlanAgainstPermissions(plan: Plan, permissions?: PermissionSnapshot): string[] {
  if (!permissions) return [];

  const errors: string[] = [];
  const steps = Array.isArray(plan.steps) ? plan.steps : [];

  const executePolicy = permissions.executePlan;
  if (executePolicy?.allowed === false) {
    errors.push("Current role/mode is not allowed to execute plans.");
  }

  const allowedActions = Array.isArray(executePolicy?.allowed_actions)
    ? executePolicy.allowed_actions.filter((action): action is string => typeof action === "string")
    : [];

  if (allowedActions.length > 0) {
    steps.forEach((step, index) => {
      if (!includesIgnoreCase(allowedActions, step.action)) {
        errors.push(`Step ${index} action '${step.action}' is not allowed.`);
      }
    });
  }

  if (typeof executePolicy?.max_steps === "number" && executePolicy.max_steps >= 0 && steps.length > executePolicy.max_steps) {
    errors.push(`Plan has ${steps.length} steps but max_steps is ${executePolicy.max_steps}.`);
  }

  const spawnSteps = steps.filter((step) => step.action === "spawn");
  const spawnPolicy = permissions.spawn;
  if (spawnPolicy?.allowed === false && spawnSteps.length > 0) {
    errors.push("Spawn actions are not allowed for the current role/mode.");
  }

  if (typeof spawnPolicy?.max_spawn_per_plan === "number"
    && spawnPolicy.max_spawn_per_plan >= 0
    && spawnSteps.length > spawnPolicy.max_spawn_per_plan) {
    errors.push(`Plan has ${spawnSteps.length} spawn steps but max_spawn_per_plan is ${spawnPolicy.max_spawn_per_plan}.`);
  }

  const restrictedClasses = Array.isArray(spawnPolicy?.allowedClasses)
    ? spawnPolicy.allowedClasses.filter((value): value is string => typeof value === "string")
    : [];
  const classesUnrestricted = spawnPolicy?.classes_unrestricted === true;
  if (!classesUnrestricted && restrictedClasses.length > 0) {
    steps.forEach((step, index) => {
      if (step.action !== "spawn") return;
      const requestedClass = step.params?.type;
      if (typeof requestedClass === "string" && !includesIgnoreCase(restrictedClasses, requestedClass)) {
        errors.push(`Spawn step ${index} class '${requestedClass}' is outside allowedClasses.`);
      }
    });
  }

  return errors;
}
