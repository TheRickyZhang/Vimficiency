import { useNavigate } from '@tanstack/react-router';
import { optimizerIndexRoute } from '../router';
import { App } from '../components/App';

export function OptimizerPage() {
  const { data, categories, optimizerName, repoUrl } = optimizerIndexRoute.useLoaderData();
  const { cat, bench } = optimizerIndexRoute.useSearch();
  const { optimizer } = optimizerIndexRoute.useParams();
  const navigate = useNavigate({ from: optimizerIndexRoute.fullPath });

  return (
    <App
      data={data}
      categories={categories}
      optimizerName={optimizerName}
      optimizer={optimizer}
      repoUrl={repoUrl}
      activeCategory={cat ?? null}
      openBench={bench ?? null}
      onNavigate={(newCat, benchName) => {
        void navigate({
          search: { cat: newCat || undefined, bench: benchName || undefined },
        });
        window.scrollTo(0, 0);
      }}
      onBenchConsumed={() => {
        void navigate({
          search: { cat, bench: undefined },
          replace: true,
        });
      }}
    />
  );
}
