// vite.config.ts
 import { defineConfig } from "vite";
 import preact from "@preact/preset-vite";
 import { espViteBuild } from "@ikoz/vite-plugin-preact-esp32";
 import settings from './mock-responses/settings.json';
 import hp from './mock-responses/hp.json';



 export default defineConfig({
    plugins: [espViteBuild(), preact()],

    server: {
      port: 5173,
      proxy: {
        '/mock-responses/settings': {
          target: `http://localhost:5173/mock-responses`,
          changeOrigin: true,
          bypass: function (req, res, proxyOptions) {
            res.setHeader('Content-Type', 'application/json')
            res.write(JSON.stringify(settings));
            res.end();
            return ;
          }
        },
        '/mock-responses/hp': {
          target: `http://localhost:5173/mock-responses`,
          changeOrigin: true,
          bypass: function (req, res, proxyOptions) {
            res.setHeader('Content-Type', 'application/json')
            res.write(JSON.stringify(hp));
            res.end();
            return ;
          }
        }
      }
    }

 });