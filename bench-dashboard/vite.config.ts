import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  base: '/Vimficiency/bench/',
  build: {
    rollupOptions: {
      input: {
        home: 'index.html',
        optimizer: 'optimizer.html',
        explore: 'explore.html',
      },
    },
  },
  css: {
    modules: {
      localsConvention: 'camelCaseOnly',
    },
  },
});
