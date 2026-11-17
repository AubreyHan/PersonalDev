import { Hono } from 'hono';
import { authMiddleware } from '../middleware/auth';
import { enrichWord } from '../services/enricher';
import { normalizeText } from '../../../shared/inflections';
import { getTodayDateStr } from '../../../shared/ebbinghaus';
import type { Env, HonoVariables } from '../types';
import type { WordItem, UserWeakWord } from '../../../shared/types';

export const wordsRouter = new Hono<{ Bindings: Env; Variables: HonoVariables }>();

// 任意登录用户均可访问
wordsRouter.use('*', authMiddleware);

/**
 * 单词/词组智能补全预览
 */
wordsRouter.post('/enrich', async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const { text } = body;
  if (!text || typeof text !== 'string') {
    return c.json({ error: 'BAD_REQUEST', message: '请输入要补全的单词或词组' }, 400);
  }

  const enriched = await enrichWord(text, c.env.AI);
  return c.json({ word: enriched });
});

/**
 * 获取当前用户的个人词库列表
 */
wordsRouter.get('/', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const query = c.req.query('q')?.toLowerCase();
  const tag = c.req.query('tag');

  let sql = 'SELECT * FROM user_words WHERE user_id = ?';
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
  const words: WordItem[] = rows.results.map((r) => ({
    id: r.id,
    userId: r.user_id,
    teacherId: r.user_id,
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
 * 添加单词/词组到当前用户的个人词库
 */
wordsRouter.post('/', async (c) => {
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

  if (!text || typeof text !== 'string') {
    return c.json({ error: 'BAD_REQUEST', message: '单词/词组内容不能为空' }, 400);
  }

  const normalizedText = normalizeText(text);

  // 检查当前用户是否已存在该词
  const existing = await db
    .prepare('SELECT id, text FROM user_words WHERE user_id = ? AND normalized_text = ?')
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
      `INSERT INTO user_words (
        id, user_id, text, normalized_text, syllables,
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
      userId: user.id,
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
  }, 201);
});

/**
 * 编辑词条
 */
wordsRouter.put('/:wordId', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const wordId = c.req.param('wordId');
  const body = await c.req.json().catch(() => ({}));

  const existing = await db
    .prepare('SELECT id FROM user_words WHERE id = ? AND user_id = ?')
    .bind(wordId, user.id)
    .first();

  if (!existing) {
    return c.json({ error: 'NOT_FOUND', message: '词条不存在或无权编辑' }, 404);
  }

  const now = Date.now();
  await db
    .prepare(
      `UPDATE user_words SET
        text = ?, syllables = ?, phonetic_us = ?, phonetic_uk = ?,
        audio_us_url = ?, audio_uk_url = ?, meanings_json = ?,
        inflections_json = ?, homophones_json = ?, examples_json = ?,
        tags_json = ?, updated_at = ?
       WHERE id = ? AND user_id = ?`
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
wordsRouter.delete('/:wordId', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const wordId = c.req.param('wordId');

  await db
    .prepare('DELETE FROM user_words WHERE id = ? AND user_id = ?')
    .bind(wordId, user.id)
    .run();

  return c.json({ success: true, message: '词条已删除' });
});

/**
 * 获取当前用户的学情统计与高频薄弱词榜单
 */
wordsRouter.get('/stats', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const today = getTodayDateStr();

  const totalWordsRow = await db
    .prepare('SELECT COUNT(*) as count FROM user_words WHERE user_id = ?')
    .bind(user.id)
    .first<{ count: number }>();
  const totalWords = totalWordsRow?.count || 0;

  const progressRows = await db
    .prepare('SELECT status, next_review_date, last_reviewed_date FROM user_word_progress WHERE user_id = ?')
    .bind(user.id)
    .all<any>();

  let unlearnedCount = totalWords;
  let reviewingCount = 0;
  let masteredCount = 0;
  let todayDueReviewCount = 0;
  let todayCompletedReviewCount = 0;

  for (const p of progressRows.results) {
    if (p.status === 'learning' || p.status === 'reviewing') {
      reviewingCount++;
      unlearnedCount--;
      if (p.next_review_date && p.next_review_date <= today) {
        todayDueReviewCount++;
      }
    } else if (p.status === 'mastered') {
      masteredCount++;
      unlearnedCount--;
    }
    if (p.last_reviewed_date === today) {
      todayCompletedReviewCount++;
    }
  }

  // 高频薄弱错词榜单 (error_count > 0, 降序)
  const weakRows = await db
    .prepare(
      `SELECT p.normalized_text, p.error_count, p.stage_reset_count, p.last_error_step,
              p.ebbinghaus_stage, p.next_review_date,
              uw.text, uw.meanings_json
       FROM user_word_progress p
       JOIN user_words uw ON uw.normalized_text = p.normalized_text AND uw.user_id = p.user_id
       WHERE p.user_id = ? AND p.error_count > 0
       ORDER BY p.error_count DESC, p.stage_reset_count DESC
       LIMIT 20`
    )
    .bind(user.id)
    .all<any>();

  const weakWords: UserWeakWord[] = weakRows.results.map((r) => ({
    normalizedText: r.normalized_text,
    text: r.text,
    meanings: JSON.parse(r.meanings_json || '[]'),
    errorCount: r.error_count,
    stageResetCount: r.stage_reset_count,
    lastErrorStep: r.last_error_step,
    currentStage: r.ebbinghaus_stage,
    nextReviewDate: r.next_review_date,
  }));

  return c.json({
    stats: {
      userId: user.id,
      studentId: user.id,
      username: user.username,
      displayName: user.displayName,
      totalWords,
      unlearnedCount: Math.max(0, unlearnedCount),
      reviewingCount,
      masteredCount,
      todayDueReviewCount,
      todayCompletedReviewCount,
      lastActiveAt: Date.now(),
    },
    weakWords,
  });
});
