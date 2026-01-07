(() => {
  const appDirInput = document.getElementById("appDir");
  const outNameInput = document.getElementById("outName");
  const runBuildBtn = document.getElementById("runBuildBtn");
  const buildLog = document.getElementById("buildLog");
  const osLabel = document.getElementById("osLabel");
  const copyLogBtn = document.getElementById("copyLogBtn");

  // Copy build log to clipboard
  if (copyLogBtn) {
    copyLogBtn.addEventListener("click", async () => {
      const logText = buildLog.textContent;
      try {
        await navigator.clipboard.writeText(logText);
        // Show copied feedback
        copyLogBtn.classList.add("copied");
        const span = copyLogBtn.querySelector("span");
        const originalText = span.textContent;
        span.textContent = "Copied!";
        setTimeout(() => {
          copyLogBtn.classList.remove("copied");
          span.textContent = originalText;
        }, 2000);
      } catch (err) {
        console.error("Failed to copy:", err);
        // Fallback for older browsers
        const textarea = document.createElement("textarea");
        textarea.value = logText;
        textarea.style.position = "fixed";
        textarea.style.opacity = "0";
        document.body.appendChild(textarea);
        textarea.select();
        try {
          document.execCommand("copy");
          copyLogBtn.classList.add("copied");
          const span = copyLogBtn.querySelector("span");
          span.textContent = "Copied!";
          setTimeout(() => {
            copyLogBtn.classList.remove("copied");
            span.textContent = "Copy";
          }, 2000);
        } catch (e) {
          console.error("Fallback copy failed:", e);
        }
        document.body.removeChild(textarea);
      }
    });
  }

  // Build stages for progress tracking
  const BUILD_STAGES = {
    CHECKING: { label: "Checking dependencies...", progress: 5, indeterminate: false },
    DOWNLOADING_COSMOCC: { label: "Downloading Cosmopolitan toolchain (~14MB)...", progress: 15, indeterminate: true },
    EXTRACTING_COSMOCC: { label: "Extracting toolchain...", progress: 25, indeterminate: false },
    CLONING_DEPS: { label: "Cloning dependencies...", progress: 35, indeterminate: false },
    COMPILING_QUICKJS: { label: "Compiling QuickJS...", progress: 45, indeterminate: false },
    COMPILING_SOKOL: { label: "Compiling sokol shims...", progress: 60, indeterminate: false },
    COMPILING_APP: { label: "Compiling application...", progress: 75, indeterminate: false },
    LINKING: { label: "Linking executable...", progress: 85, indeterminate: false },
    PACKAGING: { label: "Creating distribution package...", progress: 95, indeterminate: false },
    DONE: { label: "Build complete!", progress: 100, indeterminate: false },
    ERROR: { label: "Build failed", progress: 0, indeterminate: false }
  };

  /**
   * Filter out noisy progress output (percentage spam, # bars, etc.)
   */
  function filterNoisyOutput(text) {
    // Filter out lines that are just percentages, # progress bars, or cursor movements
    const lines = text.split('\n');
    const filtered = lines.filter(line => {
      const trimmed = line.trim();
      // Skip empty lines that are just whitespace
      if (!trimmed) return false;
      // Skip lines that are just percentages
      if (/^\d+\.\d+%\s*$/.test(trimmed)) return false;
      // Skip lines that are just # progress indicators
      if (/^[#O=-]+\s*$/.test(trimmed)) return false;
      // Skip lines with only whitespace and percentage
      if (/^\s*\d+(\.\d+)?%?\s*$/.test(trimmed)) return false;
      // Skip curl/wget progress bar artifacts
      if (/^[\s#=\->O]+$/.test(trimmed)) return false;
      return true;
    });
    return filtered.join('\n');
  }

  function getCurrentValues() {
    const appDir = (appDirInput.value || "app").trim() || "app";
    const outName = (outNameInput.value || "minirend").trim() || "minirend";
    return { appDir, outName };
  }

  /**
   * Detect the current build stage from output text
   */
  function detectStage(text) {
    const lower = text.toLowerCase();
    
    // Check for explicit stage markers first (from build scripts)
    if (text.includes("[STAGE] Downloading cosmocc")) {
      return BUILD_STAGES.DOWNLOADING_COSMOCC;
    }
    if (text.includes("[STAGE] Extracting")) {
      return BUILD_STAGES.EXTRACTING_COSMOCC;
    }
    if (text.includes("[STAGE] Cloning dependencies")) {
      return BUILD_STAGES.CLONING_DEPS;
    }
    if (text.includes("[STAGE] Compiling application")) {
      return BUILD_STAGES.COMPILING_APP;
    }
    if (text.includes("[STAGE] Linking executable")) {
      return BUILD_STAGES.LINKING;
    }
    if (text.includes("[STAGE] Creating distribution")) {
      return BUILD_STAGES.PACKAGING;
    }
    if (text.includes("[STAGE] Build complete") || text.includes("[STAGE] Dependencies ready")) {
      return BUILD_STAGES.DONE;
    }
    
    // Fallback to text pattern matching
    if (lower.includes("downloading cosmocc") || lower.includes("not found; downloading")) {
      return BUILD_STAGES.DOWNLOADING_COSMOCC;
    }
    if (lower.includes("extracting") || lower.includes("unzip") || lower.includes("expand-archive")) {
      return BUILD_STAGES.EXTRACTING_COSMOCC;
    }
    if (lower.includes("cloning quickjs") || lower.includes("cloning lexbor") || lower.includes("git clone")) {
      return BUILD_STAGES.CLONING_DEPS;
    }
    if (lower.includes("quickjs") && (lower.includes("compiling") || lower.includes(".o:"))) {
      return BUILD_STAGES.COMPILING_QUICKJS;
    }
    if (lower.includes("sokol") && (lower.includes("building") || lower.includes("compiling") || lower.includes(".o:"))) {
      return BUILD_STAGES.COMPILING_SOKOL;
    }
    if (lower.includes("running make") || lower.includes("building minirend")) {
      return BUILD_STAGES.COMPILING_APP;
    }
    if (lower.includes("linking") || lower.includes("-o minirend") || lower.includes("apelink")) {
      return BUILD_STAGES.LINKING;
    }
    if (lower.includes("creating app.zip") || lower.includes("embedding") || lower.includes("dist/")) {
      return BUILD_STAGES.PACKAGING;
    }
    if (lower.includes("build complete") || lower.includes("built single-file")) {
      return BUILD_STAGES.DONE;
    }
    if (lower.includes("error:") || lower.includes("failed") || lower.includes("cannot find")) {
      return BUILD_STAGES.ERROR;
    }
    return null;
  }

  /**
   * Create a progress bar UI element
   */
  function createProgressBar(container) {
    const wrapper = document.createElement("div");
    wrapper.id = "buildProgressWrapper";
    wrapper.style.cssText = `
      width: 100%;
      margin: 0.5rem 0 1rem 0;
      padding: 1rem;
      background: rgba(15, 23, 42, 0.85);
      border-radius: 8px;
      border: 1px solid rgba(75, 85, 99, 0.9);
    `;

    const stageLabel = document.createElement("div");
    stageLabel.id = "buildStageLabel";
    stageLabel.textContent = "Preparing build...";
    stageLabel.style.cssText = `
      margin-bottom: 0.5rem;
      font-size: 0.9rem;
      color: #e5e9f0;
      font-weight: 500;
    `;

    const progressBar = document.createElement("div");
    progressBar.style.cssText = `
      width: 100%;
      height: 8px;
      background: rgba(75, 85, 99, 0.5);
      border-radius: 4px;
      overflow: hidden;
      margin-bottom: 0.5rem;
    `;

    const progressFill = document.createElement("div");
    progressFill.id = "buildProgressFill";
    progressFill.style.cssText = `
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, #38bdf8, #0ea5e9);
      transition: width 0.3s ease;
      border-radius: 4px;
    `;

    // Add CSS for indeterminate animation
    const style = document.createElement("style");
    style.textContent = `
      @keyframes indeterminate {
        0% { transform: translateX(-100%); }
        100% { transform: translateX(400%); }
      }
      .progress-indeterminate {
        width: 25% !important;
        animation: indeterminate 1.5s ease-in-out infinite;
      }
    `;
    document.head.appendChild(style);

    const percentLabel = document.createElement("div");
    percentLabel.id = "buildPercentLabel";
    percentLabel.textContent = "0%";
    percentLabel.style.cssText = `
      font-size: 0.75rem;
      color: #94a3b8;
      text-align: right;
    `;

    progressBar.appendChild(progressFill);
    wrapper.appendChild(stageLabel);
    wrapper.appendChild(progressBar);
    wrapper.appendChild(percentLabel);

    if (container) {
      container.insertBefore(wrapper, container.firstChild);
    }

    return {
      element: wrapper,
      setStage(stage) {
        const label = document.getElementById("buildStageLabel");
        const fill = document.getElementById("buildProgressFill");
        const percent = document.getElementById("buildPercentLabel");
        
        if (label) {
          label.textContent = stage.label;
          if (stage === BUILD_STAGES.DONE) {
            label.style.color = "#4ade80";
          } else if (stage === BUILD_STAGES.ERROR) {
            label.style.color = "#f87171";
          } else {
            label.style.color = "#e5e9f0";
          }
        }
        if (fill) {
          // Handle indeterminate mode (animated bar for downloads)
          if (stage.indeterminate) {
            fill.classList.add("progress-indeterminate");
            fill.style.width = "25%";
          } else {
            fill.classList.remove("progress-indeterminate");
            fill.style.width = `${stage.progress}%`;
          }
          
          if (stage === BUILD_STAGES.DONE) {
            fill.style.background = "linear-gradient(90deg, #4ade80, #22c55e)";
          } else if (stage === BUILD_STAGES.ERROR) {
            fill.style.background = "linear-gradient(90deg, #f87171, #ef4444)";
          } else {
            fill.style.background = "linear-gradient(90deg, #38bdf8, #0ea5e9)";
          }
        }
        if (percent) {
          if (stage.indeterminate) {
            percent.textContent = "downloading...";
          } else {
            percent.textContent = `${stage.progress}%`;
          }
        }
      },
      setProgress(value) {
        const fill = document.getElementById("buildProgressFill");
        const percent = document.getElementById("buildPercentLabel");
        const clamped = Math.max(0, Math.min(100, value));
        if (fill) fill.style.width = `${clamped}%`;
        if (percent) percent.textContent = `${Math.round(clamped)}%`;
      },
      remove() {
        if (wrapper.parentNode) {
          wrapper.parentNode.removeChild(wrapper);
        }
      }
    };
  }

  /**
   * Run build with streaming output support
   */
  async function runBuild() {
    const { appDir, outName } = getCurrentValues();
    const payload = { appDir, outName };

    buildLog.textContent = "";
    runBuildBtn.disabled = true;
    runBuildBtn.textContent = "Building...";

    // Remove any existing progress bar
    const existingProgress = document.getElementById("buildProgressWrapper");
    if (existingProgress) existingProgress.remove();

    // Create progress bar
    const logContainer = buildLog.parentNode;
    const progressBar = createProgressBar(logContainer);
    progressBar.setStage(BUILD_STAGES.CHECKING);

    let fullOutput = "";
    let currentStage = BUILD_STAGES.CHECKING;

    try {
      // Try streaming first
      const response = await fetch("/api/build?stream=true", {
        method: "POST",
        headers: { 
          "Content-Type": "application/json",
          "Accept": "text/event-stream"
        },
        body: JSON.stringify(payload),
      });

      if (response.headers.get("Content-Type")?.includes("text/event-stream")) {
        // Handle streaming response
        const reader = response.body.getReader();
        const decoder = new TextDecoder();
        let buffer = "";

        while (true) {
          const { done, value } = await reader.read();
          if (done) break;

          buffer += decoder.decode(value, { stream: true });
          const lines = buffer.split("\n");
          buffer = lines.pop() || "";

          for (const line of lines) {
            if (line.startsWith("data: ")) {
              try {
                const event = JSON.parse(line.slice(6));
                
                if (event.type === "stdout" || event.type === "stderr") {
                  // Filter out noisy progress output
                  const filtered = filterNoisyOutput(event.data);
                  if (filtered.trim()) {
                    fullOutput += filtered + "\n";
                    buildLog.textContent = fullOutput;
                    buildLog.scrollTop = buildLog.scrollHeight;
                  }

                  // Detect and update stage (use original data for detection)
                  const detected = detectStage(event.data);
                  if (detected && detected !== currentStage) {
                    currentStage = detected;
                    progressBar.setStage(currentStage);
                  }
                } else if (event.type === "done") {
                  if (event.data.ok) {
                    progressBar.setStage(BUILD_STAGES.DONE);
                  } else {
                    progressBar.setStage(BUILD_STAGES.ERROR);
                  }
                  
                  // Add final status
                  fullOutput += `\n\n=== Build ${event.data.ok ? "SUCCEEDED" : "FAILED"} (exit code: ${event.data.exitCode}) ===\n`;
                  buildLog.textContent = fullOutput;
                }
              } catch (e) {
                // Ignore JSON parse errors for partial data
              }
            }
          }
        }
      } else {
        // Fallback to non-streaming
        const data = await response.json();
        
        if (data.stdout) {
          fullOutput += filterNoisyOutput(data.stdout);
        }
        if (data.stderr) {
          const filteredStderr = filterNoisyOutput(data.stderr);
          if (filteredStderr.trim()) {
            fullOutput += "\n\nstderr:\n" + filteredStderr;
          }
        }
        
        buildLog.textContent = fullOutput;
        
        if (data.ok || data.exitCode === 0) {
          progressBar.setStage(BUILD_STAGES.DONE);
        } else {
          progressBar.setStage(BUILD_STAGES.ERROR);
        }
        
        fullOutput += `\n\n=== Build ${data.ok ? "SUCCEEDED" : "FAILED"} (exit code: ${data.exitCode}) ===\n`;
        buildLog.textContent = fullOutput;
      }

      // Keep progress bar visible for a moment on success
      if (currentStage === BUILD_STAGES.DONE) {
        setTimeout(() => progressBar.remove(), 3000);
      }

    } catch (err) {
      console.error("Build error:", err);
      progressBar.setStage(BUILD_STAGES.ERROR);
      buildLog.textContent = `Error running build:\n${err.message}\n\nMake sure the configurator server is running.`;
    } finally {
      runBuildBtn.disabled = false;
      runBuildBtn.textContent = "Run build";
    }
  }

  /**
   * Check dependency status on page load
   */
  async function checkStatus() {
    try {
      const response = await fetch("/api/status");
      const status = await response.json();
      
      if (status.ready) {
        buildLog.textContent = "✓ All dependencies ready. Click 'Run build' to compile.\n\nDependencies:\n" +
          `  - Cosmopolitan: ${status.cosmocc ? "✓" : "✗"}\n` +
          `  - QuickJS: ${status.quickjs ? "✓" : "✗"}\n` +
          `  - Lexbor: ${status.lexbor ? "✓" : "✗"}\n` +
          `  - Sokol: ${status.sokol ? "✓" : "✗"}\n` +
          `  - Platform: ${status.platform ? "✓" : "✗"}`;
      } else {
        buildLog.textContent = "Dependencies will be downloaded on first build.\n\nStatus:\n" +
          `  - Cosmopolitan: ${status.cosmocc ? "✓ ready" : "⬇ will download (~14MB)"}\n` +
          `  - QuickJS: ${status.quickjs ? "✓ ready" : "⬇ will clone"}\n` +
          `  - Lexbor: ${status.lexbor ? "✓ ready" : "⬇ will clone"}\n` +
          `  - Sokol: ${status.sokol ? "✓ ready" : "⬇ will download"}\n` +
          `  - Platform: ${status.platform ? "✓ ready" : "○ will create"}`;
      }
    } catch (err) {
      buildLog.textContent = "(Could not check dependency status - server may not be running)";
    }
  }

  // Event listeners
  runBuildBtn.addEventListener("click", runBuild);

  // Fetch server OS info
  fetch("/api/meta")
    .then((res) => res.json())
    .then((data) => {
      if (data && data.osLabel) {
        osLabel.textContent = data.osLabel;
      } else if (data && data.platform) {
        osLabel.textContent = data.platform;
      } else {
        osLabel.textContent = "Unknown";
      }
    })
    .catch(() => {
      osLabel.textContent = "Unknown";
    });

  // Check status on load
  checkStatus();
})();
