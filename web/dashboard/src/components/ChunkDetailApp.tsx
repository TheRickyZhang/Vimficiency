import { useMemo, useState, useRef, useEffect, useCallback } from 'react';
import { Link, useParams } from '@tanstack/react-router';
import type { ExplorationData, ChunkedResultEntry } from '../types/exploration';
import { BufferPreview } from './BufferPreview';
import { EffortHistogram } from './EffortHistogram';
import { ExplorationTimeline } from './ExplorationTimeline';
import { ExplorationTree } from './ExplorationTree';
import {
  findCase,
  isChunkedCase,
  deriveChunkDetail,
  deriveChunkBufferContext,
  getChunkType,
  getChunkText,
} from '../utils/exploration';

interface Props {
  data: ExplorationData;
  caseName: string;
  chunkIndex: number;
}

export function ChunkDetailApp({ data, caseName, chunkIndex }: Props) {
  const { optimizer } = useParams({ strict: false });
  const entry = data.entries[data.entries.length - 1]!;
  const activeCase = useMemo(() => findCase(entry.cases, caseName), [entry.cases, caseName]);

  if (!activeCase || !isChunkedCase(activeCase)) {
    return (
      <div>
        <h1>Chunk Detail</h1>
        <p className="text-muted">Case not found or not a chunked case.</p>
      </div>
    );
  }

  const chunkType = getChunkType(activeCase, chunkIndex);
  const chunkText = getChunkText(activeCase, chunkIndex);
  const cResults = activeCase.results as unknown as ChunkedResultEntry[];

  const detail = useMemo(
    () => deriveChunkDetail(activeCase, chunkIndex, chunkType),
    [activeCase, chunkIndex, chunkType],
  );

  const bufferContext = useMemo(
    () => deriveChunkBufferContext(activeCase.context, activeCase.diffs, chunkIndex, chunkType, cResults),
    [activeCase.context, activeCase.diffs, chunkIndex, chunkType, cResults],
  );

  const sortedResults = useMemo(() => {
    if (!detail) return [];
    return [...detail.results].sort((a, b) => a.effort - b.effort);
  }, [detail]);

  const [selectedSeqs, setSelectedSeqs] = useState<Set<string>>(() => {
    if (sortedResults.length > 0) return new Set([sortedResults[0]!.tokens.join('')]);
    return new Set();
  });

  // Reset selection when results change
  const resultsKey = sortedResults.map(r => r.tokens.join('')).join('\0');
  useEffect(() => {
    if (sortedResults.length > 0) {
      setSelectedSeqs(new Set([sortedResults[0]!.tokens.join('')]));
    } else {
      setSelectedSeqs(new Set());
    }
  }, [resultsKey]); // eslint-disable-line react-hooks/exhaustive-deps

  const toggleSeq = (seq: string) => {
    setSelectedSeqs(prev => {
      const next = new Set(prev);
      if (next.has(seq)) next.delete(seq);
      else next.add(seq);
      return next;
    });
  };

  const primarySelectedSeq = selectedSeqs.size > 0 ? [...selectedSeqs][0]! : null;

  if (!detail) {
    return (
      <div>
        <h1>Chunk Detail</h1>
        <p className="text-muted">No detail data available for this chunk.</p>
      </div>
    );
  }

  return (
    <div>
      {/* Header */}
      <div className="flex items-center gap-3 mb-2">
        <Link
          to="/$optimizer/explore"
          params={{ optimizer: optimizer! }}
          search={{ case: caseName }}
          className="link-brand text-sm"
        >
          &larr; Back to explorer
        </Link>
      </div>
      <h1>
        <span className={chunkType === 'motion' ? 'text-[#1565c0]' : 'text-[#e65100]'}>
          {chunkType}
        </span>
        {' chunk: '}
        <code>{chunkText}</code>
      </h1>
      <p className="text-[#666] text-[1.1rem] mb-10">
        Detail view for chunk {chunkIndex} of case <code>{caseName}</code>
      </p>

      {/* Buffer Preview */}
      {bufferContext && (
        <BufferPreview
          context={bufferContext}
          diffs={chunkType === 'edit' ? undefined : activeCase.diffs}
        />
      )}

      {/* Found Sequences */}
      {sortedResults.length > 0 && (
        <Card title="Found Sequences" className="mb-6">
          <div className="flex flex-wrap gap-2 justify-center">
            {sortedResults.map(r => {
              const seq = r.tokens.join('');
              const isSelected = selectedSeqs.has(seq);
              return (
                <div
                  key={seq}
                  onClick={() => toggleSeq(seq)}
                  className={`flex items-baseline gap-2 px-3.5 py-1.5 rounded-md cursor-pointer transition-[border-color,background] duration-150 border-2 ${
                    isSelected
                      ? 'bg-[#e3f2fd] border-[#1976d2]'
                      : 'bg-[#f5f5f5] border-border'
                  }`}
                >
                  <code className="font-bold text-base">
                    {seq.length > 30 ? seq.slice(0, 27) + '...' : seq}
                  </code>
                  <span className="text-[0.8rem] text-muted">
                    {r.effort.toFixed(2)}
                  </span>
                </div>
              );
            })}
          </div>
          {detail.states.length > 0 && (
            <div className="text-xs text-muted mt-1.5">
              Click to toggle paths in tree (multi-select)
            </div>
          )}
        </Card>
      )}

      {/* Exploration Tree */}
      {detail.states.length > 0 ? (
        <>
          <Card title={`Exploration Tree (${detail.states.length.toLocaleString()} states)`} className="mb-6">
            <ExplorationTree
              states={detail.states}
              results={sortedResults}
              selectedSeqs={selectedSeqs}
            />
          </Card>

          {/* Charts */}
          <div className="grid grid-cols-2 gap-6 mb-6">
            <ExpandableCard title="Effort Distribution">
              <EffortHistogram states={detail.states} results={sortedResults} selectedSeq={primarySelectedSeq} />
            </ExpandableCard>
            <ExpandableCard title="Exploration Timeline">
              <ExplorationTimeline states={detail.states} />
            </ExpandableCard>
          </div>
        </>
      ) : (
        <div className="card p-5 mb-6 text-center">
          <p className="text-sm text-muted">No exploration states for this chunk.</p>
        </div>
      )}
    </div>
  );
}

function Card({ title, children, className }: { title: string; children: React.ReactNode; className?: string }) {
  return (
    <div className={`card p-5 ${className ?? ''}`}>
      <h3 className="text-base font-bold mb-3 text-[#333]">{title}</h3>
      {children}
    </div>
  );
}

function ExpandableCard({ title, children }: { title: string; children: React.ReactNode }) {
  const ref = useRef<HTMLDivElement>(null);
  const [isFs, setIsFs] = useState(false);

  useEffect(() => {
    const handler = () => setIsFs(document.fullscreenElement === ref.current);
    document.addEventListener('fullscreenchange', handler);
    return () => document.removeEventListener('fullscreenchange', handler);
  }, []);

  const toggle = useCallback(() => {
    if (document.fullscreenElement) document.exitFullscreen();
    else ref.current?.requestFullscreen();
  }, []);

  return (
    <div
      ref={ref}
      className="card p-5 relative"
      style={isFs ? { display: 'flex', flexDirection: 'column' } : undefined}
    >
      <h3 className="text-base font-bold mb-3 text-[#333]">{title}</h3>
      <div style={isFs ? { flex: 1, minHeight: 0 } : undefined}>
        {children}
      </div>
      <button
        onClick={toggle}
        className="fullscreen-btn"
        title={isFs ? 'Exit fullscreen' : 'Fullscreen'}
      >
        <svg viewBox="0 0 16 16" width="14" height="14">
          {isFs ? (
            <path d="M5 2v3H2M11 2v3h3M2 11h3v3M14 11h-3v3" fill="none" stroke="currentColor" strokeWidth="1.5" />
          ) : (
            <path d="M2 6V2h4M10 2h4v4M14 10v4h-4M6 14H2v-4" fill="none" stroke="currentColor" strokeWidth="1.5" />
          )}
        </svg>
      </button>
    </div>
  );
}
