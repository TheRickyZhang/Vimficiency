import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  base: '/Vimficiency/bench/',
  build: {
    rollupOptions: {
      input: {
        optimizer: 'optimizer.html',
      },
    },
  },
  css: {
    modules: {
      localsConvention: 'camelCaseOnly',
    },
  },
});
