import { useState, useMemo } from 'react';
import type { ExploredStateEntry } from '../types/exploration';

interface TreeNode {
  move: string;       // the motion/edit that leads to this node
  fullSeq: string;    // full sequence from root
  effort: number;     // effort at this node (min of all states with this prefix)
  count: number;      // number of explored states at or below this node
  directCount: number; // states exactly at this node
  children: Map<string, TreeNode>;
}

// Simple tokenizer: splits Vim sequence into individual moves
// Handles: digits+letter (counted), f/F/t/T+char, single chars, <Esc>, etc.
function tokenize(seq: string): string[] {
  const tokens: string[] = [];
  let i = 0;
  while (i < seq.length) {
    // Counted motion: digits followed by a letter
    if (seq[i]! >= '0' && seq[i]! <= '9') {
      let j = i;
      while (j < seq.length && seq[j]! >= '0' && seq[j]! <= '9') j++;
      if (j < seq.length) {
        // Check for two-char commands after count: dd, dw, de, db, etc.
        if (j + 1 < seq.length && seq[j] === 'd' && 'dewbWBEjk'.includes(seq[j + 1]!)) {
          tokens.push(seq.substring(i, j + 2));
          i = j + 2;
        } else {
          tokens.push(seq.substring(i, j + 1));
          i = j + 1;
        }
      } else {
        tokens.push(seq.substring(i));
        i = j;
      }
    }
    // f/F/t/T + target char
    else if ('fFtT'.includes(seq[i]!) && i + 1 < seq.length) {
      tokens.push(seq.substring(i, i + 2));
      i += 2;
    }
    // Two-char operators: dd, dw, de, db, dW, dB, dE, gj, gk, ge, gE, gg
    else if (i + 1 < seq.length && (
      (seq[i] === 'd' && 'dewbWBEjk$0'.includes(seq[i + 1]!)) ||
      (seq[i] === 'g' && 'jkeEg'.includes(seq[i + 1]!))
    )) {
      tokens.push(seq.substring(i, i + 2));
      i += 2;
    }
    // Special: <Esc> represented as \x1b or similar
    else if (seq[i] === '\x1b') {
      tokens.push('<Esc>');
      i++;
    }
    // Single char
    else {
      tokens.push(seq[i]!);
      i++;
    }
  }
  return tokens;
}

function buildTree(states: ExploredStateEntry[]): TreeNode {
  const root: TreeNode = {
    move: '(root)',
    fullSeq: '',
    effort: 0,
    count: states.length,
    directCount: 0,
    children: new Map(),
  };

  for (const state of states) {
    if (!state.seq) {
      root.directCount++;
      root.effort = state.effort;
      continue;
    }

    const tokens = tokenize(state.seq);
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

interface TreeNodeProps {
  node: TreeNode;
  depth: number;
  totalStates: number;
  defaultExpanded: boolean;
}

function TreeNodeRow({ node, depth, totalStates, defaultExpanded }: TreeNodeProps) {
  const [expanded, setExpanded] = useState(defaultExpanded);
  const hasChildren = node.children.size > 0;
  const pct = ((node.count / totalStates) * 100).toFixed(1);

  // Sort children by count descending
  const sortedChildren = useMemo(() =>
    [...node.children.values()].sort((a, b) => b.count - a.count),
    [node.children]
  );

  // Color based on % of total exploration
  const barWidth = Math.max(2, (node.count / totalStates) * 100);
  const barColor = node.count > totalStates * 0.1 ? '#4285f4'
    : node.count > totalStates * 0.02 ? '#34a853'
    : '#ccc';

  return (
    <>
      <tr
        onClick={() => hasChildren && setExpanded(!expanded)}
        style={{
          cursor: hasChildren ? 'pointer' : 'default',
          background: depth === 0 ? '#f8f9fa' : undefined,
        }}
      >
        <td style={{ padding: '3px 8px', whiteSpace: 'nowrap' }}>
          <span style={{ display: 'inline-block', width: depth * 20 }} />
          {hasChildren ? (
            <span style={{ display: 'inline-block', width: 16, color: '#888', fontSize: '0.8rem' }}>
              {expanded ? '\u25BC' : '\u25B6'}
            </span>
          ) : (
            <span style={{ display: 'inline-block', width: 16 }} />
          )}
          <code style={{
            fontWeight: depth === 0 ? 400 : 600,
            fontSize: '0.9rem',
            background: depth > 0 ? '#f0f0f0' : undefined,
            padding: depth > 0 ? '1px 4px' : undefined,
            borderRadius: 3,
          }}>
            {node.move}
          </code>
        </td>
        <td style={{ padding: '3px 8px', textAlign: 'right', fontSize: '0.85rem', color: '#555' }}>
          {node.effort.toFixed(2)}
        </td>
        <td style={{ padding: '3px 8px', textAlign: 'right', fontSize: '0.85rem' }}>
          {node.count}
        </td>
        <td style={{ padding: '3px 8px', width: 120 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <div style={{
              height: 8, width: `${barWidth}%`, background: barColor,
              borderRadius: 4, minWidth: 2, transition: 'width 0.2s',
            }} />
            <span style={{ fontSize: '0.75rem', color: '#888' }}>{pct}%</span>
          </div>
        </td>
      </tr>
      {expanded && sortedChildren.map((child) => (
        <TreeNodeRow
          key={child.move}
          node={child}
          depth={depth + 1}
          totalStates={totalStates}
          defaultExpanded={depth < 1 && child.count > totalStates * 0.1}
        />
      ))}
    </>
  );
}

interface Props {
  states: ExploredStateEntry[];
}

export function ExplorationTree({ states }: Props) {
  const tree = useMemo(() => buildTree(states), [states]);

  if (!states.length) return null;

  // Sort root children by count descending
  const sortedRootChildren = useMemo(() =>
    [...tree.children.values()].sort((a, b) => b.count - a.count),
    [tree.children]
  );

  return (
    <div style={{ overflowX: 'auto' }}>
      <table style={{
        width: '100%', borderCollapse: 'collapse', fontSize: '0.9rem',
      }}>
        <thead>
          <tr style={{ borderBottom: '2px solid #e0e0e0', textAlign: 'left' }}>
            <th style={{ padding: '6px 8px', fontWeight: 700 }}>Move</th>
            <th style={{ padding: '6px 8px', fontWeight: 700, textAlign: 'right' }}>Min Effort</th>
            <th style={{ padding: '6px 8px', fontWeight: 700, textAlign: 'right' }}>States</th>
            <th style={{ padding: '6px 8px', fontWeight: 700 }}>Share</th>
          </tr>
        </thead>
        <tbody>
          {sortedRootChildren.map((child) => (
            <TreeNodeRow
              key={child.move}
              node={child}
              depth={0}
              totalStates={states.length}
              defaultExpanded={child.count > states.length * 0.1}
            />
          ))}
        </tbody>
      </table>
    </div>
  );
}
