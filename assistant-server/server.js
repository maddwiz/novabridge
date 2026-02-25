"use strict";

const fs = require("fs");
const path = require("path");
const http = require("http");
const { URL } = require("url");
const {
  generatePlan,
  executePlan,
  getCatalogSnapshot,
  getHealthSnapshot
} = require("./assistant_engine");

const PORT_RAW = Number.parseInt(process.env.NOVABRIDGE_ASSISTANT_PORT || "30016", 10);
const PORT = Number.isFinite(PORT_RAW) && PORT_RAW > 0 ? PORT_RAW : 30016;
const STATIC_DIR = path.join(__dirname, "public");
const DEFAULT_DEPS = {
  generatePlan,
  executePlan,
  getCatalogSnapshot,
  getHealthSnapshot
};

function writeJson(res, statusCode, payload) {
  const body = Buffer.from(JSON.stringify(payload, null, 2));
  res.writeHead(statusCode, {
    "Content-Type": "application/json",
    "Content-Length": String(body.length),
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Headers": "Content-Type, X-API-Key",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS"
  });
  res.end(body);
}

function writeText(res, statusCode, text, contentType = "text/plain; charset=utf-8") {
  const body = Buffer.from(text);
  res.writeHead(statusCode, {
    "Content-Type": contentType,
    "Content-Length": String(body.length),
    "Access-Control-Allow-Origin": "*"
  });
  res.end(body);
}

function readRequestJson(req) {
  return new Promise((resolve, reject) => {
    let raw = "";
    req.on("data", (chunk) => {
      raw += chunk.toString("utf-8");
      if (raw.length > 2 * 1024 * 1024) {
        reject(new Error("Request body too large"));
      }
    });
    req.on("end", () => {
      if (!raw.trim()) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(raw));
      } catch (_error) {
        reject(new Error("Invalid JSON body"));
      }
    });
    req.on("error", (error) => reject(error));
  });
}

function resolveStudioPath(pathname) {
  if (pathname === "/nova/studio" || pathname === "/nova/studio/") {
    return path.join(STATIC_DIR, "index.html");
  }
  if (pathname.startsWith("/nova/studio/")) {
    const relative = pathname.replace("/nova/studio/", "");
    const filePath = path.normalize(path.join(STATIC_DIR, relative));
    if (filePath.startsWith(STATIC_DIR)) {
      return filePath;
    }
  }
  return null;
}

function guessContentType(filePath) {
  if (filePath.endsWith(".html")) return "text/html; charset=utf-8";
  if (filePath.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (filePath.endsWith(".css")) return "text/css; charset=utf-8";
  if (filePath.endsWith(".json")) return "application/json; charset=utf-8";
  return "application/octet-stream";
}

function createRequestHandler(deps = DEFAULT_DEPS) {
  return async function handleRequest(req, res) {
    if (!req.url) {
      writeJson(res, 400, { status: "error", error: "Missing URL" });
      return;
    }

    if (req.method === "OPTIONS") {
      res.writeHead(204, {
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Headers": "Content-Type, X-API-Key",
        "Access-Control-Allow-Methods": "GET, POST, OPTIONS"
      });
      res.end();
      return;
    }

    const url = new URL(req.url, `http://${req.headers.host || "localhost"}`);
    const pathname = url.pathname;

    if (pathname === "/") {
      res.writeHead(302, { Location: "/nova/studio" });
      res.end();
      return;
    }

    const studioPath = resolveStudioPath(pathname);
    if (studioPath) {
      if (!fs.existsSync(studioPath) || !fs.statSync(studioPath).isFile()) {
        writeText(res, 404, "Not found");
        return;
      }
      const file = fs.readFileSync(studioPath);
      writeText(res, 200, file, guessContentType(studioPath));
      return;
    }

    try {
      if (pathname === "/assistant/health" && req.method === "GET") {
        const payload = await deps.getHealthSnapshot();
        writeJson(res, 200, payload);
        return;
      }

      if (pathname === "/assistant/catalog" && req.method === "GET") {
        const payload = await deps.getCatalogSnapshot();
        writeJson(res, 200, payload);
        return;
      }

      if (pathname === "/assistant/plan" && req.method === "POST") {
        const body = await readRequestJson(req);
        const prompt = typeof body.prompt === "string" ? body.prompt : "";
        const mode = body.mode === "runtime" ? "runtime" : "editor";
        if (!prompt.trim()) {
          writeJson(res, 400, { status: "error", error: "Missing prompt" });
          return;
        }
        const payload = await deps.generatePlan({ prompt, mode });
        writeJson(res, 200, payload);
        return;
      }

      if (pathname === "/assistant/execute" && req.method === "POST") {
        const body = await readRequestJson(req);
        const plan = body.plan && typeof body.plan === "object" ? body.plan : null;
        if (!plan) {
          writeJson(res, 400, { status: "error", error: "Missing plan object" });
          return;
        }

        const allowHighRisk = body.allow_high_risk === true;
        const risk = body.risk && typeof body.risk === "object" ? body.risk : null;
        if (!allowHighRisk && risk && risk.highest_risk === "high") {
          writeJson(res, 409, {
            status: "blocked",
            error: "High-risk plan requires allow_high_risk=true",
            risk
          });
          return;
        }

        const payload = await deps.executePlan(plan);
        writeJson(res, 200, payload);
        return;
      }

      writeJson(res, 404, { status: "error", error: "Not found" });
    } catch (error) {
      writeJson(res, 500, {
        status: "error",
        error: error instanceof Error ? error.message : String(error)
      });
    }
  };
}

function createServer(deps = DEFAULT_DEPS) {
  const handler = createRequestHandler(deps);
  return http.createServer((req, res) => {
    handler(req, res);
  });
}

function startServer(port = PORT, host = "127.0.0.1", deps = DEFAULT_DEPS) {
  const server = createServer(deps);
  server.listen(port, host, () => {
    process.stdout.write(
      `[assistant-server] listening on http://${host}:${port} (studio: /nova/studio)\n`
    );
  });
  return server;
}

if (require.main === module) {
  startServer();
}

module.exports = {
  createRequestHandler,
  createServer,
  startServer,
  readRequestJson,
  resolveStudioPath,
  guessContentType
};
