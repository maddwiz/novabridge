import type { Config } from "tailwindcss";

const config: Config = {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      boxShadow: {
        glow: "0 0 0 1px rgba(98, 220, 255, 0.35), 0 0 35px rgba(122, 97, 255, 0.2)"
      }
    }
  },
  plugins: []
};

export default config;
