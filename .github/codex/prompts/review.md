Return JSON that matches the provided schema exactly.

Write a concise PR review focused on:
- logical/correctness bugs (including UB/lifetime)
- code smells/design limitations
- long-term semantic concerns (invariants, API boundaries, coupling)

Rules:
- body: 5–15 bullets max.
- comments: only actionable issues; prefer severity high/medium.
- For each inline comment: use the repo-relative file path, and a line number in the NEW file (RIGHT side).
- Do not output any extra text outside the JSON.
