#!/usr/bin/env bun
// Maintain branch entries in branches.json (gh-pages root).
// Run from within the gh-pages checkout.
//
// Usage:
//   bun scripts/update-branches.ts upsert <branch-name> <safe-branch-name> <repo-owner> <repo-full-name> <timestamp>
//   bun scripts/update-branches.ts remove <branch-name> <repo-full-name>
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
  repoFullName?: string;
}

interface BranchesFile {
  branches: BranchEntry[];
}

function usage(): never {
  console.error(
    [
      "Usage:",
      "  bun scripts/update-branches.ts upsert <branch-name> <safe-branch-name> <repo-owner> <repo-full-name> <timestamp>",
      "  bun scripts/update-branches.ts remove <branch-name> <repo-full-name>",
    ].join("\n")
  );
  process.exit(2);
}

const FILE = "branches.json";

function readBranchesFile(): { data: BranchesFile; needsRewrite: boolean } {
  if (!existsSync(FILE)) {
    return { data: { branches: [] }, needsRewrite: false };
  }
  try {
    const parsed = JSON.parse(readFileSync(FILE, "utf8")) as Partial<BranchesFile>;
    if (!Array.isArray(parsed.branches)) {
      console.warn(`Self-healing ${FILE}: missing branches array`);
      return { data: { branches: [] }, needsRewrite: true };
    }
    return { data: { branches: parsed.branches }, needsRewrite: false };
  } catch (error) {
    console.warn(`Self-healing ${FILE}: invalid JSON (${String(error)})`);
    return { data: { branches: [] }, needsRewrite: true };
  }
}

function writeBranchesFile(data: BranchesFile): void {
  writeFileSync(FILE, JSON.stringify(data, null, 2));
}

function sortBranches(data: BranchesFile): void {
  data.branches.sort((a, b) => b.updatedAt.localeCompare(a.updatedAt));
}

function branchKey(branchName: string, repoFullName?: string): string {
  return `${repoFullName ?? ""}\n${branchName}`;
}

function upsertBranch(
  branchName: string,
  safeName: string,
  repoOwner: string,
  repoFullName: string,
  updatedAt: string
): void {
  if (!branchName || !safeName || !repoOwner || !repoFullName || !updatedAt) usage();
  const { data, needsRewrite } = readBranchesFile();
  const conflict = data.branches.find(
    (b) =>
      b.safeName === safeName &&
      (b.repoFullName ?? "") === repoFullName &&
      b.name !== branchName
  );
  if (conflict) {
    throw new Error(
      `Safe branch collision: ${branchName} and ${conflict.name} both map to ${safeName}`
    );
  }

  const key = branchKey(branchName, repoFullName);
  data.branches = data.branches.filter(
    (b) => branchKey(b.name, b.repoFullName) !== key
  );
  data.branches.push({
    name: branchName,
    safeName,
    url: `https://${repoOwner}.github.io/Vimficiency/branch/${safeName}/`,
    updatedAt,
    repoFullName,
  });
  sortBranches(data);
  if (needsRewrite || data.branches.length > 0) {
    writeBranchesFile(data);
  }
  console.log(`Updated ${FILE}: ${data.branches.length} branch(es)`);
}

function removeBranch(branchName: string, repoFullName: string): void {
  if (!branchName || !repoFullName) usage();
  if (!existsSync(FILE)) {
    console.log(`No ${FILE} present, nothing to update`);
    return;
  }

  const { data, needsRewrite } = readBranchesFile();
  const before = data.branches.length;
  const key = branchKey(branchName, repoFullName);
  data.branches = data.branches.filter(
    (b) => branchKey(b.name, b.repoFullName) !== key
  );

  if (!needsRewrite && data.branches.length === before) {
    console.log(`No branch entry found for ${branchName} in ${repoFullName}`);
    return;
  }

  sortBranches(data);
  writeBranchesFile(data);

  if (data.branches.length == before) {
    console.log(`Self-healed ${FILE} while removing ${branchName}`);
  } else {
    console.log(`Removed ${branchName} from ${FILE}`);
  }
}

const args = process.argv.slice(2);

if (args.length == 4) {
  upsertBranch(args[0], args[1], args[2], args[2], args[3]);
  process.exit(0);
}

const [command, ...rest] = args;
switch (command) {
  case "upsert":
    if (rest.length != 5) usage();
    upsertBranch(rest[0], rest[1], rest[2], rest[3], rest[4]);
    break;
  case "remove":
    if (rest.length != 2) usage();
    removeBranch(rest[0], rest[1]);
    break;
  default:
    usage();
}
