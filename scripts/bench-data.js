#!/usr/bin/env node
// Shared read/transform/write logic for benchmark data.json files.
//
// Usage:
//   node scripts/bench-data.js ingest <dir> <result.json> --commit-id=... --commit-msg=... --commit-ts=... --author=... --repo-url=...
//   node scripts/bench-data.js prune <dir> [max=100]
//   node scripts/bench-data.js remove <dir> <sha-prefix>

const fs = require("fs");
const path = require("path");

// Read data.json, falling back to legacy data.js (window.BENCHMARK_DATA = ...)
function readData(dir) {
  const jsonPath = path.join(dir, "data.json");
  const jsPath = path.join(dir, "data.js");

  if (fs.existsSync(jsonPath)) {
    return JSON.parse(fs.readFileSync(jsonPath, "utf8"));
  }

  if (fs.existsSync(jsPath)) {
    const src = fs.readFileSync(jsPath, "utf8");
    const json = src
      .replace(/^window\.BENCHMARK_DATA\s*=\s*/, "")
      .replace(/;\s*$/, "");
    console.log(`  Migrating ${jsPath} -> ${jsonPath}`);
    return JSON.parse(json);
  }

  return null;
}

function writeData(dir, data) {
  fs.writeFileSync(path.join(dir, "data.json"), JSON.stringify(data, null, 0));
}

// Parse Google Benchmark JSON and append a benchmark run to data.json
function ingest(dir, resultFile, opts) {
  const result = JSON.parse(fs.readFileSync(resultFile, "utf8"));

  const benches = result.benchmarks.map((b) => ({
    name: b.name,
    value: b.real_time,
    unit: b.time_unit + "/iter",
  }));

  const entry = {
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

function prune(dir, max) {
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

function remove(dir, shaPrefix) {
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

function parseOpts(args) {
  const opts = {};
  for (const arg of args) {
    const m = arg.match(/^--([a-z-]+)=(.*)$/);
    if (m) {
      // --commit-id -> commitId
      const key = m[1].replace(/-([a-z])/g, (_, c) => c.toUpperCase());
      opts[key] = m[2];
    }
  }
  return opts;
}

if (!command) {
  console.error(
    "Usage:\n" +
      "  node scripts/bench-data.js ingest <dir> <result.json> --commit-id=... --commit-msg=... --commit-ts=... --author=... --repo-url=...\n" +
      "  node scripts/bench-data.js prune <dir> [max=100]\n" +
      "  node scripts/bench-data.js remove <dir> <sha-prefix>"
  );
  process.exit(1);
}

try {
  if (command === "ingest") {
    const dir = args[1];
    const resultFile = args[2];
    const opts = parseOpts(args.slice(3));
    if (!dir || !resultFile) {
      console.error("ingest requires <dir> and <result.json>");
      process.exit(1);
    }
    ingest(dir, resultFile, opts);
  } else if (command === "prune") {
    const dir = args[1];
    if (!dir) {
      console.error("prune requires <dir>");
      process.exit(1);
    }
    const max = parseInt(args[2], 10) || 100;
    prune(dir, max);
  } else if (command === "remove") {
    const dir = args[1];
    const shaPrefix = args[2];
    if (!dir || !shaPrefix) {
      console.error("remove requires <dir> and <sha-prefix>");
      process.exit(1);
    }
    remove(dir, shaPrefix);
  } else {
    console.error(`Unknown command: ${command}`);
    process.exit(1);
  }
} catch (err) {
  console.error(`Error: ${err.message}`);
  process.exit(1);
}
