# Minrend Architecture

## The Challenge

Creating a truly portable GUI application that runs from a single executable on Windows, Linux, and macOS is fundamentally difficult because:

1. **GUI APIs are platform-specific**: Windows uses Win32, Linux uses X11/Wayland, macOS uses Cocoa
2. **SDL2 abstracts these**, but SDL2 itself is a platform-specific library
3. **Cosmopolitan** can load SDL2 dynamically, but SDL2's internal Win32 calls crash under Cosmopolitan's Windows emulation

## Recommended Architectures

### Architecture A: Web-Based Renderer (Recommended for Portability)

```
┌─────────────────────────────────────────┐
│           Minrend APE Binary            │
│  ┌───────────────────────────────────┐  │
│  │  QuickJS Engine                   │  │
│  │  HTML/CSS Parser (Modest)         │  │
│  │  HTTP Server (built-in)           │  │
│  └───────────────────────────────────┘  │
│                    │                    │
│            localhost:8080               │
└─────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────┐
│         System Web Browser              │
│  (Chrome, Firefox, Safari, Edge)        │
│  ┌───────────────────────────────────┐  │
│  │  WebGL / Canvas Rendering         │  │
│  │  Native window management         │  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

**Advantages:**
- Single APE binary works on ALL platforms
- Full WebGL/Canvas support via browser
- Native windowing handled by browser
- Users can use fullscreen, resize, etc.

**Usage:**
```bash
./minrend.exe           # Starts server, opens browser
./minrend.exe --port 3000   # Custom port
./minrend.exe --headless    # Server only, don't open browser
```

### Architecture B: Native Builds Per Platform

```
minrend-windows.exe     (MSVC/MinGW build, uses SDL2.dll)
minrend-linux           (GCC build, links libSDL2)
minrend-macos           (Clang build, links SDL2.framework)
minrend.exe             (Cosmopolitan APE for CLI/server features)
```

**Advantages:**
- Full native performance
- Direct GPU access
- No browser dependency

**Disadvantages:**
- Multiple binaries to distribute
- Build complexity

### Architecture C: Electron-style (WebView)

Use native WebView instead of full browser:
- Windows: WebView2 (Edge/Chromium)
- Linux: WebKitGTK
- macOS: WKWebView

More complex but gives native-feeling windowed app.

## Current Implementation

Minrend currently uses **Architecture A** concepts:
- QuickJS for JavaScript execution
- Modest for HTML/CSS parsing
- SDL2 for rendering (with known Windows/Cosmopolitan limitations)

## build.config Settings

```ini
# Window settings (used when native windowing is available)
WINDOW_TITLE=Minrend
WINDOW_WIDTH=1280
WINDOW_HEIGHT=720
WINDOW_MODE=windowed    # windowed, fullscreen, borderless

# Rendering settings
VSYNC=true
OPENGL_MAJOR=3
OPENGL_MINOR=0

# Server settings (for web-based mode)
SERVER_PORT=8080
AUTO_OPEN_BROWSER=true
```

## Platform Support Matrix

| Feature | Linux | macOS | Windows (Native) | Windows (Cosmo APE) |
|---------|-------|-------|------------------|---------------------|
| CLI/Server | ✅ | ✅ | ✅ | ✅ |
| SDL2 Window | ✅ | ✅ | ✅ | ❌ |
| OpenGL | ✅ | ✅ | ✅ | ❌ |
| Web-based UI | ✅ | ✅ | ✅ | ✅ |

