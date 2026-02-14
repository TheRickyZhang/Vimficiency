import { useState, useMemo, useRef, useEffect, useCallback } from 'react';
import { hierarchy, tree as d3tree } from 'd3-hierarchy';
import { select } from 'd3-selection';
import { zoom as d3zoom, zoomIdentity } from 'd3-zoom';
import type { ExploredStateEntry, FoundResultEntry } from '../types/exploration';

// --- TreeNode (kept, exported for types) ---

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

// --- Solution path marking ---
// Walk from root toward each selected sequence, marking every node on the path.
// Handles shared prefixes naturally: if "jw" and "jl" are both selected,
// "j" is marked by both walks.

function markSolutionPaths(root: TreeNode, selectedSeqs: Set<string>): Set<string> {
  const onPath = new Set<string>();
  for (const seq of selectedSeqs) {
    const walk = (node: TreeNode): boolean => {
      if (node.fullSeq === seq) {
        onPath.add(node.fullSeq);
        return true;
      }
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

// --- Visible tree (d3-compatible) ---

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
  originalChildren: TreeNode[];
  children?: VisibleNode[];
}

function buildVisibleTree(
  root: TreeNode,
  solutionPaths: Set<string>,
  foundSeqs: Set<string>,
  expandedSet: Set<string>,
  collapsedSet: Set<string>,
): VisibleNode {
  const convert = (node: TreeNode): VisibleNode => {
    const isOnPath = solutionPaths.has(node.fullSeq);
    const isFound = foundSeqs.has(node.fullSeq);

    const userExpanded = expandedSet.has(node.fullSeq);
    const userCollapsed = collapsedSet.has(node.fullSeq);

    // Default: expanded if on a selected path
    const showChildren = userExpanded || (isOnPath && !userCollapsed);

    const sortedChildren = [...node.children.values()].sort((a, b) => b.count - a.count);

    const mkLeaf = (): VisibleNode => ({
      id: node.fullSeq || '_root',
      move: node.move,
      fullSeq: node.fullSeq,
      effort: node.effort,
      count: node.count,
      isFound,
      isOnPath,
      isSummary: false,
      summaryBranches: 0,
      summaryNodes: 0,
      originalChildren: sortedChildren,
      children: undefined,
    });

    if (!showChildren || sortedChildren.length === 0) return mkLeaf();

    // Separate children on a selected path from the rest
    const pathChildren: TreeNode[] = [];
    const otherChildren: TreeNode[] = [];
    for (const child of sortedChildren) {
      if (solutionPaths.has(child.fullSeq)) {
        pathChildren.push(child);
      } else {
        otherChildren.push(child);
      }
    }

    const visibleChildren: VisibleNode[] = pathChildren.map(convert);

    // Non-path children: collapse into summary unless user expanded
    if (otherChildren.length > 0) {
      const summaryId = node.fullSeq + '::summary';
      const summaryExpanded = expandedSet.has(summaryId);

      if (summaryExpanded) {
        for (const child of otherChildren) {
          visibleChildren.push(convert(child));
        }
      } else {
        const totalNodes = otherChildren.reduce((sum, c) => sum + c.count, 0);
        visibleChildren.push({
          id: summaryId,
          move: '',
          fullSeq: summaryId,
          effort: 0,
          count: totalNodes,
          isFound: false,
          isOnPath: false,
          isSummary: true,
          summaryBranches: otherChildren.length,
          summaryNodes: totalNodes,
          originalChildren: otherChildren,
          children: undefined,
        });
      }
    }

    return {
      id: node.fullSeq || '_root',
      move: node.move,
      fullSeq: node.fullSeq,
      effort: node.effort,
      count: node.count,
      isFound,
      isOnPath,
      isSummary: false,
      summaryBranches: 0,
      summaryNodes: 0,
      originalChildren: sortedChildren,
      children: visibleChildren.length > 0 ? visibleChildren : undefined,
    };
  };

  return convert(root);
}

// --- Layout constants ---
const NODE_W = 120;
const NODE_H = 36;
const NODE_SPACING_X = 140;
const NODE_SPACING_Y = 70;

// --- SVG Tree Component ---

interface Props {
  states: ExploredStateEntry[];
  results?: FoundResultEntry[];
  selectedSeqs: Set<string>;
}

export function ExplorationTree({ states, results, selectedSeqs }: Props) {
  const svgRef = useRef<SVGSVGElement>(null);
  const gRef = useRef<SVGGElement>(null);
  const [expandedSet, setExpandedSet] = useState<Set<string>>(new Set());
  const [collapsedSet, setCollapsedSet] = useState<Set<string>>(new Set());
  const [tooltip, setTooltip] = useState<{ x: number; y: number; node: VisibleNode } | null>(null);

  const treeRoot = useMemo(() => buildTree(states), [states]);

  // All found sequences — for green "found" highlighting on nodes
  const foundSeqs = useMemo(() => {
    const s = new Set<string>();
    if (results) for (const r of results) s.add(r.tokens.join(''));
    return s;
  }, [results]);

  // Selected sequences drive which paths are auto-expanded
  const solutionPaths = useMemo(
    () => markSolutionPaths(treeRoot, selectedSeqs),
    [treeRoot, selectedSeqs],
  );

  // Clear manual overrides when selection changes so new paths open cleanly
  const selectionKey = useMemo(() => [...selectedSeqs].sort().join('\0'), [selectedSeqs]);
  useEffect(() => {
    setExpandedSet(new Set());
    setCollapsedSet(new Set());
  }, [selectionKey]);

  const visibleRoot = useMemo(
    () => buildVisibleTree(treeRoot, solutionPaths, foundSeqs, expandedSet, collapsedSet),
    [treeRoot, solutionPaths, foundSeqs, expandedSet, collapsedSet],
  );

  // d3 layout
  const layout = useMemo(() => {
    const root = hierarchy(visibleRoot, (d) => d.children);
    const treeLayout = d3tree<VisibleNode>().nodeSize([NODE_SPACING_X, NODE_SPACING_Y]);
    treeLayout(root);
    return root;
  }, [visibleRoot]);

  // Pan/zoom setup
  useEffect(() => {
    const svg = svgRef.current;
    const g = gRef.current;
    if (!svg || !g) return;

    const zoomBehavior = d3zoom<SVGSVGElement, unknown>()
      .scaleExtent([0.1, 3])
      .on('zoom', (event) => {
        select(g).attr('transform', event.transform.toString());
      });

    select(svg).call(zoomBehavior);

    // Initial: center the root node
    const svgRect = svg.getBoundingClientRect();
    const initialX = svgRect.width / 2;
    const initialY = 40;
    select(svg).call(
      zoomBehavior.transform,
      zoomIdentity.translate(initialX, initialY).scale(1),
    );

    return () => {
      select(svg).on('.zoom', null);
    };
  }, []);

  const handleNodeClick = useCallback((node: VisibleNode) => {
    const key = node.fullSeq;

    if (node.isSummary) {
      setExpandedSet((prev) => {
        const next = new Set(prev);
        next.add(key);
        return next;
      });
      return;
    }

    // Toggle: if default is expanded (on path), add to collapsedSet;
    //         if default is collapsed (not on path), add to expandedSet
    if (node.isOnPath) {
      setCollapsedSet((prev) => {
        const next = new Set(prev);
        if (next.has(key)) next.delete(key);
        else next.add(key);
        return next;
      });
    } else {
      setExpandedSet((prev) => {
        const next = new Set(prev);
        if (next.has(key)) next.delete(key);
        else next.add(key);
        return next;
      });
    }
  }, []);

  if (!states.length) return null;

  const nodes = layout.descendants();
  const links = layout.links();

  return (
    <div className="relative" style={{ height: 500 }}>
      <svg
        ref={svgRef}
        width="100%"
        height="100%"
        style={{ cursor: 'grab', background: '#fafafa', borderRadius: 8 }}
      >
        <g ref={gRef}>
          {/* Links */}
          {links.map((link, i) => {
            const sx = link.source.x!;
            const sy = link.source.y! + NODE_H / 2;
            const tx = link.target.x!;
            const ty = link.target.y! - NODE_H / 2;
            const my = (sy + ty) / 2;
            const path = `M${sx},${sy} C${sx},${my} ${tx},${my} ${tx},${ty}`;
            const td = link.target.data;
            const isOnSolution = td.isOnPath && !td.isSummary;
            return (
              <path
                key={i}
                d={path}
                fill="none"
                stroke={isOnSolution ? '#34a853' : '#ccc'}
                strokeWidth={isOnSolution ? 2 : 1}
                strokeDasharray={td.isSummary ? '4,3' : undefined}
              />
            );
          })}

          {/* Nodes */}
          {nodes.map((d) => {
            const node = d.data;
            const x = d.x!;
            const y = d.y!;
            const hasChildren = node.originalChildren.length > 0;
            const isExpanded = d.children != null && d.children.length > 0;

            return (
              <g
                key={node.id}
                transform={`translate(${x},${y})`}
                style={{ cursor: hasChildren || node.isSummary ? 'pointer' : 'default' }}
                onClick={() => (hasChildren || node.isSummary) && handleNodeClick(node)}
                onMouseEnter={(e) => {
                  const svgRect = svgRef.current?.getBoundingClientRect();
                  if (svgRect) {
                    setTooltip({
                      x: e.clientX - svgRect.left,
                      y: e.clientY - svgRect.top - 10,
                      node,
                    });
                  }
                }}
                onMouseLeave={() => setTooltip(null)}
              >
                {node.isSummary ? (
                  <SummaryNodeSVG node={node} />
                ) : (
                  <TreeNodeSVG
                    node={node}
                    hasChildren={hasChildren}
                    isExpanded={isExpanded}
                  />
                )}
              </g>
            );
          })}
        </g>
      </svg>

      {/* Tooltip */}
      {tooltip && (
        <div
          className="absolute pointer-events-none bg-[#333] text-white text-xs px-2.5 py-1.5 rounded shadow-lg"
          style={{
            left: tooltip.x,
            top: tooltip.y,
            transform: 'translate(-50%, -100%)',
            zIndex: 10,
          }}
        >
          {tooltip.node.isSummary ? (
            <>
              {tooltip.node.summaryBranches} branches, {tooltip.node.summaryNodes} states
              <br />
              <span className="text-[#aaa]">Click to expand</span>
            </>
          ) : (
            <>
              <span className="font-bold">{tooltip.node.fullSeq || '(start)'}</span>
              <br />
              effort: {tooltip.node.effort.toFixed(2)} &middot; {tooltip.node.count} state{tooltip.node.count !== 1 ? 's' : ''}
              {tooltip.node.isFound && <><br /><span className="text-[#4caf50] font-bold">found result</span></>}
            </>
          )}
        </div>
      )}

      {/* Legend */}
      <div className="absolute bottom-2 right-2 text-xs text-muted flex gap-3 bg-white/80 px-2 py-1 rounded">
        <span><span className="inline-block w-2.5 h-2.5 rounded-sm bg-[#34a853] mr-1 align-middle" />selected path</span>
        <span><span className="inline-block w-2.5 h-2.5 rounded-sm border border-dashed border-[#999] bg-[#f5f5f5] mr-1 align-middle" />collapsed</span>
        <span>scroll to zoom, drag to pan</span>
      </div>
    </div>
  );
}

// --- Individual node renderers ---

function TreeNodeSVG({
  node,
  hasChildren,
  isExpanded,
}: {
  node: VisibleNode;
  hasChildren: boolean;
  isExpanded: boolean;
}) {
  const w = NODE_W;
  const h = NODE_H;
  const isFound = node.isFound;
  const isRoot = node.fullSeq === '';

  const fill = isFound ? '#e8f5e9' : isRoot ? '#e3f2fd' : '#fff';
  const stroke = isFound ? '#34a853' : isRoot ? '#1976d2' : '#ccc';

  return (
    <>
      <rect
        x={-w / 2}
        y={-h / 2}
        width={w}
        height={h}
        rx={6}
        fill={fill}
        stroke={stroke}
        strokeWidth={isFound ? 2 : 1}
      />
      {/* Move label */}
      <text
        textAnchor="middle"
        dominantBaseline="central"
        y={-4}
        fontSize={isRoot ? 11 : 13}
        fontFamily="monospace"
        fontWeight={isFound ? 700 : 600}
        fill={isFound ? '#2e7d32' : '#333'}
      >
        {isRoot ? '(start)' : node.move}
      </text>
      {/* Effort + count */}
      <text
        textAnchor="middle"
        dominantBaseline="central"
        y={12}
        fontSize={9}
        fill="#888"
      >
        {node.effort.toFixed(1)} &middot; {node.count}
      </text>
      {/* Expand/collapse indicator */}
      {hasChildren && (
        <text
          textAnchor="middle"
          dominantBaseline="central"
          x={w / 2 - 10}
          y={0}
          fontSize={8}
          fill="#888"
        >
          {isExpanded ? '\u25BC' : '\u25B6'}
        </text>
      )}
    </>
  );
}

function SummaryNodeSVG({ node }: { node: VisibleNode }) {
  const w = NODE_W;
  const h = NODE_H;

  return (
    <>
      <rect
        x={-w / 2}
        y={-h / 2}
        width={w}
        height={h}
        rx={6}
        fill="#f5f5f5"
        stroke="#999"
        strokeWidth={1}
        strokeDasharray="4,3"
      />
      <text
        textAnchor="middle"
        dominantBaseline="central"
        y={-4}
        fontSize={10}
        fill="#888"
      >
        {node.summaryBranches} branch{node.summaryBranches !== 1 ? 'es' : ''}
      </text>
      <text
        textAnchor="middle"
        dominantBaseline="central"
        y={10}
        fontSize={9}
        fill="#aaa"
      >
        {node.summaryNodes} states
      </text>
    </>
  );
}
