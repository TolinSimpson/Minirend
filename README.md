Minrend – Cross‑Platform Web App Runtime
========================================

Minrend is a **minimal Electron‑style runtime** built in C with:

- Cosmopolitan Libc – build‑once, run‑anywhere C runtime  
- SDL2 – cross‑platform window + OpenGL context  
- QuickJS – embeddable JavaScript engine  
- Modest – HTML/CSS rendering (planned integration)  

You build **one native binary** that:

- Opens a desktop window on major OSes  
- Loads your HTML/CSS/JS app  
- Provides a DOM‑like API, `localStorage`, `fetch` (basic), and WebGL hooks  

### Requirements

- Git  
- PowerShell (Windows)  
- A C toolchain is bundled via Cosmopolitan’s `cosmocc` (downloaded automatically)  
- SDL2 + OpenGL development libraries on your platform:
  - Linux: `libsdl2-dev`, `mesa-dev` (or distro equivalents)  
  - macOS: Homebrew `sdl2` + system OpenGL  

### Folder layout

- `src/` – Minrend runtime (engine) C sources  
- `include/` – public headers  
- `third_party/` – QuickJS, Modest (cloned automatically)  
- `app/` – **your** web app (HTML/JS/CSS)  
  - Default entries: `app/index.html`, `app/main.js`  
- `scripts/` – helper scripts (`bootstrap_deps`, `build`)  
- `dist/` – build output (runtime executable + bundled `app/`)  

### Install & bootstrap

1. Clone this repository:

```bash
git clone <your-repo-url> minrend
cd minrend
```

2. (Optional) Manually bootstrap toolchains and deps:

```bash
./scripts/bootstrap_deps
```

> On Windows, you can also use `scripts\bootstrap_deps` or `scripts\bootstrap_deps.cmd`.
> The unified scripts work on both Windows and Unix.

### Create your app

1. Put your web sources in `app/`:
   - `app/index.html` – main HTML file  
   - `app/main.js` – main JavaScript entry  

2. Use browser‑style JS APIs provided by the runtime:
   - `console.log`, `localStorage`, `sessionStorage`  
   - `requestAnimationFrame`, `performance.now`  
   - `fetch` (basic `http://` support)  

### Build the runtime + app

- **Unix/macOS:**

```bash
./scripts/build
```

- **Windows:**

```bat
scripts\build
```

or

```bat
scripts\build.cmd
```

> The unified `scripts/build` script works on both Windows and Unix.

What you get:

- `dist/minrend` or `dist/minrend.exe` – the Minrend runtime executable  
- `dist/app/` – copied from your root `app/` directory  

You can distribute `dist/` as a portable package: run the binary next to the
`app/` folder on any supported OS.

### Run your app from source (dev)

For rapid iteration you can also run directly from the project root (after building):

```bash
./minrend                 # Unix/macOS/Git Bash
./minrend app/index.html app/main.js
```

On Windows PowerShell/cmd, run `minrend.exe` from the build directory after
using `scripts\build.cmd`.

### Status

Minrend is **experimental** and intentionally small:

- DOM APIs are minimal and focused on common UI + three.js use cases  
- WebGL is exposed as a thin layer over OpenGL ES‑style calls and will evolve  
- Networking and storage are thin wrappers over the host OS via Cosmopolitan  

See `src/` and `Makefile` for deeper integration details.
