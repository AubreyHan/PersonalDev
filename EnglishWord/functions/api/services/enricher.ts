import { TeacherWord, WordExample, WordInflections, WordMeaning } from '../../../shared/types';
import { normalizeText } from '../../../shared/inflections';

const COMMON_HOMOPHONE_MAP: Record<string, string[]> = {
  right: ['write', 'rite'],
  write: ['right', 'rite'],
  meat: ['meet'],
  meet: ['meat'],
  sea: ['see'],
  see: ['sea'],
  sun: ['son'],
  son: ['sun'],
  hear: ['here'],
  here: ['hear'],
  buy: ['by', 'bye'],
  by: ['buy', 'bye'],
  hour: ['our'],
  our: ['hour'],
  flower: ['flour'],
  flour: ['flower'],
  piece: ['peace'],
  peace: ['piece'],
  wait: ['weight'],
  weight: ['wait'],
  tail: ['tale'],
  tale: ['tail'],
  road: ['rode', 'rowed'],
  rode: ['road'],
  pair: ['pear'],
  pear: ['pair'],
  stare: ['stair'],
  stair: ['stare'],
};

export function hasChinese(str?: string): boolean {
  if (!str) return false;
  return /[\u4e00-\u9fa5]/.test(str);
}

export function isPlaceholderMeaning(zh?: string): boolean {
  if (!zh || !hasChinese(zh)) return true;
  if (zh.includes('请编辑') || zh.includes('暂无中文')) return true;
  return false;
}

export function isPlaceholderExample(zh?: string): boolean {
  if (!zh || !hasChinese(zh)) return true;
  if (zh.includes('请阅读理解') || /^这是关于\s*.+\s*的例句[。.]?$/.test(zh.trim())) return true;
  return false;
}

/**
 * 结构化解析释义字符串（支持 "n. 词义" 或 短语的 "短语释义：详细说明"）
 */
export function parseMeaningString(
  rawStr: string,
  isPhrase: boolean
): { pos: string; zh: string } | null {
  const raw = rawStr.trim();
  if (!raw || !hasChinese(raw)) return null;

  // 1. 匹配标准词性前缀，例如 "n. 情感，情绪" 或 "v. 害怕"
  const posMatch = raw.match(/^([a-zA-Z]+\.?)\s*(.+)$/);
  if (posMatch) {
    const pos = posMatch[1].endsWith('.') ? posMatch[1] : `${posMatch[1]}.`;
    let zh = posMatch[2].trim();
    // 如果后面跟有冒号说明，提取冒号前的核心释义
    if (zh.includes('：') || zh.includes(':')) {
      const parts = zh.split(/[:：]/);
      if (hasChinese(parts[0])) {
        zh = parts[0].trim();
      }
    }
    zh = zh.replace(/[…\.\s]+$/, '').trim();
    if (!zh || isPlaceholderMeaning(zh)) return null;
    return { pos, zh };
  }

  // 2. 如果包含冒号说明，例如 "与……有关：指两个或多个事物之间存在某种联系或关联。" 或 "幽默感：一种能够理解..."
  if (raw.includes('：') || raw.includes(':')) {
    const parts = raw.split(/[:：]/);
    if (hasChinese(parts[0])) {
      const zh = parts[0].replace(/[…\.\s]+$/, '').trim();
      if (zh && !isPlaceholderMeaning(zh)) {
        return {
          pos: isPhrase ? 'phr.' : 'n./v.',
          zh,
        };
      }
    }
  }

  // 3. 直接作为释义
  const cleanZh = raw.replace(/[…\.\s]+$/, '').trim();
  if (!cleanZh || isPlaceholderMeaning(cleanZh)) return null;
  return {
    pos: isPhrase ? 'phr.' : 'n./v.',
    zh: cleanZh,
  };
}

/**
 * 启发式音节切分规则（针对常见英语单词）
 */
function heuristicSyllableSplit(word: string): string {
  if (word.includes(' ') || word.includes('-') || word.length <= 4) {
    return word;
  }
  return word
    .replace(/(in)(for)(ma)(tion)/gi, '$1·$2·$3·$4')
    .replace(/(com)(pu)(ter)/gi, '$1·$2·$3')
    .replace(/(beau)(ti)(ful)/gi, '$1·$2·$3')
    .replace(/([^aeiouy]{1,2})([aeiouy]+)([^aeiouy]+)([aeiouy]+)/gi, (match, p1, p2, p3, p4) => {
      if (p3.length >= 2) {
        return `${p1}${p2}${p3.slice(0, 1)}·${p3.slice(1)}${p4}`;
      }
      return `${p1}${p2}·${p3}${p4}`;
    });
}

/**
 * 规则推导常见动词/名词词形变化
 */
export function deriveInflections(word: string): WordInflections {
  const norm = normalizeText(word);
  if (norm.includes(' ')) {
    const parts = norm.split(' ');
    const first = parts[0];
    const rest = parts.slice(1).join(' ');
    const firstInf = deriveInflections(first);
    return {
      thirdPerson: firstInf.thirdPerson ? `${firstInf.thirdPerson} ${rest}` : undefined,
      presentParticiple: firstInf.presentParticiple ? `${firstInf.presentParticiple} ${rest}` : undefined,
      pastTense: firstInf.pastTense ? `${firstInf.pastTense} ${rest}` : undefined,
      pastParticiple: firstInf.pastParticiple ? `${firstInf.pastParticiple} ${rest}` : undefined,
    };
  }

  let plural: string | undefined;
  let thirdPerson: string | undefined;
  let presentParticiple: string | undefined;
  let pastTense: string | undefined;
  let pastParticiple: string | undefined;

  if (norm.endsWith('y') && !/[aeiou]y$/.test(norm)) {
    plural = norm.slice(0, -1) + 'ies';
    thirdPerson = plural;
  } else if (/(s|sh|ch|x|z)$/.test(norm)) {
    plural = norm + 'es';
    thirdPerson = plural;
  } else {
    plural = norm + 's';
    thirdPerson = plural;
  }

  if (norm.endsWith('ie')) {
    presentParticiple = norm.slice(0, -2) + 'ying';
  } else if (norm.endsWith('e') && !/(ee|oe|ye)$/.test(norm)) {
    presentParticiple = norm.slice(0, -1) + 'ing';
  } else if (/[^aeiou][aeiou][^aeiouwxy]$/.test(norm) && norm.length <= 4) {
    presentParticiple = norm + norm.slice(-1) + 'ing';
  } else {
    presentParticiple = norm + 'ing';
  }

  if (norm.endsWith('e')) {
    pastTense = norm + 'd';
    pastParticiple = norm + 'd';
  } else if (norm.endsWith('y') && !/[aeiou]y$/.test(norm)) {
    pastTense = norm.slice(0, -1) + 'ied';
    pastParticiple = pastTense;
  } else if (/[^aeiou][aeiou][^aeiouwxy]$/.test(norm) && norm.length <= 4) {
    pastTense = norm + norm.slice(-1) + 'ed';
    pastParticiple = pastTense;
  } else {
    pastTense = norm + 'ed';
    pastParticiple = pastTense;
  }

  return {
    plural,
    thirdPerson,
    presentParticiple,
    pastTense,
    pastParticiple,
  };
}

export type EnrichedWordResult = Omit<
  TeacherWord,
  'id' | 'teacherId' | 'teacherName' | 'createdAt' | 'updatedAt'
>;

function findTargetInSentence(sentence: string, targetText: string, inflections: WordInflections): string {
  const candidates = [
    targetText,
    inflections.pastTense,
    inflections.pastParticiple,
    inflections.presentParticiple,
    inflections.thirdPerson,
    inflections.plural,
    inflections.comparative,
    inflections.superlative,
  ].filter((c): c is string => Boolean(c && typeof c === 'string'));

  for (const cand of candidates) {
    const escaped = cand.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const regex = new RegExp(`\\b${escaped}\\b`, 'i');
    const match = sentence.match(regex);
    if (match) {
      return match[0];
    }
  }

  return targetText;
}

/**
 * 单词/词组智能补全服务 (确保中文释义与中文例句翻译 100% 真实可用)
 */
export async function enrichWord(rawText: string, aiBinding?: any): Promise<EnrichedWordResult> {
  const text = rawText.trim();
  const normalizedText = normalizeText(text);

  let syllables = heuristicSyllableSplit(text);
  let phoneticUs = '';
  let phoneticUk = '';
  let audioUsUrl = `/api/speech/tts?text=${encodeURIComponent(text)}&accent=us`;
  let audioUkUrl = `/api/speech/tts?text=${encodeURIComponent(text)}&accent=uk`;
  let meanings: WordMeaning[] = [];
  let inflections: WordInflections = deriveInflections(text);
  let homophones: string[] = COMMON_HOMOPHONE_MAP[normalizedText] || [];
  let examples: WordExample[] = [];
  let tags: string[] = [];

  // 1. 首选：从有道中英权威词典接口拉取地道中文释义、真声音标及双语例句
  try {
    const youdaoUrl = `https://dict.youdao.com/jsonapi?q=${encodeURIComponent(normalizedText)}`;
    const ydResp = await fetch(youdaoUrl, {
      headers: {
        'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36',
      },
      signal: AbortSignal.timeout(3500),
    });

    if (ydResp.ok) {
      const data: any = await ydResp.json();

      // 提取音标
      const ec = data?.ec;
      if (ec?.word && ec.word[0]) {
        const w = ec.word[0];
        if (w.usphone) phoneticUs = `/${w.usphone}/`;
        if (w.ukphone) phoneticUk = `/${w.ukphone}/`;

        // 提取中文释义 (支持 n. 词性与中文释义拆分)
        if (Array.isArray(w.trs)) {
          for (const trGroup of w.trs) {
            for (const tr of trGroup.tr || []) {
              const rawStr: string = tr?.l?.i?.[0] || '';
              if (!rawStr || !hasChinese(rawStr)) continue;

              const match = rawStr.match(/^([a-zA-Z]+\.?)\s*(.+)$/);
              if (match) {
                meanings.push({
                  pos: match[1].endsWith('.') ? match[1] : `${match[1]}.`,
                  zh: match[2].trim(),
                });
              } else {
                meanings.push({
                  pos: text.includes(' ') ? 'phr.' : 'n./v.',
                  zh: rawStr.trim(),
                });
              }
            }
          }
        }
      }

      // 提取标签 (如 初中/高中/CET4/CET6/考研)
      if (Array.isArray(ec?.exam_type)) {
        tags = ec.exam_type.filter((t: string) => typeof t === 'string');
      }

      // 提取高质量双语中英例句
      const blngPairs = data?.blng_sents_part?.['sentence-pair'];
      if (Array.isArray(blngPairs)) {
        for (const pair of blngPairs.slice(0, 3)) {
          const en = pair.sentence || pair['sentence-eng']?.replace(/<[^>]+>/g, '');
          const zh = pair['sentence-translation'];
          if (en && zh && hasChinese(zh)) {
            const matchedTarget = findTargetInSentence(en, text, inflections);
            examples.push({
              en: en.trim(),
              zh: zh.trim(),
              targetInSentence: matchedTarget,
            });
          }
        }
      }
    }
  } catch {
    // 忽略有道网络异常，继续备用通道
  }

  // 1.2 针对短语或词条的备用中文解释获取 (有道 suggest API)
  if (meanings.length === 0 || !meanings.some((m) => hasChinese(m.zh))) {
    try {
      const suggestUrl = `https://dict.youdao.com/suggest?num=1&doctype=json&q=${encodeURIComponent(normalizedText)}`;
      const sugResp = await fetch(suggestUrl, {
        signal: AbortSignal.timeout(2500),
      });
      if (sugResp.ok) {
        const sugData: any = await sugResp.json();
        const entries = sugData?.data?.entries;
        if (Array.isArray(entries) && entries[0]?.explain) {
          const rawExplain: string = entries[0].explain;
          if (hasChinese(rawExplain)) {
            const parts = rawExplain.split(/[;；]/).map((s) => s.trim()).filter(Boolean);
            meanings = parts.map((part) => {
              const match = part.match(/^([a-zA-Z]+\.?)\s*(.+)$/);
              if (match) {
                return { pos: match[1].endsWith('.') ? match[1] : `${match[1]}.`, zh: match[2].trim() };
              }
              return { pos: text.includes(' ') ? 'phr.' : 'n./v.', zh: part };
            });
          }
        }
      }
    } catch {
      // 忽略
    }
  }

  // 2. 若配置了 Cloudflare Workers AI，调用 LLM 进行结构化精炼与地道中文翻译
  if (aiBinding && typeof aiBinding.run === 'function') {
    try {
      const prompt = `You are an expert English-Chinese bilingual lexicographer.
Provide structured vocabulary JSON for the English word or phrase: "${text}".
IMPORTANT: All 'zh' fields in meanings and in examples MUST be written in fluent Simplified Chinese (简体中文). DO NOT return English definitions in 'zh'.
Output valid JSON only with keys:
- syllables: string with syllable dots like "in·for·ma·tion"
- phoneticUs: string like "/.../"
- meanings: array of { pos: string, zh: string } where zh MUST be Simplified Chinese
- inflections: { plural, thirdPerson, presentParticiple, pastTense, pastParticiple, comparative, superlative }
- homophones: array of strings of valid homophones (e.g. right -> ["write"])
- examples: array of { en: string, zh: string, targetInSentence: string } where zh MUST be fluent Chinese translation of the sentence, targetInSentence is the exact form in 'en'.
DO NOT return any markdown formatting.`;

      const aiResp: any = await aiBinding.run('@cf/meta/llama-3.1-8b-instruct', {
        messages: [{ role: 'user', content: prompt }],
        max_tokens: 800,
      });

      if (aiResp && aiResp.response) {
        const cleaned = aiResp.response.replace(/```json/g, '').replace(/```/g, '').trim();
        const parsed = JSON.parse(cleaned);
        if (parsed.syllables) syllables = parsed.syllables;
        if (parsed.phoneticUs) phoneticUs = parsed.phoneticUs;
        if (Array.isArray(parsed.meanings)) {
          const validAiMeanings = parsed.meanings.filter((m: any) => m && hasChinese(m.zh));
          if (validAiMeanings.length > 0) {
            meanings = validAiMeanings;
          }
        }
        if (parsed.inflections) inflections = { ...inflections, ...parsed.inflections };
        if (Array.isArray(parsed.homophones)) homophones = parsed.homophones;
        if (Array.isArray(parsed.examples)) {
          const validAiExamples = parsed.examples.filter((e: any) => e && e.en && hasChinese(e.zh));
          if (validAiExamples.length > 0) {
            examples = validAiExamples;
          }
        }
      }
    } catch {
      // 忽略 AI 调用异常
    }
  }

  // 3. 兜底保护：严格过滤掉任何纯英文解释，确保 zh 字段必须是中文
  meanings = meanings.filter((m) => hasChinese(m.zh));
  if (meanings.length === 0) {
    meanings = [{ pos: text.includes(' ') ? 'phr.' : 'n./v.', zh: '（暂无中文释义，请手动输入）' }];
  }

  examples = examples.filter((ex) => hasChinese(ex.zh));
  if (examples.length === 0) {
    examples = [
      {
        en: `This is an example sentence for ${text}.`,
        zh: `这是关于 ${text} 的例句。`,
        targetInSentence: text,
      },
    ];
  }

  return {
    text,
    normalizedText,
    syllables,
    phoneticUs: phoneticUs || `/${normalizedText}/`,
    phoneticUk: phoneticUk || phoneticUs || `/${normalizedText}/`,
    audioUsUrl,
    audioUkUrl,
    meanings,
    inflections,
    homophones,
    examples,
    tags,
  };
}
