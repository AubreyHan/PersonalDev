import { describe, it, expect } from 'vitest';
import {
  mergeWordInflections,
  tokenizeIntoSlots,
  normalizeText,
} from '../inflections';
import {
  isWordDueForReview,
  calculateNextReviewState,
  initializeFirstStudyState,
  addDays,
} from '../ebbinghaus';
import {
  isHomophoneMatch,
  levenshteinDistance,
  evaluatePronunciationSimilarity,
} from '../phonetics';

describe('shared/inflections', () => {
  it('should normalize texts properly', () => {
    expect(normalizeText('  Hello World! ')).toBe('hello world!');
  });

  it('should merge identical inflections and filter base-identical ones', () => {
    // 规则动词 play: pastTense === pastParticiple === played
    const playedMerged = mergeWordInflections('play', {
      thirdPerson: 'plays',
      presentParticiple: 'playing',
      pastTense: 'played',
      pastParticiple: 'played',
    });

    expect(playedMerged).toEqual([
      { labels: ['第三人称单数'], form: 'plays' },
      { labels: ['现在分词'], form: 'playing' },
      { labels: ['过去式', '过去分词'], form: 'played' },
    ]);

    // cut: pastTense = pastParticiple = cut (与原词相同，不单独作为变形考核)
    const cutMerged = mergeWordInflections('cut', {
      thirdPerson: 'cuts',
      presentParticiple: 'cutting',
      pastTense: 'cut',
      pastParticiple: 'cut',
    });

    expect(cutMerged).toEqual([
      { labels: ['第三人称单数'], form: 'cuts' },
      { labels: ['现在分词'], form: 'cutting' },
    ]);
  });

  it('should tokenize word and phrases into slots', () => {
    const singleWord = tokenizeIntoSlots('book');
    expect(singleWord).toHaveLength(4);
    expect(singleWord.every((t) => t.isSlot)).toBe(true);

    const phrase = tokenizeIntoSlots('look at');
    expect(phrase[4]).toEqual({ index: 4, char: ' ', isSlot: false });
    expect(phrase[0].isSlot).toBe(true);
    expect(phrase[5].isSlot).toBe(true);
  });
});

describe('shared/ebbinghaus', () => {
  it('should correctly determine due words including overdue natural rollover', () => {
    const today = '2026-11-05';

    // 到期等于今天
    expect(isWordDueForReview({ status: 'reviewing', nextReviewDate: '2026-11-05' }, today)).toBe(true);
    // 逾期（昨天没复习顺延到今天）
    expect(isWordDueForReview({ status: 'reviewing', nextReviewDate: '2026-11-03' }, today)).toBe(true);
    // 明天到期，今天不复习
    expect(isWordDueForReview({ status: 'reviewing', nextReviewDate: '2026-11-06' }, today)).toBe(false);
    // 非 reviewing 状态
    expect(isWordDueForReview({ status: 'unlearned', nextReviewDate: '2026-11-05' }, today)).toBe(false);
    expect(isWordDueForReview({ status: 'mastered', nextReviewDate: '2026-11-05' }, today)).toBe(false);
  });

  it('should initialize first study state properly', () => {
    const init = initializeFirstStudyState('2026-11-05');
    expect(init.status).toBe('reviewing');
    expect(init.ebbinghausStage).toBe(1);
    expect(init.nextReviewDate).toBe('2026-11-06'); // +1 day
  });

  it('should advance stage on success and reset on error', () => {
    const current = {
      status: 'reviewing' as const,
      ebbinghausStage: 1,
      totalReviewCount: 1,
      errorCount: 0,
      stageResetCount: 0,
    };

    // Stage 1 -> 2 (+2 days)
    const passResult = calculateNextReviewState(current, true, null, '2026-11-05');
    expect(passResult.status).toBe('reviewing');
    expect(passResult.ebbinghausStage).toBe(2);
    expect(passResult.nextReviewDate).toBe('2026-11-07');
    expect(passResult.totalReviewCount).toBe(2);
    expect(passResult.errorCount).toBe(0);

    // 出错或看提示 -> 重置回 Stage 1 (+1 day)，累加 errorCount 与 stageResetCount
    const failResult = calculateNextReviewState(current, false, '2.1', '2026-11-05');
    expect(failResult.status).toBe('reviewing');
    expect(failResult.ebbinghausStage).toBe(1);
    expect(failResult.nextReviewDate).toBe('2026-11-06');
    expect(failResult.errorCount).toBe(1);
    expect(failResult.stageResetCount).toBe(1);
    expect(failResult.lastErrorStep).toBe('2.1');

    // Stage 6 成功 -> mastered
    const stage6 = {
      ...current,
      ebbinghausStage: 6,
    };
    const masterResult = calculateNextReviewState(stage6, true, null, '2026-11-05');
    expect(masterResult.status).toBe('mastered');
    expect(masterResult.ebbinghausStage).toBe(6);
    expect(masterResult.nextReviewDate).toBeNull();
  });
});

describe('shared/phonetics', () => {
  it('should detect homophones accurately without false positives on target', () => {
    const homophones = ['right', 'rite'];
    expect(isHomophoneMatch('right', 'write', homophones)).toBe(true);
    expect(isHomophoneMatch('rite', 'write', homophones)).toBe(true);
    expect(isHomophoneMatch('write', 'write', homophones)).toBe(false);
    expect(isHomophoneMatch('wrong', 'write', homophones)).toBe(false);
  });

  it('should calculate Levenshtein distance', () => {
    expect(levenshteinDistance('kitten', 'sitting')).toBe(3);
    expect(levenshteinDistance('book', 'book')).toBe(0);
  });

  it('should evaluate pronunciation score and accuracy threshold (>= 80)', () => {
    const perfect = evaluatePronunciationSimilarity('information', 'information');
    expect(perfect.score).toBe(100);
    expect(perfect.isAccurate).toBe(true);

    const close = evaluatePronunciationSimilarity('information', 'informatio');
    expect(close.score).toBeGreaterThanOrEqual(90);
    expect(close.isAccurate).toBe(true);

    const bad = evaluatePronunciationSimilarity('information', 'apple');
    expect(bad.score).toBeLessThan(40);
    expect(bad.isAccurate).toBe(false);
  });
});
