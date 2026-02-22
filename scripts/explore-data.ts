#!/usr/bin/env bun
// Shared read/transform/write logic for explore.json files.
//
// Manages historical exploration data: keeps full state data only for the
// latest entry, strips states from older entries to save space while
// preserving summary information (nodesExplored, results, context).
//
// Usage:
//   bun scripts/explore-data.ts ingest <dir> <explore.json> --commit-id=... --commit-msg=... --commit-ts=... --author=... --repo-url=...
//   bun scripts/explore-data.ts prune <dir> [max=100]

import { existsSync, readFileSync, writeFileSync } from "fs";
import { join } from "path";

interface CommitInfo {
  id: string;
  message: string;
  timestamp: string;
  url: string;
  author: { username: string };
}

interface ExplorationEntry {
  commit: CommitInfo;
  date: number;
  cases: unknown[];
}

interface ExplorationData {
  lastUpdate: number;
  entries: ExplorationEntry[];
}

interface IngestOpts {
  commitId: string;
  commitMsg: string;
  commitTs: string;
  author: string;
  repoUrl: string;
}

// Strip exploration states from an entry to produce a summary-only version.
// Preserves: nodesExplored, results, context, diffs (minus editStates).
// Removes: states, editStates on diffs.
function stripStates(entry: ExplorationEntry): ExplorationEntry {
  return {
    ...entry,
    cases: entry.cases.map((c: any) => {
      const stripped: any = { ...c };
      stripped.states = [];
      if (stripped.diffs) {
        stripped.diffs = stripped.diffs.map((d: any) => {
          const { editStates, ...rest } = d;
          return rest;
        });
      }
      return stripped;
    }),
  };
}

function readData(dir: string): ExplorationData | null {
  const jsonPath = join(dir, "explore.json");
  const jsPath = join(dir, "explore.js");

  if (existsSync(jsonPath)) {
    return JSON.parse(readFileSync(jsonPath, "utf8"));
  }

  if (existsSync(jsPath)) {
    const src = readFileSync(jsPath, "utf8");
    const json = src
      .replace(/^window\.EXPLORATION_DATA\s*=\s*/, "")
      .replace(/;\s*$/, "");
    console.log(`  Migrating ${jsPath} -> ${jsonPath}`);
    return JSON.parse(json);
  }

  return null;
}

function writeData(dir: string, data: ExplorationData): void {
  writeFileSync(join(dir, "explore.json"), JSON.stringify(data, null, 0));
}

function ingest(dir: string, exploreFile: string, opts: IngestOpts): void {
  const raw = JSON.parse(readFileSync(exploreFile, "utf8"));
  const cases: unknown[] = raw.cases;

  if (!cases || !Array.isArray(cases)) {
    console.error(`  ${exploreFile}: no cases array found`);
    process.exit(1);
  }

  const entry: ExplorationEntry = {
    commit: {
      id: opts.commitId,
      message: opts.commitMsg,
      timestamp: opts.commitTs,
      url: `${opts.repoUrl}/commit/${opts.commitId}`,
      author: { username: opts.author },
    },
    date: Date.now(),
    cases,
  };

  let data = readData(dir);
  if (!data) {
    data = { lastUpdate: 0, entries: [] };
  }

  // Strip states from all existing entries (only latest keeps full data)
  data.entries = data.entries.map(stripStates);

  // Append new entry with full data
  data.entries.push(entry);
  data.lastUpdate = Date.now();

  writeData(dir, data);

  const caseCount = cases.length;
  console.log(
    `  ${dir}: ingested ${caseCount} cases, ${data.entries.length} total entries`
  );
}

function prune(dir: string, max: number): void {
  const data = readData(dir);
  if (!data) {
    console.log(`  ${dir}: no data found, skipping`);
    return;
  }

  const before = data.entries.length;
  data.entries = data.entries.slice(-max);
  console.log(`  ${dir}: ${before} -> ${data.entries.length} entries`);

  writeData(dir, data);
}

// --- CLI ---
const args = process.argv.slice(2);
const command = args[0];

function parseOpts(args: string[]): IngestOpts {
  const raw: Record<string, string> = {};
  for (const arg of args) {
    const m = arg.match(/^--([a-z-]+)=(.*)$/);
    if (m) {
      const key = m[1]!.replace(/-([a-z])/g, (_, c: string) => c.toUpperCase());
      raw[key] = m[2]!;
    }
  }
  return raw as unknown as IngestOpts;
}

if (!command) {
  console.error(
    "Usage:\n" +
      "  bun scripts/explore-data.ts ingest <dir> <explore.json> --commit-id=... --commit-msg=... --commit-ts=... --author=... --repo-url=...\n" +
      "  bun scripts/explore-data.ts prune <dir> [max=100]"
  );
  process.exit(1);
}

try {
  if (command === "ingest") {
    const dir = args[1];
    const exploreFile = args[2];
    if (!dir || !exploreFile) {
      console.error("ingest requires <dir> and <explore.json>");
      process.exit(1);
    }
    ingest(dir, exploreFile, parseOpts(args.slice(3)));
  } else if (command === "prune") {
    const dir = args[1];
    if (!dir) {
      console.error("prune requires <dir>");
      process.exit(1);
    }
    const max = parseInt(args[2] ?? "", 10) || 100;
    prune(dir, max);
  } else {
    console.error(`Unknown command: ${command}`);
    process.exit(1);
  }
} catch (err) {
  console.error(`Error: ${(err as Error).message}`);
  process.exit(1);
}
