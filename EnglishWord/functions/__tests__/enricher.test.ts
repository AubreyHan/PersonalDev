import { describe, it, expect } from 'vitest';
import { parseMeaningString, enrichWord, isPlaceholderMeaning, isPlaceholderExample, deriveInflections } from '../api/services/enricher';

describe('enrichWord & phrase translation tests', () => {
  it('parseMeaningString should correctly parse English words and phrase definitions', () => {
    // Single word with pos
    const r1 = parseMeaningString('n. 情感，情绪；激情', false);
    expect(r1).toEqual({ pos: 'n.', zh: '情感，情绪；激情' });

    // Phrase with colon
    const r2 = parseMeaningString('与……有关：指两个或多个事物之间存在某种联系或关联。', true);
    expect(r2).toEqual({ pos: 'phr.', zh: '与……有关' });

    // Phrase without colon
    const r3 = parseMeaningString('幽默感', true);
    expect(r3).toEqual({ pos: 'phr.', zh: '幽默感' });

    // Phrase with verb pos and colon
    const r4 = parseMeaningString('v. 放弃，停止：指放弃某事', true);
    expect(r4).toEqual({ pos: 'v.', zh: '放弃，停止' });

    // Placeholder strings should return null
    expect(parseMeaningString('（请编辑输入中文释义）', true)).toBeNull();
    expect(parseMeaningString('（暂无中文释义，请手动输入）', true)).toBeNull();
  });

  it('isPlaceholderMeaning and isPlaceholderExample work accurately', () => {
    expect(isPlaceholderMeaning('（请编辑输入中文释义）')).toBe(true);
    expect(isPlaceholderMeaning('（暂无中文释义，请手动输入）')).toBe(true);
    expect(isPlaceholderMeaning('与……有关')).toBe(false);
    expect(isPlaceholderMeaning('emotion definition in English')).toBe(true);

    expect(isPlaceholderExample('这是关于 be related to 的例句。')).toBe(true);
    expect(isPlaceholderExample('请阅读理解该例句')).toBe(true);
    expect(isPlaceholderExample('它被认为与食物消费有关。')).toBe(false);
  });

  it('deriveInflections handles "be" and "be related to"', () => {
    const inf = deriveInflections('be related to');
    expect(inf.thirdPerson).toBe('is related to');
    expect(inf.presentParticiple).toBe('being related to');
    expect(inf.pastTense).toBe('was related to');
    expect(inf.pastParticiple).toBe('been related to');
  });

  it('enrichWord enriches phrases like "be related to" with genuine Chinese and examples', async () => {
    const res = await enrichWord('be related to');
    expect(res.meanings.length).toBeGreaterThan(0);
    expect(res.meanings.some((m) => m.zh.includes('与') && m.zh.includes('关'))).toBe(true);
    expect(res.meanings.every((m) => !isPlaceholderMeaning(m.zh))).toBe(true);
    expect(res.examples.length).toBeGreaterThan(0);
    expect(res.examples.some((ex) => !isPlaceholderExample(ex.zh))).toBe(true);
  });

  it('enrichWord enriches phrases like "a sense of humour"', async () => {
    const res = await enrichWord('a sense of humour');
    expect(res.meanings.length).toBeGreaterThan(0);
    expect(res.meanings.some((m) => m.zh.includes('幽默感'))).toBe(true);
    expect(res.examples.length).toBeGreaterThan(0);
    expect(res.examples.some((ex) => !isPlaceholderExample(ex.zh))).toBe(true);
  });
});
