#!/usr/bin/env node

/**
 * Minrend configurator server
 * ---------------------------
 * Small local HTTP server that:
 *   - Serves the configurator UI from /configurator
 *   - Exposes POST /api/build to:
 *       * write build.config in the repo root
 *       * run scripts/build_with_config.(sh|cmd)
 *       * return build stdout/stderr + exit code as JSON
 *
 * Usage:
 *   node scripts/config_server.js
 *   (then open http://localhost:4173/ in your browser)
 */

const http = require("http");
const fs = require("fs");
const path = require("path");
const { spawn } = require("child_process");

const ROOT = path.resolve(__dirname, "..");
const PUBLIC_DIR = path.join(ROOT, "configurator");
const PORT = process.env.PORT || 4173;

function sendCors(res) {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");
}

function sendJson(res, statusCode, data) {
  const body = JSON.stringify(data);
  res.statusCode = statusCode;
  res.setHeader("Content-Type", "application/json; charset=utf-8");
  sendCors(res);
  res.end(body);
}

function handleMeta(req, res) {
  if (req.method === "OPTIONS") {
    res.statusCode = 204;
    sendCors(res);
    return res.end();
  }
  if (req.method !== "GET") {
    return sendJson(res, 405, { ok: false, error: "Method not allowed" });
  }

  const platform = process.platform;
  let osLabel = platform;
  if (platform === "win32") osLabel = "Windows";
  else if (platform === "darwin") osLabel = "macOS";
  else osLabel = "Linux/Unix";

  return sendJson(res, 200, {
    ok: true,
    platform,
    osLabel,
  });
}

function handleBootstrap(req, res) {
  if (req.method === "OPTIONS") {
    res.statusCode = 204;
    sendCors(res);
    return res.end();
  }

  if (req.method !== "POST") {
    return sendJson(res, 405, { ok: false, error: "Method not allowed" });
  }

  // Check if client wants streaming output
  const acceptHeader = req.headers.accept || "";
  const wantsStreaming = acceptHeader.includes("text/event-stream") || 
                         req.url.includes("?stream=true");

  const isWindows = process.platform === "win32";
  const script = path.join(ROOT, "scripts", "bootstrap_deps");

  const child = spawn(script, [], {
    cwd: ROOT,
    shell: isWindows,
    env: isWindows
      ? { ...process.env, MINREND_BOOTSTRAP_ONLY: "1" }
      : process.env,
  });

  let stdout = "";
  let stderr = "";

  // If streaming, send chunks as they arrive
  if (wantsStreaming) {
    res.statusCode = 200;
    res.setHeader("Content-Type", "text/event-stream; charset=utf-8");
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("Connection", "keep-alive");
    sendCors(res);

    const sendChunk = (type, data) => {
      const json = JSON.stringify({ type, data, timestamp: Date.now() });
      res.write(`data: ${json}\n\n`);
    };

    child.stdout.on("data", (chunk) => {
      const text = chunk.toString("utf8");
      stdout += text;
      sendChunk("stdout", text);
    });

    child.stderr.on("data", (chunk) => {
      const text = chunk.toString("utf8");
      stderr += text;
      sendChunk("stderr", text);
    });

    child.on("error", (err) => {
      sendChunk("error", `Failed to start bootstrap script: ${err.message}`);
      sendChunk("done", { ok: false, exitCode: -1, stdout, stderr });
      res.end();
    });

    child.on("close", (code) => {
      sendChunk("done", {
        ok: code === 0,
        exitCode: code,
        stdout,
        stderr,
      });
      res.end();
    });
  } else {
    // Non-streaming: collect all output and send at end
    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString("utf8");
    });

    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString("utf8");
    });

    child.on("error", (err) => {
      sendJson(res, 500, {
        ok: false,
        error: `Failed to start bootstrap script: ${err.message}`,
        stdout,
        stderr,
      });
    });

    child.on("close", (code) => {
      sendJson(res, 200, {
        ok: code === 0,
        exitCode: code,
        stdout,
        stderr,
      });
    });
  }
}

function handleBuild(req, res) {
  if (req.method === "OPTIONS") {
    res.statusCode = 204;
    sendCors(res);
    return res.end();
  }

  if (req.method !== "POST") {
    return sendJson(res, 405, { ok: false, error: "Method not allowed" });
  }

  let body = "";
  req.on("data", (chunk) => {
    body += chunk.toString("utf8");
  });

  req.on("end", () => {
    let payload = {};
    try {
      if (body.trim()) {
        payload = JSON.parse(body);
      }
    } catch (err) {
      return sendJson(res, 400, { ok: false, error: "Invalid JSON body" });
    }

    const appDir = (payload.appDir || "app").trim() || "app";
    const outName = (payload.outName || "minrend").trim() || "minrend";

    const configText = [
      "# Minrend build config",
      "# Generated by configurator server",
      "",
      `APP_DIR=${appDir}`,
      `OUT_NAME=${outName}`,
      "",
    ].join("\n");

    const configPath = path.join(ROOT, "build.config");
    fs.writeFile(configPath, configText, "utf8", (writeErr) => {
      if (writeErr) {
        return sendJson(res, 500, {
          ok: false,
          error: `Failed to write build.config: ${writeErr.message}`,
        });
      }

      const script = path.join(ROOT, "scripts", "build_with_config");

      const child = spawn(script, [], {
        cwd: ROOT,
        shell: process.platform === "win32",
      });

      let stdout = "";
      let stderr = "";

      child.stdout.on("data", (chunk) => {
        stdout += chunk.toString("utf8");
      });

      child.stderr.on("data", (chunk) => {
        stderr += chunk.toString("utf8");
      });

      child.on("error", (err) => {
        sendJson(res, 500, {
          ok: false,
          error: `Failed to start build script: ${err.message}`,
          stdout,
          stderr,
        });
      });

      child.on("close", (code) => {
        sendJson(res, 200, {
          ok: code === 0,
          exitCode: code,
          stdout,
          stderr,
        });
      });
    });
  });
}

function serveStatic(req, res) {
  let urlPath = req.url.split("?")[0];
  if (urlPath === "/") {
    urlPath = "/index.html";
  }

  const filePath = path.join(PUBLIC_DIR, urlPath);
  if (!filePath.startsWith(PUBLIC_DIR)) {
    res.statusCode = 403;
    return res.end("Forbidden");
  }

  fs.stat(filePath, (err, stats) => {
    if (err || !stats.isFile()) {
      res.statusCode = 404;
      return res.end("Not found");
    }

    const ext = path.extname(filePath).toLowerCase();
    const typeMap = {
      ".html": "text/html; charset=utf-8",
      ".js": "text/javascript; charset=utf-8",
      ".css": "text/css; charset=utf-8",
    };

    res.statusCode = 200;
    res.setHeader("Content-Type", typeMap[ext] || "application/octet-stream");
    fs.createReadStream(filePath).pipe(res);
  });
}

const server = http.createServer((req, res) => {
  if (req.url.startsWith("/api/meta")) {
    return handleMeta(req, res);
  }
  if (req.url.startsWith("/api/bootstrap")) {
    return handleBootstrap(req, res);
  }
  if (req.url.startsWith("/api/build")) {
    return handleBuild(req, res);
  }
  return serveStatic(req, res);
});

server.listen(PORT, () => {
  // eslint-disable-next-line no-console
  console.log(`Minrend configurator server running at http://localhost:${PORT}/`);
});


