# Minrend – Cross‑Platform Web App Runtime

Minrend is a **minimal Electron‑style runtime** built in C with:

- **Cosmopolitan Libc** – build‑once, run‑anywhere C runtime  
- **Sokol** – cross‑platform graphics (D3D11 on Windows, OpenGL on Linux)  
- **QuickJS** – embeddable JavaScript engine  
- **Modest** – HTML/CSS rendering (planned integration)  

You build **one native binary** that:

- Opens a desktop window on major OSes  
- Loads your HTML/CSS/JS app  
- Provides a DOM‑like API, `localStorage`, `fetch` (basic), and WebGL hooks  

## Requirements

- Git  
- Node.js (for the configurator UI)  
- PowerShell (Windows)  
- A C toolchain is bundled via Cosmopolitan's `cosmocc` (downloaded automatically)  

## Folder Layout

```
Minrend/
├── app/              # Your web app (HTML/JS/CSS)
├── build_scripts/    # Build scripts & configurator UI
│   └── configurator/ # Web-based build configurator
├── src/              # Runtime source code
│   ├── minrend.h     # Public API header
│   └── platform/     # Platform-specific implementations
├── third_party/      # Dependencies (auto-downloaded)
├── minrend.js        # Development server
└── Makefile
```

## Quick Start

### 1. Start the configurator

```bash
node minrend.js
```

This opens the configurator UI in your browser at `http://localhost:4173/`.

### 2. Build from command line

```bash
# Unix/macOS/Git Bash
./build_scripts/build

# Windows
build_scripts\build.cmd
```

## Create Your App

1. Put your web sources in `app/`:
   - `app/index.html` – main HTML file  
   - `app/main.js` – main JavaScript entry  

2. Use browser‑style JS APIs provided by the runtime:
   - `console.log`, `localStorage`, `sessionStorage`  
   - `requestAnimationFrame`, `performance.now`  
   - `fetch` (basic `http://` support)  

## Build Output

After building:

- `dist/minrend` (Unix) or `dist/minrend.exe` (Windows) – the runtime executable  
- `dist/app/` – copied from your root `app/` directory  

You can distribute `dist/` as a portable package: run the binary next to the
`app/` folder on any supported OS.

## Run Your App (Dev Mode)

For rapid iteration you can also run directly from the project root (after building):

```bash
./minrend                 # Unix/macOS/Git Bash
./minrend app/index.html app/main.js
```

On Windows PowerShell/cmd, run `minrend.exe` from the build directory.

## Status

Minrend is **experimental** and intentionally small:

- DOM APIs are minimal and focused on common UI + three.js use cases  
- WebGL is exposed as a thin layer over OpenGL ES‑style calls and will evolve  
- Networking and storage are thin wrappers over the host OS via Cosmopolitan  

See `src/` and `Makefile` for deeper integration details.
