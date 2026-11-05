const AUDIO_CACHE_NAME = 'english-word-audio-v1';

/**
 * 静默预拉取并存入浏览器 Cache Storage
 */
export async function prefetchWordAudio(urls: string[]): Promise<void> {
  if (typeof caches === 'undefined' || !navigator.onLine) return;

  try {
    const cache = await caches.open(AUDIO_CACHE_NAME);
    for (const url of urls) {
      if (!url) continue;
      const matched = await cache.match(url);
      if (!matched) {
        // 后台静默抓取并缓存
        fetch(url)
          .then((resp) => {
            if (resp.ok) cache.put(url, resp);
          })
          .catch(() => {});
      }
    }
  } catch {
    // 忽略缓存异常
  }
}

/**
 * 播放音频，支持 Cache Storage 离线命中与 SpeechSynthesis 降级
 */
export async function playWordAudio(
  audioUrl?: string,
  fallbackText?: string,
  accent: 'us' | 'uk' = 'us'
): Promise<void> {
  // 1. 若有音频 URL，尝试通过 Audio 元素播放 (CacheFirst 由 Service Worker 或 Cache API 保障)
  if (audioUrl) {
    try {
      if (typeof caches !== 'undefined') {
        const cache = await caches.open(AUDIO_CACHE_NAME);
        const cachedResp = await cache.match(audioUrl);
        if (cachedResp) {
          const blob = await cachedResp.blob();
          const blobUrl = URL.createObjectURL(blob);
          const audio = new Audio(blobUrl);
          await audio.play();
          return;
        }
      }

      // 未命中缓存时直接请求网络
      const audio = new Audio(audioUrl);
      await audio.play();
      return;
    } catch {
      // 网络不可达或音频播放失败，继续降级
    }
  }

  // 2. 离线且无预缓存时的系统 TTS 语音合成兜底
  if (fallbackText && typeof window !== 'undefined' && 'speechSynthesis' in window) {
    try {
      window.speechSynthesis.cancel();
      const utterance = new SpeechSynthesisUtterance(fallbackText);
      utterance.lang = accent === 'uk' ? 'en-GB' : 'en-US';
      utterance.rate = 0.9;
      window.speechSynthesis.speak(utterance);
    } catch {
      // 忽略语音合成不可用异常
    }
  }
}
