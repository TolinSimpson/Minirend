// Main JavaScript entry for your minirend-powered app.
// This is the file you should edit to implement your web UI / logic.

console.log("minirend app starting up from app/main.js...");

// localStorage demo: count how many times the app was launched.
let launches = parseInt(localStorage.getItem("launchCount") || "0", 10);
launches += 1;
localStorage.setItem("launchCount", String(launches));
console.log("Application launch count (persisted via localStorage):", launches);

// Progress bar utility for downloading cosmocc during bootstrapping
function createProgressBar(container, label = "Downloading cosmocc...") {
  const wrapper = document.createElement("div");
  wrapper.style.cssText = `
    width: 100%;
    margin: 1rem 0;
    padding: 1rem;
    background: rgba(15, 23, 42, 0.85);
    border-radius: 8px;
    border: 1px solid rgba(75, 85, 99, 0.9);
  `;

  const labelEl = document.createElement("div");
  labelEl.textContent = label;
  labelEl.style.cssText = `
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
  `;

  const progressFill = document.createElement("div");
  progressFill.style.cssText = `
    height: 100%;
    width: 0%;
    background: linear-gradient(90deg, #38bdf8, #0ea5e9);
    transition: width 0.3s ease;
    border-radius: 4px;
  `;

  progressBar.appendChild(progressFill);
  wrapper.appendChild(labelEl);
  wrapper.appendChild(progressBar);

  if (container) {
    container.appendChild(wrapper);
  }

  return {
    element: wrapper,
    setProgress: (percent) => {
      const clamped = Math.max(0, Math.min(100, percent));
      progressFill.style.width = `${clamped}%`;
      if (clamped === 100) {
        labelEl.textContent = "Download complete!";
        labelEl.style.color = "#4ade80";
      }
    },
    setLabel: (text) => {
      labelEl.textContent = text;
    },
    remove: () => {
      if (wrapper.parentNode) {
        wrapper.parentNode.removeChild(wrapper);
      }
    }
  };
}

// Export progress bar utility for use in bootstrap UI
window.MinirendProgressBar = createProgressBar;

// requestAnimationFrame demo: simple ticking timer for a couple of seconds.
let start = performance.now();
function tick(now) {
  const elapsedSec = ((now - start) / 1000).toFixed(2);
  console.log("tick at", elapsedSec, "seconds");
  if (parseFloat(elapsedSec) < 2.0) {
    requestAnimationFrame(tick);
  } else {
    console.log("Animation demo complete.");
  }
}
requestAnimationFrame(tick);


