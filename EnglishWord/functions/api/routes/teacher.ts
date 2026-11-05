import { Hono } from 'hono';
import { authMiddleware, requireRole } from '../middleware/auth';
import { enrichWord } from '../services/enricher';
import { normalizeText } from '../../../shared/inflections';
import { getTodayDateStr } from '../../../shared/ebbinghaus';
import type { Env, HonoVariables } from '../types';
import type { TeacherWord } from '../../../shared/types';

export const teacherRouter = new Hono<{ Bindings: Env; Variables: HonoVariables }>();

// 仅教师角色可访问
teacherRouter.use('*', authMiddleware, requireRole('teacher'));

/**
 * 单词/词组智能补全预览
 */
teacherRouter.post('/words/enrich', async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const { text } = body;
  if (!text || typeof text !== 'string') {
    return c.json({ error: 'BAD_REQUEST', message: '请输入要补全的单词或词组' }, 400);
  }

  const enriched = await enrichWord(text, c.env.AI);
  return c.json({ word: enriched });
});

/**
 * 获取当前教师的所有词条
 */
teacherRouter.get('/words', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const query = c.req.query('q')?.toLowerCase();
  const tag = c.req.query('tag');

  let sql = 'SELECT * FROM teacher_words WHERE teacher_id = ?';
  const params: any[] = [user.id];

  if (query) {
    sql += ' AND (normalized_text LIKE ? OR meanings_json LIKE ?)';
    params.push(`%${query}%`, `%${query}%`);
  }

  if (tag) {
    sql += ' AND tags_json LIKE ?';
    params.push(`%"${tag}"%`);
  }

  sql += ' ORDER BY created_at DESC';

  const rows = await db.prepare(sql).bind(...params).all<any>();
  const words: TeacherWord[] = rows.results.map((r) => ({
    id: r.id,
    teacherId: r.teacher_id,
    text: r.text,
    normalizedText: r.normalized_text,
    syllables: r.syllables,
    phoneticUs: r.phonetic_us,
    phoneticUk: r.phonetic_uk,
    audioUsUrl: r.audio_us_url,
    audioUkUrl: r.audio_uk_url,
    meanings: JSON.parse(r.meanings_json || '[]'),
    inflections: JSON.parse(r.inflections_json || '{}'),
    homophones: JSON.parse(r.homophones_json || '[]'),
    examples: JSON.parse(r.examples_json || '[]'),
    tags: JSON.parse(r.tags_json || '[]'),
    createdAt: r.created_at,
    updatedAt: r.updated_at,
  }));

  return c.json({ words });
});

/**
 * 保存单个生词/词组 (支持重复录入拦截提示)
 */
teacherRouter.post('/words', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const body = await c.req.json().catch(() => ({}));

  const {
    text,
    syllables,
    phoneticUs,
    phoneticUk,
    audioUsUrl,
    audioUkUrl,
    meanings,
    inflections,
    homophones,
    examples,
    tags,
  } = body;

  if (!text) {
    return c.json({ error: 'BAD_REQUEST', message: '单词内容不能为空' }, 400);
  }

  const normalizedText = normalizeText(text);

  // 检查当前教师是否已存在该词
  const existing = await db
    .prepare('SELECT id, text FROM teacher_words WHERE teacher_id = ? AND normalized_text = ?')
    .bind(user.id, normalizedText)
    .first<{ id: string; text: string }>();

  if (existing) {
    return c.json(
      {
        error: 'DUPLICATE_WORD',
        message: `词条 "${existing.text}" 已存在于您的词库中，无需重复录入`,
        existingId: existing.id,
      },
      409
    );
  }

  const wordId = crypto.randomUUID();
  const now = Date.now();

  await db
    .prepare(
      `INSERT INTO teacher_words (
        id, teacher_id, text, normalized_text, syllables,
        phonetic_us, phonetic_uk, audio_us_url, audio_uk_url,
        meanings_json, inflections_json, homophones_json, examples_json, tags_json,
        created_at, updated_at
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`
    )
    .bind(
      wordId,
      user.id,
      text.trim(),
      normalizedText,
      syllables || text.trim(),
      phoneticUs || '',
      phoneticUk || '',
      audioUsUrl || '',
      audioUkUrl || '',
      JSON.stringify(meanings || []),
      JSON.stringify(inflections || {}),
      JSON.stringify(homophones || []),
      JSON.stringify(examples || []),
      JSON.stringify(tags || []),
      now,
      now
    )
    .run();

  return c.json({
    word: {
      id: wordId,
      teacherId: user.id,
      text: text.trim(),
      normalizedText,
      syllables: syllables || text.trim(),
      phoneticUs: phoneticUs || '',
      phoneticUk: phoneticUk || '',
      audioUsUrl: audioUsUrl || '',
      audioUkUrl: audioUkUrl || '',
      meanings: meanings || [],
      inflections: inflections || {},
      homophones: homophones || [],
      examples: examples || [],
      tags: tags || [],
      createdAt: now,
      updatedAt: now,
    },
  });
});

/**
 * 编辑词条
 */
teacherRouter.put('/words/:wordId', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const wordId = c.req.param('wordId');
  const body = await c.req.json().catch(() => ({}));

  const existing = await db
    .prepare('SELECT id FROM teacher_words WHERE id = ? AND teacher_id = ?')
    .bind(wordId, user.id)
    .first();

  if (!existing) {
    return c.json({ error: 'NOT_FOUND', message: '词条不存在或无权编辑' }, 404);
  }

  const now = Date.now();
  await db
    .prepare(
      `UPDATE teacher_words SET
        text = ?, syllables = ?, phonetic_us = ?, phonetic_uk = ?,
        audio_us_url = ?, audio_uk_url = ?, meanings_json = ?,
        inflections_json = ?, homophones_json = ?, examples_json = ?,
        tags_json = ?, updated_at = ?
       WHERE id = ? AND teacher_id = ?`
    )
    .bind(
      body.text?.trim(),
      body.syllables || body.text?.trim(),
      body.phoneticUs || '',
      body.phoneticUk || '',
      body.audioUsUrl || '',
      body.audioUkUrl || '',
      JSON.stringify(body.meanings || []),
      JSON.stringify(body.inflections || {}),
      JSON.stringify(body.homophones || []),
      JSON.stringify(body.examples || []),
      JSON.stringify(body.tags || []),
      now,
      wordId,
      user.id
    )
    .run();

  return c.json({ success: true, message: '词条更新成功' });
});

/**
 * 删除词条
 */
teacherRouter.delete('/words/:wordId', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const wordId = c.req.param('wordId');

  await db
    .prepare('DELETE FROM teacher_words WHERE id = ? AND teacher_id = ?')
    .bind(wordId, user.id)
    .run();

  return c.json({ success: true, message: '词条已删除' });
});

/**
 * 名下学生学情总览
 */
teacherRouter.get('/analytics/students', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const today = getTodayDateStr();

  // 获取名下学生
  const students = await db
    .prepare(
      `SELECT u.id as studentId, u.username, u.display_name as displayName
       FROM users u
       JOIN teacher_students ts ON u.id = ts.student_id
       WHERE ts.teacher_id = ?
       ORDER BY ts.bound_at DESC`
    )
    .bind(user.id)
    .all<{ studentId: string; username: string; displayName: string }>();

  // 当前教师的词汇总数
  const totalTeacherWords = await db
    .prepare('SELECT COUNT(*) as count FROM teacher_words WHERE teacher_id = ?')
    .bind(user.id)
    .first<{ count: number }>();
  const totalWords = totalTeacherWords?.count || 0;

  const summaries = [];
  for (const s of students.results) {
    const stats = await db
      .prepare(
        `SELECT
          SUM(CASE WHEN p.status = 'reviewing' THEN 1 ELSE 0 END) as reviewingCount,
          SUM(CASE WHEN p.status = 'mastered' THEN 1 ELSE 0 END) as masteredCount,
          SUM(CASE WHEN p.status = 'reviewing' AND p.next_review_date <= ? THEN 1 ELSE 0 END) as todayDueCount,
          SUM(CASE WHEN p.last_reviewed_date = ? THEN 1 ELSE 0 END) as todayCompletedCount,
          MAX(p.last_studied_at) as lastActiveAt
         FROM teacher_words tw
         LEFT JOIN student_word_progress p
           ON p.student_id = ? AND p.normalized_text = tw.normalized_text
         WHERE tw.teacher_id = ?`
      )
      .bind(today, today, s.studentId, user.id)
      .first<{
        reviewingCount: number | null;
        masteredCount: number | null;
        todayDueCount: number | null;
        todayCompletedCount: number | null;
        lastActiveAt: number | null;
      }>();

    const reviewing = stats?.reviewingCount || 0;
    const mastered = stats?.masteredCount || 0;
    const unlearned = Math.max(0, totalWords - reviewing - mastered);

    summaries.push({
      studentId: s.studentId,
      username: s.username,
      displayName: s.displayName,
      totalWords,
      unlearnedCount: unlearned,
      reviewingCount: reviewing,
      masteredCount: mastered,
      todayDueReviewCount: stats?.todayDueCount || 0,
      todayCompletedReviewCount: stats?.todayCompletedCount || 0,
      lastActiveAt: stats?.lastActiveAt || null,
    });
  }

  return c.json({ summaries });
});

/**
 * 查看指定学生的薄弱错词榜单 (按 errorCount DESC 排序)
 */
teacherRouter.get('/analytics/students/:studentId/weak-words', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const studentId = c.req.param('studentId');

  // 验证师生关系
  const isBound = await db
    .prepare('SELECT 1 FROM teacher_students WHERE teacher_id = ? AND student_id = ?')
    .bind(user.id, studentId)
    .first();

  if (!isBound) {
    return c.json({ error: 'FORBIDDEN', message: '无权查看非名下学生学情' }, 403);
  }

  const rows = await db
    .prepare(
      `SELECT tw.text, tw.normalized_text as normalizedText, tw.meanings_json as meaningsJson,
              p.error_count as errorCount, p.stage_reset_count as stageResetCount,
              p.last_error_step as lastErrorStep, p.ebbinghaus_stage as currentStage,
              p.next_review_date as nextReviewDate
       FROM student_word_progress p
       JOIN teacher_words tw ON tw.normalized_text = p.normalized_text
       WHERE p.student_id = ? AND tw.teacher_id = ? AND p.error_count > 0
       ORDER BY p.error_count DESC, p.stage_reset_count DESC
       LIMIT 50`
    )
    .bind(studentId, user.id)
    .all<any>();

  const weakWords = rows.results.map((r) => ({
    text: r.text,
    normalizedText: r.normalizedText,
    meanings: JSON.parse(r.meaningsJson || '[]'),
    errorCount: r.errorCount,
    stageResetCount: r.stageResetCount,
    lastErrorStep: r.lastErrorStep,
    currentStage: r.currentStage,
    nextReviewDate: r.nextReviewDate,
  }));

  return c.json({ weakWords });
});
