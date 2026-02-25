#!/usr/bin/env bun
// Converts GTest JSON output (--gtest_output=json:...) to Google Benchmark JSON format
// so it can be ingested by bench-data.ts.
//
// Usage:
//   bun scripts/convert-gtest-timing.ts <gtest.json> <output.json>

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

function parseTimeToMs(timeStr: string): number {
  // GTest time is like "25.500s" — seconds with 's' suffix
  const match = timeStr.match(/^([\d.]+)s$/);
  if (!match) {
    throw new Error(`Cannot parse GTest time string: "${timeStr}"`);
  }
  return parseFloat(match[1]!) * 1000;
}

const args = process.argv.slice(2);
const inputFile = args[0];
const outputFile = args[1];

if (!inputFile || !outputFile) {
  console.error(
    "Usage: bun scripts/convert-gtest-timing.ts <gtest.json> <output.json>"
  );
  process.exit(1);
}

const gtest: GTestOutput = JSON.parse(readFileSync(inputFile, "utf8"));

const benchmarks: GoogleBenchmark[] = [];
let caseCount = 0;

// Overall total
const totalMs = parseTimeToMs(gtest.time);
benchmarks.push({
  name: "Tests/Total/All",
  real_time: totalMs,
  cpu_time: totalMs,
  time_unit: "ms",
});

// Per-suite totals
for (const suite of gtest.testsuites) {
  const suiteMs = parseTimeToMs(suite.time);
  benchmarks.push({
    name: `Tests/Suites/${suite.name}`,
    real_time: suiteMs,
    cpu_time: suiteMs,
    time_unit: "ms",
  });

  for (const testCase of suite.testsuite ?? []) {
    const caseMs = parseTimeToMs(testCase.time);
    benchmarks.push({
      name: `Tests/Cases/${suite.name}.${testCase.name}`,
      real_time: caseMs,
      cpu_time: caseMs,
      time_unit: "ms",
    });
    caseCount++;
  }
}

writeFileSync(outputFile, JSON.stringify({ benchmarks }, null, 2));
console.log(
  `Converted 1 total + ${gtest.testsuites.length} suites + ${caseCount} cases -> ${outputFile}`
);
