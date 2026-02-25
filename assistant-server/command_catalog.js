"use strict";

const ACTION_METADATA = {
  spawn: {
    risk: "medium",
    requires_confirmation: false,
    description: "Create a new actor in the scene."
  },
  delete: {
    risk: "high",
    requires_confirmation: true,
    description: "Delete an actor from the scene."
  },
  set: {
    risk: "medium",
    requires_confirmation: false,
    description: "Set properties on an actor or component."
  },
  screenshot: {
    risk: "low",
    requires_confirmation: false,
    description: "Capture viewport output."
  },
  "scene.spawn": {
    risk: "medium",
    requires_confirmation: false,
    description: "Spawn actor through scene route."
  },
  "scene.delete": {
    risk: "high",
    requires_confirmation: true,
    description: "Delete actor through scene route."
  },
  "scene.set-property": {
    risk: "medium",
    requires_confirmation: false,
    description: "Set scene property value."
  },
  "asset.import": {
    risk: "medium",
    requires_confirmation: false,
    description: "Import an external asset into project content."
  },
  "asset.delete": {
    risk: "high",
    requires_confirmation: true,
    description: "Delete an asset from content browser."
  },
  "optimize.nanite": {
    risk: "low",
    requires_confirmation: false,
    description: "Enable or configure Nanite settings."
  },
  "optimize.lumen": {
    risk: "low",
    requires_confirmation: false,
    description: "Enable or configure Lumen settings."
  },
  "exec.command": {
    risk: "high",
    requires_confirmation: true,
    description: "Execute console command."
  }
};

const RISK_ORDER = {
  low: 0,
  medium: 1,
  high: 2
};

function normalizeAction(action) {
  if (typeof action !== "string") {
    return "";
  }
  return action.trim().toLowerCase();
}

function classifyAction(action) {
  const normalized = normalizeAction(action);
  const known = ACTION_METADATA[normalized];
  if (known) {
    return {
      action: normalized,
      risk: known.risk,
      requires_confirmation: known.requires_confirmation,
      description: known.description
    };
  }

  return {
    action: normalized,
    risk: "medium",
    requires_confirmation: false,
    description: "No explicit risk profile yet."
  };
}

function buildCommandCatalog(capabilities) {
  const actions = new Set();
  if (Array.isArray(capabilities)) {
    for (const capability of capabilities) {
      if (!capability || typeof capability !== "object") {
        continue;
      }
      if (typeof capability.action === "string") {
        actions.add(normalizeAction(capability.action));
      }
    }
  }

  for (const action of Object.keys(ACTION_METADATA)) {
    actions.add(action);
  }

  return Array.from(actions)
    .filter(Boolean)
    .map((action) => classifyAction(action))
    .sort((left, right) => left.action.localeCompare(right.action));
}

function summarizePlanRisk(planSteps) {
  const steps = Array.isArray(planSteps) ? planSteps : [];
  let highest = "low";
  let confirmationRequired = false;

  const perStep = steps.map((step, index) => {
    const action = step && typeof step === "object" ? step.action : "";
    const classified = classifyAction(action);
    if (RISK_ORDER[classified.risk] > RISK_ORDER[highest]) {
      highest = classified.risk;
    }
    if (classified.requires_confirmation) {
      confirmationRequired = true;
    }
    return {
      step: index,
      action: classified.action,
      risk: classified.risk,
      requires_confirmation: classified.requires_confirmation
    };
  });

  return {
    highest_risk: highest,
    requires_confirmation: confirmationRequired,
    steps: perStep
  };
}

module.exports = {
  ACTION_METADATA,
  classifyAction,
  buildCommandCatalog,
  summarizePlanRisk
};
