import { useState, useMemo, useRef, useEffect, useCallback } from 'react';
import { hierarchy, tree as d3tree } from 'd3-hierarchy';
import { select } from 'd3-selection';
import { zoom as d3zoom, zoomIdentity } from 'd3-zoom';
import type { ExploredStateEntry, FoundResultEntry } from '../types/exploration';

// --- TreeNode ---

export interface TreeNode {
  move: string;
  fullSeq: string;
  effort: number;
  count: number;
  directCount: number;
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
    if (!tokens.length) { root.directCount++; root.effort = state.effort; continue; }
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
      node = child;
    }
  }
  return root;
}

// --- Solution path marking ---

function markSolutionPaths(root: TreeNode, selectedSeqs: Set<string>): Set<string> {
  const onPath = new Set<string>();
  for (const seq of selectedSeqs) {
    const walk = (node: TreeNode): boolean => {
      if (node.fullSeq === seq) { onPath.add(node.fullSeq); return true; }
      for (const child of node.children.values()) {
        if (seq.startsWith(child.fullSeq) && walk(child)) {
          onPath.add(node.fullSeq);
          return true;
        }
      }
      return false;
    };
    walk(root);
  }
  return onPath;
}

// --- Visible tree ---
//
// Three expansion levels (total order):
//   0: hidden — children replaced by a single "..." summary
//   1: partial — solution-path children + fill to MIN_CHILDREN total + "..." for rest
//   2: full — all children shown
//
// Default: solution-path nodes (and root) → level 1, others → level 0
// Clicking a node cycles: 0 → 1 → 2 → 0

const MIN_CHILDREN = 3;

interface VisibleNode {
  id: string;
  move: string;
  fullSeq: string;
  effort: number;
  count: number;
  isFound: boolean;
  isOnPath: boolean;
  isSummary: boolean;
  summaryBranches: number;
  summaryNodes: number;
  parentFullSeq: string;
  childCount: number;
  level: number;
  children?: VisibleNode[];
}

function makeSummary(parentSeq: string, branches: TreeNode[]): VisibleNode {
  const totalNodes = branches.reduce((sum, c) => sum + c.count, 0);
  return {
    id: parentSeq + '::...',
    move: '...',
    fullSeq: parentSeq + '::...',
    effort: 0,
    count: totalNodes,
    isFound: false,
    isOnPath: false,
    isSummary: true,
    summaryBranches: branches.length,
    summaryNodes: totalNodes,
    parentFullSeq: parentSeq,
    childCount: 0,
    level: 0,
    children: undefined,
  };
}

function buildVisibleTree(
  root: TreeNode,
  solutionPaths: Set<string>,
  foundSeqs: Set<string>,
  overrides: Map<string, number>,
): VisibleNode {
  const convert = (node: TreeNode): VisibleNode => {
    const { fullSeq } = node;
    const isOnPath = solutionPaths.has(fullSeq);
    const isFound = foundSeqs.has(fullSeq);
    const defaultLevel = (isOnPath || fullSeq === '') ? 1 : 0;
    const level = overrides.get(fullSeq) ?? defaultLevel;

    const sorted = [...node.children.values()].sort((a, b) => b.count - a.count);

    const mk = (children?: VisibleNode[]): VisibleNode => ({
      id: fullSeq || '_root',
      move: node.move,
      fullSeq,
      effort: node.effort,
      count: node.count,
      isFound,
      isOnPath,
      isSummary: false,
      summaryBranches: 0,
      summaryNodes: 0,
      parentFullSeq: '',
      childCount: node.children.size,
      level,
      children,
    });

    if (sorted.length === 0) return mk();

    // Level 0: single "..." for all children
    if (level === 0) return mk([makeSummary(fullSeq, sorted)]);

    // Level 2: all children
    if (level === 2) return mk(sorted.map(c => convert(c)));

    // Level 1: path children + fill to MIN_CHILDREN
    const pathCh: TreeNode[] = [];
    const otherCh: TreeNode[] = [];
    for (const c of sorted) {
      if (solutionPaths.has(c.fullSeq)) pathCh.push(c);
      else otherCh.push(c);
    }

    const vis: VisibleNode[] = pathCh.map(c => convert(c));
    const fill = Math.max(0, MIN_CHILDREN - pathCh.length);
    const shown = Math.min(fill, otherCh.length);
    for (let i = 0; i < shown; i++) vis.push(convert(otherCh[i]!));

    const hidden = otherCh.slice(shown);
    if (hidden.length > 0) vis.push(makeSummary(fullSeq, hidden));

    return mk(vis.length > 0 ? vis : undefined);
  };

  return convert(root);
}

// --- Layout ---

const NODE_H = 36;
const NODE_SPACING_Y = 70;
const CHAR_W_MONO = 7.8;
const CHAR_W_SMALL = 5.4;
const NODE_PAD = 8;
const MIN_NODE_W = 30;
const NODE_GAP = 12;

function computeNodeWidth(node: VisibleNode): number {
  if (node.isSummary) {
    const topW = 3 * CHAR_W_MONO; // "..."
    const subText = `${node.summaryNodes} st`;
    const subW = subText.length * CHAR_W_SMALL;
    return Math.max(MIN_NODE_W, Math.ceil(Math.max(topW, subW)) + NODE_PAD);
  }
  const moveText = node.fullSeq === '' ? '(start)' : node.move;
  const subText = `${node.effort.toFixed(1)} \u00B7 ${node.count}`;
  const moveW = moveText.length * CHAR_W_MONO;
  const subW = subText.length * CHAR_W_SMALL;
  return Math.max(MIN_NODE_W, Math.ceil(Math.max(moveW, subW)) + NODE_PAD);
}

// --- Node colors (subtle differences) ---

function nodeFill(node: VisibleNode): string {
  if (node.isFound) return '#c8e6c9';
  if (node.childCount === 0) return '#ffffff';
  if (node.level === 2) return '#f2f8f2';
  if (node.level === 1) return '#f0f4ff';
  return '#f5f5f5';
}

function nodeStroke(node: VisibleNode): string {
  if (node.isFound) return '#2e7d32';
  return '#d0d0d0';
}

// --- Component ---

interface Props {
  states: ExploredStateEntry[];
  results?: FoundResultEntry[];
  selectedSeqs: Set<string>;
  /** Token indices (0-based depth-1) that can be expanded via right-click/ctrl+click */
  expandableTokens?: Set<number>;
  onChunkDetail?: (tokenIndex: number, tokenText: string) => void;
}

export function ExplorationTree({ states, results, selectedSeqs, expandableTokens, onChunkDetail }: Props) {
  const svgRef = useRef<SVGSVGElement>(null);
  const gRef = useRef<SVGGElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const [overrides, setOverrides] = useState<Map<string, number>>(new Map());
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [tooltip, setTooltip] = useState<{ x: number; y: number; node: VisibleNode } | null>(null);
  const [zoomScale, setZoomScale] = useState(1);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; tokenIndex: number; tokenText: string } | null>(null);

  // Merge states + results for complete root-to-leaf paths
  const allEntries = useMemo(() => {
    const entries: ExploredStateEntry[] = [...states];
    const keys = new Set(states.map(s => s.tokens.join('\0')));
    if (results) {
      for (const r of results) {
        if (!keys.has(r.tokens.join('\0'))) entries.push({ effort: r.effort, tokens: r.tokens });
      }
    }
    return entries;
  }, [states, results]);

  const treeRoot = useMemo(() => buildTree(allEntries), [allEntries]);

  const solutionPaths = useMemo(
    () => markSolutionPaths(treeRoot, selectedSeqs),
    [treeRoot, selectedSeqs],
  );

  // Reset overrides when selection changes
  const selKey = useMemo(() => [...selectedSeqs].sort().join('\0'), [selectedSeqs]);
  useEffect(() => { setOverrides(new Map()); }, [selKey]);

  // Use selectedSeqs (not all foundSeqs) for green highlighting
  const visibleRoot = useMemo(
    () => buildVisibleTree(treeRoot, solutionPaths, selectedSeqs, overrides),
    [treeRoot, solutionPaths, selectedSeqs, overrides],
  );

  const nodeWidths = useMemo(() => {
    const m = new Map<string, number>();
    const walk = (n: VisibleNode) => { m.set(n.id, computeNodeWidth(n)); n.children?.forEach(walk); };
    walk(visibleRoot);
    return m;
  }, [visibleRoot]);

  const layout = useMemo(() => {
    const root = hierarchy(visibleRoot, d => d.children);
    d3tree<VisibleNode>()
      .nodeSize([1, NODE_SPACING_Y])
      .separation((a, b) => {
        const wa = nodeWidths.get(a.data.id) ?? 60;
        const wb = nodeWidths.get(b.data.id) ?? 60;
        return (wa + wb) / 2 + NODE_GAP;
      })(root);
    return root;
  }, [visibleRoot, nodeWidths]);

  // d3-zoom
  useEffect(() => {
    const svg = svgRef.current;
    const g = gRef.current;
    if (!svg || !g) return;
    const zoomBehavior = d3zoom<SVGSVGElement, unknown>()
      .scaleExtent([0.1, 3])
      .on('zoom', e => {
        select(g).attr('transform', e.transform.toString());
        setZoomScale(e.transform.k);
      });
    select(svg).call(zoomBehavior);
    const rect = svg.getBoundingClientRect();
    select(svg).call(zoomBehavior.transform, zoomIdentity.translate(rect.width / 2, 40));
    return () => { select(svg).on('.zoom', null); };
  }, []);

  // Fullscreen API
  useEffect(() => {
    const handler = () => setIsFullscreen(!!document.fullscreenElement);
    document.addEventListener('fullscreenchange', handler);
    return () => document.removeEventListener('fullscreenchange', handler);
  }, []);

  const toggleFullscreen = useCallback(() => {
    if (document.fullscreenElement) document.exitFullscreen();
    else containerRef.current?.requestFullscreen();
  }, []);

  // Click node to cycle expansion (0 → 1 → 2 → 0)
  const solRef = useRef(solutionPaths);
  solRef.current = solutionPaths;

  const toggleNode = useCallback((fullSeq: string) => {
    setOverrides(prev => {
      const next = new Map(prev);
      const isOnPath = solRef.current.has(fullSeq);
      const def = (isOnPath || fullSeq === '') ? 1 : 0;
      const cur = prev.get(fullSeq) ?? def;
      const nxt = (cur + 1) % 3;
      if (nxt === def) next.delete(fullSeq);
      else next.set(fullSeq, nxt);
      return next;
    });
  }, []);

  if (!states.length) return null;

  const nodes = layout.descendants();
  const links = layout.links();

  return (
    <div
      ref={containerRef}
      className="relative"
      style={{ height: isFullscreen ? '100vh' : 500, background: '#fafafa' }}
    >
      <svg
        ref={svgRef}
        width="100%"
        height="100%"
        style={{ cursor: 'grab', background: '#fafafa', borderRadius: isFullscreen ? 0 : 8 }}
      >
        <g ref={gRef}>
          {links.map((link, i) => {
            const sx = link.source.x!;
            const sy = link.source.y! + NODE_H / 2;
            const tx = link.target.x!;
            const ty = link.target.y! - NODE_H / 2;
            const my = (sy + ty) / 2;
            const td = link.target.data;
            return (
              <path
                key={i}
                d={`M${sx},${sy} C${sx},${my} ${tx},${my} ${tx},${ty}`}
                fill="none"
                stroke={td.isOnPath && !td.isSummary ? '#34a853' : '#ccc'}
                strokeWidth={td.isOnPath && !td.isSummary ? 2 : 1}
                strokeDasharray={td.isSummary ? '4,3' : undefined}
              />
            );
          })}

          {nodes.map(d => {
            const node = d.data;
            const x = d.x!;
            const y = d.y!;
            const w = nodeWidths.get(node.id) ?? 60;
            const showDetail = zoomScale >= 0.6;

            if (node.isSummary) {
              return (
                <g
                  key={node.id}
                  transform={`translate(${x},${y})`}
                  style={{ cursor: 'pointer' }}
                  onClick={() => toggleNode(node.parentFullSeq)}
                  onMouseEnter={e => showTooltip(e, node)}
                  onMouseLeave={() => setTooltip(null)}
                >
                  <rect
                    x={-w / 2} y={-NODE_H / 2} width={w} height={NODE_H} rx={6}
                    fill="#f5f5f5" stroke="#999" strokeWidth={1} strokeDasharray="4,3"
                  />
                  <text textAnchor="middle" dominantBaseline="central" y={showDetail ? -4 : 0} fontSize={11} fill="#888">
                    ...
                  </text>
                  {showDetail && (
                    <text textAnchor="middle" dominantBaseline="central" y={10} fontSize={9} fill="#aaa">
                      {node.summaryNodes} st
                    </text>
                  )}
                </g>
              );
            }

            const fill = nodeFill(node);
            const stroke = nodeStroke(node);
            const isRoot = node.fullSeq === '';
            const clickable = node.childCount > 0;
            const tokenIndex = d.depth - 1; // depth 1 = token 0

            return (
              <g
                key={node.id}
                transform={`translate(${x},${y})`}
                style={{ cursor: clickable ? 'pointer' : 'default' }}
                onClick={(e) => {
                  if (onChunkDetail && expandableTokens?.has(tokenIndex) && (e.ctrlKey || e.metaKey)) {
                    onChunkDetail(tokenIndex, node.move);
                    return;
                  }
                  if (clickable) toggleNode(node.fullSeq);
                }}
                onContextMenu={(e) => {
                  if (!onChunkDetail || !expandableTokens?.has(tokenIndex)) return;
                  e.preventDefault();
                  const containerRect = containerRef.current?.getBoundingClientRect();
                  if (containerRect) {
                    setContextMenu({
                      x: e.clientX - containerRect.left,
                      y: e.clientY - containerRect.top,
                      tokenIndex,
                      tokenText: node.move,
                    });
                  }
                }}
                onMouseEnter={e => showTooltip(e, node)}
                onMouseLeave={() => setTooltip(null)}
              >
                <rect
                  x={-w / 2} y={-NODE_H / 2} width={w} height={NODE_H} rx={6}
                  fill={fill} stroke={stroke} strokeWidth={node.isFound ? 2 : 1}
                />
                <text
                  textAnchor="middle" dominantBaseline="central" y={showDetail ? -4 : 0}
                  fontSize={isRoot ? 11 : 13} fontFamily="monospace"
                  fontWeight={node.isFound ? 700 : 600}
                  fill={node.isFound ? '#2e7d32' : '#333'}
                >
                  {isRoot ? '(start)' : node.move}
                </text>
                {showDetail && (
                  <text textAnchor="middle" dominantBaseline="central" y={12} fontSize={9} fill="#888">
                    {node.effort.toFixed(1)} {'\u00B7'} {node.count}
                  </text>
                )}
              </g>
            );
          })}
        </g>
      </svg>

      {/* Fullscreen toggle */}
      <button
        onClick={toggleFullscreen}
        className="fullscreen-btn"
        title={isFullscreen ? 'Exit fullscreen' : 'Fullscreen'}
      >
        <svg viewBox="0 0 16 16" width="14" height="14">
          {isFullscreen ? (
            <path d="M5 2v3H2M11 2v3h3M2 11h3v3M14 11h-3v3" fill="none" stroke="currentColor" strokeWidth="1.5" />
          ) : (
            <path d="M2 6V2h4M10 2h4v4M14 10v4h-4M6 14H2v-4" fill="none" stroke="currentColor" strokeWidth="1.5" />
          )}
        </svg>
      </button>

      {/* Tooltip */}
      {tooltip && (
        <div
          className="absolute pointer-events-none bg-[#333] text-white text-xs px-2.5 py-1.5 rounded shadow-lg"
          style={{ left: tooltip.x, top: tooltip.y, transform: 'translate(-50%, 4px)', zIndex: 10 }}
        >
          {tooltip.node.isSummary ? (
            <>
              {tooltip.node.summaryBranches} branch{tooltip.node.summaryBranches !== 1 ? 'es' : ''},{' '}
              {tooltip.node.summaryNodes} states
              <br /><span className="text-[#aaa]">Click to expand</span>
            </>
          ) : (
            <>
              <span className="font-bold">{tooltip.node.fullSeq || '(start)'}</span>
              <br />effort: {tooltip.node.effort.toFixed(2)} {'\u00B7'} {tooltip.node.count} state{tooltip.node.count !== 1 ? 's' : ''}
              {tooltip.node.isFound && <><br /><span className="text-[#4caf50] font-bold">found result</span></>}
              {tooltip.node.childCount > 0 && <><br /><span className="text-[#aaa]">Click to toggle expansion</span></>}
            </>
          )}
        </div>
      )}

      {/* Context menu */}
      {contextMenu && (
        <>
          <div className="fixed inset-0 z-30" onClick={() => setContextMenu(null)} onContextMenu={(e) => { e.preventDefault(); setContextMenu(null); }} />
          <div
            className="absolute z-40 bg-white border border-[#ccc] rounded shadow-lg py-1 min-w-[140px]"
            style={{ left: contextMenu.x, top: contextMenu.y }}
          >
            <div
              className="px-3 py-1.5 text-sm cursor-pointer hover:bg-[#f0f4ff]"
              onClick={() => {
                onChunkDetail?.(contextMenu.tokenIndex, contextMenu.tokenText);
                setContextMenu(null);
              }}
            >
              Expand chunk
            </div>
          </div>
        </>
      )}

      {/* Legend */}
      <div className="absolute bottom-2 right-2 text-xs text-muted flex gap-3 bg-white/80 px-2 py-1 rounded">
        <span><span className="inline-block w-2.5 h-2.5 rounded-sm bg-[#34a853] mr-1 align-middle" />solution path</span>
        <span><span className="inline-block w-2.5 h-2.5 rounded-sm border border-dashed border-[#999] bg-[#f5f5f5] mr-1 align-middle" />collapsed</span>
        <span>scroll to zoom, drag to pan</span>
      </div>
    </div>
  );

  function showTooltip(e: React.MouseEvent, node: VisibleNode) {
    const containerRect = containerRef.current?.getBoundingClientRect();
    const nodeRect = (e.currentTarget as SVGElement).getBoundingClientRect();
    if (containerRect) {
      setTooltip({
        x: nodeRect.left + nodeRect.width / 2 - containerRect.left,
        y: nodeRect.bottom - containerRect.top,
        node,
      });
    }
  }
}
