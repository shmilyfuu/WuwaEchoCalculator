import { defineConfig } from 'vite';
import { resolve } from 'node:path';

export default defineConfig({
  root: 'web',
  publicDir: '../public',
  base: './',
  build: {
    outDir: '../dist-web',
    emptyOutDir: true,
    target: 'es2022',
    rollupOptions: {
      input: {
        main: resolve(process.cwd(), 'web/index.html'),
        ocr: resolve(process.cwd(), 'web/ocr-sandbox.html')
      }
    }
  }
});
