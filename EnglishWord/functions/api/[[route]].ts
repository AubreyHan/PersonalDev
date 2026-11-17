import { Hono } from 'hono';
import { handle } from 'hono/cloudflare-pages';
import { authRouter } from './routes/auth';
import { adminRouter } from './routes/admin';
import { wordsRouter } from './routes/words';
import { syncRouter } from './routes/sync';
import { speechRouter } from './routes/speech';
import type { Env, HonoVariables } from './types';

const app = new Hono<{ Bindings: Env; Variables: HonoVariables }>().basePath('/api');

// 全局 CORS 支持
app.use('*', async (c, next) => {
  await next();
  c.res.headers.set('Access-Control-Allow-Origin', '*');
  c.res.headers.set('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
  c.res.headers.set('Access-Control-Allow-Headers', 'Content-Type, Authorization');
});

app.options('*', (c) => c.body(null, 204));

app.route('/auth', authRouter);
app.route('/admin', adminRouter);
app.route('/words', wordsRouter);
app.route('/sync', syncRouter);
app.route('/speech', speechRouter);

// 兼容过渡：将旧的 /teacher 和 /student 路径映射至新的通用路由
app.route('/teacher', wordsRouter);
app.route('/student', syncRouter);

export const onRequest = handle(app);
export default app;
