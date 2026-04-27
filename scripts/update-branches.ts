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

function inferRepoFullNameFromUrl(url: string): string | undefined {
  const match = url.match(/^https:\/\/([^/]+)\.github\.io\/([^/]+)\//);
  if (!match) return undefined;
  return `${match[1]}/${match[2]}`;
}

function buildDashboardUrl(
  repoOwner: string,
  repoFullName: string | undefined,
  safeName: string
): string {
  // Current workflows always pass repoFullName. The default only exists for the
  // deprecated repo-less compatibility path, which still needs a stable URL shape.
  const repoName = repoFullName?.split("/")[1] ?? "Vimfy";
  return `https://${repoOwner}.github.io/${repoName}/branch/${safeName}/`;
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

    let needsRewrite = false;
    const branches: BranchEntry[] = [];
    for (const raw of parsed.branches) {
      if (!raw ||
          typeof raw.name !== "string" ||
          typeof raw.safeName !== "string" ||
          typeof raw.url !== "string" ||
          typeof raw.updatedAt !== "string") {
        console.warn(`Self-healing ${FILE}: dropping malformed branch entry`);
        needsRewrite = true;
        continue;
      }

      const inferredRepoFullName = typeof raw.repoFullName === "string" && raw.repoFullName
        ? raw.repoFullName
        : inferRepoFullNameFromUrl(raw.url);
      if (inferredRepoFullName !== raw.repoFullName) {
        needsRewrite = true;
      }

      branches.push({
        name: raw.name,
        safeName: raw.safeName,
        url: raw.url,
        updatedAt: raw.updatedAt,
        repoFullName: inferredRepoFullName,
      });
    }

    return { data: { branches }, needsRewrite };
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

function matchesBranch(entry: BranchEntry, branchName: string, repoFullName?: string): boolean {
  if (entry.name !== branchName) return false;

  // Backward compatibility: older workflow versions did not persist repoFullName.
  // readBranchesFile() upgrades most legacy entries by inferring from their URL. If repo
  // identity is still missing after that, only the deprecated repo-less caller is allowed
  // to fall back to branch-name matching; repo-aware calls must not clobber another repo's
  // entry just because branch names happen to match.
  if (!entry.repoFullName) return repoFullName === undefined;
  if (!repoFullName) return false;
  return entry.repoFullName === repoFullName;
}

function resolveLegacyRepoFullName(): string | undefined {
  const repoFullName = process.env.GITHUB_REPOSITORY;
  if (repoFullName) return repoFullName;

  // Deprecated compatibility path for manual/older callers. Without repo identity we fall
  // back to branch-name matching so the new repo-aware index remains upgradeable instead of
  // encoding an incorrect owner-only key.
  console.warn(
    "Legacy 4-arg mode is deprecated without GITHUB_REPOSITORY; falling back to branch-name matching"
  );
  return undefined;
}

function upsertBranch(
  branchName: string,
  safeName: string,
  repoOwner: string,
  repoFullName: string | undefined,
  updatedAt: string
): void {
  if (!branchName || !safeName || !repoOwner || !updatedAt) usage();
  const { data } = readBranchesFile();
  const url = buildDashboardUrl(repoOwner, repoFullName, safeName);
  const conflict = data.branches.find(
    (b) =>
      b.url === url &&
      !matchesBranch(b, branchName, repoFullName)
  );
  if (conflict) {
    throw new Error(
      `Dashboard URL collision: ${branchName} and ${conflict.name} both map to ${url}`
    );
  }

  data.branches = data.branches.filter(
    (b) => !matchesBranch(b, branchName, repoFullName)
  );
  data.branches.push({
    name: branchName,
    safeName,
    url,
    updatedAt,
    ...(repoFullName ? { repoFullName } : {}),
  });
  sortBranches(data);
  // Upsert always rewrites the file, so any repoFullName inferred during the
  // read-side self-healing path is persisted even when replacing the same entry.
  writeBranchesFile(data);
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
  data.branches = data.branches.filter(
    (b) => !matchesBranch(b, branchName, repoFullName)
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
  upsertBranch(args[0], args[1], args[2], resolveLegacyRepoFullName(), args[3]);
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
