import { useState, useMemo } from 'react';
import type { BufferContext, DiffRegion } from '../types/exploration';

const COLLAPSE_THRESHOLD = 10;

// Rotating palette for per-diff coloring (bg color, darker border/text variant)
const DIFF_COLORS = [
  { bg: 'rgba(255,183,77,0.35)', border: '#e65100' },   // orange
  { bg: 'rgba(129,199,132,0.35)', border: '#2e7d32' },   // green
  { bg: 'rgba(144,202,249,0.35)', border: '#1565c0' },   // blue
  { bg: 'rgba(206,147,216,0.35)', border: '#7b1fa2' },   // purple
  { bg: 'rgba(239,154,154,0.35)', border: '#c62828' },   // red
  { bg: 'rgba(128,222,234,0.35)', border: '#00838f' },   // cyan
  { bg: 'rgba(255,224,130,0.35)', border: '#f9a825' },   // yellow
  { bg: 'rgba(188,170,164,0.35)', border: '#4e342e' },   // brown
];

// Half-open range [begin, end)
interface CharRange {
  beginLine: number;
  beginCol: number;
  endLine: number;
  endCol: number;
  colorIdx: number;
}

function computeGoalRanges(diffs: DiffRegion[]): CharRange[] {
  const ranges: CharRange[] = [];
  let lineShift = 0;
  let colShift = 0;
  let activeColShiftLine = -1;

  for (let i = 0; i < diffs.length; i++) {
    const diff = diffs[i]!;
    const goalBeginLine = diff.beginLine + lineShift;
    const goalBeginCol = diff.beginCol + (diff.beginLine === activeColShiftLine ? colShift : 0);

    const insertedParts = diff.insertedText.split('\n');
    const deletedNewlines = (diff.deletedText.match(/\n/g) || []).length;
    const insertedNewlines = insertedParts.length - 1;

    let goalEndLine: number, goalEndCol: number;
    if (insertedParts.length === 1) {
      goalEndLine = goalBeginLine;
      goalEndCol = goalBeginCol + insertedParts[0]!.length;
    } else {
      goalEndLine = goalBeginLine + insertedNewlines;
      goalEndCol = insertedParts[insertedParts.length - 1]!.length;
    }

    if (goalEndLine !== goalBeginLine || goalEndCol !== goalBeginCol) {
      ranges.push({
        beginLine: goalBeginLine, beginCol: goalBeginCol,
        endLine: goalEndLine, endCol: goalEndCol,
        colorIdx: i % DIFF_COLORS.length,
      });
    }

    lineShift += insertedNewlines - deletedNewlines;
    activeColShiftLine = diff.endLine;
    colShift = goalEndCol - diff.endCol;
  }

  return ranges;
}

function computeInitialRanges(diffs: DiffRegion[]): CharRange[] {
  return diffs
    .map((d, i) => ({
      beginLine: d.beginLine, beginCol: d.beginCol,
      endLine: d.endLine, endCol: d.endCol,
      colorIdx: i % DIFF_COLORS.length,
    }))
    .filter(r => r.endLine !== r.beginLine || r.endCol !== r.beginCol);
}

// Check if position (line, col) is inside any range, return colorIdx or -1
function getDiffColor(line: number, col: number, ranges: CharRange[]): number {
  for (const r of ranges) {
    // Check if (line, col) is within [begin, end)
    const afterBegin = line > r.beginLine || (line === r.beginLine && col >= r.beginCol);
    const beforeEnd = line < r.endLine || (line === r.endLine && col < r.endCol);
    if (afterBegin && beforeEnd) return r.colorIdx;
  }
  return -1;
}

interface Props {
  context: BufferContext;
  diffs?: DiffRegion[];
}

function BufferPanel({ title, lines, cursor, prefix, suffix, hasLinesAbove, hasLinesBelow, diffRanges, showDiffs }: {
  title: string;
  lines: string[];
  cursor?: [number, number];
  prefix?: string;
  suffix?: string;
  hasLinesAbove?: boolean;
  hasLinesBelow?: boolean;
  diffRanges?: CharRange[];
  showDiffs?: boolean;
}) {
  const [expanded, setExpanded] = useState(false);
  const isEmpty = lines.length === 0;
  const needsCollapse = lines.length > COLLAPSE_THRESHOLD;
  const displayLines = needsCollapse && !expanded ? lines.slice(0, COLLAPSE_THRESHOLD) : lines;
  const prefixLen = prefix?.length ?? 0;
  const suffixLen = suffix?.length ?? 0;
  const lastLineIdx = lines.length - 1;

  return (
    <div className="flex-1 min-w-0">
      <div className="text-xs font-bold text-muted mb-1 uppercase tracking-wide">{title}</div>
      <div
        className={`border border-[#e0e0e0] rounded bg-[#fafafa] overflow-x-auto text-sm font-mono${needsCollapse ? ' cursor-pointer' : ''}`}
        onClick={needsCollapse ? () => setExpanded(!expanded) : undefined}
      >
        {hasLinesAbove && (
          <div className="px-2 py-0.5 text-xs text-[#ef9a9a] select-none border-b border-[#f0f0f0]">...</div>
        )}
        {isEmpty ? (
          <div className="px-2 py-1 text-muted italic">(empty)</div>
        ) : (
          displayLines.map((line, lineIdx) => (
            <div key={lineIdx} className="flex">
              <span className="text-[#bbb] text-right w-6 pr-1.5 select-none shrink-0 text-xs leading-5">
                {lineIdx + 1}
              </span>
              <pre className="m-0 leading-5 whitespace-pre">
                {renderLine(line, lineIdx, cursor, prefixLen, suffixLen, lastLineIdx,
                  showDiffs ? diffRanges : undefined)}
              </pre>
            </div>
          ))
        )}
        {hasLinesBelow && (
          <div className="px-2 py-0.5 text-xs text-[#ef9a9a] select-none border-t border-[#f0f0f0]">...</div>
        )}
        {needsCollapse && (
          <div className="w-full text-xs text-center py-1 text-[#1976d2] border-t border-[#e0e0e0] select-none">
            {expanded ? 'Click to collapse' : `Click to show all (${lines.length} lines)`}
          </div>
        )}
      </div>
    </div>
  );
}

function renderLine(
  line: string,
  lineIdx: number,
  cursor?: [number, number],
  prefixLen = 0,
  suffixLen = 0,
  lastLineIdx = 0,
  diffRanges?: CharRange[],
): React.ReactNode {
  if (line.length === 0) {
    if (cursor && cursor[0] === lineIdx) {
      return <span className="bg-[#bbdefb]">{'\u00A0'}</span>;
    }
    return '\u00A0';
  }

  const chars = Array.from(line);
  const spans: React.ReactNode[] = [];
  let i = 0;

  while (i < chars.length) {
    const isCursor = cursor && cursor[0] === lineIdx && cursor[1] === i;
    const isPrefix = lineIdx === 0 && prefixLen > 0 && i < prefixLen;
    const isSuffix = lineIdx === lastLineIdx && suffixLen > 0 && i >= chars.length - suffixLen;
    const diffColor = diffRanges ? getDiffColor(lineIdx, i, diffRanges) : -1;

    let j = i + 1;
    while (j < chars.length) {
      const nextIsCursor = cursor && cursor[0] === lineIdx && cursor[1] === j;
      const nextIsPrefix = lineIdx === 0 && prefixLen > 0 && j < prefixLen;
      const nextIsSuffix = lineIdx === lastLineIdx && suffixLen > 0 && j >= chars.length - suffixLen;
      const nextDiffColor = diffRanges ? getDiffColor(lineIdx, j, diffRanges) : -1;
      if (nextIsCursor !== isCursor || nextIsPrefix !== isPrefix ||
          nextIsSuffix !== isSuffix || nextDiffColor !== diffColor) break;
      j++;
    }

    const text = chars.slice(i, j).join('');

    if (isCursor) {
      spans.push(<span key={i} className="bg-[#bbdefb]">{text}</span>);
    } else if (isPrefix || isSuffix) {
      spans.push(<span key={i} className="bg-[#ffcdd2]">{text}</span>);
    } else if (diffColor >= 0) {
      const color = DIFF_COLORS[diffColor]!;
      spans.push(<span key={i} style={{
        backgroundColor: color.bg,
        borderBottom: `2px solid ${color.border}`,
      }}>{text}</span>);
    } else {
      spans.push(<span key={i}>{text}</span>);
    }
    i = j;
  }

  return <>{spans}</>;
}

export function BufferPreview({ context, diffs }: Props) {
  const [showDiffs, setShowDiffs] = useState(true);
  const hasDiffs = diffs && diffs.length > 0;

  const initialRanges = useMemo(
    () => hasDiffs ? computeInitialRanges(diffs) : [],
    [diffs, hasDiffs]
  );
  const goalRanges = useMemo<CharRange[]>(() => {
    if (hasDiffs) return computeGoalRanges(diffs);
    // Motion range cases have no diffs; highlight the acceptable landing span.
    if (context.goalRange) {
      const [bl, bc, el, ec] = context.goalRange;
      return [{ beginLine: bl, beginCol: bc, endLine: el, endCol: ec, colorIdx: 1 }];
    }
    return [];
  }, [diffs, hasDiffs, context.goalRange]);

  return (
    <div className="card p-4 mb-6">
      <div className="flex items-center justify-between mb-3">
        <h3 className="text-base font-bold text-[#333]">Buffer Preview</h3>
        {hasDiffs && (
          <label className="flex items-center gap-1.5 text-xs text-muted cursor-pointer select-none">
            <input
              type="checkbox"
              checked={showDiffs}
              onChange={() => setShowDiffs(!showDiffs)}
              className="accent-[#e65100]"
            />
            Show diffs
          </label>
        )}
      </div>
      <div className="flex gap-4">
        <BufferPanel
          title="Initial"
          lines={context.initialLines}
          cursor={context.initialCursor}
          prefix={context.prefix}
          suffix={context.suffix}
          hasLinesAbove={context.hasLinesAbove}
          hasLinesBelow={context.hasLinesBelow}
          diffRanges={initialRanges}
          showDiffs={showDiffs}
        />
        <BufferPanel
          title="Goal"
          lines={context.goalLines}
          cursor={context.goalCursor}
          diffRanges={goalRanges}
          showDiffs={showDiffs}
        />
      </div>
    </div>
  );
}
