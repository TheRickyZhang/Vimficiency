#!/usr/bin/env bun
// Compare Google Benchmark JSON files (current vs baseline).
//
// Usage:
//   bun scripts/bench-compare.ts [--threshold 1.15] <current.json> <baseline.json> [<current2> <baseline2> ...]
//
// Exits 0 if no regressions, 1 if any benchmark exceeds the threshold ratio.

import { readFileSync, appendFileSync } from "fs";

interface Benchmark {
  name: string;
  real_time: number;
  time_unit: string;
}

interface BenchmarkJSON {
  benchmarks: Benchmark[];
}

interface Comparison {
  name: string;
  current: number;
  baseline: number;
  ratio: number;
  unit: string;
}

function parseArgs(args: string[]): { threshold: number; pairs: [string, string][] } {
  let threshold = 1.15;
  const files: string[] = [];

  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--threshold" && i + 1 < args.length) {
      threshold = parseFloat(args[i + 1]!);
      i++;
    } else {
      files.push(args[i]!);
    }
  }

  if (files.length < 2 || files.length % 2 !== 0) {
    console.error(
      "Usage: bun scripts/bench-compare.ts [--threshold 1.15] <current.json> <baseline.json> [...]"
    );
    process.exit(2);
  }

  const pairs: [string, string][] = [];
  for (let i = 0; i < files.length; i += 2) {
    pairs.push([files[i]!, files[i + 1]!]);
  }

  return { threshold, pairs };
}

function loadBenchmarks(path: string): Map<string, Benchmark> {
  const data: BenchmarkJSON = JSON.parse(readFileSync(path, "utf8"));
  const map = new Map<string, Benchmark>();
  for (const b of data.benchmarks) {
    map.set(b.name, b);
  }
  return map;
}

function fmtTime(value: number, unit: string): string {
  // Normalize to nanoseconds first
  let ns = value;
  if (unit === "us") ns = value * 1e3;
  else if (unit === "ms") ns = value * 1e6;
  else if (unit === "s") ns = value * 1e9;

  if (ns >= 1e9) return (ns / 1e9).toFixed(2) + " s";
  if (ns >= 1e6) return (ns / 1e6).toFixed(2) + " ms";
  if (ns >= 1e3) return (ns / 1e3).toFixed(1) + " us";
  return Math.round(ns) + " ns";
}

function compare(currentPath: string, baselinePath: string): Comparison[] {
  const current = loadBenchmarks(currentPath);
  const baseline = loadBenchmarks(baselinePath);
  const results: Comparison[] = [];

  for (const [name, cur] of current) {
    const base = baseline.get(name);
    if (!base) continue;
    results.push({
      name,
      current: cur.real_time,
      baseline: base.real_time,
      ratio: cur.real_time / base.real_time,
      unit: cur.time_unit,
    });
  }

  return results;
}

function formatTable(comparisons: Comparison[], threshold: number): string {
  const lines: string[] = [];
  lines.push("| Benchmark | Baseline | Current | Ratio | Status |");
  lines.push("|-----------|----------|---------|-------|--------|");

  const sorted = [...comparisons].sort((a, b) => b.ratio - a.ratio);

  for (const c of sorted) {
    const pct = ((c.ratio - 1) * 100).toFixed(1);
    const sign = c.ratio >= 1 ? "+" : "";
    const status = c.ratio > threshold ? "regression" : c.ratio < 1 / threshold ? "improvement" : "";
    lines.push(
      `| ${c.name} | ${fmtTime(c.baseline, c.unit)} | ${fmtTime(c.current, c.unit)} | ${sign}${pct}% | ${status} |`
    );
  }

  return lines.join("\n");
}

// --- Main ---
const { threshold, pairs } = parseArgs(process.argv.slice(2));

let allComparisons: Comparison[] = [];
for (const [currentPath, baselinePath] of pairs) {
  allComparisons = allComparisons.concat(compare(currentPath, baselinePath));
}

if (allComparisons.length === 0) {
  console.log("No matching benchmarks found.");
  process.exit(0);
}

const regressions = allComparisons.filter((c) => c.ratio > threshold);
const improvements = allComparisons.filter((c) => c.ratio < 1 / threshold);

const header = regressions.length > 0
  ? `## Benchmark Comparison: ${regressions.length} regression(s) detected`
  : `## Benchmark Comparison: No regressions (threshold: ${((threshold - 1) * 100).toFixed(0)}%)`;

const summary = improvements.length > 0
  ? `\n${improvements.length} improvement(s) detected.\n`
  : "";

const table = formatTable(allComparisons, threshold);
const output = `${header}\n${summary}\n${table}\n`;

console.log(output);

const summaryPath = process.env.GITHUB_STEP_SUMMARY;
if (summaryPath) {
  appendFileSync(summaryPath, output);
}

process.exit(regressions.length > 0 ? 1 : 0);
