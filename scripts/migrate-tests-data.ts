#!/usr/bin/env bun
// One-time reshape of tests/data.json to the unit+approval-only timing shape.
// Run once against the gh-pages tests/data.json (and the dev fixture), then
// delete this script. After it runs, the dashboard's legacy-handling branches
// are dead and removed.
//
// Per entry, keep only:
//   Tests/Binaries/{Unit,Approval}
//   Tests/Cases/{Unit,Approval}/<Suite>.<Case>
// Drop Tests/Total/All, Tests/Suites/*, and all Property/Safety timing.
// Legacy unit-only entries (no Tests/Binaries/*) are upgraded: Total/All
// becomes Tests/Binaries/Unit and bare Tests/Cases/<Suite>.<Case> gain the
// Unit/ label.
//
// Usage:
//   bun scripts/migrate-tests-data.ts <data.json> [out.json]   # out defaults to in place

import { readFileSync, writeFileSync } from "fs";

interface Bench { name: string; [k: string]: unknown }
interface Run { benches: Bench[]; [k: string]: unknown }
interface Data { entries: Record<string, Run[]>; [k: string]: unknown }

const TIMED = new Set(["Unit", "Approval"]);

function migrateBenches(benches: Bench[]): Bench[] {
  const legacy = !benches.some((b) => b.name.startsWith("Tests/Binaries/"));
  const out: Bench[] = [];

  for (const b of benches) {
    const n = b.name;

    if (n.startsWith("Tests/Suites/")) continue;

    if (n === "Tests/Total/All") {
      if (legacy) out.push({ ...b, name: "Tests/Binaries/Unit" });
      continue;
    }

    if (n.startsWith("Tests/Binaries/")) {
      if (TIMED.has(n.slice("Tests/Binaries/".length))) out.push(b);
      continue;
    }

    if (n.startsWith("Tests/Cases/")) {
      const rest = n.slice("Tests/Cases/".length);
      const slash = rest.indexOf("/");
      const label = legacy || slash < 0 ? "Unit" : rest.slice(0, slash);
      const tail = legacy || slash < 0 ? rest : rest.slice(slash + 1);
      if (TIMED.has(label)) out.push({ ...b, name: `Tests/Cases/${label}/${tail}` });
      continue;
    }
    // Any other Tests/* name is dropped.
  }

  return out;
}

const inputPath = process.argv[2];
const outputPath = process.argv[3] ?? inputPath;
if (!inputPath) {
  console.error("Usage: bun scripts/migrate-tests-data.ts <data.json> [out.json]");
  process.exit(1);
}

const data: Data = JSON.parse(readFileSync(inputPath, "utf8"));
let entriesChanged = 0;
for (const runs of Object.values(data.entries)) {
  for (const run of runs) {
    const before = run.benches.length;
    run.benches = migrateBenches(run.benches);
    if (run.benches.length !== before) entriesChanged++;
  }
}

writeFileSync(outputPath, JSON.stringify(data, null, 2));
console.log(`Migrated ${entriesChanged} run(s) -> ${outputPath}`);
