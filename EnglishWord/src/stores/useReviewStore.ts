import { create } from 'zustand';
import { localDb } from '../db';
import { triggerSync, saveLocalProgressUpdate } from '../db/syncEngine';
import {
  isWordDueForReview,
  calculateNextReviewState,
  getTodayDateStr,
} from '@shared/ebbinghaus';
import type { TeacherWord, StudentWordProgress } from '@shared/types';

interface ReviewQueueItem {
  word: TeacherWord;
  progress: StudentWordProgress;
  hasErroredInCurrentSession: boolean; // 是否在本次复习中出过错/看过提示
}

interface ReviewState {
  reviewQueue: ReviewQueueItem[];
  currentIndex: number;
  currentSubStep: '2.1' | '2.2' | '2.3' | '2.4';
  isReviewing: boolean;
  isSpotCheck: boolean; // 是否处于已掌握词抽查模式
  todayDueCount: number;
  todayCompletedCount: number;
  masteredCount: number;
  stageDistribution: Record<number, number>; // stage 1..6 counts
  isLoading: boolean;
  penaltyActive: boolean;

  loadReviewDashboard: () => Promise<void>;
  startTodayReview: () => void;
  startMasteredSpotCheck: () => void;
  moveToSubStep: (step: '2.1' | '2.2' | '2.3' | '2.4') => void;
  triggerPenalty: (step: '2.1' | '2.2' | '2.3' | '2.4') => Promise<void>;
  completeCurrentWord: () => Promise<void>;
  exitReview: () => void;
}

export const useReviewStore = create<ReviewState>((set, get) => ({
  reviewQueue: [],
  currentIndex: 0,
  currentSubStep: '2.1',
  isReviewing: false,
  isSpotCheck: false,
  todayDueCount: 0,
  todayCompletedCount: 0,
  masteredCount: 0,
  stageDistribution: { 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0 },
  isLoading: true,
  penaltyActive: false,

  loadReviewDashboard: async () => {
    set({ isLoading: true });
    triggerSync().catch(() => {});

    const todayStr = getTodayDateStr();
    const allWords = await localDb.words.toArray();
    const allProgress = await localDb.progress.toArray();
    const progressMap = new Map(allProgress.map((p) => [p.normalizedText, p]));

    let dueCount = 0;
    let completedToday = 0;
    let mastered = 0;
    const stages: Record<number, number> = { 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0 };

    for (const w of allWords) {
      const p = progressMap.get(w.normalizedText);
      if (!p) continue;

      if (p.status === 'mastered') {
        mastered++;
      } else if (p.status === 'reviewing') {
        if (p.ebbinghausStage >= 1 && p.ebbinghausStage <= 6) {
          stages[p.ebbinghausStage] = (stages[p.ebbinghausStage] || 0) + 1;
        }

        if (isWordDueForReview(p, todayStr)) {
          dueCount++;
        }
      }

      if (p.lastReviewedDate === todayStr) {
        completedToday++;
      }
    }

    set({
      todayDueCount: dueCount,
      todayCompletedCount: completedToday,
      masteredCount: mastered,
      stageDistribution: stages,
      isLoading: false,
      isReviewing: false,
    });
  },

  startTodayReview: async () => {
    const todayStr = getTodayDateStr();
    const allWords = await localDb.words.toArray();
    const allProgress = await localDb.progress.toArray();
    const wordMap = new Map(allWords.map((w) => [w.normalizedText, w]));

    // 筛选今日到期未复习的词条
    const dueQueue: ReviewQueueItem[] = [];
    for (const p of allProgress) {
      if (isWordDueForReview(p, todayStr)) {
        const w = wordMap.get(p.normalizedText);
        if (w) {
          dueQueue.push({
            word: w,
            progress: p,
            hasErroredInCurrentSession: false,
          });
        }
      }
    }

    // Fisher-Yates 随机洗牌（严禁剧透原顺序）
    for (let i = dueQueue.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [dueQueue[i], dueQueue[j]] = [dueQueue[j], dueQueue[i]];
    }

    set({
      reviewQueue: dueQueue,
      currentIndex: 0,
      currentSubStep: '2.1',
      isReviewing: true,
      isSpotCheck: false,
      penaltyActive: false,
    });
  },

  startMasteredSpotCheck: async () => {
    const allWords = await localDb.words.toArray();
    const allProgress = await localDb.progress.toArray();
    const wordMap = new Map(allWords.map((w) => [w.normalizedText, w]));

    const masteredQueue: ReviewQueueItem[] = [];
    for (const p of allProgress) {
      if (p.status === 'mastered') {
        const w = wordMap.get(p.normalizedText);
        if (w) {
          masteredQueue.push({
            word: w,
            progress: p,
            hasErroredInCurrentSession: false,
          });
        }
      }
    }

    // 随机抽查 10 个已掌握词
    for (let i = masteredQueue.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [masteredQueue[i], masteredQueue[j]] = [masteredQueue[j], masteredQueue[i]];
    }

    set({
      reviewQueue: masteredQueue.slice(0, 10),
      currentIndex: 0,
      currentSubStep: '2.1',
      isReviewing: true,
      isSpotCheck: true,
      penaltyActive: false,
    });
  },

  moveToSubStep: (step) => {
    set({ currentSubStep: step });
  },

  triggerPenalty: async (step) => {
    const { reviewQueue, currentIndex, isSpotCheck } = get();
    const currentItem = reviewQueue[currentIndex];
    if (!currentItem) return;

    currentItem.hasErroredInCurrentSession = true;
    set({ penaltyActive: true });

    // 计算艾宾浩斯降级：出错重置回 Stage 1 (次日必须再次复习)
    const todayStr = getTodayDateStr();
    const failUpdate = calculateNextReviewState(
      currentItem.progress,
      false,
      step,
      todayStr
    );

    // 持久化更新
    await saveLocalProgressUpdate(currentItem.word.normalizedText, failUpdate);

    // 今日队尾回炉重考机制 (Intra-day Re-queue)：追加到队列尾部重新盲测一次！
    const updatedQueue = [...reviewQueue];
    updatedQueue.push({
      word: currentItem.word,
      progress: {
        ...currentItem.progress,
        ...failUpdate,
      },
      hasErroredInCurrentSession: false, // 回炉时重新考验
    });

    set({ reviewQueue: updatedQueue });
  },

  completeCurrentWord: async () => {
    const { reviewQueue, currentIndex, isSpotCheck } = get();
    const currentItem = reviewQueue[currentIndex];
    if (!currentItem) return;

    const todayStr = getTodayDateStr();

    // 若本次无错一次通关，晋级下一阶段
    if (!currentItem.hasErroredInCurrentSession) {
      const passUpdate = calculateNextReviewState(
        currentItem.progress,
        true,
        null,
        todayStr
      );
      await saveLocalProgressUpdate(currentItem.word.normalizedText, passUpdate);
    }

    const nextIndex = currentIndex + 1;
    if (nextIndex < reviewQueue.length) {
      set({
        currentIndex: nextIndex,
        currentSubStep: '2.1',
        penaltyActive: false,
      });
    } else {
      // 今日复习全部完成
      set({ isReviewing: false, penaltyActive: false });
      get().loadReviewDashboard();
    }
  },

  exitReview: () => {
    set({ isReviewing: false, penaltyActive: false });
    get().loadReviewDashboard();
  },
}));
