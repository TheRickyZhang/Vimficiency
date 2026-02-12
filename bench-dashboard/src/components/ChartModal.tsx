import { useEffect } from 'react';
import { createPortal } from 'react-dom';
import { BenchmarkChart } from './BenchmarkChart';
import type { Unit } from '../utils/format';
import type { TimePoint } from '../utils/data';
import styles from './ChartModal.module.css';

interface Props {
  title: string;
  series: TimePoint[];
  unit: Unit;
  hiddenCount: number;
  onPointClick: (sha: string) => void;
  onResetHidden: () => void;
  onClose: () => void;
}

export function ChartModal({ title, series, unit, hiddenCount, onPointClick, onResetHidden, onClose }: Props) {
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
    <div className={styles.overlay} onClick={(e) => { if (e.target === e.currentTarget) onClose(); }}>
      <div className={styles.content}>
        <div className={styles.header}>
          <div>
            <h3>{title}</h3>
            <span className={styles.hint}>Click a point to hide it</span>
          </div>
          <div className={styles.headerRight}>
            {hiddenCount > 0 && (
              <span className={styles.hiddenInfo}>
                {hiddenCount} hidden
                <button className={styles.resetBtn} onClick={onResetHidden}>Reset</button>
              </span>
            )}
            <button className={styles.close} onClick={onClose}>&times;</button>
          </div>
        </div>
        <div className={styles.chart}>
          <BenchmarkChart series={series} unit={unit} large onPointClick={onPointClick} />
        </div>
      </div>
    </div>,
    document.body,
  );
}
