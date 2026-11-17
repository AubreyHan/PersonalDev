import { Hono } from 'hono';
import { authMiddleware } from '../middleware/auth';
import type { Env, HonoVariables } from '../types';
import type { UserWordProgress, WordItem } from '../../../shared/types';

export const syncRouter = new Hono<{ Bindings: Env; Variables: HonoVariables }>();

// 任意登录用户均可访问同步接口
syncRouter.use('*', authMiddleware);

/**
 * 用户端拉取个人最新词库与云端进度
 */
syncRouter.get('/pull', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;

  // 1. 获取当前用户的所有词条
  const wordRows = await db
    .prepare(
      `SELECT * FROM user_words
       WHERE user_id = ?
       ORDER BY created_at ASC`
    )
    .bind(user.id)
    .all<any>();

  const words: WordItem[] = wordRows.results.map((r) => ({
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

  // 2. 获取该用户的所有词条学习与复习进度
  const progressRows = await db
    .prepare('SELECT * FROM user_word_progress WHERE user_id = ?')
    .bind(user.id)
    .all<any>();

  const progressList: UserWordProgress[] = progressRows.results.map((r) => ({
    userId: r.user_id,
    studentId: r.user_id,
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
    words,
    progress: progressList,
    serverTime: Date.now(),
  });
});

/**
 * 用户端推送离线产生的进度变更 (Upsert)
 */
syncRouter.post('/push', async (c) => {
  const db = c.env.DB;
  const user = c.get('user')!;
  const body = await c.req.json().catch(() => ({}));
  const updates: Partial<UserWordProgress>[] = body.updates || [];

  if (!Array.isArray(updates) || updates.length === 0) {
    return c.json({ success: true, processedCount: 0 });
  }

  const now = Date.now();

  for (const item of updates) {
    if (!item.normalizedText) continue;

    await db
      .prepare(
        `INSERT INTO user_word_progress (
          user_id, normalized_text, status, ebbinghaus_stage, next_review_date,
          last_studied_at, last_reviewed_date, study_checkpoint_json,
          total_review_count, error_count, stage_reset_count, last_error_step,
          updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(user_id, normalized_text) DO UPDATE SET
          status = excluded.status,
          ebbinghaus_stage = excluded.ebbinghaus_stage,
          next_review_date = excluded.next_review_date,
          last_studied_at = COALESCE(excluded.last_studied_at, user_word_progress.last_studied_at),
          last_reviewed_date = COALESCE(excluded.last_reviewed_date, user_word_progress.last_reviewed_date),
          study_checkpoint_json = excluded.study_checkpoint_json,
          total_review_count = excluded.total_review_count,
          error_count = excluded.error_count,
          stage_reset_count = excluded.stage_reset_count,
          last_error_step = excluded.last_error_step,
          updated_at = excluded.updated_at
        WHERE excluded.updated_at >= user_word_progress.updated_at`
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
