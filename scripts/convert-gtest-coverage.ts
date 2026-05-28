#!/usr/bin/env bun
// Converts GTest JSON output (--gtest_output=json:...) for the generated fuzz
// suites (Property, Safety) into a coverage snapshot. These run FuzzTest in
// unit mode, whose wall-time is meaningless (see dev/decisions.md), so the
// dashboard shows what they *cover* — suites, cases, pass status, counts —
// rather than a duration trend.
//
// Usage:
//   bun scripts/convert-gtest-coverage.ts <coverage.json> Property=property.json Safety=safety.json

import { readFileSync, writeFileSync } from "fs";

interface GTestCase {
  name: string;
  result?: string;
  failures?: unknown[];
}

interface GTestSuite {
  name: string;
  testsuite: GTestCase[];
}

interface GTestOutput {
  testsuites: GTestSuite[];
}

interface CoverageCase {
  name: string;
  passed: boolean;
}

interface CoverageSuite {
  name: string;
  failures: number;
  cases: CoverageCase[];
}

interface CoverageBinary {
  totalCases: number;
  failures: number;
  suites: CoverageSuite[];
}

function usage(): never {
  console.error(
    "Usage:\n" +
      "  bun scripts/convert-gtest-coverage.ts <coverage.json> Property=property.json Safety=safety.json"
  );
  process.exit(1);
}

function casePassed(c: GTestCase): boolean {
  if (c.result === "SKIPPED") return false;
  return !(c.failures && c.failures.length > 0);
}

const args = process.argv.slice(2);
const outputFile = args[0];
if (!outputFile || args.length < 2) usage();

const binaries: Record<string, CoverageBinary> = {};

for (const spec of args.slice(1)) {
  const equals = spec.indexOf("=");
  if (equals <= 0 || equals === spec.length - 1) {
    throw new Error(`Expected Label=path input, got "${spec}"`);
  }
  const label = spec.slice(0, equals);
  const gtest: GTestOutput = JSON.parse(readFileSync(spec.slice(equals + 1), "utf8"));

  const suites: CoverageSuite[] = gtest.testsuites.map((suite) => {
    const cases = suite.testsuite.map((c) => ({ name: c.name, passed: casePassed(c) }));
    return {
      name: suite.name,
      failures: cases.filter((c) => !c.passed).length,
      cases,
    };
  });

  binaries[label] = {
    totalCases: suites.reduce((n, s) => n + s.cases.length, 0),
    failures: suites.reduce((n, s) => n + s.failures, 0),
    suites,
  };
}

writeFileSync(outputFile, JSON.stringify({ generated: Date.now(), binaries }, null, 2));
console.log(`Wrote coverage for ${Object.keys(binaries).join(", ")} -> ${outputFile}`);
