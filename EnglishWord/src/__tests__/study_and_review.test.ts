import { describe, it, expect, beforeEach } from 'vitest';
import { useStudyStore } from '../stores/useStudyStore';
import { useReviewStore } from '../stores/useReviewStore';
import { localDb } from '../db';
import { initializeFirstStudyState, calculateNextReviewState } from '@shared/ebbinghaus';
import type { TeacherWord } from '@shared/types';

describe('Student Study & Review Pipeline Integration', () => {
  const sampleWord: TeacherWord = {
    id: 'w-1',
    teacherId: 't-1',
    text: 'write',
    normalizedText: 'write',
    syllables: 'write',
    phoneticUs: '/raɪt/',
    phoneticUk: '/raɪt/',
    audioUsUrl: '/api/speech/tts?text=write',
    audioUkUrl: '/api/speech/tts?text=write',
    meanings: [{ pos: 'v.', zh: '书写；写作' }],
    inflections: {
      pastTense: 'wrote',
      pastParticiple: 'written',
      presentParticiple: 'writing',
      thirdPerson: 'writes',
    },
    homophones: ['right', 'rite'],
    examples: [
      {
        en: 'She wrote a letter yesterday.',
        zh: '她昨天写了一封信。',
        targetInSentence: 'wrote',
      },
    ],
    tags: ['初中核心'],
    createdAt: Date.now(),
    updatedAt: Date.now(),
  };

  beforeEach(async () => {
    await localDb.words.clear();
    await localDb.progress.clear();
    await localDb.syncOutbox.clear();
    await localDb.words.put(sampleWord);
  });

  it('AC-3: should progress through 4-step study and initialize into Ebbinghaus Stage 1', async () => {
    const studyStore = useStudyStore.getState();
    await studyStore.loadWordsAndProgress();

    expect(useStudyStore.getState().words).toHaveLength(1);
    expect(useStudyStore.getState().currentSubStep).toBe('1');

    // 1 -> 2.1 (盲打听写)
    useStudyStore.getState().moveToSubStep('2.1');
    expect(useStudyStore.getState().currentSubStep).toBe('2.1');

    // 2.1 记录子步骤断点并进入 2.2
    useStudyStore.getState().updateCheckpoint({ dictationRepCount: 3 });
    useStudyStore.getState().moveToSubStep('2.2');
    expect(useStudyStore.getState().currentSubStep).toBe('2.2');

    // 2.2 -> 2.3 (跟读评测)
    useStudyStore.getState().moveToSubStep('2.3');
    useStudyStore.getState().updateCheckpoint({ pronunciationPassCount: 3 });
    expect(useStudyStore.getState().currentSubStep).toBe('2.3');

    // 2.3 -> 2.4 (例句填空)
    useStudyStore.getState().moveToSubStep('2.4');
    expect(useStudyStore.getState().currentSubStep).toBe('2.4');

    // 2.4 完成 -> 触发单词完成
    await useStudyStore.getState().completeCurrentWord();

    // 验证 IndexedDB 中已保存为 reviewing 状态且 stage = 1
    const progress = await localDb.progress.get('write');
    expect(progress).toBeDefined();
    expect(progress?.status).toBe('reviewing');
    expect(progress?.ebbinghausStage).toBe(1);
    expect(progress?.nextReviewDate).toBeDefined();
  });

  it('AC-4: review flow: 1-rep pass advances stage, error triggers penalty & re-queues', async () => {
    // 准备一个到期待复习的词条
    const todayStr = '2026-11-05';
    await localDb.progress.put({
      studentId: 's-1',
      normalizedText: 'write',
      status: 'reviewing',
      ebbinghausStage: 1,
      nextReviewDate: todayStr,
      lastStudiedAt: Date.now(),
      lastReviewedDate: null,
      studyCheckpoint: null,
      totalReviewCount: 0,
      errorCount: 0,
      stageResetCount: 0,
      lastErrorStep: null,
      updatedAt: Date.now(),
    });

    const reviewStore = useReviewStore.getState();
    await reviewStore.loadReviewDashboard();

    expect(useReviewStore.getState().todayDueCount).toBe(1);

    // 开始复习
    await reviewStore.startTodayReview();
    expect(useReviewStore.getState().reviewQueue).toHaveLength(1);
    expect(useReviewStore.getState().currentSubStep).toBe('2.1'); // 无步骤1展示页直接开考

    // 模拟复习出错触发惩罚
    useReviewStore.getState().triggerPenalty('2.1');

    const updatedState = useReviewStore.getState();
    expect(updatedState.penaltyActive).toBe(true);
    // 验证触发队尾回炉：队列被追加 1 个重考项
    expect(updatedState.reviewQueue).toHaveLength(2);

    // 验证数据库中该词 stage 被重置回 1，累加 errorCount 与 stageResetCount
    const p = await localDb.progress.get('write');
    expect(p?.ebbinghausStage).toBe(1);
    expect(p?.errorCount).toBe(1);
    expect(p?.stageResetCount).toBe(1);
  });
});
