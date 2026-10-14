// Local dev with mock backend.
// Launches the repository's offline mock (scripts/mock_api.py) and Vite
// together. Falls back to `python` when `python3` is not available (Windows).
// Both processes are stopped with Ctrl+C.
import { spawn, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";
import fs from "node:fs";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");

// The mock lives beside this frontend in the exported project, and one level up
// when this folder sits inside the AURA repository.
const candidates = [
  path.join(root, "scripts", "mock_api.py"),
  path.join(root, "..", "scripts", "mock_api.py"),
];
const mockPath = candidates.find((p) => fs.existsSync(p));

if (!mockPath) {
  console.error(
    "[dev-with-mock] scripts/mock_api.py not found — start the mock manually: python scripts/mock_api.py --port 8790",
  );
}

export const pythonBin =
  spawnSync("python3", ["--version"], { encoding: "utf8" }).status === 0 ? "python3" : "python";
const py = pythonBin;

const procs = [];
const killAll = () => {
  for (const p of procs) {
    try {
      p.kill();
    } catch {
      /* already gone */
    }
  }
  process.exit(0);
};
process.on("SIGINT", killAll);
process.on("SIGTERM", killAll);

if (mockPath) {
  // DEMO MODE labels every screen in the UI, so mock data is never mistaken
  // for real market data.
  const mock = spawn(py, [mockPath, "--port", "8790"], {
    stdio: "inherit",
    env: { ...process.env },
  });
  procs.push(mock);
  console.log("[dev-with-mock] mock API on http://127.0.0.1:8790/api/v1/ (DEMO DATA)");
}

const vite = spawn("npx", ["vite", "--host"], {
  stdio: "inherit",
  shell: process.platform === "win32",
  env: { ...process.env, VITE_API_BASE_URL: "/api/v1/", VITE_DEMO_MODE: "1" },
});
procs.push(vite);
vite.on("exit", killAll);
