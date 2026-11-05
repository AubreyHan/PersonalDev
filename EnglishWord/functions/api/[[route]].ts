import { Hono } from 'hono';
import { handle } from 'hono/cloudflare-pages';
import { authRouter } from './routes/auth';
import { adminRouter } from './routes/admin';
import { teacherRouter } from './routes/teacher';
import { speechRouter } from './routes/speech';
import { studentRouter } from './routes/student';
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
app.route('/teacher', teacherRouter);
app.route('/speech', speechRouter);
app.route('/student', studentRouter);

export const onRequest = handle(app);
export default app;
