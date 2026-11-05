import { Hono } from 'hono';
import { authMiddleware, requireRole } from '../middleware/auth';
import type { Env, HonoVariables } from '../types';
import type { StudentWordProgress, TeacherWord, WordExample } from '../../../shared/types';

export const studentRouter = new Hono<{ Bindings: Env; Variables: HonoVariables }>();

// 仅学生角色可访问
studentRouter.use('*', authMiddleware, requireRole('student'));

/**
 * 学生端增量拉取：所属教师列表、关联词库(多教师同词例句聚合)、云端学习/复习进度
 */
studentRouter.get('/sync/pull', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;

  // 1. 获取关联的教师列表
  const teachers = await db
    .prepare(
      `SELECT u.id, u.display_name as displayName
       FROM users u
       JOIN teacher_students ts ON u.id = ts.teacher_id
       WHERE ts.student_id = ?`
    )
    .bind(user.id)
    .all<{ id: string; displayName: string }>();

  const teacherIds = teachers.results.map((t) => t.id);

  if (teacherIds.length === 0) {
    return c.json({
      teachers: [],
      words: [],
      progress: [],
      serverTime: Date.now(),
    });
  }

  // 2. 获取这些教师的所有词条
  const placeholders = teacherIds.map(() => '?').join(',');
  const wordRows = await db
    .prepare(
      `SELECT tw.*, u.display_name as teacherName
       FROM teacher_words tw
       JOIN users u ON u.id = tw.teacher_id
       WHERE tw.teacher_id IN (${placeholders})
       ORDER BY tw.created_at ASC`
    )
    .bind(...teacherIds)
    .all<any>();

  // 跨教师同词归一化聚合：同 normalizedText 的词合并例句
  const wordMap = new Map<string, TeacherWord>();
  for (const r of wordRows.results) {
    const norm = r.normalized_text;
    const examples: WordExample[] = JSON.parse(r.examples_json || '[]');

    if (!wordMap.has(norm)) {
      wordMap.set(norm, {
        id: r.id,
        teacherId: r.teacher_id,
        teacherName: r.teacherName,
        text: r.text,
        normalizedText: norm,
        syllables: r.syllables,
        phoneticUs: r.phonetic_us,
        phoneticUk: r.phonetic_uk,
        audioUsUrl: r.audio_us_url,
        audioUkUrl: r.audio_uk_url,
        meanings: JSON.parse(r.meanings_json || '[]'),
        inflections: JSON.parse(r.inflections_json || '{}'),
        homophones: JSON.parse(r.homophones_json || '[]'),
        examples,
        tags: JSON.parse(r.tags_json || '[]'),
        createdAt: r.created_at,
        updatedAt: r.updated_at,
      });
    } else {
      // 合并不同教师提供的例句（去重）
      const existing = wordMap.get(norm)!;
      for (const ex of examples) {
        if (!existing.examples.some((e) => e.en === ex.en)) {
          existing.examples.push(ex);
        }
      }
    }
  }

  // 3. 获取该学生的所有词条进度
  const progressRows = await db
    .prepare('SELECT * FROM student_word_progress WHERE student_id = ?')
    .bind(user.id)
    .all<any>();

  const progressList: StudentWordProgress[] = progressRows.results.map((r) => ({
    studentId: r.student_id,
    normalizedText: r.normalized_text,
    status: r.status,
    ebbinghausStage: r.ebbinghaus_stage,
    nextReviewDate: r.next_review_date,
    lastStudiedAt: r.last_studied_at,
    lastReviewedDate: r.last_reviewed_date,
    studyCheckpoint: r.study_checkpoint_json ? JSON.parse(r.study_checkpoint_json) : null,
    totalReviewCount: r.total_review_count,
    errorCount: r.error_count,
    stageResetCount: r.stage_reset_count,
    lastErrorStep: r.last_error_step,
    updatedAt: r.updated_at,
  }));

  return c.json({
    teachers: teachers.results,
    words: Array.from(wordMap.values()),
    progress: progressList,
    serverTime: Date.now(),
  });
});

/**
 * 学生端推送离线产生的进度变更 (Upsert)
 */
studentRouter.post('/sync/push', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const body = await c.req.json().catch(() => ({}));
  const updates: Partial<StudentWordProgress>[] = body.updates || [];

  if (!Array.isArray(updates) || updates.length === 0) {
    return c.json({ success: true, processedCount: 0 });
  }

  const now = Date.now();

  for (const item of updates) {
    if (!item.normalizedText) continue;

    await db
      .prepare(
        `INSERT INTO student_word_progress (
          student_id, normalized_text, status, ebbinghaus_stage, next_review_date,
          last_studied_at, last_reviewed_date, study_checkpoint_json,
          total_review_count, error_count, stage_reset_count, last_error_step, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(student_id, normalized_text) DO UPDATE SET
          status = excluded.status,
          ebbinghaus_stage = excluded.ebbinghaus_stage,
          next_review_date = excluded.next_review_date,
          last_studied_at = excluded.last_studied_at,
          last_reviewed_date = excluded.last_reviewed_date,
          study_checkpoint_json = excluded.study_checkpoint_json,
          total_review_count = excluded.total_review_count,
          error_count = excluded.error_count,
          stage_reset_count = excluded.stage_reset_count,
          last_error_step = excluded.last_error_step,
          updated_at = excluded.updated_at
        WHERE excluded.updated_at >= student_word_progress.updated_at`
      )
      .bind(
        user.id,
        item.normalizedText,
        item.status || 'unlearned',
        item.ebbinghausStage ?? 0,
        item.nextReviewDate || null,
        item.lastStudiedAt || null,
        item.lastReviewedDate || null,
        item.studyCheckpoint ? JSON.stringify(item.studyCheckpoint) : null,
        item.totalReviewCount ?? 0,
        item.errorCount ?? 0,
        item.stageResetCount ?? 0,
        item.lastErrorStep || null,
        item.updatedAt || now
      )
      .run();
  }

  return c.json({ success: true, processedCount: updates.length });
});
