"use strict";

const healthDot = document.getElementById("healthDot");
const healthText = document.getElementById("healthText");
const promptInput = document.getElementById("prompt");
const planBtn = document.getElementById("planBtn");
const execBtn = document.getElementById("execBtn");
const planJson = document.getElementById("planJson");
const activity = document.getElementById("activity");
const planSource = document.getElementById("planSource");
const planRisk = document.getElementById("planRisk");

let currentPlan = null;
let currentRisk = null;

function log(message) {
  const now = new Date().toISOString();
  const line = `[${now}] ${message}`;
  if (activity.textContent === "ready") {
    activity.textContent = line;
    return;
  }
  activity.textContent = `${line}\n${activity.textContent}`;
}

function setHealth(state, message) {
  healthDot.classList.remove("dot-off", "dot-on", "dot-bad");
  healthDot.classList.add(state);
  healthText.textContent = message;
}

async function refreshHealth() {
  try {
    const res = await fetch("/assistant/health");
    const json = await res.json();
    if (json.status === "ok") {
      const mode = json.nova && json.nova.mode ? json.nova.mode : "unknown";
      setHealth("dot-on", `Connected (${mode})`);
    } else {
      setHealth("dot-bad", "Assistant degraded");
    }
  } catch (error) {
    setHealth("dot-bad", "Assistant offline");
    log(`health error: ${error.message}`);
  }
}

function renderPlan(payload) {
  currentPlan = payload.plan;
  currentRisk = payload.risk || null;
  planJson.textContent = JSON.stringify(payload, null, 2);
  planSource.textContent = `source: ${payload.source || "unknown"}`;
  planRisk.textContent = `risk: ${payload.risk ? payload.risk.highest_risk : "n/a"}`;
  execBtn.disabled = !currentPlan;
}

planBtn.addEventListener("click", async () => {
  const prompt = promptInput.value.trim();
  if (!prompt) {
    log("prompt is empty");
    return;
  }
  planBtn.disabled = true;
  try {
    const res = await fetch("/assistant/plan", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ prompt, mode: "editor" })
    });
    const json = await res.json();
    if (!res.ok) {
      log(`plan error: ${json.error || "unknown error"}`);
      return;
    }
    renderPlan(json);
    log(`plan generated (${json.source})`);
  } catch (error) {
    log(`plan error: ${error.message}`);
  } finally {
    planBtn.disabled = false;
  }
});

execBtn.addEventListener("click", async () => {
  if (!currentPlan) {
    log("no plan to execute");
    return;
  }
  execBtn.disabled = true;
  try {
    const res = await fetch("/assistant/execute", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        plan: currentPlan,
        risk: currentRisk,
        allow_high_risk: currentRisk ? currentRisk.highest_risk !== "high" : true
      })
    });
    const json = await res.json();
    if (!res.ok) {
      log(`execute blocked: ${json.error || "unknown error"}`);
      return;
    }
    log("plan executed");
    planJson.textContent = JSON.stringify(json, null, 2);
  } catch (error) {
    log(`execute error: ${error.message}`);
  } finally {
    execBtn.disabled = false;
  }
});

refreshHealth();
window.setInterval(refreshHealth, 10000);
