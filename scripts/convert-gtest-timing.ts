#!/usr/bin/env bun
// Converts GTest JSON output (--gtest_output=json:...) to Google Benchmark
// JSON format so it can be ingested by bench-data.ts.
//
// Usage:
//   bun scripts/convert-gtest-timing.ts <gtest.json> <output.json>
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
      "  bun scripts/convert-gtest-timing.ts <gtest.json> <output.json>\n" +
      "  bun scripts/convert-gtest-timing.ts <output.json> Unit=unit.json Approval=approval.json"
  );
  process.exit(1);
}

function parseArgs(args: string[]): { outputFile: string; inputs: TimedInput[]; legacySingle: boolean } {
  if (args.length === 2 && !args[0]!.includes("=") && !args[1]!.includes("=")) {
    return {
      outputFile: args[1]!,
      inputs: [{ label: "All", path: args[0]! }],
      legacySingle: true,
    };
  }

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

  return { outputFile, inputs, legacySingle: false };
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
const { outputFile, inputs, legacySingle } = parseArgs(args);

const benchmarks: GoogleBenchmark[] = [];
let totalMs = 0;
const parsedInputs = inputs.map((input) => {
  const gtest: GTestOutput = JSON.parse(readFileSync(input.path, "utf8"));
  const inputMs = parseTimeToMs(gtest.time);
  totalMs += inputMs;
  return { ...input, gtest, inputMs };
});

// Overall total
pushBenchmark(benchmarks, "Tests/Total/All", totalMs);

for (const input of parsedInputs) {
  if (!legacySingle) {
    pushBenchmark(benchmarks, `Tests/Binaries/${input.label}`, input.inputMs);
  }

  for (const suite of input.gtest.testsuites) {
    const suiteName = legacySingle
      ? `Tests/Suites/${suite.name}`
      : `Tests/Suites/${input.label}/${suite.name}`;
    pushBenchmark(benchmarks, suiteName, parseTimeToMs(suite.time));

    for (const testCase of suite.testsuite) {
      const caseName = legacySingle
        ? `Tests/Cases/${suite.name}.${testCase.name}`
        : `Tests/Cases/${input.label}/${suite.name}.${testCase.name}`;
      pushBenchmark(benchmarks, caseName, parseTimeToMs(testCase.time));
    }
  }
}

writeFileSync(outputFile, JSON.stringify({ benchmarks }, null, 2));
console.log(
  `Converted ${inputs.length} gtest file${inputs.length === 1 ? "" : "s"} -> ${outputFile}`
);
