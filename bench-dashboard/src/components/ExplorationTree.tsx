import { useState, useMemo } from 'react';
import type { ExploredStateEntry, FoundResultEntry } from '../types/exploration';

export interface TreeNode {
  move: string;       // the motion/edit that leads to this node
  fullSeq: string;    // full sequence from root
  effort: number;     // effort at this node (min of all states with this prefix)
  count: number;      // number of explored states at or below this node
  directCount: number; // states exactly at this node
  children: Map<string, TreeNode>;
}

export function buildTree(states: ExploredStateEntry[]): TreeNode {
  const root: TreeNode = {
    move: '(root)',
    fullSeq: '',
    effort: 0,
    count: states.length,
    directCount: 0,
    children: new Map(),
  };

  for (const state of states) {
    const tokens = state.tokens;
    if (!tokens.length) {
      root.directCount++;
      root.effort = state.effort;
      continue;
    }

    let node = root;

    for (let i = 0; i < tokens.length; i++) {
      const token = tokens[i]!;
      let child = node.children.get(token);
      if (!child) {
        child = {
          move: token,
          fullSeq: tokens.slice(0, i + 1).join(''),
          effort: state.effort,
          count: 0,
          directCount: 0,
          children: new Map(),
        };
        node.children.set(token, child);
      }
      child.count++;
      if (i === tokens.length - 1) {
        child.directCount++;
        child.effort = Math.min(child.effort, state.effort);
      }
    }
  }

  return root;
}

interface TreeNodeRowProps {
  node: TreeNode;
  depth: number;
  totalStates: number;
  defaultExpanded: boolean;
  foundSeqs: Set<string>;
  isLast: boolean;
  ancestorPrefixes: boolean[]; // for each ancestor depth, whether it's the last sibling
}

function TreeNodeRow({ node, depth, totalStates, defaultExpanded, foundSeqs, isLast, ancestorPrefixes }: TreeNodeRowProps) {
  const [expanded, setExpanded] = useState(defaultExpanded);
  const hasChildren = node.children.size > 0;
  const isLeaf = !hasChildren;
  const isFound = foundSeqs.has(node.fullSeq);

  // Sort children by count descending
  const sortedChildren = useMemo(() =>
    [...node.children.values()].sort((a, b) => b.count - a.count),
    [node.children]
  );

  // Tree connector lines
  const connectors = ancestorPrefixes.map((isLastAtDepth, i) => (
    <span key={i} className="inline-block w-5 text-center text-border select-none">
      {isLastAtDepth ? '' : '\u2502'}
    </span>
  ));

  const branchChar = isLast ? '\u2514' : '\u251C';

  return (
    <>
      <div
        className={`flex items-center py-0.5 ${hasChildren ? 'cursor-pointer hover:bg-[#f5f5f5]' : ''}`}
        onClick={() => hasChildren && setExpanded(!expanded)}
      >
        {/* Tree connectors */}
        {depth > 0 && (
          <span className="font-mono text-border text-[0.85rem] select-none whitespace-pre">
            {connectors}
            <span className="inline-block w-5 text-center">{branchChar}\u2500</span>
          </span>
        )}

        {/* Expand/collapse indicator */}
        {hasChildren ? (
          <span className="inline-block w-4 text-muted text-[0.75rem] text-center shrink-0">
            {expanded ? '\u25BC' : '\u25B6'}
          </span>
        ) : (
          <span className="inline-block w-4 shrink-0" />
        )}

        {/* Move label */}
        <code className={`text-[0.9rem] px-1 py-px rounded-[3px] ${
          isFound ? 'font-bold bg-good/15 text-good' : 'font-semibold bg-[#f0f0f0]'
        }`}>
          {node.move}
        </code>

        {/* Metadata */}
        <span className="text-[0.8rem] text-muted ml-2">
          {node.effort.toFixed(2)}
        </span>
        <span className="text-xs text-[#bbb] ml-2">
          {node.count} state{node.count !== 1 ? 's' : ''}
        </span>

        {/* Leaf tags */}
        {isLeaf && isFound && (
          <span className="ml-2 text-xs font-semibold text-good bg-good/10 px-1.5 py-px rounded">
            found
          </span>
        )}
        {isLeaf && !isFound && (
          <span className="ml-2 text-xs text-muted">
            pruned
          </span>
        )}
      </div>

      {expanded && sortedChildren.map((child, i) => (
        <TreeNodeRow
          key={child.move}
          node={child}
          depth={depth + 1}
          totalStates={totalStates}
          defaultExpanded={child.count > totalStates * 0.05}
          foundSeqs={foundSeqs}
          isLast={i === sortedChildren.length - 1}
          ancestorPrefixes={[...ancestorPrefixes, isLast]}
        />
      ))}
    </>
  );
}

interface Props {
  states: ExploredStateEntry[];
  results?: FoundResultEntry[];
}

export function ExplorationTree({ states, results }: Props) {
  const tree = useMemo(() => buildTree(states), [states]);

  const foundSeqs = useMemo(() => {
    const s = new Set<string>();
    if (results) for (const r of results) s.add(r.tokens.join(''));
    return s;
  }, [results]);

  if (!states.length) return null;

  // Sort root children by count descending
  const sortedRootChildren = useMemo(() =>
    [...tree.children.values()].sort((a, b) => b.count - a.count),
    [tree.children]
  );

  return (
    <div className="font-mono text-[0.9rem]">
      {/* Root node */}
      <div className="flex items-center py-0.5 text-muted">
        <span className="inline-block w-4" />
        <code className="text-[0.9rem] font-normal">(start)</code>
        <span className="text-xs ml-2">{states.length} states total</span>
      </div>

      {sortedRootChildren.map((child, i) => (
        <TreeNodeRow
          key={child.move}
          node={child}
          depth={1}
          totalStates={states.length}
          defaultExpanded={child.count > states.length * 0.05}
          foundSeqs={foundSeqs}
          isLast={i === sortedRootChildren.length - 1}
          ancestorPrefixes={[]}
        />
      ))}
    </div>
  );
}
