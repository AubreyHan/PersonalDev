import { MergedInflectionItem, WordInflections } from './types';

export function normalizeText(text: string): string {
  return text.trim().toLowerCase();
}

const INFLECTION_META: Array<{ key: keyof WordInflections; label: string }> = [
  { key: 'plural', label: '复数' },
  { key: 'thirdPerson', label: '第三人称单数' },
  { key: 'presentParticiple', label: '现在分词' },
  { key: 'pastTense', label: '过去式' },
  { key: 'pastParticiple', label: '过去分词' },
  { key: 'comparative', label: '比较级' },
  { key: 'superlative', label: '最高级' },
];

/**
 * 自动合并相同拼写的词形变化，过滤与原词拼写完全一致的无意义重复项
 */
export function mergeWordInflections(
  baseText: string,
  inflections?: WordInflections
): MergedInflectionItem[] {
  if (!inflections) return [];

  const normalizedBase = normalizeText(baseText);
  const map = new Map<string, { labels: string[]; originalForm: string }>();

  for (const { key, label } of INFLECTION_META) {
    const rawVal = inflections[key];
    if (!rawVal) continue;

    const trimmed = rawVal.trim();
    if (!trimmed) continue;

    const normalized = normalizeText(trimmed);
    // 与原词完全相同的变化形式（如 cut 的过去式/过去分词）不单独作为变形考核
    if (normalized === normalizedBase) continue;

    const existing = map.get(normalized);
    if (existing) {
      if (!existing.labels.includes(label)) {
        existing.labels.push(label);
      }
    } else {
      map.set(normalized, {
        labels: [label],
        originalForm: trimmed,
      });
    }
  }

  return Array.from(map.values()).map((item) => ({
    labels: item.labels,
    form: item.originalForm,
  }));
}

export interface SlotToken {
  index: number;
  char: string;
  isSlot: boolean; // 字母为 true；空格/连字符等分隔符为 false
}

/**
 * 将单词或词组拆解为下划线字母槽位和静态标点/空格
 */
export function tokenizeIntoSlots(targetText: string): SlotToken[] {
  const result: SlotToken[] = [];
  const chars = Array.from(targetText);

  for (let i = 0; i < chars.length; i++) {
    const char = chars[i];
    // 字母属于可输入槽位，其余（空格、横杠、点、标点）为静态展示
    const isLetter = /[a-zA-Z]/.test(char);
    result.push({
      index: i,
      char,
      isSlot: isLetter,
    });
  }

  return result;
}
