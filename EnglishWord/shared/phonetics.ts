import { normalizeText } from './inflections';

export function isHomophoneMatch(
  input: string,
  target: string,
  homophones: string[] = []
): boolean {
  const normInput = normalizeText(input);
  const normTarget = normalizeText(target);

  if (normInput === normTarget) {
    return false;
  }

  return homophones.some((h) => normalizeText(h) === normInput);
}

export function levenshteinDistance(s1: string, s2: string): number {
  const m = s1.length;
  const n = s2.length;
  const dp: number[][] = Array.from({ length: m + 1 }, () => Array(n + 1).fill(0));

  for (let i = 0; i <= m; i++) dp[i][0] = i;
  for (let j = 0; j <= n; j++) dp[0][j] = j;

  for (let i = 1; i <= m; i++) {
    for (let j = 1; j <= n; j++) {
      if (s1[i - 1] === s2[j - 1]) {
        dp[i][j] = dp[i - 1][j - 1];
      } else {
        dp[i][j] = 1 + Math.min(dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]);
      }
    }
  }

  return dp[m][n];
}

export interface PronunciationScoreResult {
  score: number;
  isAccurate: boolean;
  cleanTarget: string;
  cleanRecognized: string;
}

/**
 * 评测跟读转写文本与目标单词/词组的相似度得分 (0..100)
 * 得分 >= 80 判定为准确
 */
export function evaluatePronunciationSimilarity(
  targetText: string,
  recognizedText: string
): PronunciationScoreResult {
  // 净化非英文字母及空格
  const cleanTarget = normalizeText(targetText).replace(/[^a-z0-9\s]/g, '');
  const cleanRecognized = normalizeText(recognizedText).replace(/[^a-z0-9\s]/g, '');

  if (!cleanRecognized) {
    return { score: 0, isAccurate: false, cleanTarget, cleanRecognized };
  }

  if (cleanTarget === cleanRecognized) {
    return { score: 100, isAccurate: true, cleanTarget, cleanRecognized };
  }

  const maxLen = Math.max(cleanTarget.length, cleanRecognized.length);
  if (maxLen === 0) {
    return { score: 100, isAccurate: true, cleanTarget, cleanRecognized };
  }

  const distance = levenshteinDistance(cleanTarget, cleanRecognized);
  const ratio = Math.max(0, 1 - distance / maxLen);
  const score = Math.round(ratio * 100);

  return {
    score,
    isAccurate: score >= 80,
    cleanTarget,
    cleanRecognized,
  };
}
