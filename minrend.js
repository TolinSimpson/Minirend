#!/usr/bin/env node

/**
 * Minrend - Development Server & Build Configurator
 * --------------------------------------------------
 * Run with: node minrend.js
 * 
 * This starts the configurator UI and automatically opens your browser.
 */

const http = require("http");
const fs = require("fs");
const path = require("path");
const { spawn, exec } = require("child_process");

const ROOT = __dirname;
const PUBLIC_DIR = path.join(ROOT, "build_scripts", "configurator");
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

/**
 * Check the status of dependencies
 */
function handleStatus(req, res) {
  if (req.method === "OPTIONS") {
    res.statusCode = 204;
    sendCors(res);
    return res.end();
  }
  if (req.method !== "GET") {
    return sendJson(res, 405, { ok: false, error: "Method not allowed" });
  }

  const cosmoPath = path.join(ROOT, "third_party", "cosmocc", "bin", "cosmocc");
  const quickjsPath = path.join(ROOT, "third_party", "quickjs");
  const modestPath = path.join(ROOT, "third_party", "modest");
  const sokolPath = path.join(ROOT, "third_party", "sokol");
  const platformPath = path.join(ROOT, "src", "platform");

  const status = {
    cosmocc: fs.existsSync(cosmoPath) || fs.existsSync(cosmoPath + ".exe"),
    quickjs: fs.existsSync(quickjsPath),
    modest: fs.existsSync(modestPath),
    sokol: fs.existsSync(sokolPath),
    platform: fs.existsSync(platformPath),
  };

  status.ready = status.cosmocc && status.quickjs && status.sokol && status.platform;

  return sendJson(res, 200, { ok: true, ...status });
}

/**
 * Stream output helper for Server-Sent Events
 */
function setupSSE(res) {
  res.statusCode = 200;
  res.setHeader("Content-Type", "text/event-stream; charset=utf-8");
  res.setHeader("Cache-Control", "no-cache");
  res.setHeader("Connection", "keep-alive");
  sendCors(res);

  return {
    send(type, data) {
      const json = JSON.stringify({ type, data, timestamp: Date.now() });
      res.write(`data: ${json}\n\n`);
    },
    end() {
      res.end();
    }
  };
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
                         req.url.includes("stream=true");

  const isWindows = process.platform === "win32";
  const script = path.join(ROOT, "build_scripts", "bootstrap_deps");
  
  const env = { ...process.env, MINREND_BOOTSTRAP_ONLY: "1" };

  let child;
  
  if (isWindows) {
    const bashPath = findBash();
    if (!bashPath) {
      return sendJson(res, 500, {
        ok: false,
        error: "Could not find bash. Please install Git for Windows from https://git-scm.com/download/win",
      });
    }
    
    child = spawn(bashPath, [script], {
      cwd: ROOT,
      env,
    });
  } else {
    child = spawn(script, [], {
      cwd: ROOT,
      env,
    });
  }

  let stdout = "";
  let stderr = "";

  if (wantsStreaming) {
    const sse = setupSSE(res);

    child.stdout.on("data", (chunk) => {
      const text = chunk.toString("utf8");
      stdout += text;
      sse.send("stdout", text);
    });

    child.stderr.on("data", (chunk) => {
      const text = chunk.toString("utf8");
      stderr += text;
      sse.send("stderr", text);
    });

    child.on("error", (err) => {
      sse.send("error", `Failed to start bootstrap script: ${err.message}`);
      sse.send("done", { ok: false, exitCode: -1, stdout, stderr });
      sse.end();
    });

    child.on("close", (code) => {
      sse.send("done", { ok: code === 0, exitCode: code, stdout, stderr });
      sse.end();
    });
  } else {
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

/**
 * Find a bash executable on Windows
 */
function findBash() {
  const possiblePaths = [
    "C:\\Program Files\\Git\\bin\\bash.exe",
    "C:\\Program Files (x86)\\Git\\bin\\bash.exe",
    "C:\\msys64\\usr\\bin\\bash.exe",
    "C:\\cygwin64\\bin\\bash.exe",
    "C:\\cygwin\\bin\\bash.exe",
  ];
  
  for (const p of possiblePaths) {
    if (fs.existsSync(p)) {
      return p;
    }
  }
  
  // Try PATH
  const { execSync } = require("child_process");
  try {
    const result = execSync("where bash", { encoding: "utf8", stdio: ["pipe", "pipe", "ignore"] });
    const firstLine = result.trim().split("\n")[0];
    if (firstLine && fs.existsSync(firstLine.trim())) {
      return firstLine.trim();
    }
  } catch (e) {
    // bash not in PATH
  }
  
  return null;
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

  // Check if client wants streaming output
  const acceptHeader = req.headers.accept || "";
  const wantsStreaming = acceptHeader.includes("text/event-stream") || 
                         req.url.includes("stream=true");

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
      "# Generated by minrend.js configurator",
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

      const isWindows = process.platform === "win32";
      const script = path.join(ROOT, "build_scripts", "build");

      const env = { ...process.env };

      let child;
      
      if (isWindows) {
        // On Windows, find bash and run the script directly
        const bashPath = findBash();
        if (!bashPath) {
          return sendJson(res, 500, {
            ok: false,
            error: "Could not find bash. Please install Git for Windows from https://git-scm.com/download/win",
          });
        }
        
        child = spawn(bashPath, [script], {
          cwd: ROOT,
          env,
        });
      } else {
        child = spawn(script, [], {
          cwd: ROOT,
          env,
        });
      }

      let stdout = "";
      let stderr = "";

      if (wantsStreaming) {
        const sse = setupSSE(res);

        child.stdout.on("data", (chunk) => {
          const text = chunk.toString("utf8");
          stdout += text;
          sse.send("stdout", text);
        });

        child.stderr.on("data", (chunk) => {
          const text = chunk.toString("utf8");
          stderr += text;
          sse.send("stderr", text);
        });

        child.on("error", (err) => {
          sse.send("error", `Failed to start build script: ${err.message}`);
          sse.send("done", { ok: false, exitCode: -1, stdout, stderr });
          sse.end();
        });

        child.on("close", (code) => {
          sse.send("done", { ok: code === 0, exitCode: code, stdout, stderr });
          sse.end();
        });
      } else {
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
      }
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
      ".json": "application/json; charset=utf-8",
      ".png": "image/png",
      ".svg": "image/svg+xml",
    };

    res.statusCode = 200;
    res.setHeader("Content-Type", typeMap[ext] || "application/octet-stream");
    fs.createReadStream(filePath).pipe(res);
  });
}

/**
 * Open the browser to the given URL
 */
function openBrowser(url) {
  const platform = process.platform;
  let cmd;
  
  if (platform === "win32") {
    cmd = `start "" "${url}"`;
  } else if (platform === "darwin") {
    cmd = `open "${url}"`;
  } else {
    // Linux/Unix - try common browsers
    cmd = `xdg-open "${url}" || sensible-browser "${url}" || x-www-browser "${url}"`;
  }
  
  exec(cmd, (err) => {
    if (err) {
      console.log(`  (Could not auto-open browser: ${err.message})`);
      console.log(`  Please open ${url} manually`);
    }
  });
}

const server = http.createServer((req, res) => {
  if (req.url.startsWith("/api/meta")) {
    return handleMeta(req, res);
  }
  if (req.url.startsWith("/api/status")) {
    return handleStatus(req, res);
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
  const url = `http://localhost:${PORT}/`;
  console.log();
  console.log("  ┌─────────────────────────────────────────┐");
  console.log("  │                                         │");
  console.log("  │   Minrend Development Server            │");
  console.log("  │                                         │");
  console.log(`  │   Local:   ${url.padEnd(27)}│`);
  console.log("  │                                         │");
  console.log("  └─────────────────────────────────────────┘");
  console.log();
  
  // Auto-open browser
  openBrowser(url);
});

