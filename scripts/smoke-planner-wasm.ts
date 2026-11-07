// Loads the built planner module and calls every export, because the widgets
// degrade to a "not available" message rather than failing the docs build.
// Usage: bun scripts/smoke-planner-wasm.ts <dir containing vimfy_planner.{js,wasm}>
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const dir = resolve(process.argv[2]);
const wasmBinary = await Bun.file(`${dir}/vimfy_planner.wasm`).arrayBuffer();
const { default: load } = await import(pathToFileURL(`${dir}/vimfy_planner.js`).href);
const M = await load({ wasmBinary });

function call(fn: string, ...args: (string | number)[]) {
  const raw = args.map((a) => (typeof a === 'string' ? M.stringToNewUTF8(a) : a));
  const out = M.cwrap(fn, 'number', args.map(() => 'number'))(...raw);
  const parsed = JSON.parse(M.UTF8ToString(out));
  if (typeof parsed.error === 'string') throw new Error(`${fn}: ${parsed.error}`);
  return parsed;
}

function expectNonEmpty(fn: string, items: unknown[]) {
  if (items.length === 0) throw new Error(`${fn}: returned no results`);
}

const initial = 'the quick brown fox\njumps over';
const goal = 'the slow brown fox\njumps';

expectNonEmpty('vf_plan', call('vf_plan', initial, goal, 'qwerty', 0, 0).regions);
expectNonEmpty('vf_grid', call('vf_grid', initial, goal, 'qwerty', 0, 0).blocks);
expectNonEmpty('vf_tiling', call('vf_tiling', initial, 0, 15, 'delete', 0, 0).chunks);
expectNonEmpty('vf_seal', call('vf_seal', initial, goal, 'qwerty', 0, 0).blocks);

console.log('planner wasm smoke test passed');
