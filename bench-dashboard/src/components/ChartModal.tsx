import { useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import { BenchmarkChart } from './BenchmarkChart';
import { getToken, setToken, deleteBenchCommit } from '../utils/github';
import type { Unit } from '../utils/format';
import type { TimePoint } from '../utils/data';

interface Props {
  title: string;
  series: TimePoint[];
  unit: Unit;
  hiddenShas: Set<string>;
  repoUrl: string;
  onPointClick: (sha: string) => void;
  onResetHidden: () => void;
  onClose: () => void;
}

export function ChartModal({ title, series, unit, hiddenShas, repoUrl, onPointClick, onResetHidden, onClose }: Props) {
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
        await deleteBenchCommit(sha, token, repoUrl);
      }
      setDeleteMsg({ text: `Triggered removal of ${shas.length} commit(s). Takes ~30s to apply.`, error: false });
    } catch (e) {
      setDeleteMsg({ text: e instanceof Error ? e.message : 'Failed', error: true });
    } finally {
      setDeleting(false);
    }
  };

  return createPortal(
    <div className="fixed inset-0 bg-black/65 z-[1000] flex justify-center items-center p-10" onClick={(e) => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="bg-surface rounded-xl p-8 w-full max-w-[1000px] max-h-[90vh] shadow-[0_8px_32px_rgba(0,0,0,0.2)]">
        <div className="flex justify-between items-start mb-5">
          <div>
            <h3 className="text-[1.3rem] font-bold">{title}</h3>
            <span className="text-xs text-[#999]">Click a point to hide it &middot; Scroll to zoom &middot; Drag to pan</span>
          </div>
          <div className="flex items-center gap-3">
            {hiddenCount > 0 && (
              <span className="text-[0.85rem] text-[#666] flex items-center gap-1.5">
                {hiddenCount} hidden
                <button className="bg-transparent border border-[#ccc] rounded px-2 py-0.5 text-xs text-[#666] cursor-pointer hover:border-[#999] hover:text-[#333]" onClick={onResetHidden}>Reset</button>
                <button
                  className="bg-transparent border border-[#e57373] rounded px-2 py-0.5 text-xs text-[#c62828] cursor-pointer hover:bg-[#ffebee] hover:border-[#c62828] disabled:opacity-50 disabled:cursor-not-allowed"
                  onClick={handleDelete}
                  disabled={deleting}
                >
                  {deleting ? 'Deleting...' : 'Delete from data'}
                </button>
              </span>
            )}
            {deleteMsg && (
              <span className={`text-[0.8rem] ${deleteMsg.error ? 'text-[#c62828]' : 'text-[#2e7d32]'}`}>
                {deleteMsg.text}
              </span>
            )}
            <button className="bg-transparent border-none text-[1.8rem] cursor-pointer text-[#666] px-2 py-1 leading-none hover:text-[#333]" onClick={onClose}>&times;</button>
          </div>
        </div>
        <div className="relative h-[500px]">
          <BenchmarkChart series={series} unit={unit} large onPointClick={onPointClick} />
        </div>
      </div>
    </div>,
    document.body,
  );
}
