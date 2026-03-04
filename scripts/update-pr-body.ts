#!/usr/bin/env bun
// Prepend (or update) a benchmark dashboard link at the top of a PR's body.
//
// Usage:
//   bun scripts/update-pr-body.ts <pr-number> <dashboard-url> <commit-sha> <repo>

import { spawnSync } from "child_process";

const MARKER_START = "<!-- bench-dashboard-link-start -->";
const MARKER_END = "<!-- bench-dashboard-link-end -->";

function usage(): void {
  console.error(
    "Usage: bun scripts/update-pr-body.ts <pr-number> <dashboard-url> <commit-sha> <repo>"
  );
  process.exit(2);
}

const [prNumber, dashboardUrl, commitSha, repo] = process.argv.slice(2);
if (!prNumber || !dashboardUrl || !commitSha || !repo) usage();

const timestamp = new Date().toISOString().replace("T", " ").slice(0, 19) + " UTC";
const block = [
  MARKER_START,
  `**Benchmark Dashboard:** ${dashboardUrl}`,
  "",
  `_Updated at ${timestamp} for commit ${commitSha}_`,
  MARKER_END,
].join("\n");

// Fetch current PR body
const prProc = spawnSync("gh", ["api", `repos/${repo}/pulls/${prNumber}`], {
  encoding: "utf8",
});
if (prProc.status !== 0) {
  console.error("Failed to fetch PR data:", prProc.stderr);
  process.exit(1);
}

const prData = JSON.parse(prProc.stdout);
const currentBody: string = prData.body ?? "";

let newBody: string;
if (currentBody.includes(MARKER_START)) {
  const pattern = new RegExp(
    escapeRegex(MARKER_START) + "[\\s\\S]*?" + escapeRegex(MARKER_END)
  );
  newBody = currentBody.replace(pattern, block);
} else {
  newBody = currentBody ? block + "\n\n" + currentBody : block;
}

const editProc = spawnSync("gh", ["pr", "edit", prNumber, "--body", newBody], {
  encoding: "utf8",
  stdio: "inherit",
});
process.exit(editProc.status ?? 0);

function escapeRegex(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}
