import { useEffect } from 'react';
import { createPortal } from 'react-dom';
import { Link } from '@tanstack/react-router';
import { BenchmarkChart } from './BenchmarkChart';
import type { Metric } from './BenchmarkChart';
import { MetricToggles } from './MetricToggles';
import type { Unit } from '../utils/format';
import type { TimePoint } from '../utils/data';

interface Props {
  title: string;
  series: TimePoint[];
  unit: Unit;
  onClose: () => void;
  exploreLink?: { optimizer: string; case: string };
  activeMetrics: Set<Metric>;
  onMetricsChange: (metrics: Set<Metric>) => void;
}

export function ChartModal({ title, series, unit, onClose, exploreLink, activeMetrics, onMetricsChange }: Props) {
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    document.addEventListener('keydown', handler);
    document.body.style.overflow = 'hidden';
    return () => {
      document.removeEventListener('keydown', handler);
      document.body.style.overflow = '';
    };
  }, [onClose]);

  return createPortal(
    <div className="fixed inset-0 bg-black/65 z-[1000] flex justify-center items-center p-10" onClick={(e) => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="bg-surface rounded-xl p-8 w-full max-w-[1000px] max-h-[90vh] shadow-[0_8px_32px_rgba(0,0,0,0.2)]">
        <div className="flex justify-between items-start mb-5">
          <h3 className="text-left text-[1.3rem] font-bold m-0">{title}</h3>
          <div className="flex items-center gap-3">
            <MetricToggles activeMetrics={activeMetrics} onChange={onMetricsChange} />
            <div className="w-px h-6 bg-border-light mx-1" />
            {exploreLink && (
              <Link
                to="/$optimizer/explore"
                params={{ optimizer: exploreLink.optimizer }}
                search={{ case: exploreLink.case }}
                title="Explore search space"
                className="explore-btn"
              >
                Explore
              </Link>
            )}
            <button className="bg-transparent border-none text-[1.8rem] cursor-pointer text-[#666] px-2 py-1 leading-none hover:text-[#333]" onClick={onClose}>&times;</button>
          </div>
        </div>
        <div className="relative h-[500px]">
          <BenchmarkChart series={series} unit={unit} large activeMetrics={activeMetrics} />
        </div>
        <div className="text-xs text-[#999] mt-2">Scroll to zoom &middot; Drag to pan</div>
      </div>
    </div>,
    document.body,
  );
}
