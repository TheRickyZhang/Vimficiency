#!/usr/bin/env bun
// Shared read/transform/write logic for benchmark data.json files.
//
// Usage:
//   bun scripts/bench-data.ts ingest <dir> <result.json> --commit-id=... --commit-msg=... --commit-ts=... --author=... --repo-url=...
//   bun scripts/bench-data.ts prune <dir> [max=100]
//   bun scripts/bench-data.ts remove <dir> <sha-prefix>

import { existsSync, readFileSync, writeFileSync } from "fs";
import { join } from "path";

interface BenchEntry {
  name: string;
  value: number;
  unit: string;
  cpuTime?: number;
}

interface CommitInfo {
  id: string;
  message: string;
  timestamp: string;
  url: string;
  author: { username: string };
}

interface BenchmarkRun {
  commit: CommitInfo;
  date: number;
  benches: BenchEntry[];
}

interface BenchmarkData {
  lastUpdate: number;
  repoUrl: string;
  entries: Record<string, BenchmarkRun[]>;
}

interface GoogleBenchmark {
  name: string;
  real_time: number;
  cpu_time: number;
  time_unit: string;
}

interface GoogleBenchmarkJSON {
  benchmarks: GoogleBenchmark[];
}

interface IngestOpts {
  commitId: string;
  commitMsg: string;
  commitTs: string;
  author: string;
  repoUrl: string;
}

// Read data.json, falling back to legacy data.js (window.BENCHMARK_DATA = ...)
function readData(dir: string): BenchmarkData | null {
  const jsonPath = join(dir, "data.json");
  const jsPath = join(dir, "data.js");

  if (existsSync(jsonPath)) {
    return JSON.parse(readFileSync(jsonPath, "utf8"));
  }

  if (existsSync(jsPath)) {
    const src = readFileSync(jsPath, "utf8");
    const json = src
      .replace(/^window\.BENCHMARK_DATA\s*=\s*/, "")
      .replace(/;\s*$/, "");
    console.log(`  Migrating ${jsPath} -> ${jsonPath}`);
    return JSON.parse(json);
  }

  return null;
}

function writeData(dir: string, data: BenchmarkData): void {
  writeFileSync(join(dir, "data.json"), JSON.stringify(data, null, 0));
}

// Parse Google Benchmark JSON and append a benchmark run to data.json
function ingest(dir: string, resultFile: string, opts: IngestOpts): void {
  const result: GoogleBenchmarkJSON = JSON.parse(readFileSync(resultFile, "utf8"));

  const benches: BenchEntry[] = result.benchmarks.map((b) => ({
    name: b.name,
    value: b.real_time,
    unit: b.time_unit + "/iter",
    cpuTime: b.cpu_time,
  }));

  const entry: BenchmarkRun = {
    commit: {
      id: opts.commitId,
      message: opts.commitMsg,
      timestamp: opts.commitTs,
      url: `${opts.repoUrl}/commit/${opts.commitId}`,
      author: { username: opts.author },
    },
    date: Date.now(),
    benches,
  };

  let data = readData(dir);
  if (!data) {
    data = {
      lastUpdate: 0,
      repoUrl: opts.repoUrl,
      entries: {},
    };
  }

  // benchmark-action uses the first benchmark name's prefix as the suite key
  const suiteKey = benches[0]?.name.split("/")[0] ?? "Benchmark";
  if (!data.entries[suiteKey]) {
    data.entries[suiteKey] = [];
  }
  data.entries[suiteKey].push(entry);
  data.lastUpdate = Date.now();
  data.repoUrl = opts.repoUrl;

  writeData(dir, data);
  console.log(
    `  ${dir}: ingested ${benches.length} benchmarks from ${resultFile}`
  );
}

function prune(dir: string, max: number): void {
  const data = readData(dir);
  if (!data) {
    console.log(`  ${dir}: no data found, skipping`);
    return;
  }

  for (const [key, runs] of Object.entries(data.entries)) {
    const before = runs.length;
    data.entries[key] = runs.slice(-max);
    console.log(`  ${dir}/${key}: ${before} -> ${data.entries[key].length}`);
  }

  writeData(dir, data);
}

function cleanSuites(dir: string): void {
  const data = readData(dir);
  if (!data) {
    console.log(`  ${dir}: no data found, skipping`);
    return;
  }

  const keys = Object.keys(data.entries);
  if (keys.length <= 1) return;

  // Keep only the suite with the most recent entry
  let bestKey = keys[0]!;
  let bestDate = 0;
  for (const [key, runs] of Object.entries(data.entries)) {
    const latest = runs[runs.length - 1]?.date ?? 0;
    if (latest > bestDate) {
      bestDate = latest;
      bestKey = key;
    }
  }

  for (const key of keys) {
    if (key !== bestKey) {
      console.log(`  ${dir}: removing stale suite "${key}" (${data.entries[key]!.length} entries)`);
      delete data.entries[key];
    }
  }

  writeData(dir, data);
}

function remove(dir: string, shaPrefix: string): void {
  const data = readData(dir);
  if (!data) {
    console.log(`  ${dir}: no data found, skipping`);
    return;
  }

  for (const [key, runs] of Object.entries(data.entries)) {
    const before = runs.length;
    data.entries[key] = runs.filter(
      (r) => !r.commit.id.startsWith(shaPrefix)
    );
    console.log(`  ${dir}/${key}: ${before} -> ${data.entries[key].length}`);
  }

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
      // --commit-id -> commitId
      const key = m[1]!.replace(/-([a-z])/g, (_, c: string) => c.toUpperCase());
      raw[key] = m[2]!;
    }
  }
  return raw as unknown as IngestOpts;
}

if (!command) {
  console.error(
    "Usage:\n" +
      "  bun scripts/bench-data.ts ingest <dir> <result.json> --commit-id=... --commit-msg=... --commit-ts=... --author=... --repo-url=...\n" +
      "  bun scripts/bench-data.ts prune <dir> [max=100]\n" +
      "  bun scripts/bench-data.ts remove <dir> <sha-prefix>\n" +
      "  bun scripts/bench-data.ts clean-suites <dir>"
  );
  process.exit(1);
}

try {
  if (command === "ingest") {
    const dir = args[1];
    const resultFile = args[2];
    if (!dir || !resultFile) {
      console.error("ingest requires <dir> and <result.json>");
      process.exit(1);
    }
    ingest(dir, resultFile, parseOpts(args.slice(3)));
  } else if (command === "prune") {
    const dir = args[1];
    if (!dir) {
      console.error("prune requires <dir>");
      process.exit(1);
    }
    const max = parseInt(args[2] ?? "", 10) || 100;
    prune(dir, max);
  } else if (command === "remove") {
    const dir = args[1];
    const shaPrefix = args[2];
    if (!dir || !shaPrefix) {
      console.error("remove requires <dir> and <sha-prefix>");
      process.exit(1);
    }
    remove(dir, shaPrefix);
  } else if (command === "clean-suites") {
    const dir = args[1];
    if (!dir) {
      console.error("clean-suites requires <dir>");
      process.exit(1);
    }
    cleanSuites(dir);
  } else {
    console.error(`Unknown command: ${command}`);
    process.exit(1);
  }
} catch (err) {
  console.error(`Error: ${(err as Error).message}`);
  process.exit(1);
}
