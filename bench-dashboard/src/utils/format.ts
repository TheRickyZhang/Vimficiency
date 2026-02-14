declare const NsBrand: unique symbol;

/** Branded type ensuring values are in nanoseconds. Use ns() or toNanoseconds() to construct. */
export type Nanoseconds = number & { readonly [NsBrand]: true };

/** Construct a Nanoseconds value from a known-nanosecond number. */
export function ns(value: number): Nanoseconds {
  return value as Nanoseconds;
}

export interface Unit {
  d: number;
  l: string;
}

export function fmtTime(value: Nanoseconds): string {
  if (value >= 1e9) return (value / 1e9).toFixed(2) + ' s';
  if (value >= 1e6) return (value / 1e6).toFixed(2) + ' ms';
  if (value >= 1e3) return (value / 1e3).toFixed(1) + ' us';
  return Math.round(value) + ' ns';
}

export function bestUnit(values: Nanoseconds[]): Unit {
  if (!values.length) return { d: 1e6, l: 'ms' };
  const mx = Math.max(...values);
  if (mx >= 1e9) return { d: 1e9, l: 's' };
  if (mx >= 1e6) return { d: 1e6, l: 'ms' };
  if (mx >= 1e3) return { d: 1e3, l: 'us' };
  return { d: 1, l: 'ns' };
}
