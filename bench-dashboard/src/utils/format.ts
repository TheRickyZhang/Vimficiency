export interface Unit {
  d: number;
  l: string;
}

export function fmtTime(ns: number): string {
  if (ns >= 1e9) return (ns / 1e9).toFixed(2) + ' s';
  if (ns >= 1e6) return (ns / 1e6).toFixed(2) + ' ms';
  if (ns >= 1e3) return (ns / 1e3).toFixed(1) + ' us';
  return Math.round(ns) + ' ns';
}

export function bestUnit(values: number[]): Unit {
  if (!values.length) return { d: 1e6, l: 'ms' };
  const mx = Math.max(...values);
  if (mx >= 1e9) return { d: 1e9, l: 's' };
  if (mx >= 1e6) return { d: 1e6, l: 'ms' };
  if (mx >= 1e3) return { d: 1e3, l: 'us' };
  return { d: 1, l: 'ns' };
}
