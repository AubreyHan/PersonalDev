import { Hono } from 'hono';
import { hashPassword } from '../services/crypto';
import { authMiddleware, requireRole } from '../middleware/auth';
import type { Env, HonoVariables } from '../types';

export const adminRouter = new Hono<{ Bindings: Env; Variables: HonoVariables }>();

// 所有 admin 路由都需要 admin 角色
adminRouter.use('*', authMiddleware, requireRole('admin'));

const DEFAULT_NEW_USER_PASSWORD = 'passw0rd';

/**
 * 获取所有教师列表及每个教师名下的学生数、词条数
 */
adminRouter.get('/teachers', async (c) => {
  const db = c.env.DB;
  const teachers = await db
    .prepare(
      `SELECT u.id, u.username, u.display_name as displayName, u.created_at as createdAt,
              (SELECT COUNT(*) FROM teacher_students ts WHERE ts.teacher_id = u.id) as studentCount,
              (SELECT COUNT(*) FROM teacher_words tw WHERE tw.teacher_id = u.id) as wordCount
       FROM users u
       WHERE u.role = 'teacher'
       ORDER BY u.created_at DESC`
    )
    .all<{
      id: string;
      username: string;
      displayName: string;
      createdAt: number;
      studentCount: number;
      wordCount: number;
    }>();

  return c.json({ teachers: teachers.results });
});

/**
 * 创建新教师账号 (默认密码 passw0rd)
 */
adminRouter.post('/teachers', async (c) => {
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

  const teacherId = crypto.randomUUID();
  const passwordHash = await hashPassword(DEFAULT_NEW_USER_PASSWORD);
  const now = Date.now();

  await db
    .prepare(
      'INSERT INTO users (id, username, display_name, password_hash, role, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)'
    )
    .bind(
      teacherId,
      trimmedUsername,
      displayName?.trim() || trimmedUsername,
      passwordHash,
      'teacher',
      now,
      now
    )
    .run();

  return c.json({
    teacher: {
      id: teacherId,
      username: trimmedUsername,
      displayName: displayName?.trim() || trimmedUsername,
      role: 'teacher',
      studentCount: 0,
      wordCount: 0,
      createdAt: now,
    },
  });
});

/**
 * 删除教师
 */
adminRouter.delete('/teachers/:teacherId', async (c) => {
  const db = c.env.DB;
  const teacherId = c.req.param('teacherId');
  const deleteOrphanStudents = c.req.query('deleteOrphanStudents') === 'true';

  const teacher = await db
    .prepare("SELECT id FROM users WHERE id = ? AND role = 'teacher'")
    .bind(teacherId)
    .first();

  if (!teacher) {
    return c.json({ error: 'NOT_FOUND', message: '教师不存在' }, 404);
  }

  // 检查名下学生是否有仅归属于该教师的孤儿学生
  const boundStudents = await db
    .prepare('SELECT student_id FROM teacher_students WHERE teacher_id = ?')
    .bind(teacherId)
    .all<{ student_id: string }>();

  const orphanStudentIds: string[] = [];
  for (const s of boundStudents.results) {
    const otherTeacherCount = await db
      .prepare('SELECT COUNT(*) as count FROM teacher_students WHERE student_id = ? AND teacher_id != ?')
      .bind(s.student_id, teacherId)
      .first<{ count: number }>();

    if (!otherTeacherCount || otherTeacherCount.count === 0) {
      orphanStudentIds.push(s.student_id);
    }
  }

  if (orphanStudentIds.length > 0 && !deleteOrphanStudents) {
    return c.json(
      {
        error: 'ORPHAN_STUDENTS_EXIST',
        message: `存在 ${orphanStudentIds.length} 名仅归属该教师的学生，请确认是否连带清理账号`,
        orphanCount: orphanStudentIds.length,
      },
      409
    );
  }

  // 若确认连带清理孤儿学生
  if (deleteOrphanStudents && orphanStudentIds.length > 0) {
    for (const orphanId of orphanStudentIds) {
      await db.prepare('DELETE FROM users WHERE id = ?').bind(orphanId).run();
    }
  }

  // 删除教师（外键级联清理 teacher_students 与 teacher_words）
  await db.prepare('DELETE FROM users WHERE id = ?').bind(teacherId).run();

  return c.json({ success: true, message: '教师及关联资源已成功删除' });
});

/**
 * 获取教师名下绑定的学生列表及系统中可选的其他已有学生
 */
adminRouter.get('/teachers/:teacherId/students', async (c) => {
  const db = c.env.DB;
  const teacherId = c.req.param('teacherId');

  // 该教师名下已绑定的学生
  const boundStudents = await db
    .prepare(
      `SELECT u.id, u.username, u.display_name as displayName, ts.bound_at as boundAt,
              (SELECT COUNT(*) FROM teacher_students ts2 WHERE ts2.student_id = u.id) as totalTeachersCount
       FROM users u
       JOIN teacher_students ts ON u.id = ts.student_id
       WHERE ts.teacher_id = ?
       ORDER BY ts.bound_at DESC`
    )
    .bind(teacherId)
    .all();

  // 系统中未绑定该教师的其它学生
  const availableStudents = await db
    .prepare(
      `SELECT u.id, u.username, u.display_name as displayName,
              (SELECT COUNT(*) FROM teacher_students ts WHERE ts.student_id = u.id) as totalTeachersCount
       FROM users u
       WHERE u.role = 'student'
         AND u.id NOT IN (SELECT student_id FROM teacher_students WHERE teacher_id = ?)
       ORDER BY u.created_at DESC`
    )
    .bind(teacherId)
    .all();

  return c.json({
    boundStudents: boundStudents.results,
    availableStudents: availableStudents.results,
  });
});

/**
 * 在教师名下新建学生 (mode=create) 或关联已有学生 (mode=bind_existing)
 */
adminRouter.post('/teachers/:teacherId/students', async (c) => {
  const db = c.env.DB;
  const teacherId = c.req.param('teacherId');
  const body = await c.req.json().catch(() => ({}));
  const { mode, username, displayName, studentIds } = body;

  const teacher = await db
    .prepare("SELECT id FROM users WHERE id = ? AND role = 'teacher'")
    .bind(teacherId)
    .first();

  if (!teacher) {
    return c.json({ error: 'TEACHER_NOT_FOUND', message: '指定教师不存在' }, 404);
  }

  const now = Date.now();

  if (mode === 'create') {
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

    const studentId = crypto.randomUUID();
    const passwordHash = await hashPassword(DEFAULT_NEW_USER_PASSWORD);

    await db
      .prepare(
        'INSERT INTO users (id, username, display_name, password_hash, role, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)'
      )
      .bind(
        studentId,
        trimmedUsername,
        displayName?.trim() || trimmedUsername,
        passwordHash,
        'student',
        now,
        now
      )
      .run();

    await db
      .prepare('INSERT INTO teacher_students (teacher_id, student_id, bound_at) VALUES (?, ?, ?)')
      .bind(teacherId, studentId, now)
      .run();

    return c.json({
      student: {
        id: studentId,
        username: trimmedUsername,
        displayName: displayName?.trim() || trimmedUsername,
        boundAt: now,
        totalTeachersCount: 1,
      },
    });
  } else if (mode === 'bind_existing') {
    if (!Array.isArray(studentIds) || studentIds.length === 0) {
      return c.json({ error: 'BAD_REQUEST', message: '请选择要绑定的学生' }, 400);
    }

    for (const sid of studentIds) {
      await db
        .prepare('INSERT OR IGNORE INTO teacher_students (teacher_id, student_id, bound_at) VALUES (?, ?, ?)')
        .bind(teacherId, sid, now)
        .run();
    }

    return c.json({ success: true, boundCount: studentIds.length });
  }

  return c.json({ error: 'BAD_REQUEST', message: '无效的 mode 操作' }, 400);
});

/**
 * 从教师名下解绑学生 (action=unbind) 或彻底删除学生账号 (action=delete_account)
 */
adminRouter.delete('/teachers/:teacherId/students/:studentId', async (c) => {
  const db = c.env.DB;
  const teacherId = c.req.param('teacherId');
  const studentId = c.req.param('studentId');
  const action = c.req.query('action') || 'unbind';

  if (action === 'delete_account') {
    await db.prepare('DELETE FROM users WHERE id = ?').bind(studentId).run();
    return c.json({ success: true, message: '学生账号已彻底删除' });
  }

  // 解绑操作
  const otherTeachers = await db
    .prepare('SELECT COUNT(*) as count FROM teacher_students WHERE student_id = ? AND teacher_id != ?')
    .bind(studentId, teacherId)
    .first<{ count: number }>();

  const isOrphan = !otherTeachers || otherTeachers.count === 0;
  const force = c.req.query('force') === 'true';

  if (isOrphan && !force) {
    return c.json(
      {
        error: 'LAST_TEACHER_WARNING',
        message: '该学生当前仅归属这一名教师，移出后将失去所有教师归属。请确认是否强制移出并彻底删除账号？',
      },
      409
    );
  }

  if (isOrphan && force) {
    await db.prepare('DELETE FROM users WHERE id = ?').bind(studentId).run();
    return c.json({ success: true, message: '学生唯一归属已解除，账号已彻底清理' });
  }

  await db
    .prepare('DELETE FROM teacher_students WHERE teacher_id = ? AND student_id = ?')
    .bind(teacherId, studentId)
    .run();

  return c.json({ success: true, message: '已从当前教师名下移出' });
});

/**
 * 一键重置任意用户密码为 passw0rd
 */
adminRouter.post('/users/:userId/reset-password', async (c) => {
  const db = c.env.DB;
  const userId = c.req.param('userId');

  const user = await db.prepare('SELECT id FROM users WHERE id = ?').bind(userId).first();
  if (!user) {
    return c.json({ error: 'NOT_FOUND', message: '用户不存在' }, 404);
  }

  const newHash = await hashPassword(DEFAULT_NEW_USER_PASSWORD);
  const now = Date.now();
  await db
    .prepare('UPDATE users SET password_hash = ?, updated_at = ? WHERE id = ?')
    .bind(newHash, now, userId)
    .run();

  return c.json({ success: true, message: `密码已重置为默认密码: ${DEFAULT_NEW_USER_PASSWORD}` });
});
