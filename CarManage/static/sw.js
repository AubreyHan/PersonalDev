/**
 * 我的车管家 Service Worker (离线缓存核心)
 */

const CACHE_NAME = 'car-tracker-cache-v75';
const STATIC_ASSETS = [
  './',
  './index.html',
  './style.css',
  './db.js',
  './modules/auth.js',
  './modules/sync_ui.js',
  './modules/tri_calc.js',
  './modules/stations.js',
  './modules/records.js',
  './modules/energy_charts.js',
  './app.js',
  './manifest.json',
  './icon-192.png',
  './icon-512.png',
  'https://cdn.tailwindcss.com'
];

// 1. 安装事件：预缓存关键静态资源
self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      console.log('[Service Worker] 预缓存静态应用壳资源与 CDN');
      return cache.addAll(STATIC_ASSETS);
    }).then(() => self.skipWaiting())
  );
});

// 2. 激活事件：清理旧缓存
self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keyList) => {
      return Promise.all(
        keyList.map((key) => {
          if (key !== CACHE_NAME) {
            console.log('[Service Worker] 移除旧版本缓存:', key);
            return caches.delete(key);
          }
        })
      );
    }).then(() => self.clients.claim())
  );
});

// 3. 拦截网络请求：
// - API 请求：Network-First (网络优先，离线由客户端 IndexedDB 接管)
// - 页面导航 (HTML)：Network-First (网络优先，及时拉取最新版本，离线降级使用缓存)
// - 静态资源：Cache-First (缓存优先，后台后台异步更新)
self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url);

  // 跨域第三方外部 API (如高德地图、百度地图等) 直接放行走网络，绝不拦截缓存
  if (url.origin !== self.location.origin && !url.hostname.includes('tailwindcss')) {
    return;
  }

  // API 路由交由网络或由前端 db.js 处理
  if (url.pathname.startsWith('/api/')) {
    event.respondWith(
      fetch(event.request).catch(() => {
        return new Response(JSON.stringify({ offline: true, message: '处于离线状态' }), {
          headers: { 'Content-Type': 'application/json' }
        });
      })
    );
    return;
  }

  // 页面导航请求（HTML）：网络优先，保证每次刷新/启动 PWA 都能拿到最新版本号，离线时降级使用缓存壳
  if (event.request.mode === 'navigate' || url.pathname.endsWith('.html') || url.pathname === '/' || url.pathname === '') {
    event.respondWith(
      fetch(event.request).then((networkResponse) => {
        if (networkResponse && networkResponse.status === 200) {
          const responseToCache = networkResponse.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(event.request, responseToCache));
        }
        return networkResponse;
      }).catch(() => {
        return caches.match(event.request).then((cached) => cached || caches.match('./index.html') || caches.match('./'));
      })
    );
    return;
  }

  // 静态页面与资源请求：缓存优先，后台异步刷新 (Stale-While-Revalidate)
  event.respondWith(
    caches.match(event.request).then((cachedResponse) => {
      if (cachedResponse) {
        fetch(event.request).then((networkResponse) => {
          if (networkResponse && (networkResponse.status === 200 || networkResponse.type === 'opaque')) {
            caches.open(CACHE_NAME).then((cache) => cache.put(event.request, networkResponse));
          }
        }).catch(() => {/* 离线静默忽略 */});
        return cachedResponse;
      }
      return fetch(event.request).then((networkResponse) => {
        if (!networkResponse || (networkResponse.status !== 200 && networkResponse.type !== 'opaque')) {
          return networkResponse;
        }
        const responseToCache = networkResponse.clone();
        caches.open(CACHE_NAME).then((cache) => cache.put(event.request, responseToCache));
        return networkResponse;
      });
    })
  );
});
