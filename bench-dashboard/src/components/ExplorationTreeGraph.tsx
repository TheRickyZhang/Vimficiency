import { useMemo, useRef, useState, useCallback } from 'react';
import { hierarchy, tree as d3Tree } from 'd3-hierarchy';
import type { ExploredStateEntry, FoundResultEntry } from '../types/exploration';
import { buildTree, type TreeNode } from './ExplorationTree';

interface HierarchyData {
  name: string;
  fullSeq: string;
  effort: number;
  count: number;
  directCount: number;
  children?: HierarchyData[];
}

function toHierarchyData(node: TreeNode): HierarchyData {
  const sorted = [...node.children.values()].sort((a, b) => b.count - a.count);
  return {
    name: node.move,
    fullSeq: node.fullSeq,
    effort: node.effort,
    count: node.count,
    directCount: node.directCount,
    children: sorted.length > 0 ? sorted.map(toHierarchyData) : undefined,
  };
}

// Curved link path (horizontal tree: depth → x, spread → y)
function linkPath(sx: number, sy: number, tx: number, ty: number): string {
  const mx = (sx + tx) / 2;
  return `M${sx},${sy}C${mx},${sy} ${mx},${ty} ${tx},${ty}`;
}

/** Compute token-level prefixes from a token array (matching tree node fullSeq values). */
function tokenPrefixes(tokens: string[]): string[] {
  const prefixes: string[] = [];
  for (let i = 1; i <= tokens.length; i++) {
    prefixes.push(tokens.slice(0, i).join(''));
  }
  return prefixes;
}

interface Props {
  states: ExploredStateEntry[];
  results?: FoundResultEntry[];
  selectedSeq?: string | null;
}

export function ExplorationTreeGraph({ states, results, selectedSeq }: Props) {
  const containerRef = useRef<HTMLDivElement>(null);
  const [zoom, setZoom] = useState(1);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const [dragging, setDragging] = useState(false);
  const dragStart = useRef({ x: 0, y: 0, panX: 0, panY: 0 });
  const [tooltip, setTooltip] = useState<{ x: number; y: number; text: string } | null>(null);

  // Build set of found sequence strings for highlighting endpoints
  const foundSeqs = useMemo(() => {
    const s = new Set<string>();
    if (results) for (const r of results) s.add(r.tokens.join(''));
    return s;
  }, [results]);

  // Token-level prefixes of ALL found sequences
  const allFoundPrefixes = useMemo(() => {
    const s = new Set<string>();
    if (results) {
      for (const r of results) {
        for (const p of tokenPrefixes(r.tokens)) s.add(p);
      }
    }
    return s;
  }, [results]);

  // Token-level prefixes of ONLY the selected sequence's tokens
  const selectedPrefixes = useMemo(() => {
    const s = new Set<string>();
    if (selectedSeq && results) {
      const sel = results.find((r) => r.tokens.join('') === selectedSeq);
      if (sel) {
        for (const p of tokenPrefixes(sel.tokens)) s.add(p);
      }
    }
    return s;
  }, [selectedSeq, results]);

  const { root, width, height } = useMemo(() => {
    const tree = buildTree(states);
    const data = toHierarchyData(tree);
    const root = hierarchy(data);

    const nodeCount = root.descendants().length;
    const vSpacing = Math.max(16, Math.min(24, 400 / Math.sqrt(nodeCount)));
    const hSpacing = 100;

    const layout = d3Tree<HierarchyData>().nodeSize([vSpacing, hSpacing]);
    layout(root);

    // Compute bounding box (x/y are always defined after layout())
    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    for (const node of root.descendants()) {
      const nx = node.x!, ny = node.y!;
      if (nx < minX) minX = nx;
      if (nx > maxX) maxX = nx;
      if (ny < minY) minY = ny;
      if (ny > maxY) maxY = ny;
    }

    // Shift all nodes so the tree starts at (margin, margin)
    const margin = 40;
    for (const node of root.descendants()) {
      node.x = node.x! - minX + margin;
      node.y = node.y! - minY + margin;
    }

    const width = (maxY - minY) + margin * 2 + 60; // extra for labels
    const height = (maxX - minX) + margin * 2;

    return { root, width, height };
  }, [states]);

  const handleWheel = useCallback((e: React.WheelEvent) => {
    e.preventDefault();
    const delta = e.deltaY > 0 ? 0.9 : 1.1;
    setZoom((z) => Math.max(0.1, Math.min(5, z * delta)));
  }, []);

  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    if (e.button !== 0) return;
    setDragging(true);
    dragStart.current = { x: e.clientX, y: e.clientY, panX: pan.x, panY: pan.y };
  }, [pan]);

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    if (!dragging) return;
    setPan({
      x: dragStart.current.panX + (e.clientX - dragStart.current.x),
      y: dragStart.current.panY + (e.clientY - dragStart.current.y),
    });
  }, [dragging]);

  const handleMouseUp = useCallback(() => setDragging(false), []);

  if (!states.length) return null;

  const nodes = root.descendants();
  const links = root.links();

  return (
    <div style={{ position: 'relative' }}>
      <div style={{
        fontSize: '0.75rem', color: '#888', marginBottom: 4, userSelect: 'none',
      }}>
        Scroll to zoom, drag to pan. {nodes.length} nodes.
      </div>
      <div
        ref={containerRef}
        style={{
          overflow: 'hidden',
          border: '1px solid #e0e0e0',
          borderRadius: 6,
          background: '#fafafa',
          height: Math.min(600, height * zoom + 40),
          cursor: dragging ? 'grabbing' : 'grab',
          position: 'relative',
        }}
        onWheel={handleWheel}
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={handleMouseUp}
      >
        <svg
          width={width * zoom}
          height={height * zoom}
          style={{
            transform: `translate(${pan.x}px, ${pan.y}px)`,
            willChange: 'transform',
          }}
        >
          <g transform={`scale(${zoom})`}>
            {/* Links */}
            {links.map((link, i) => {
              const seq = link.target.data.fullSeq;
              const isSelected = selectedSeq ? selectedPrefixes.has(seq) : false;
              const isOnFoundPath = allFoundPrefixes.has(seq);
              const highlight = isSelected || (!selectedSeq && isOnFoundPath);
              return (
                <path
                  key={i}
                  d={linkPath(link.source.y!, link.source.x!, link.target.y!, link.target.x!)}
                  fill="none"
                  stroke={isSelected ? '#1976d2' : highlight ? '#4caf50' : '#d0d0d0'}
                  strokeWidth={isSelected ? 2.5 : highlight ? 2 : 1}
                  opacity={selectedSeq ? (isSelected ? 1 : 0.3) : (highlight ? 1 : 0.6)}
                />
              );
            })}
            {/* Nodes */}
            {nodes.map((node, i) => {
              const seq = node.data.fullSeq;
              const isFoundEnd = foundSeqs.has(seq);
              const isSelected = selectedSeq ? selectedPrefixes.has(seq) : false;
              const isSelectedEnd = selectedSeq === seq;
              const isOnFoundPath = allFoundPrefixes.has(seq);
              const isRoot = node.depth === 0;

              const highlight = isSelected || (!selectedSeq && isOnFoundPath);
              const dimmed = selectedSeq != null && !isSelected;

              const r = (isFoundEnd || isSelectedEnd) ? 5 : isRoot ? 4 : 3;
              const fill = isSelectedEnd ? '#1976d2'
                : isSelected ? '#64b5f6'
                : dimmed ? '#ddd'
                : isFoundEnd ? '#4caf50'
                : (highlight && isOnFoundPath) ? '#81c784'
                : node.data.directCount > 0 ? '#4285f4'
                : '#bbb';

              return (
                <g
                  key={i}
                  transform={`translate(${node.y},${node.x})`}
                  onMouseEnter={(e) => {
                    const rect = containerRef.current?.getBoundingClientRect();
                    if (!rect) return;
                    setTooltip({
                      x: e.clientX - rect.left,
                      y: e.clientY - rect.top - 30,
                      text: `${seq || '(start)'} | effort: ${node.data.effort.toFixed(2)} | ${node.data.count} states`,
                    });
                  }}
                  onMouseLeave={() => setTooltip(null)}
                  style={{ cursor: 'default' }}
                  opacity={dimmed ? 0.4 : 1}
                >
                  <circle
                    r={r}
                    fill={fill}
                    stroke={isSelectedEnd ? '#0d47a1' : isFoundEnd ? '#2e7d32' : 'none'}
                    strokeWidth={(isSelectedEnd || isFoundEnd) ? 2 : 0}
                  />
                  <text
                    x={8}
                    dy="0.35em"
                    fontSize={11}
                    fontFamily="monospace"
                    fontWeight={(isFoundEnd || isSelectedEnd) ? 700 : 400}
                    fill={dimmed ? '#aaa' : isSelectedEnd ? '#0d47a1' : isFoundEnd ? '#2e7d32' : '#333'}
                  >
                    {isRoot ? '' : node.data.name}
                  </text>
                </g>
              );
            })}
          </g>
        </svg>
        {/* Tooltip */}
        {tooltip && (
          <div style={{
            position: 'absolute',
            left: tooltip.x,
            top: tooltip.y,
            background: 'rgba(0,0,0,0.8)',
            color: 'white',
            padding: '4px 8px',
            borderRadius: 4,
            fontSize: '0.8rem',
            fontFamily: 'monospace',
            pointerEvents: 'none',
            whiteSpace: 'nowrap',
            zIndex: 10,
          }}>
            {tooltip.text}
          </div>
        )}
      </div>
    </div>
  );
}
