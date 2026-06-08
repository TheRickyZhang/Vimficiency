import { chunkDetailRoute } from '../router';
import { ChunkDetailApp } from '../components/ChunkDetailApp';

export function ChunkDetailPage() {
  const data = chunkDetailRoute.useLoaderData();
  const search = chunkDetailRoute.useSearch();

  if (!data) {
    return (
      <>
        <h1>Chunk Detail</h1>
        <p className="subtitle">No exploration data available.</p>
      </>
    );
  }

  if (search.chunkIndex == null || !search.case) {
    return (
      <>
        <h1>Chunk Detail</h1>
        <p className="subtitle">Missing case or chunkIndex parameter.</p>
      </>
    );
  }

  return (
    <ChunkDetailApp
      data={data}
      caseName={search.case}
      chunkIndex={search.chunkIndex}
    />
  );
}
