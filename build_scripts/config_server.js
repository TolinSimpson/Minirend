/**
 * minirend Configurator Server
 * 
 * Serves the configurator UI and handles build requests.
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');
const os = require('os');

const PORT = 3456;
const ROOT = path.resolve(__dirname, '..');
const CONFIGURATOR_DIR = path.join(__dirname, 'configurator');

// MIME types
const MIME_TYPES = {
  '.html': 'text/html',
  '.css': 'text/css',
  '.js': 'application/javascript',
  '.json': 'application/json',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

/**
 * Check if dependencies are present
 */
function checkDependencies() {
  const cosmocc = fs.existsSync(path.join(ROOT, 'third_party', 'cosmocc', 'bin', 'cosmocc')) ||
                  fs.existsSync(path.join(ROOT, 'third_party', 'cosmocc', 'bin', 'cosmocc.exe'));
  const quickjs = fs.existsSync(path.join(ROOT, 'third_party', 'quickjs', 'quickjs.c'));
  const sokol = fs.existsSync(path.join(ROOT, 'third_party', 'sokol', 'sokol_app.h'));
  const shims = fs.existsSync(path.join(ROOT, 'src', 'platform', 'sokol_cosmo.c'));
  
  return {
    cosmocc,
    quickjs,
    sokol,
    shims,
    ready: cosmocc && quickjs && sokol && shims
  };
}

/**
 * Find bash executable
 * 
 * Priority on Windows:
 * 1. WSL (best for cross-platform builds with Linux headers)
 * 2. Git Bash (fallback)
 */
function findBash() {
  if (process.platform === 'win32') {
    // Check if WSL is available (preferred for Linux header support)
    try {
      const { execSync } = require('child_process');
      execSync('wsl --status', { stdio: 'ignore' });
      console.log('[config_server] Using WSL for builds (Linux headers available)');
      return 'wsl';
    } catch (e) {
      // WSL not available, try Git Bash
    }
    
    // Try Git Bash locations
    const locations = [
      'C:\\Program Files\\Git\\bin\\bash.exe',
      'C:\\Program Files (x86)\\Git\\bin\\bash.exe',
      'C:\\Windows\\System32\\bash.exe',
    ];
    for (const loc of locations) {
      if (fs.existsSync(loc)) {
        console.log(`[config_server] Using Git Bash: ${loc}`);
        return loc;
      }
    }
  }
  return 'bash';
}

/**
 * Convert Windows path to WSL path
 */
function toWslPath(winPath) {
  // Convert C:\path\to\dir to /mnt/c/path/to/dir
  return winPath.replace(/^([A-Za-z]):/, (_, drive) => `/mnt/${drive.toLowerCase()}`)
                .replace(/\\/g, '/');
}

/**
 * Run build script
 */
function runBuild(config, onData, onDone) {
  const bash = findBash();
  const isWsl = bash === 'wsl';
  
  // Convert paths for WSL if needed
  const projectRoot = isWsl ? toWslPath(ROOT) : ROOT;
  const buildScript = isWsl 
    ? `${projectRoot}/build-wsl.sh`  // Use WSL-specific build script
    : path.join(ROOT, 'build_scripts', 'build');
  
  // Set environment variables
  const env = {
    ...process.env,
    SOKOL: config.sokol ? '1' : '0',
    APP_DIR: config.appDir || 'app',
    OUT_NAME: config.outName || 'minirend',
  };
  
  console.log(`Running build with SOKOL=${env.SOKOL}, using ${isWsl ? 'WSL' : 'bash'}`);
  
  // Build the command arguments
  let args;
  if (isWsl) {
    // For WSL, pass the script as an argument to bash
    args = ['bash', buildScript];
  } else {
    args = [buildScript];
  }
  
  const child = spawn(bash, args, {
    cwd: ROOT,
    env,
    shell: false,
  });
  
  child.stdout.on('data', (data) => {
    const text = data.toString();
    console.log(text);
    if (onData) onData('stdout', text);
  });
  
  child.stderr.on('data', (data) => {
    const text = data.toString();
    console.error(text);
    if (onData) onData('stderr', text);
  });
  
  child.on('error', (err) => {
    console.error('Build process error:', err);
    if (onData) onData('stderr', `Error: ${err.message}`);
    if (onDone) onDone(1, false);
  });
  
  child.on('close', (code) => {
    console.log(`Build process exited with code ${code}`);
    if (onDone) onDone(code, code === 0);
  });
  
  return child;
}

/**
 * Handle API requests
 */
function handleApi(req, res, pathname) {
  if (pathname === '/api/meta') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      platform: os.platform(),
      arch: os.arch(),
      osLabel: `${os.type()} ${os.release()} (${os.arch()})`,
    }));
    return true;
  }
  
  if (pathname === '/api/status') {
    const status = checkDependencies();
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(status));
    return true;
  }
  
  if (pathname === '/api/build' || pathname.startsWith('/api/build?')) {
    if (req.method !== 'POST') {
      res.writeHead(405, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Method not allowed' }));
      return true;
    }
    
    // Read body
    let body = '';
    req.on('data', chunk => body += chunk);
    req.on('end', () => {
      let config = {};
      try {
        config = JSON.parse(body);
      } catch (e) {
        // Use defaults
      }
      
      // Check if streaming is requested
      const stream = pathname.includes('stream=true') || 
                    req.headers.accept?.includes('text/event-stream');
      
      if (stream) {
        // SSE streaming response
        res.writeHead(200, {
          'Content-Type': 'text/event-stream',
          'Cache-Control': 'no-cache',
          'Connection': 'keep-alive',
        });
        
        runBuild(
          config,
          (type, data) => {
            res.write(`data: ${JSON.stringify({ type, data })}\n\n`);
          },
          (exitCode, ok) => {
            res.write(`data: ${JSON.stringify({ type: 'done', data: { exitCode, ok } })}\n\n`);
            res.end();
          }
        );
      } else {
        // Non-streaming response
        let stdout = '';
        let stderr = '';
        
        runBuild(
          config,
          (type, data) => {
            if (type === 'stdout') stdout += data;
            else stderr += data;
          },
          (exitCode, ok) => {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ stdout, stderr, exitCode, ok }));
          }
        );
      }
    });
    
    return true;
  }
  
  return false;
}

/**
 * Serve static files
 */
function serveStatic(res, pathname) {
  let filePath = pathname === '/' ? 'index.html' : pathname.slice(1);
  filePath = path.join(CONFIGURATOR_DIR, filePath);
  
  // Security: prevent path traversal
  if (!filePath.startsWith(CONFIGURATOR_DIR)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }
  
  const ext = path.extname(filePath);
  const contentType = MIME_TYPES[ext] || 'application/octet-stream';
  
  fs.readFile(filePath, (err, data) => {
    if (err) {
      if (err.code === 'ENOENT') {
        res.writeHead(404);
        res.end('Not found');
      } else {
        res.writeHead(500);
        res.end('Server error');
      }
      return;
    }
    
    res.writeHead(200, { 'Content-Type': contentType });
    res.end(data);
  });
}

/**
 * Request handler
 */
function handleRequest(req, res) {
  const url = new URL(req.url, `http://localhost:${PORT}`);
  const pathname = url.pathname;
  
  console.log(`${req.method} ${pathname}`);
  
  // Handle API routes
  if (pathname.startsWith('/api/')) {
    if (!handleApi(req, res, pathname)) {
      res.writeHead(404, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Not found' }));
    }
    return;
  }
  
  // Serve static files
  serveStatic(res, pathname);
}

// Start server
const server = http.createServer(handleRequest);
server.listen(PORT, () => {
  console.log(`minirend Configurator running at http://localhost:${PORT}`);
  console.log(`Project root: ${ROOT}`);
  console.log(`Configurator UI: ${CONFIGURATOR_DIR}`);
  console.log('');
  console.log('Press Ctrl+C to stop');
});

