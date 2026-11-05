import { Hono } from 'hono';
import { evaluatePronunciationSimilarity } from '../../../shared/phonetics';
import type { Env, HonoVariables } from '../types';

export const speechRouter = new Hono<{ Bindings: Env; Variables: HonoVariables }>();

/**
 * 单词与短语真人发音代理 (带 30 天强缓存头)
 */
speechRouter.get('/tts', async (c) => {
  const text = c.req.query('text');
  const accent = c.req.query('accent') || 'us';

  if (!text) {
    return c.text('Text required', 400);
  }

  // type=1 为英音, type=2 为美音
  const voiceType = accent === 'uk' ? '1' : '2';
  const upstreamUrl = `https://dict.youdao.com/dictvoice?audio=${encodeURIComponent(text)}&type=${voiceType}`;

  try {
    const upstreamResp = await fetch(upstreamUrl, {
      headers: {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
      },
    });

    if (!upstreamResp.ok) {
      return c.text('Audio fetch failed', 502);
    }

    const audioBytes = await upstreamResp.arrayBuffer();
    return new Response(audioBytes, {
      headers: {
        'Content-Type': 'audio/mpeg',
        'Cache-Control': 'public, max-age=2592000, immutable', // 30天强缓存
        'Access-Control-Allow-Origin': '*',
      },
    });
  } catch {
    return c.text('Audio proxy error', 500);
  }
});

/**
 * 跟读发音评测接口 (Cloudflare Workers AI Whisper + 音素评分)
 */
speechRouter.post('/evaluate', async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const { targetText, recognizedTextFallback, audioBase64 } = body;

  if (!targetText) {
    return c.json({ error: 'BAD_REQUEST', message: 'targetText is required' }, 400);
  }

  let recognized = recognizedTextFallback || '';

  // 若提供了音频且绑定了 Workers AI，调用 Whisper 转写
  if (audioBase64 && c.env.AI && typeof c.env.AI.run === 'function') {
    try {
      const binaryString = atob(audioBase64);
      const bytes = new Uint8Array(binaryString.length);
      for (let i = 0; i < binaryString.length; i++) {
        bytes[i] = binaryString.charCodeAt(i);
      }

      const whisperResp: any = await c.env.AI.run('@cf/openai/whisper', {
        audio: [...bytes],
      });

      if (whisperResp && whisperResp.text) {
        recognized = whisperResp.text;
      }
    } catch {
      // 若 Whisper 报错，降级使用 recognizedTextFallback
    }
  }

  const result = evaluatePronunciationSimilarity(targetText, recognized);

  return c.json({
    recognizedText: recognized,
    score: result.score,
    isAccurate: result.isAccurate,
    feedback: result.isAccurate ? '发音非常标准！' : '读音与目标略有偏差，请重听标准音对比后再试',
  });
});
