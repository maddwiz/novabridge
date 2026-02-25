import { describe, expect, it } from "vitest";
import { isPlan } from "./validate";

describe("isPlan", () => {
  it("accepts a fully-typed valid plan", () => {
    const plan = {
      plan_id: "abc-123",
      mode: "editor",
      steps: [
        {
          action: "spawn",
          params: {
            type: "PointLight",
            label: "LaunchSmokeLight",
            transform: {
              location: [0, 0, 200],
              rotation: [0, 0, 0],
              scale: [1, 1, 1]
            },
            props: { Intensity: 5000 }
          }
        },
        {
          action: "set",
          params: { target: "LaunchSmokeLight", props: { Intensity: 12000 } }
        },
        {
          action: "screenshot",
          params: { width: 1280, height: 720, format: "raw" }
        }
      ]
    };

    expect(isPlan(plan)).toBe(true);
  });

  it("rejects malformed steps", () => {
    const malformed = {
      plan_id: "abc-123",
      mode: "editor",
      steps: [
        {
          action: "spawn",
          params: {
            type: "PointLight",
            transform: { location: [0, 0] }
          }
        }
      ]
    };

    expect(isPlan(malformed)).toBe(false);
  });

  it("rejects empty plan id", () => {
    const malformed = {
      plan_id: "",
      mode: "editor",
      steps: []
    };

    expect(isPlan(malformed)).toBe(false);
  });
});

