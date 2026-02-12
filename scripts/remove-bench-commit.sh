#!/usr/bin/env bash
# Remove a single commit's benchmark data from gh-pages.
# Usage: ./scripts/remove-bench-commit.sh <7-char-sha>
set -euo pipefail

SHA="${1:-}"
if [ -z "$SHA" ] || [ ${#SHA} -lt 7 ]; then
  echo "Usage: $0 <7-char-sha>"
  exit 1
fi

git fetch origin gh-pages
git checkout gh-pages
git pull origin gh-pages

for dir in bench/edit bench/motion bench/composition; do
  [ -f "$dir/data.js" ] || continue
  node -e "
    const fs = require('fs');
    const src = fs.readFileSync('$dir/data.js', 'utf8');
    const json = src.replace(/^window\.BENCHMARK_DATA\s*=\s*/, '').replace(/;\s*$/, '');
    const data = JSON.parse(json);
    for (const [key, runs] of Object.entries(data.entries)) {
      const before = runs.length;
      data.entries[key] = runs.filter(r => r.commit.id.indexOf('$SHA') !== 0);
      console.log('$dir: ' + before + ' -> ' + data.entries[key].length);
    }
    fs.writeFileSync('$dir/data.js', 'window.BENCHMARK_DATA = ' + JSON.stringify(data) + ';\n');
  "
done

git add bench/edit/data.js bench/motion/data.js bench/composition/data.js
git -c user.name=github-actions -c user.email=github-actions@github.com \
  commit -m "Remove benchmark data for $SHA" || { echo "Nothing to remove"; git checkout -; exit 1; }
git push origin gh-pages
git checkout -
echo "Done — removed $SHA from benchmark data"
