#!/usr/bin/env node
// Shared read/transform/write logic for benchmark data.js files.
//
// Usage:
//   node scripts/bench-data.js prune <dir> [max=100]
//   node scripts/bench-data.js remove <dir> <sha-prefix>

const fs = require("fs");
const path = require("path");

function readDataJs(filePath) {
  const src = fs.readFileSync(filePath, "utf8");
  const json = src
    .replace(/^window\.BENCHMARK_DATA\s*=\s*/, "")
    .replace(/;\s*$/, "");
  return JSON.parse(json);
}

function writeDataJs(filePath, data) {
  // Match benchmark-action's format exactly: no semicolon, no trailing newline
  fs.writeFileSync(
    filePath,
    "window.BENCHMARK_DATA = " + JSON.stringify(data, null, 0)
  );
}

function prune(dir, max) {
  const filePath = path.join(dir, "data.js");
  const data = readDataJs(filePath);
  let totalBefore = 0;
  let totalAfter = 0;

  for (const [key, runs] of Object.entries(data.entries)) {
    const before = runs.length;
    data.entries[key] = runs.slice(-max);
    const after = data.entries[key].length;
    totalBefore += before;
    totalAfter += after;
    console.log(`${dir}/${key}: ${before} -> ${after}`);
  }

  if (totalAfter === 0 && totalBefore > 0) {
    console.warn(`WARNING: prune would remove all entries in ${dir}`);
  }

  writeDataJs(filePath, data);
}

function remove(dir, shaPrefix) {
  const filePath = path.join(dir, "data.js");
  const data = readDataJs(filePath);
  let totalBefore = 0;
  let totalAfter = 0;

  for (const [key, runs] of Object.entries(data.entries)) {
    const before = runs.length;
    data.entries[key] = runs.filter(
      (r) => !r.commit.id.startsWith(shaPrefix)
    );
    const after = data.entries[key].length;
    totalBefore += before;
    totalAfter += after;
    console.log(`${dir}/${key}: ${before} -> ${after}`);
  }

  if (totalAfter === 0 && totalBefore > 0) {
    console.warn(
      `WARNING: remove would clear all entries in ${dir} (SHA prefix: ${shaPrefix})`
    );
  }

  writeDataJs(filePath, data);
}

// --- CLI ---
const [command, dir, ...rest] = process.argv.slice(2);

if (!command || !dir) {
  console.error(
    "Usage:\n" +
      "  node scripts/bench-data.js prune <dir> [max=100]\n" +
      "  node scripts/bench-data.js remove <dir> <sha-prefix>"
  );
  process.exit(1);
}

try {
  if (command === "prune") {
    const max = parseInt(rest[0], 10) || 100;
    prune(dir, max);
  } else if (command === "remove") {
    const shaPrefix = rest[0];
    if (!shaPrefix) {
      console.error("remove requires a SHA prefix");
      process.exit(1);
    }
    remove(dir, shaPrefix);
  } else {
    console.error(`Unknown command: ${command}`);
    process.exit(1);
  }
} catch (err) {
  console.error(`Error processing ${dir}/data.js: ${err.message}`);
  process.exit(1);
}
