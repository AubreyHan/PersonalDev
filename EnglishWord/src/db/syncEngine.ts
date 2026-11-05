import { localDb } from './index';
import { apiRequest, getStoredToken, getStoredUser } from '../services/apiClient';
import { prefetchWordAudio } from './audioCache';
import type { StudentWordProgress } from '../../shared/types';

let isSyncing = false;

/**
 * 执行一次全量/增量双向同步 (Pull + Push)
 */
export async function triggerSync(): Promise<void> {
  const token = getStoredToken();
  const user = getStoredUser();

  if (!token || !user || user.role !== 'student' || !navigator.onLine) {
    return;
  }

  if (isSyncing) return;
  isSyncing = true;

  try {
    // 1. 先 Push 本地 Outbox 变更
    const outboxItems = await localDb.syncOutbox.toArray();
    if (outboxItems.length > 0) {
      const updates = outboxItems.map((item) => item.payload);
      await apiRequest('/api/student/sync/push', {
        method: 'POST',
        body: JSON.stringify({ updates }),
      });
      // 成功推送后清空当前已同步的 outbox 项
      const ids = outboxItems.map((item) => item.id!).filter(Boolean);
      await localDb.syncOutbox.bulkDelete(ids);
    }

    // 2. 从服务端 Pull 最新词条和云端进度
    const pullData = await apiRequest('/api/student/sync/pull');
    const { words, progress } = pullData;

    if (Array.isArray(words) && words.length > 0) {
      await localDb.words.bulkPut(words);
      // 收集发音音频链接，后台静默预缓存
      const audioUrls = words.map((w: any) => w.audioUsUrl).filter(Boolean);
      prefetchWordAudio(audioUrls);
    }

    if (Array.isArray(progress) && progress.length > 0) {
      await localDb.progress.bulkPut(progress);
    }
  } catch (err) {
    console.warn('[SyncEngine] 同步发生波动，将在网络稳定后重试', err);
  } finally {
    isSyncing = false;
  }
}

/**
 * 记录一条本地进度变更：写入 IndexedDB progress，并追加到 syncOutbox
 */
export async function saveLocalProgressUpdate(
  normalizedText: string,
  update: Partial<StudentWordProgress>
): Promise<void> {
  const existing = await localDb.progress.get(normalizedText);
  const now = Date.now();

  const merged: StudentWordProgress = {
    studentId: existing?.studentId || getStoredUser()?.id || '',
    normalizedText,
    status: update.status ?? existing?.status ?? 'unlearned',
    ebbinghausStage: update.ebbinghausStage ?? existing?.ebbinghausStage ?? 0,
    nextReviewDate: update.nextReviewDate !== undefined ? update.nextReviewDate : (existing?.nextReviewDate ?? null),
    lastStudiedAt: update.lastStudiedAt ?? existing?.lastStudiedAt ?? now,
    lastReviewedDate: update.lastReviewedDate !== undefined ? update.lastReviewedDate : (existing?.lastReviewedDate ?? null),
    studyCheckpoint: update.studyCheckpoint !== undefined ? update.studyCheckpoint : (existing?.studyCheckpoint ?? null),
    totalReviewCount: update.totalReviewCount ?? existing?.totalReviewCount ?? 0,
    errorCount: update.errorCount ?? existing?.errorCount ?? 0,
    stageResetCount: update.stageResetCount ?? existing?.stageResetCount ?? 0,
    lastErrorStep: update.lastErrorStep !== undefined ? update.lastErrorStep : (existing?.lastErrorStep ?? null),
    updatedAt: now,
  };

  await localDb.progress.put(merged);

  await localDb.syncOutbox.add({
    type: 'progress_update',
    normalizedText,
    payload: merged,
    createdAt: now,
  });

  // 尝试后台触发网络推送
  if (navigator.onLine) {
    triggerSync().catch(() => {});
  }
}

// 监听网络恢复，自动触发同步
if (typeof window !== 'undefined') {
  window.addEventListener('online', () => {
    triggerSync().catch(() => {});
  });
}
