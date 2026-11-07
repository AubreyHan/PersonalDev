import app from '../functions/api/[[route]]';
// @ts-ignore
import { ASSETS } from './static_assets.js';

export default {
  async fetch(request: Request, env: any, ctx: any): Promise<Response> {
    const url = new URL(request.url);
    const pathname = decodeURIComponent(url.pathname);

    // 1. 如果是 /api 开头的请求，交由 Hono 路由器处理
    if (pathname.startsWith('/api')) {
      return app.fetch(request, env, ctx);
    }

    // 2. OPTIONS 预检请求快速返回
    if (request.method === 'OPTIONS') {
      return new Response(null, {
        headers: {
          'Access-Control-Allow-Origin': '*',
          'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
          'Access-Control-Allow-Headers': 'Content-Type, Authorization',
          'Access-Control-Max-Age': '86400',
        },
      });
    }

    // 3. 静态文件与 SPA 路由回退
    const normalizedPath = '/' + pathname.replace(/^\/(?:static\/)?/, '');
    const assetMap = (ASSETS as Record<string, any>) || {};
    let asset = assetMap[pathname] || assetMap[normalizedPath];

    if (!asset && pathname.endsWith('/')) {
      asset = assetMap[pathname + 'index.html'];
    }
    // SPA 路由回退: 只要不是带文件扩展名的静态资源请求，均回退到 /index.html
    if (!asset && !pathname.slice(1).includes('.')) {
      asset = assetMap['/index.html'] || assetMap['/'];
    }

    if (asset) {
      let cacheControl = 'public, max-age=3600';
      if (pathname === '/' || pathname.endsWith('.html') || pathname.endsWith('sw.js')) {
        cacheControl = 'no-cache, no-store, must-revalidate';
      } else if (pathname.startsWith('/assets/')) {
        cacheControl = 'public, max-age=31536000, immutable';
      }

      if (asset.base64) {
        const binary = Uint8Array.from(atob(asset.base64), (c) => c.charCodeAt(0));
        return new Response(binary, {
          headers: {
            'Content-Type': asset.contentType,
            'Cache-Control': cacheControl,
            'Access-Control-Allow-Origin': '*',
          },
        });
      }
      return new Response(asset.content, {
        headers: {
          'Content-Type': asset.contentType,
          'Cache-Control': cacheControl,
          'Access-Control-Allow-Origin': '*',
        },
      });
    }

    // 兜底到 index.html
    const fallback = assetMap['/index.html'] || assetMap['/'];
    if (fallback) {
      return new Response(fallback.content, {
        headers: {
          'Content-Type': 'text/html; charset=utf-8',
          'Cache-Control': 'no-cache, no-store, must-revalidate',
        },
      });
    }

    return new Response('404 Not Found', { status: 404 });
  },
};
