# minirend – Cross‑Platform Web App Runtime

> ⚠️ **EXPERIMENTAL** – This project is under active development and APIs may change without notice. Use at your own risk.

---
## The tiny web ecosystem
### [Minibun](https://github.com/TolinSimpson/Minibun) - A tiny javascript bundler implementation.
### [minima-js](https://github.com/TolinSimpson/minima-js) - A tiny, fully-featured, zero-dependency JavaScript framework. 
### [LLM Tiny Web](https://github.com/TolinSimpson/LLM-Agent-Tiny-Web-Instructions) - Optimized LLM instructions for writing tiny web code
---

You build **one native binary** that:

- Opens a desktop window on major OSes  
- Loads your HTML/CSS/JS app  
- Provides a DOM‑like API, `localStorage`, `fetch` (basic), and WebGL hooks  
- Bundle custom C libraries with JS interop

## Requirements

- Git  
- Node.js (for the configurator UI)  
- PowerShell (Windows)  
- A C toolchain is bundled via Cosmopolitan's `cosmocc` (downloaded automatically)  

## Dependencies 

- **Cosmopolitan Libc** – build‑once, run‑anywhere C runtime  
- **Sokol** – cross‑platform graphics abstraction (handles D3D11, Metal, OpenGL, WebGPU backends automatically)  
- **QuickJS** – embeddable JavaScript engine  
- **Lexbor** – fast HTML/CSS parsing and DOM construction  

### Feature Comparison

| Feature | minirend | Neutralino.js | Electron | Wails |
|---------|----------|---------------|----------|-------|
| **Rendering backend** | Custom C DOM | OS WebView | Chromium + Node.js | OS WebView + Go |
| **App size / footprint** | Tiny | Tiny | Large | Medium |
| **Win, Mac, Linux Builds** | ✅ | ✅ | ✅ | ✅ |
| **Built‑in dev tooling** | ✅ | ✅ | ✅ | ✅ |
| **JS, HTML, CSS Support** | ✅ + C | ✅ | ✅ | ✅ + Go |
| **Single Cross-platform Binary** | ✅ | ❌ | ❌ | ❌ |
| **Custom Browser** | ✅ | ❌ | ❌ | ❌ |
| **Native UI integration** | ❌ (wip) | ✅ (basic) | ✅ | ✅ |

## Folder Layout

```
minirend/
├── app/              # Your web app (HTML/JS/CSS)
├── build_scripts/    # Build scripts & configurator UI
│   └── configurator/ # Web-based build configurator
├── src/              # Runtime source code
│   ├── minirend.h    # Public API header
│   └── platform/     # Platform-specific implementations
├── third_party/      # Dependencies (auto-downloaded)
├── minirend.js       # Development server
└── Makefile
```

## Quick Start

### 1. Start the configurator

```bash
node minirend.js
```

This opens the configurator UI in your browser at `http://localhost:4173/`.

### Or build from command line

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

- `dist/minirend` (Unix) or `dist/minirend.exe` (Windows) – the runtime executable  
- `dist/app/` – copied from your root `app/` directory  

You can distribute `dist/` as a portable package: run the binary next to the
`app/` folder on any supported OS.

## Run Your App (Dev Mode)

For rapid iteration you can also run directly from the project root (after building):

```bash
./minirend                 # Unix/macOS/Git Bash
./minirend app/index.html app/main.js
```

On Windows PowerShell/cmd, run `minirend.exe` from the build directory.

## Status

minirend is **experimental** and intentionally small:

- DOM APIs are minimal and focused on common UI use cases  
- WebGL is exposed via Sokol's cross-platform graphics abstraction  
- Networking and storage are thin wrappers over the host OS via Cosmopolitan  

See `src/` and `Makefile` for deeper integration details.