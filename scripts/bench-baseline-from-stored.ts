#!/usr/bin/env bun
// Pull a commit's previously-recorded benchmark numbers out of a
// gh-pages-style data.json and re-emit them in Google Benchmark JSON
// format so bench-compare.ts can use them as a baseline.
//
// Usage:
//   bun scripts/bench-baseline-from-stored.ts <data-dir> <sha> <out.json>
//
// Exit 0 on success (out.json written). Exit 1 if the commit has no stored
// record under <data-dir> — caller treats this as "no baseline available."

import { existsSync, readFileSync, writeFileSync } from "fs";
import { join } from "path";

const [dataDir, sha, outFile] = process.argv.slice(2);
if (!dataDir || !sha || !outFile) {
  console.error("usage: bun bench-baseline-from-stored.ts <data-dir> <sha> <out.json>");
  process.exit(2);
}

const dataPath = join(dataDir, "data.json");
if (!existsSync(dataPath)) process.exit(1);

const data = JSON.parse(readFileSync(dataPath, "utf8"));
const suites = Object.values(data.entries ?? {}) as Array<Array<{
  commit?: { id?: string };
  benches: Array<{ name: string; value: number; unit: string }>;
}>>;

for (const runs of suites) {
  const match = runs.find((r) => r.commit?.id === sha);
  if (match) {
    // bench-data.ts stores `unit` as "<time_unit>/iter" — strip the suffix
    // so bench-compare.ts gets back the raw Google Benchmark time_unit.
    const benchmarks = match.benches.map((b) => ({
      name: b.name,
      real_time: b.value,
      time_unit: b.unit.replace(/\/iter$/, ""),
    }));
    writeFileSync(outFile, JSON.stringify({ benchmarks }, null, 2));
    process.exit(0);
  }
}

process.exit(1);
