import { Hono } from 'hono';
import { hashPassword, verifyPassword, signJwt } from '../services/crypto';
import { authMiddleware } from '../middleware/auth';
import type { Env, HonoVariables } from '../types';

export const authRouter = new Hono<{ Bindings: Env; Variables: HonoVariables }>();

/**
 * 确保默认 admin 用户存在
 */
async function ensureAdminUser(db: Env['DB']) {
  const existing = await db
    .prepare('SELECT id FROM users WHERE username = ?')
    .bind('admin')
    .first();

  if (!existing) {
    const adminId = crypto.randomUUID();
    const passwordHash = await hashPassword('P@ss=w0rd');
    const now = Date.now();
    await db
      .prepare(
        'INSERT INTO users (id, username, display_name, password_hash, role, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)'
      )
      .bind(adminId, 'admin', '系统管理员', passwordHash, 'admin', now, now)
      .run();
  }
}

authRouter.post('/login', async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const { username, password } = body;

  if (!username || !password) {
    return c.json({ error: 'BAD_REQUEST', message: '用户名和密码不能为空' }, 400);
  }

  const db = c.env.DB;
  if (!db) {
    return c.json({ error: 'DB_ERROR', message: '数据库未连接' }, 500);
  }

  // 保证默认 admin 可用
  if (username === 'admin') {
    await ensureAdminUser(db);
  }

  const user = await db
    .prepare('SELECT id, username, display_name, password_hash, role FROM users WHERE username = ?')
    .bind(username.trim())
    .first<{
      id: string;
      username: string;
      display_name: string;
      password_hash: string;
      role: 'admin' | 'teacher' | 'student';
    }>();

  if (!user) {
    return c.json({ error: 'INVALID_CREDENTIALS', message: '用户名或密码错误' }, 401);
  }

  const isMatch = await verifyPassword(password, user.password_hash);
  if (!isMatch) {
    return c.json({ error: 'INVALID_CREDENTIALS', message: '用户名或密码错误' }, 401);
  }

  const authUser = {
    id: user.id,
    username: user.username,
    displayName: user.display_name,
    role: user.role,
  };

  const token = await signJwt(authUser, c.env.JWT_SECRET);

  return c.json({
    token,
    user: authUser,
  });
});

authRouter.get('/me', authMiddleware, async (c) => {
  const user = c.get('user');
  return c.json({ user });
});

authRouter.post('/change-password', authMiddleware, async (c) => {
  const user = c.get('user')!;
  const body = await c.req.json().catch(() => ({}));
  const { currentPassword, newPassword } = body;

  if (!currentPassword || !newPassword) {
    return c.json({ error: 'BAD_REQUEST', message: '原密码和新密码不能为空' }, 400);
  }

  if (newPassword.length < 6) {
    return c.json({ error: 'BAD_REQUEST', message: '新密码长度至少需要 6 个字符' }, 400);
  }

  const db = c.env.DB;
  const dbUser = await db
    .prepare('SELECT password_hash FROM users WHERE id = ?')
    .bind(user.id)
    .first<{ password_hash: string }>();

  if (!dbUser) {
    return c.json({ error: 'USER_NOT_FOUND', message: '用户不存在' }, 404);
  }

  const isMatch = await verifyPassword(currentPassword, dbUser.password_hash);
  if (!isMatch) {
    return c.json({ error: 'INVALID_CURRENT_PASSWORD', message: '原密码错误' }, 400);
  }

  const newHash = await hashPassword(newPassword);
  const now = Date.now();
  await db
    .prepare('UPDATE users SET password_hash = ?, updated_at = ? WHERE id = ?')
    .bind(newHash, now, user.id)
    .run();

  return c.json({ success: true, message: '密码修改成功' });
});
