#!/usr/bin/env bun
// Converts GTest JSON output (--gtest_output=json:...) to Google Benchmark
// JSON format so it can be ingested by bench-data.ts.
//
// Only the deterministically-timed binaries (Unit, Approval) go through here;
// property/safety are coverage, not timing (see convert-gtest-coverage.ts and
// dev/decisions.md). Emits one per-binary total plus one entry per test case:
//   Tests/Binaries/<Label>
//   Tests/Cases/<Label>/<Suite>.<Case>
//
// Usage:
//   bun scripts/convert-gtest-timing.ts <output.json> Unit=unit.json Approval=approval.json

import { readFileSync, writeFileSync } from "fs";

interface GTestCase {
  name: string;
  time: string; // e.g. "0.025s"
}

interface GTestSuite {
  name: string;
  time: string;
  testsuite: GTestCase[];
}

interface GTestOutput {
  time: string;
  testsuites: GTestSuite[];
}

interface GoogleBenchmark {
  name: string;
  real_time: number;
  cpu_time: number;
  time_unit: string;
}

interface TimedInput {
  label: string;
  path: string;
}

function parseTimeToMs(timeStr: string): number {
  // GTest time is seconds with an "s" suffix, e.g. "25.500s".
  const match = timeStr.match(/^([\d.]+)s$/);
  if (!match) {
    throw new Error(`Cannot parse GTest time string: "${timeStr}"`);
  }
  return parseFloat(match[1]!) * 1000;
}

function usage(): never {
  console.error(
    "Usage:\n" +
      "  bun scripts/convert-gtest-timing.ts <output.json> Unit=unit.json Approval=approval.json"
  );
  process.exit(1);
}

function parseArgs(args: string[]): { outputFile: string; inputs: TimedInput[] } {
  const outputFile = args[0];
  if (!outputFile || args.length < 2) usage();

  const inputs = args.slice(1).map((spec) => {
    const equals = spec.indexOf("=");
    if (equals <= 0 || equals === spec.length - 1) {
      throw new Error(`Expected Label=path input, got "${spec}"`);
    }
    return {
      label: spec.slice(0, equals),
      path: spec.slice(equals + 1),
    };
  });

  return { outputFile, inputs };
}

function pushBenchmark(benchmarks: GoogleBenchmark[], name: string, valueMs: number): void {
  benchmarks.push({
    name,
    real_time: valueMs,
    cpu_time: valueMs,
    time_unit: "ms",
  });
}

const args = process.argv.slice(2);
const { outputFile, inputs } = parseArgs(args);

const benchmarks: GoogleBenchmark[] = [];

for (const input of inputs) {
  const gtest: GTestOutput = JSON.parse(readFileSync(input.path, "utf8"));
  pushBenchmark(benchmarks, `Tests/Binaries/${input.label}`, parseTimeToMs(gtest.time));

  for (const suite of gtest.testsuites) {
    for (const testCase of suite.testsuite) {
      pushBenchmark(
        benchmarks,
        `Tests/Cases/${input.label}/${suite.name}.${testCase.name}`,
        parseTimeToMs(testCase.time)
      );
    }
  }
}

writeFileSync(outputFile, JSON.stringify({ benchmarks }, null, 2));
console.log(
  `Converted ${inputs.length} gtest file${inputs.length === 1 ? "" : "s"} -> ${outputFile}`
);
