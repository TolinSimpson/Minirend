(() => {
  const appDirInput = document.getElementById("appDir");
  const outNameInput = document.getElementById("outName");
  const runBuildBtn = document.getElementById("runBuildBtn");
  const buildLog = document.getElementById("buildLog");
  const osLabel = document.getElementById("osLabel");

  function getCurrentValues() {
    const appDir = (appDirInput.value || "app").trim() || "app";
    const outName = (outNameInput.value || "minrend").trim() || "minrend";
    return { appDir, outName };
  }

  async function runBuild() {
    const { appDir, outName } = getCurrentValues();
    const payload = { appDir, outName };

    buildLog.textContent = "Starting build...\n";
    runBuildBtn.disabled = true;
    runBuildBtn.textContent = "Building...";

    let progressBar = null;
    let progressInterval = null;
    let isDownloading = false;
    let downloadStartTime = null;

    // Function to create and show progress bar for dependency downloads
    function showProgressBar(labelText = "Downloading dependencies...") {
      if (!progressBar) {
        progressBar = document.createElement("div");
        progressBar.style.cssText = `
          width: 100%;
          margin: 0.5rem 0;
          padding: 0.75rem;
          background: rgba(15, 23, 42, 0.85);
          border-radius: 8px;
          border: 1px solid rgba(75, 85, 99, 0.9);
        `;

        const label = document.createElement("div");
        label.id = "buildProgressLabel";
        label.textContent = labelText;
        label.style.cssText = `
          margin-bottom: 0.5rem;
          font-size: 0.9rem;
          color: #e5e9f0;
          font-weight: 500;
        `;

        const barWrapper = document.createElement("div");
        barWrapper.style.cssText = `
          width: 100%;
          height: 8px;
          background: rgba(75, 85, 99, 0.5);
          border-radius: 4px;
          overflow: hidden;
        `;

        const fill = document.createElement("div");
        fill.id = "buildProgressFill";
        fill.style.cssText = `
          height: 100%;
          width: 0%;
          background: linear-gradient(90deg, #38bdf8, #0ea5e9);
          transition: width 0.3s ease;
          border-radius: 4px;
        `;

        barWrapper.appendChild(fill);
        progressBar.appendChild(label);
        progressBar.appendChild(barWrapper);
        buildLog.parentNode.insertBefore(progressBar, buildLog);
      }
    }

    // Function to update progress (simulated based on time)
    function startProgressSimulation() {
      if (progressInterval) return;
      let progress = 0;
      downloadStartTime = Date.now();
      progressInterval = setInterval(() => {
        if (!isDownloading) return;
        const elapsed = (Date.now() - downloadStartTime) / 1000;
        // Estimate progress: assume download takes ~30-60 seconds for ~14MB
        if (elapsed < 5) {
          progress = Math.min(10, elapsed * 2);
        } else if (elapsed < 30) {
          progress = 10 + ((elapsed - 5) / 25) * 70;
        } else {
          progress = Math.min(95, 80 + ((elapsed - 30) / 30) * 15);
        }
        const fill = document.getElementById("buildProgressFill");
        if (fill) {
          fill.style.width = `${progress}%`;
        }
      }, 200);
    }

    // Function to complete progress
    function completeProgress() {
      if (progressInterval) {
        clearInterval(progressInterval);
        progressInterval = null;
      }
      const fill = document.getElementById("buildProgressFill");
      const label = document.getElementById("buildProgressLabel");
      if (fill) {
        fill.style.width = "100%";
      }
      if (label) {
        label.textContent = "Dependencies ready!";
        label.style.color = "#4ade80";
      }
      setTimeout(() => {
        if (progressBar && progressBar.parentNode) {
          progressBar.parentNode.removeChild(progressBar);
          progressBar = null;
        }
      }, 1500);
    }

    try {
      const response = await fetch("/api/build", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });

      const data = await response.json().catch(() => ({}));
      const lines = [];
      let outputText = "";

      if (data.stdout) {
        outputText = data.stdout.trim();
        // Detect if bootstrap/download is happening during build
        if ((outputText.includes("downloading cosmocc") || 
             outputText.includes("Downloading cosmocc") ||
             outputText.includes("Using curl") ||
             outputText.includes("Using PowerShell")) && !isDownloading) {
          isDownloading = true;
          showProgressBar("Downloading dependencies...");
          startProgressSimulation();
        }

        // Detect download completion
        if (isDownloading && (
            outputText.includes("Extracting") ||
            outputText.includes("Extracting using") ||
            outputText.includes("cosmocc binary") ||
            outputText.includes("Cosmopolitan toolchain installed") ||
            outputText.includes("Building Minrend"))) {
          isDownloading = false;
          completeProgress();
        }
      }

      if (typeof data.exitCode === "number") {
        lines.push(`exit code: ${data.exitCode}`);
      }
      if (data.stdout) {
        lines.push("\nstdout:\n" + data.stdout.trim());
      }
      if (data.stderr) {
        lines.push("\nstderr:\n" + data.stderr.trim());
      }
      if (!lines.length && !data.ok) {
        lines.push("Build failed or no output was captured.");
      }
      if (!response.ok) {
        lines.unshift(`HTTP error: ${response.status}`);
      }

      buildLog.textContent = lines.join("\n");
      
      // Complete progress if still showing
      if (isDownloading) {
        completeProgress();
      }
    } catch (err) {
      if (progressInterval) {
        clearInterval(progressInterval);
      }
      if (progressBar && progressBar.parentNode) {
        progressBar.parentNode.removeChild(progressBar);
      }
      console.error("Failed to call /api/build:", err);
      buildLog.textContent =
        "Error calling /api/build. Is the server running?\n\n" +
        String(err);
    } finally {
      if (progressInterval) {
        clearInterval(progressInterval);
      }
      runBuildBtn.disabled = false;
      runBuildBtn.textContent = "Run build";
    }
  }

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
})();


