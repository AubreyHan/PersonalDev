import { StudentWordProgress, StudyStatus } from './types';

export const EBBINGHAUS_INTERVALS = [1, 2, 4, 7, 15, 30] as const;

export function getTodayDateStr(date: Date = new Date()): string {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  return `${year}-${month}-${day}`;
}

export function addDays(dateStr: string, days: number): string {
  const [y, m, d] = dateStr.split('-').map(Number);
  const date = new Date(y, m - 1, d);
  date.setDate(date.getDate() + days);
  return getTodayDateStr(date);
}

/**
 * 判断单词今天是否到期需要复习（支持未复习自然顺延：nextReviewDate <= todayDateStr）
 */
export function isWordDueForReview(
  progress: Pick<StudentWordProgress, 'status' | 'nextReviewDate'>,
  todayDateStr: string = getTodayDateStr()
): boolean {
  if (progress.status !== 'reviewing' || !progress.nextReviewDate) {
    return false;
  }
  return progress.nextReviewDate <= todayDateStr;
}

export interface ReviewStateUpdateResult {
  status: StudyStatus;
  ebbinghausStage: number;
  nextReviewDate: string | null;
  lastReviewedDate: string;
  totalReviewCount: number;
  errorCount: number;
  stageResetCount: number;
  lastErrorStep: '2.1' | '2.2' | '2.3' | '2.4' | null;
}

/**
 * 计算复习完成后的下一阶段状态：
 * - passedWithoutError = true：若 stage < 6 则 stage+1 并计算下次日期；若已是 stage 6 则标记为 mastered
 * - passedWithoutError = false (出错或看提示)：重置为 Stage 1 (nextReviewDate = today + 1d)，累加 errorCount 与 stageResetCount
 */
export function calculateNextReviewState(
  current: Pick<
    StudentWordProgress,
    'status' | 'ebbinghausStage' | 'totalReviewCount' | 'errorCount' | 'stageResetCount'
  >,
  passedWithoutError: boolean,
  errorStep: '2.1' | '2.2' | '2.3' | '2.4' | null = null,
  todayDateStr: string = getTodayDateStr()
): ReviewStateUpdateResult {
  const totalReviewCount = current.totalReviewCount + 1;

  if (passedWithoutError) {
    const nextStage = current.ebbinghausStage + 1;
    if (nextStage > EBBINGHAUS_INTERVALS.length) {
      // 完成第 6 阶段（30天），标记彻底掌握
      return {
        status: 'mastered',
        ebbinghausStage: 6,
        nextReviewDate: null,
        lastReviewedDate: todayDateStr,
        totalReviewCount,
        errorCount: current.errorCount,
        stageResetCount: current.stageResetCount,
        lastErrorStep: null,
      };
    } else {
      // 晋级下一阶段，从 EBBINGHAUS_INTERVALS 获取间隔天数
      const intervalDays = EBBINGHAUS_INTERVALS[nextStage - 1];
      return {
        status: 'reviewing',
        ebbinghausStage: nextStage,
        nextReviewDate: addDays(todayDateStr, intervalDays),
        lastReviewedDate: todayDateStr,
        totalReviewCount,
        errorCount: current.errorCount,
        stageResetCount: current.stageResetCount,
        lastErrorStep: null,
      };
    }
  } else {
    // 复习中出错或看提示，重置回 Stage 1（次日必考）
    return {
      status: 'reviewing',
      ebbinghausStage: 1,
      nextReviewDate: addDays(todayDateStr, 1),
      lastReviewedDate: todayDateStr,
      totalReviewCount,
      errorCount: current.errorCount + 1,
      stageResetCount: current.stageResetCount + 1,
      lastErrorStep: errorStep,
    };
  }
}

/**
 * 单词初次完成学习（四步闯关完毕）后初始化进入艾宾浩斯复习池
 */
export function initializeFirstStudyState(
  todayDateStr: string = getTodayDateStr()
): Pick<
  StudentWordProgress,
  'status' | 'ebbinghausStage' | 'nextReviewDate' | 'studyCheckpoint'
> {
  return {
    status: 'reviewing',
    ebbinghausStage: 1,
    nextReviewDate: addDays(todayDateStr, 1), // 次日第 1 次复习
    studyCheckpoint: null,
  };
}
