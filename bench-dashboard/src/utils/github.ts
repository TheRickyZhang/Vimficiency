const TOKEN_KEY = 'vimficiency-gh-token';

export function getToken(): string | null {
  return localStorage.getItem(TOKEN_KEY);
}

export function setToken(token: string): void {
  localStorage.setItem(TOKEN_KEY, token);
}

export function clearToken(): void {
  localStorage.removeItem(TOKEN_KEY);
}

function getRepoInfo(): { owner: string; repo: string } | null {
  const url = window.BENCHMARK_DATA?.repoUrl;
  if (!url) return null;
  const match = url.match(/github\.com\/([^/]+)\/([^/]+)/);
  return match ? { owner: match[1]!, repo: match[2]! } : null;
}

export async function deleteBenchCommit(sha: string, token: string): Promise<void> {
  const info = getRepoInfo();
  if (!info) throw new Error('Cannot determine repo from benchmark data');

  const res = await fetch(
    `https://api.github.com/repos/${info.owner}/${info.repo}/actions/workflows/remove-bench-data.yml/dispatches`,
    {
      method: 'POST',
      headers: {
        Authorization: `Bearer ${token}`,
        Accept: 'application/vnd.github+json',
      },
      body: JSON.stringify({ ref: 'main', inputs: { sha } }),
    },
  );

  if (res.status === 204) return;
  if (res.status === 401 || res.status === 403) {
    clearToken();
    throw new Error('Invalid or expired token');
  }
  throw new Error(`GitHub API error: ${res.status}`);
}
