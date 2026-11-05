import { Context, Next } from 'hono';
import { verifyJwt } from '../services/crypto';
import type { AuthUser, Env, HonoVariables } from '../types';
import type { UserRole } from '../../../shared/types';

export async function authMiddleware(
  c: Context<{ Bindings: Env; Variables: HonoVariables }>,
  next: Next
) {
  const authHeader = c.req.header('Authorization');
  if (!authHeader || !authHeader.startsWith('Bearer ')) {
    return c.json({ error: 'UNAUTHORIZED', message: '请先登录' }, 401);
  }

  const token = authHeader.substring(7).trim();
  const secret = c.env.JWT_SECRET;
  const user = await verifyJwt<AuthUser>(token, secret);

  if (!user) {
    return c.json({ error: 'INVALID_TOKEN', message: '登录态无效或已过期' }, 401);
  }

  c.set('user', user);
  await next();
}

export function requireRole(...allowedRoles: UserRole[]) {
  return async (
    c: Context<{ Bindings: Env; Variables: HonoVariables }>,
    next: Next
  ) => {
    const user = c.get('user');
    if (!user) {
      return c.json({ error: 'UNAUTHORIZED', message: '请先登录' }, 401);
    }

    if (!allowedRoles.includes(user.role)) {
      return c.json({ error: 'FORBIDDEN', message: '无权限访问该资源' }, 403);
    }

    await next();
  };
}
