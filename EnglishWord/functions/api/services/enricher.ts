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

/**
 * 启发式音节切分规则（针对常见英语单词）
 */
function heuristicSyllableSplit(word: string): string {
  if (word.includes(' ') || word.includes('-') || word.length <= 4) {
    return word;
  }
  // 匹配常见元音群与辅音切分
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
function deriveInflections(word: string): WordInflections {
  const norm = normalizeText(word);
  if (norm.includes(' ')) {
    // 词组，例如 look forward to -> looked forward to, looking forward to
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

  // 单词形态推断
  let plural: string | undefined;
  let thirdPerson: string | undefined;
  let presentParticiple: string | undefined;
  let pastTense: string | undefined;
  let pastParticiple: string | undefined;

  // 基础复数与三单
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

  // 现在分词
  if (norm.endsWith('ie')) {
    presentParticiple = norm.slice(0, -2) + 'ying';
  } else if (norm.endsWith('e') && !/(ee|oe|ye)$/.test(norm)) {
    presentParticiple = norm.slice(0, -1) + 'ing';
  } else if (/[^aeiou][aeiou][^aeiouwxy]$/.test(norm) && norm.length <= 4) {
    presentParticiple = norm + norm.slice(-1) + 'ing';
  } else {
    presentParticiple = norm + 'ing';
  }

  // 过去式与过去分词
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

/**
 * 单词/词组智能补全服务
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

  // 1. 尝试从权威词典 API 拉取真实音标与例句
  try {
    const dictResp = await fetch(`https://api.dictionaryapi.dev/api/v2/entries/en/${encodeURIComponent(normalizedText)}`, {
      signal: AbortSignal.timeout(2500),
    });

    if (dictResp.ok) {
      const data: any = await dictResp.json();
      if (Array.isArray(data) && data[0]) {
        const item = data[0];
        if (item.phonetics && Array.isArray(item.phonetics)) {
          for (const p of item.phonetics) {
            if (p.text && !phoneticUs) phoneticUs = p.text;
            if (p.audio && !audioUsUrl.startsWith('http')) {
              audioUsUrl = p.audio;
            }
          }
        }
        if (!phoneticUs && item.phonetic) {
          phoneticUs = item.phonetic;
        }

        if (item.meanings && Array.isArray(item.meanings)) {
          for (const m of item.meanings) {
            const pos = m.partOfSpeech ? `${m.partOfSpeech}.` : '';
            if (m.definitions && m.definitions[0]) {
              const def = m.definitions[0];
              meanings.push({
                pos: pos || 'n.',
                zh: def.definition || '',
              });

              if (def.example && examples.length < 2) {
                examples.push({
                  en: def.example,
                  zh: '请阅读理解该例句',
                  targetInSentence: text,
                });
              }
            }
          }
        }
      }
    }
  } catch {
    // 忽略词典网络超时，使用 fallback
  }

  // 2. 若配置了 Cloudflare Workers AI，调用 LLM 进行结构化精炼与中文翻译
  if (aiBinding && typeof aiBinding.run === 'function') {
    try {
      const prompt = `You are an expert English lexicographer. Provide structured vocabulary JSON for the word or phrase: "${text}".
Output valid JSON only with keys:
- syllables: string with syllable dots like "in·for·ma·tion"
- phoneticUs: string like "/.../"
- meanings: array of { pos: string, zh: string } with accurate simplified Chinese translation
- inflections: { plural, thirdPerson, presentParticiple, pastTense, pastParticiple, comparative, superlative }
- homophones: array of strings of valid homophones (e.g. right -> ["write"])
- examples: array of { en: string, zh: string, targetInSentence: string } (targetInSentence is the exact word form used in the sentence).
DO NOT return any markdown wrapping.`;

      const aiResp: any = await aiBinding.run('@cf/meta/llama-3.1-8b-instruct', {
        messages: [{ role: 'user', content: prompt }],
        max_tokens: 800,
      });

      if (aiResp && aiResp.response) {
        const cleaned = aiResp.response.replace(/```json/g, '').replace(/```/g, '').trim();
        const parsed = JSON.parse(cleaned);
        if (parsed.syllables) syllables = parsed.syllables;
        if (parsed.phoneticUs) phoneticUs = parsed.phoneticUs;
        if (Array.isArray(parsed.meanings) && parsed.meanings.length > 0) meanings = parsed.meanings;
        if (parsed.inflections) inflections = { ...inflections, ...parsed.inflections };
        if (Array.isArray(parsed.homophones)) homophones = parsed.homophones;
        if (Array.isArray(parsed.examples) && parsed.examples.length > 0) examples = parsed.examples;
      }
    } catch {
      // 忽略 AI 调用异常，保证兜底数据可用
    }
  }

  // 3. 兜底保护：确保至少有释义与带挖空标记的例句
  if (meanings.length === 0) {
    meanings = [{ pos: text.includes(' ') ? 'phr.' : 'n./v.', zh: '（请编辑输入中文释义）' }];
  }

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
