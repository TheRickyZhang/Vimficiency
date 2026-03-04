#!/usr/bin/env bun
// Add or update a branch entry in branches.json (gh-pages root).
// Run after deploying a branch dashboard, from within the gh-pages checkout.
//
// Usage:
//   bun scripts/update-branches.ts <branch-name> <safe-branch-name> <repo-owner> <timestamp>
//
// branches.json format:
//   { "branches": [{ "name", "safeName", "url", "updatedAt" }, ...] }

import { existsSync, readFileSync, writeFileSync } from "fs";

interface BranchEntry {
  name: string;
  safeName: string;
  url: string;
  updatedAt: string;
}

interface BranchesFile {
  branches: BranchEntry[];
}

function usage(): never {
  console.error(
    "Usage: bun scripts/update-branches.ts <branch-name> <safe-branch-name> <repo-owner> <timestamp>"
  );
  process.exit(2);
}

const [branchName, safeName, repoOwner, updatedAt] = process.argv.slice(2);
if (!branchName || !safeName || !repoOwner || !updatedAt) usage();

const FILE = "branches.json";
const data: BranchesFile = existsSync(FILE)
  ? JSON.parse(readFileSync(FILE, "utf8"))
  : { branches: [] };

data.branches = data.branches.filter((b) => b.name !== branchName);
data.branches.push({
  name: branchName,
  safeName,
  url: `https://${repoOwner}.github.io/Vimficiency/branch/${safeName}/`,
  updatedAt,
});
data.branches.sort((a, b) => b.updatedAt.localeCompare(a.updatedAt));

writeFileSync(FILE, JSON.stringify(data, null, 2));
console.log(`Updated ${FILE}: ${data.branches.length} branch(es)`);
