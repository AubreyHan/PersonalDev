import { Hono } from 'hono';
import { hashPassword } from '../services/crypto';
import { authMiddleware, requireRole } from '../middleware/auth';
import type { Env, HonoVariables } from '../types';

export const adminRouter = new Hono<{ Bindings: Env; Variables: HonoVariables }>();

// 所有 admin 路由都需要 admin 角色
adminRouter.use('*', authMiddleware, requireRole('admin'));

const DEFAULT_NEW_USER_PASSWORD = 'passw0rd';

/**
 * 获取所有普通用户列表及其词条数、已掌握数、最近活跃时间
 */
adminRouter.get('/users', async (c) => {
  const db = c.env.DB;
  const users = await db
    .prepare(
      `SELECT u.id, u.username, u.display_name as displayName, u.role, u.created_at as createdAt,
              (SELECT COUNT(*) FROM user_words uw WHERE uw.user_id = u.id) as wordCount,
              (SELECT COUNT(*) FROM user_word_progress uwp WHERE uwp.user_id = u.id AND uwp.status = 'mastered') as masteredCount,
              (SELECT MAX(last_studied_at) FROM user_word_progress uwp WHERE uwp.user_id = u.id) as lastActiveAt
       FROM users u
       WHERE u.role != 'admin'
       ORDER BY u.created_at DESC`
    )
    .all<{
      id: string;
      username: string;
      displayName: string;
      role: string;
      createdAt: number;
      wordCount: number;
      masteredCount: number;
      lastActiveAt: number | null;
    }>();

  return c.json({ users: users.results });
});

/**
 * 创建新用户账号 (默认密码 passw0rd)
 */
adminRouter.post('/users', async (c) => {
  const db = c.env.DB;
  const body = await c.req.json().catch(() => ({}));
  const { username, displayName } = body;

  if (!username) {
    return c.json({ error: 'BAD_REQUEST', message: '用户名不能为空' }, 400);
  }

  const trimmedUsername = username.trim();
  const existing = await db
    .prepare('SELECT id FROM users WHERE username = ?')
    .bind(trimmedUsername)
    .first();

  if (existing) {
    return c.json({ error: 'USERNAME_EXISTS', message: '该用户名已存在' }, 409);
  }

  const id = crypto.randomUUID();
  const passwordHash = await hashPassword(DEFAULT_NEW_USER_PASSWORD);
  const now = Date.now();

  await db
    .prepare(
      'INSERT INTO users (id, username, display_name, password_hash, role, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)'
    )
    .bind(id, trimmedUsername, (displayName || trimmedUsername).trim(), passwordHash, 'user', now, now)
    .run();

  return c.json({
    user: {
      id,
      username: trimmedUsername,
      displayName: (displayName || trimmedUsername).trim(),
      role: 'user',
      createdAt: now,
      updatedAt: now,
    },
  }, 201);
});

/**
 * 重置指定用户密码为默认密码 passw0rd
 */
adminRouter.post('/users/:userId/reset-password', async (c) => {
  const db = c.env.DB;
  const userId = c.req.param('userId');

  const user = await db
    .prepare('SELECT id, role FROM users WHERE id = ?')
    .bind(userId)
    .first<{ id: string; role: string }>();

  if (!user) {
    return c.json({ error: 'NOT_FOUND', message: '用户不存在' }, 404);
  }

  if (user.role === 'admin') {
    return c.json({ error: 'FORBIDDEN', message: '不能在此重置管理员密码' }, 403);
  }

  const newHash = await hashPassword(DEFAULT_NEW_USER_PASSWORD);
  const now = Date.now();

  await db
    .prepare('UPDATE users SET password_hash = ?, updated_at = ? WHERE id = ?')
    .bind(newHash, now, userId)
    .run();

  return c.json({ success: true, message: `密码已重置为 ${DEFAULT_NEW_USER_PASSWORD}` });
});

/**
 * 彻底删除用户账号（级联清理个人词库与进度）
 */
adminRouter.delete('/users/:userId', async (c) => {
  const db = c.env.DB;
  const userId = c.req.param('userId');

  const user = await db
    .prepare('SELECT id, role FROM users WHERE id = ?')
    .bind(userId)
    .first<{ id: string; role: string }>();

  if (!user) {
    return c.json({ error: 'NOT_FOUND', message: '用户不存在' }, 404);
  }

  if (user.role === 'admin') {
    return c.json({ error: 'FORBIDDEN', message: '不能删除系统管理员账号' }, 403);
  }

  // 级联清理 user_words 与 user_word_progress
  await db.batch([
    db.prepare('DELETE FROM user_words WHERE user_id = ?').bind(userId),
    db.prepare('DELETE FROM user_word_progress WHERE user_id = ?').bind(userId),
    db.prepare('DELETE FROM users WHERE id = ?').bind(userId),
  ]);

  return c.json({ success: true, message: '用户账号及相关数据已成功删除' });
});
