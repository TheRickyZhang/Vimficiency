import { Outlet, Link, useMatches } from '@tanstack/react-router';

function capitalize(s: string): string {
  return s.charAt(0).toUpperCase() + s.slice(1);
}

export function RootLayout() {
  const matches = useMatches();
  const last = matches[matches.length - 1];

  // Determine breadcrumb context from matched routes
  const optimizerParam = (last?.params as Record<string, string>)?.['optimizer'];
  const isExplore = last?.fullPath?.endsWith('/explore') ?? false;

  let breadcrumb: React.ReactNode = null;
  let title = 'Vimficiency Benchmarks';

  if (optimizerParam) {
    const label = capitalize(optimizerParam);
    if (isExplore) {
      title = `Search Space — ${label} — Vimficiency`;
      breadcrumb = (
        <div className="mb-6 text-base">
          <Link to="/" className="text-brand no-underline font-semibold hover:underline">Vimficiency</Link>
          <span className="text-[#999] mx-2">&#x203A;</span>
          <Link to="/$optimizer" params={{ optimizer: optimizerParam }} search={{ cat: undefined, bench: undefined }} className="text-brand no-underline font-semibold hover:underline">{label}</Link>
          <span className="text-[#999] mx-2">&#x203A;</span>
          <span>Search Space</span>
        </div>
      );
    } else {
      title = `${label} — Vimficiency Benchmarks`;
      breadcrumb = (
        <div className="mb-6 text-base">
          <Link to="/" className="text-brand no-underline font-semibold hover:underline">Vimficiency</Link>
          <span className="text-[#999] mx-2">&#x203A;</span>
          <span>{label}</span>
        </div>
      );
    }
  }

  document.title = title;

  return (
    <div className="max-w-[1200px] mx-auto px-6 py-10">
      {breadcrumb}
      <Outlet />
    </div>
  );
}
