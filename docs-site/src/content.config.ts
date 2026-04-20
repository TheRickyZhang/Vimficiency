// Content lives in ../doc-src/ so the same markdown serves three surfaces:
// the Starlight site, the panvimdoc-generated `:help` file, and direct
// GitHub rendering. The glob loader reads them in place — no copy or
// symlink, no sync drift.
//
// IDs match the on-disk filenames (URLs end up as `/NN-name/`). Doing it
// this way preserves Astro's automatic `[text](NN-name.md)` link
// rewriting — strip the `NN-` prefix here and the rehype plugin can no
// longer resolve cross-page links in the markdown body. README → index
// is the one mapping that's worth the override.
import { defineCollection } from 'astro:content';
import { glob } from 'astro/loaders';
import { docsSchema } from '@astrojs/starlight/schema';

export const collections = {
  docs: defineCollection({
    loader: glob({
      base: '../doc-src',
      pattern: '**/*.md',
      generateId: ({ entry }) =>
        entry === 'README.md' ? 'index' : entry.replace(/\.md$/, ''),
    }),
    schema: docsSchema(),
  }),
};
