import { useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import { BenchmarkChart } from './BenchmarkChart';
import { getToken, setToken, deleteBenchCommit } from '../utils/github';
import type { Unit } from '../utils/format';
import type { TimePoint } from '../utils/data';
import styles from './ChartModal.module.css';

interface Props {
  title: string;
  series: TimePoint[];
  unit: Unit;
  hiddenShas: Set<string>;
  onPointClick: (sha: string) => void;
  onResetHidden: () => void;
  onClose: () => void;
}

export function ChartModal({ title, series, unit, hiddenShas, onPointClick, onResetHidden, onClose }: Props) {
  const [deleting, setDeleting] = useState(false);
  const [deleteMsg, setDeleteMsg] = useState<{ text: string; error: boolean } | null>(null);
  const hiddenCount = hiddenShas.size;

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

  const handleDelete = async () => {
    let token = getToken();
    if (!token) {
      token = window.prompt('Enter GitHub PAT (needs actions:write scope):');
      if (!token) return;
      setToken(token);
    }

    setDeleting(true);
    setDeleteMsg(null);
    try {
      const shas = [...hiddenShas];
      for (const sha of shas) {
        await deleteBenchCommit(sha, token);
      }
      setDeleteMsg({ text: `Triggered removal of ${shas.length} commit(s). Takes ~30s to apply.`, error: false });
    } catch (e) {
      setDeleteMsg({ text: e instanceof Error ? e.message : 'Failed', error: true });
    } finally {
      setDeleting(false);
    }
  };

  return createPortal(
    <div className={styles.overlay} onClick={(e) => { if (e.target === e.currentTarget) onClose(); }}>
      <div className={styles.content}>
        <div className={styles.header}>
          <div>
            <h3>{title}</h3>
            <span className={styles.hint}>Click a point to hide it &middot; Scroll to zoom &middot; Drag to pan</span>
          </div>
          <div className={styles.headerRight}>
            {hiddenCount > 0 && (
              <span className={styles.hiddenInfo}>
                {hiddenCount} hidden
                <button className={styles.resetBtn} onClick={onResetHidden}>Reset</button>
                <button
                  className={styles.deleteBtn}
                  onClick={handleDelete}
                  disabled={deleting}
                >
                  {deleting ? 'Deleting...' : 'Delete from data'}
                </button>
              </span>
            )}
            {deleteMsg && (
              <span className={deleteMsg.error ? styles.deleteError : styles.deleteSuccess}>
                {deleteMsg.text}
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
