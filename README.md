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
- Supports graphic via OpenGL & WebGPU (wip)

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
| **Custom Web Renderer** | ✅ | ❌ | ❌ | ❌ |
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

---

## Feature Completeness

### JavaScript Runtime (QuickJS)

| Feature | Status |
|---------|--------|
| ES6+ syntax / modules | ✅ |
| `console.log/warn/error/info` | ✅ |
| `requestAnimationFrame` / `cancelAnimationFrame` | ✅ |
| `performance.now()` | ✅ |
| `Promise` / async-await | ✅ |
| `setTimeout` / `setInterval` | ❌ |

### HTML Parsing (Lexbor)

| Feature | Status |
|---------|--------|
| Document parsing | ✅ |
| DOM tree construction | ✅ |
| `querySelector` / `querySelectorAll` | ✅ |
| `createElement` / `appendChild` | ✅ |
| `getElementById` | ❌ |
| `innerHTML` / `textContent` | ❌ |
| Attribute get/set | ✅ (native) |

### CSS Parsing (Lexbor)

| Feature | Status |
|---------|--------|
| Stylesheet parsing | ✅ |
| Selector matching | ✅ |
| Inline style reading | ✅ |
| Computed style resolution | ✅ |
| CSS variables | ❌ |
| Media queries | ❌ |

### HTML/CSS Rendering (Clay & stb_truetype)

| Feature | Status |
|---------|--------|
| Box model layout | ✅ |
| Text rendering | ✅ |
| Flex / Grid layout | ✅ |
| Background / borders | ✅ |
| CSS transforms | ✅ |
| DOM-to-texture compositing | ✅ |

### Windowing (Sokol)

| Feature | Status |
|---------|--------|
| Window creation | ✅ |
| High-DPI support | ✅ |
| Fullscreen toggle | ✅ |
| Window resize events | ✅ |
| Mouse cursor control | ✅ |
| Window title / icon | ✅ |
| Clipboard access | ✅ |
| File drop | ✅ |

### Input

| Feature | Status |
|---------|--------|
| Mouse events (click, move, wheel) | ✅ |
| Pointer events | ✅ |
| Keyboard events (keydown/keyup) | ✅ |
| Text input (char events) | ✅ |
| Focus / blur events | ✅ |
| Event bubbling / capture | ✅ |
| Pointer capture | ✅ |
| Touch events | ❌ |
| Gamepad | ❌ |

### Audio

| Feature | Status |
|---------|--------|
| Native audio backend (sokol_audio) | ✅ |
| Web Audio API bindings | ✅ |
| `AudioContext` | ✅ |
| Audio playback | ✅ |
| Audio spatialization | ✅ |

### WebGL 1.0 / 2.0

| Feature | Status |
|---------|--------|
| Context creation (`webgl`/`webgl2`) | ✅ |
| **Buffers** | |
| `createBuffer` / `deleteBuffer` | ✅ |
| `bindBuffer` / `bufferData` / `bufferSubData` | ✅ |
| **Shaders** | |
| `createShader` / `compileShader` / `deleteShader` | ✅ |
| `shaderSource` / `getShaderParameter` / `getShaderInfoLog` | ✅ |
| **Programs** | |
| `createProgram` / `linkProgram` / `useProgram` | ✅ |
| `attachShader` / `detachShader` / `deleteProgram` | ✅ |
| `getProgramParameter` / `getProgramInfoLog` | ✅ |
| **Attributes** | |
| `getAttribLocation` / `bindAttribLocation` | ✅ |
| `enableVertexAttribArray` / `disableVertexAttribArray` | ✅ |
| `vertexAttribPointer` / `vertexAttribIPointer` | ✅ |
| `vertexAttribDivisor` | ✅ |
| **Uniforms** | |
| `getUniformLocation` | ✅ |
| `uniform[1234][if]` | ✅ |
| `uniform[1234]fv` | ✅ |
| `uniformMatrix[234]fv` | ✅ |
| `getActiveUniform` / `getActiveAttrib` | ✅ |
| **Drawing** | |
| `drawArrays` / `drawElements` | ✅ |
| `drawArraysInstanced` / `drawElementsInstanced` | ✅ |
| **VAO (WebGL2)** | |
| `createVertexArray` / `bindVertexArray` / `deleteVertexArray` | ✅ |
| **Textures** | |
| `createTexture` / `bindTexture` / `deleteTexture` | ✅ |
| `activeTexture` | ✅ |
| `texParameteri` / `texParameterf` | ✅ |
| `generateMipmap` | ✅ |
| `texImage2D` / `texSubImage2D` | ✅ |
| `copyTexImage2D` / `copyTexSubImage2D` | ✅ |
| **Framebuffers** | |
| `createFramebuffer` / `bindFramebuffer` / `deleteFramebuffer` | ✅ |
| `framebufferTexture2D` / `framebufferRenderbuffer` | ✅ |
| `checkFramebufferStatus` | ✅ |
| **Renderbuffers** | |
| `createRenderbuffer` / `bindRenderbuffer` / `deleteRenderbuffer` | ✅ |
| `renderbufferStorage` / `renderbufferStorageMultisample` | ✅ |
| **State** | |
| `enable` / `disable` / `isEnabled` | ✅ |
| `viewport` / `scissor` / `depthRange` | ✅ |
| `clearColor` / `clearDepth` / `clearStencil` / `clear` | ✅ |
| `blendFunc` / `blendFuncSeparate` | ✅ |
| `blendEquation` / `blendEquationSeparate` / `blendColor` | ✅ |
| `depthFunc` / `depthMask` | ✅ |
| `cullFace` / `frontFace` | ✅ |
| `colorMask` | ✅ |
| `pixelStorei` | ✅ |
| `stencilFunc` / `stencilOp` / `stencilMask` | ✅ |
| **Read** | |
| `readPixels` | ✅ |
| **Query** | |
| `getParameter` / `getError` | ✅ |
| `getContextAttributes` | ✅ |
| `getSupportedExtensions` / `getExtension` | ✅ |
| **WebGL2 Specific** | |
| Uniform buffer objects (UBO) | ✅ |
| Transform feedback | ✅ |
| Sync objects | ✅ |
| Sampler objects | ✅ |
| Query objects | ✅ |

### WebGPU

| Feature | Status |
|---------|--------|
| `GPUAdapter` / `GPUDevice` | ❌ |
| `GPUBuffer` / `GPUTexture` | ❌ |
| `GPURenderPipeline` / `GPUComputePipeline` | ❌ |
| `GPUCommandEncoder` / `GPURenderPassEncoder` | ❌ |
| `GPUShaderModule` (WGSL) | ❌ |
| `GPUBindGroup` / `GPUBindGroupLayout` | ❌ |
| `GPUQueue` | ❌ |

> **Note:** Sokol's internal WebGPU backend is present but no JavaScript bindings are exposed.

### Native UI Integration (via Sokol)

#### Application Identity & Branding

| Feature | Windows | macOS | Linux | Notes |
|---------|---------|-------|-------|-------|
| Window title bar icon | ✅ | — | ✅ | `sapp_set_icon` |
| Taskbar/dock icon | ✅ | ✅ | ✅ | Runtime-settable |
| Built-in default icon | ✅ | ✅ | ✅ | Rainbow 'S' fallback |
| Favicon (HTML5 target) | ✅ | ✅ | ✅ | Browser tab icon |
| Executable/bundle icon | ❌ | ❌ | ❌ | Requires OS resources at build time |
| Dock badge (count) | — | ❌ | — | macOS-specific, not exposed |
| Taskbar progress | ❌ | — | — | Windows-specific, not exposed |
| Taskbar overlay icon | ❌ | — | — | Windows-specific, not exposed |
| Jump lists | ❌ | — | — | Windows-specific, not exposed |
| Dock menu | — | ❌ | — | macOS-specific, not exposed |

> **Note:** macOS has no window title bar icon; `sapp_set_icon` changes the dock icon instead.

#### Window Management

| Feature | Windows | macOS | Linux | Notes |
|---------|---------|-------|-------|-------|
| Window creation | ✅ | ✅ | ✅ | Single window only |
| Window title | ✅ | ✅ | ✅ | `sapp_set_window_title` |
| Fullscreen toggle | ✅ | ✅ | ✅ | `sapp_toggle_fullscreen` |
| Window resize events | ✅ | ✅ | ✅ | `SAPP_EVENTTYPE_RESIZED` |
| Minimize (iconify) events | ✅ | ✅ | ✅ | `SAPP_EVENTTYPE_ICONIFIED` |
| Restore events | ✅ | ✅ | ✅ | `SAPP_EVENTTYPE_RESTORED` |
| Focus/unfocus events | ✅ | ✅ | ✅ | `SAPP_EVENTTYPE_FOCUSED/UNFOCUSED` |
| Quit request handling | ✅ | ✅ | ✅ | `sapp_request_quit`, `sapp_cancel_quit` |
| Programmatic minimize | ❌ | ❌ | ❌ | Not exposed by Sokol |
| Programmatic maximize | ❌ | ❌ | ❌ | Not exposed by Sokol |
| Window position get/set | ❌ | ❌ | ❌ | Not exposed by Sokol |
| Window size set | ❌ | ❌ | ❌ | Read-only via `sapp_width/height` |
| Multiple windows | ❌ | ❌ | ❌ | Sokol limitation |
| Always-on-top | ❌ | ❌ | ❌ | Not exposed by Sokol |
| Borderless/frameless | ❌ | ❌ | ❌ | Not exposed at runtime |

#### Display & Rendering

| Feature | Windows | macOS | Linux | Notes |
|---------|---------|-------|-------|-------|
| High-DPI support | ✅ | ✅ | ✅ | `sapp_dpi_scale` |
| DPI scale query | ✅ | ✅ | ✅ | Per-frame updates |
| VSync | ✅ | ✅ | ✅ | Configured at startup |
| MSAA sample count | ✅ | ✅ | ✅ | Configured at startup |
| Frame duration query | ✅ | ✅ | ✅ | `sapp_frame_duration` |
| Multi-monitor support | ❌ | ❌ | ❌ | Not exposed by Sokol |

#### Mouse & Cursor

| Feature | Windows | macOS | Linux | Notes |
|---------|---------|-------|-------|-------|
| Mouse click detection | ✅ | ✅ | ✅ | Left, middle, right buttons |
| Mouse move tracking | ✅ | ✅ | ✅ | Absolute coordinates |
| Mouse scroll (wheel) | ✅ | ✅ | ✅ | X and Y axis |
| Show/hide cursor | ✅ | ✅ | ✅ | `sapp_show_mouse` |
| Lock/capture mouse | ✅ | ✅ | ✅ | `sapp_lock_mouse` (FPS-style) |
| Custom cursor type | ✅ | ✅ | ✅ | `sapp_set_mouse_cursor` |
| Custom cursor image | ✅ | ✅ | ✅ | `sapp_bind_mouse_cursor_image` |
| Mouse enter/leave | ✅ | ✅ | ✅ | Window boundary events |

#### Clipboard

| Feature | Windows | macOS | Linux | Notes |
|---------|---------|-------|-------|-------|
| Copy to clipboard | ✅ | ✅ | ✅ | `sapp_set_clipboard_string` |
| Paste from clipboard | ✅ | ✅ | ✅ | `sapp_get_clipboard_string` |
| Clipboard paste event | ✅ | ✅ | ✅ | `SAPP_EVENTTYPE_CLIPBOARD_PASTED` |
| Image clipboard | ❌ | ❌ | ❌ | Text only |

#### File Operations

| Feature | Windows | macOS | Linux | Notes |
|---------|---------|-------|-------|-------|
| Drag & drop files | ✅ | ✅ | ✅ | `SAPP_EVENTTYPE_FILES_DROPPED` |
| Get dropped file paths | ✅ | ✅ | ✅ | `sapp_get_dropped_file_path` |
| Native file dialogs | ❌ | ❌ | ❌ | Not provided |
| Native folder picker | ❌ | ❌ | ❌ | Not provided |

#### Native Controls (WIP)

| Feature | Windows | macOS | Linux | Notes |
|---------|---------|-------|-------|-------|
| Native text input | ❌ | ❌ | ❌ | Scaffolding only |
| Native button | ❌ | ❌ | ❌ | Scaffolding only |
| Native select/dropdown | ❌ | ❌ | ❌ | Scaffolding only |
| Native menus | ❌ | ❌ | ❌ | Not implemented |
| System tray | ❌ | ❌ | ❌ | Not implemented |
| Native message boxes | ❌ | ❌ | ❌ | Not implemented |

#### Platform-Specific Access

| Feature | Windows | macOS | Linux | Notes |
|---------|---------|-------|-------|-------|
| Raw window handle | ✅ (HWND) | ✅ (NSWindow) | ✅ (X11 Window) | For native extensions |
| D3D11 device access | ✅ | — | — | `sapp_d3d11_get_device` |
| Metal device access | — | ✅ | — | `sapp_metal_get_device` |
| OpenGL context | ✅ | ✅ | ✅ | `sapp_gl_get_framebuffer` |

> **Note:** Native UI controls are currently scaffolding (`src/platform/native_ui.h`). Future versions may map HTML elements to real OS widgets.