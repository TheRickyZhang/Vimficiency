#!/usr/bin/env bun
// Maintain branch entries in branches.json (gh-pages root).
// Run from within the gh-pages checkout.
//
// Usage:
//   bun scripts/update-branches.ts upsert <branch-name> <safe-branch-name> <repo-owner> <timestamp>
//   bun scripts/update-branches.ts remove <branch-name>
//
// Backward-compatible usage:
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
    [
      "Usage:",
      "  bun scripts/update-branches.ts upsert <branch-name> <safe-branch-name> <repo-owner> <timestamp>",
      "  bun scripts/update-branches.ts remove <branch-name>",
    ].join("\n")
  );
  process.exit(2);
}

const FILE = "branches.json";

function readBranchesFile(): BranchesFile {
  if (!existsSync(FILE)) {
    return { branches: [] };
  }
  const data = JSON.parse(readFileSync(FILE, "utf8")) as Partial<BranchesFile>;
  if (!Array.isArray(data.branches)) {
    throw new Error(`${FILE} is missing a branches array`);
  }
  return { branches: data.branches };
}

function writeBranchesFile(data: BranchesFile): void {
  writeFileSync(FILE, JSON.stringify(data, null, 2));
}

function sortBranches(data: BranchesFile): void {
  data.branches.sort((a, b) => b.updatedAt.localeCompare(a.updatedAt));
}

function upsertBranch(
  branchName: string,
  safeName: string,
  repoOwner: string,
  updatedAt: string
): void {
  if (!branchName || !safeName || !repoOwner || !updatedAt) usage();
  const data = readBranchesFile();
  data.branches = data.branches.filter((b) => b.name !== branchName);
  data.branches.push({
    name: branchName,
    safeName,
    url: `https://${repoOwner}.github.io/Vimficiency/branch/${safeName}/`,
    updatedAt,
  });
  sortBranches(data);
  writeBranchesFile(data);
  console.log(`Updated ${FILE}: ${data.branches.length} branch(es)`);
}

function removeBranch(branchName: string): void {
  if (!branchName) usage();
  if (!existsSync(FILE)) {
    console.log(`No ${FILE} present, nothing to update`);
    return;
  }

  const data = readBranchesFile();
  const before = data.branches.length;
  data.branches = data.branches.filter((b) => b.name !== branchName);
  sortBranches(data);
  writeBranchesFile(data);

  if (data.branches.length == before) {
    console.log(`No branch entry found for ${branchName}`);
  } else {
    console.log(`Removed ${branchName} from ${FILE}`);
  }
}

const args = process.argv.slice(2);

if (args.length == 4) {
  upsertBranch(args[0], args[1], args[2], args[3]);
  process.exit(0);
}

const [command, ...rest] = args;
switch (command) {
  case "upsert":
    if (rest.length != 4) usage();
    upsertBranch(rest[0], rest[1], rest[2], rest[3]);
    break;
  case "remove":
    if (rest.length != 1) usage();
    removeBranch(rest[0]);
    break;
  default:
    usage();
}
