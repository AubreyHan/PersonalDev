import { describe, it, expect, beforeEach } from 'vitest';
import app from '../api/[[route]]';

/**
 * 内存简易 SQLite / Mock D1 实现，用于全链路 API 接口自动化测试
 */
class MockD1Database {
  private users: Map<string, any> = new Map();
  private teacherStudents: Array<{ teacher_id: string; student_id: string; bound_at: number }> = [];
  private teacherWords: Map<string, any> = new Map();
  private studentWordProgress: Map<string, any> = new Map();

  prepare(query: string) {
    const db = this;
    return {
      bind(...params: any[]) {
        return {
          async first<T = any>(): Promise<T | null> {
            const res = await this.all<T>();
            return res.results[0] || null;
          },
          async all<T = any>(): Promise<{ results: T[] }> {
            const sql = query.trim();

            if (sql.includes('FROM users WHERE username = ?')) {
              const u = Array.from(db.users.values()).find((x) => x.username === params[0]);
              return { results: (u ? [u] : []) as T[] };
            }

            if (sql.includes('FROM users WHERE id = ?')) {
              const u = db.users.get(params[0]);
              return { results: (u ? [u] : []) as T[] };
            }

            if (sql.includes('FROM users u WHERE u.role = \'teacher\'')) {
              const list = Array.from(db.users.values())
                .filter((x) => x.role === 'teacher')
                .map((u) => ({
                  id: u.id,
                  username: u.username,
                  displayName: u.display_name,
                  createdAt: u.created_at,
                  studentCount: db.teacherStudents.filter((ts) => ts.teacher_id === u.id).length,
                  wordCount: Array.from(db.teacherWords.values()).filter((w) => w.teacher_id === u.id).length,
                }));
              return { results: list as T[] };
            }

            if (sql.includes('SELECT tw.*, u.display_name as teacherName')) {
              const tids = params;
              const words = Array.from(db.teacherWords.values()).filter((w) => tids.includes(w.teacher_id));
              return { results: words as T[] };
            }

            if (sql.includes('FROM teacher_words WHERE teacher_id = ? AND normalized_text = ?')) {
              const [tid, norm] = params;
              const w = Array.from(db.teacherWords.values()).find(
                (x) => x.teacher_id === tid && x.normalized_text === norm
              );
              return { results: (w ? [w] : []) as T[] };
            }

            if (sql.includes('FROM teacher_words WHERE teacher_id = ?')) {
              const tid = params[0];
              const list = Array.from(db.teacherWords.values()).filter((w) => w.teacher_id === tid);
              return { results: list as T[] };
            }

            if (sql.includes('FROM users u JOIN teacher_students ts ON u.id = ts.student_id WHERE ts.teacher_id = ?')) {
              const tid = params[0];
              const sids = db.teacherStudents.filter((ts) => ts.teacher_id === tid).map((ts) => ts.student_id);
              const list = sids.map((id) => {
                const u = db.users.get(id);
                return {
                  id: u.id,
                  username: u.username,
                  displayName: u.display_name,
                  boundAt: Date.now(),
                  totalTeachersCount: db.teacherStudents.filter((ts) => ts.student_id === id).length,
                };
              });
              return { results: list as T[] };
            }

            if (sql.includes('FROM users u WHERE u.role = \'student\' AND u.id NOT IN')) {
              const tid = params[0];
              const boundSids = new Set(
                db.teacherStudents.filter((ts) => ts.teacher_id === tid).map((ts) => ts.student_id)
              );
              const list = Array.from(db.users.values())
                .filter((u) => u.role === 'student' && !boundSids.has(u.id))
                .map((u) => ({
                  id: u.id,
                  username: u.username,
                  displayName: u.display_name,
                  totalTeachersCount: db.teacherStudents.filter((ts) => ts.student_id === u.id).length,
                }));
              return { results: list as T[] };
            }

            if (sql.includes('FROM student_word_progress WHERE student_id = ?')) {
              const sid = params[0];
              const list = Array.from(db.studentWordProgress.values()).filter((p) => p.student_id === sid);
              return { results: list as T[] };
            }

            return { results: [] };
          },
          async run() {
            const sql = query.trim();

            if (sql.startsWith('INSERT INTO users')) {
              const [id, username, display_name, password_hash, role, created_at, updated_at] = params;
              db.users.set(id, { id, username, display_name, password_hash, role, created_at, updated_at });
            } else if (sql.startsWith('INSERT INTO teacher_students') || sql.startsWith('INSERT OR IGNORE INTO teacher_students')) {
              const [teacher_id, student_id, bound_at] = params;
              if (!db.teacherStudents.some((ts) => ts.teacher_id === teacher_id && ts.student_id === student_id)) {
                db.teacherStudents.push({ teacher_id, student_id, bound_at });
              }
            } else if (sql.startsWith('INSERT INTO teacher_words')) {
              const [
                id, teacher_id, text, normalized_text, syllables,
                phonetic_us, phonetic_uk, audio_us_url, audio_uk_url,
                meanings_json, inflections_json, homophones_json, examples_json, tags_json,
                created_at, updated_at
              ] = params;
              db.teacherWords.set(id, {
                id, teacher_id, text, normalized_text, syllables,
                phonetic_us, phonetic_uk, audio_us_url, audio_uk_url,
                meanings_json, inflections_json, homophones_json, examples_json, tags_json,
                created_at, updated_at
              });
            } else if (sql.startsWith('INSERT INTO student_word_progress')) {
              const [
                student_id, normalized_text, status, ebbinghaus_stage, next_review_date,
                last_studied_at, last_reviewed_date, study_checkpoint_json,
                total_review_count, error_count, stage_reset_count, last_error_step, updated_at
              ] = params;
              const key = `${student_id}:${normalized_text}`;
              db.studentWordProgress.set(key, {
                student_id, normalized_text, status, ebbinghaus_stage, next_review_date,
                last_studied_at, last_reviewed_date, study_checkpoint_json,
                total_review_count, error_count, stage_reset_count, last_error_step, updated_at
              });
            } else if (sql.startsWith('UPDATE users SET password_hash = ?')) {
              const [hash, updated_at, id] = params;
              const u = db.users.get(id);
              if (u) {
                u.password_hash = hash;
                u.updated_at = updated_at;
              }
            } else if (sql.startsWith('DELETE FROM users WHERE id = ?')) {
              const id = params[0];
              db.users.delete(id);
              db.teacherStudents = db.teacherStudents.filter((ts) => ts.student_id !== id && ts.teacher_id !== id);
            }
            return { success: true };
          },
        };
      },
    };
  }
}

describe('Full Backend API Integration Tests', () => {
  let mockDb: any;
  const env: any = { JWT_SECRET: 'test-secret-key-32-chars-long-1234' };

  beforeEach(() => {
    mockDb = new MockD1Database();
    env.DB = mockDb;
  });

  it('AC-1: admin should login with initial password P@ss=w0rd and manage teachers & students', async () => {
    // 1. 管理员首次登录
    const loginRes = await app.request('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: 'admin', password: 'P@ss=w0rd' }),
    }, env);

    expect(loginRes.status).toBe(200);
    const loginData: any = await loginRes.json();
    expect(loginData.token).toBeDefined();
    expect(loginData.user.role).toBe('admin');

    const adminToken = loginData.token;

    // 2. 创建教师 teacher_zhang
    const createTeacherRes = await app.request('/api/admin/teachers', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        Authorization: `Bearer ${adminToken}`,
      },
      body: JSON.stringify({ username: 'teacher_zhang', displayName: '张老师' }),
    }, env);

    expect(createTeacherRes.status).toBe(200);
    const teacherData: any = await createTeacherRes.json();
    const teacherId = teacherData.teacher.id;
    expect(teacherData.teacher.username).toBe('teacher_zhang');

    // 3. 在张老师名下新建学生 stu_li
    const createStudentRes = await app.request(`/api/admin/teachers/${teacherId}/students`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        Authorization: `Bearer ${adminToken}`,
      },
      body: JSON.stringify({ mode: 'create', username: 'stu_li', displayName: '李同学' }),
    }, env);

    expect(createStudentRes.status).toBe(200);
    const studentData: any = await createStudentRes.json();
    expect(studentData.student.username).toBe('stu_li');

    // 4. 新学生使用默认密码 passw0rd 登录
    const stuLoginRes = await app.request('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: 'stu_li', password: 'passw0rd' }),
    }, env);

    expect(stuLoginRes.status).toBe(200);
    const stuLoginData: any = await stuLoginRes.json();
    expect(stuLoginData.user.role).toBe('student');
  });

  it('AC-2: teacher should enrich and save word, and duplicate check works', async () => {
    // 准备教师
    const loginRes = await app.request('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: 'admin', password: 'P@ss=w0rd' }),
    }, env);
    const adminToken = (await loginRes.json() as any).token;

    await app.request('/api/admin/teachers', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${adminToken}` },
      body: JSON.stringify({ username: 'teacher_wang', displayName: '王老师' }),
    }, env);

    const tLogin = await app.request('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: 'teacher_wang', password: 'passw0rd' }),
    }, env);
    const teacherToken = (await tLogin.json() as any).token;

    // 智能补全 look forward to
    const enrichRes = await app.request('/api/teacher/words/enrich', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${teacherToken}` },
      body: JSON.stringify({ text: 'look forward to' }),
    }, env);

    expect(enrichRes.status).toBe(200);
    const enrichData: any = await enrichRes.json();
    expect(enrichData.word.normalizedText).toBe('look forward to');
    expect(enrichData.word.inflections.pastTense).toBe('looked forward to');

    // 保存生词
    const saveRes = await app.request('/api/teacher/words', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${teacherToken}` },
      body: JSON.stringify(enrichData.word),
    }, env);
    expect(saveRes.status).toBe(200);

    // 再次保存相同生词，触发 409 DUPLICATE_WORD 拦截
    const dupRes = await app.request('/api/teacher/words', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${teacherToken}` },
      body: JSON.stringify(enrichData.word),
    }, env);
    expect(dupRes.status).toBe(409);
    const dupData: any = await dupRes.json();
    expect(dupData.error).toBe('DUPLICATE_WORD');
  });
});
