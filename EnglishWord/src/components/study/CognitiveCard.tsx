import React, { useEffect } from 'react';
import { Volume2, ArrowRight, Sparkles, BookOpen } from 'lucide-react';
import { playWordAudio } from '../../db/audioCache';
import type { TeacherWord } from '@shared/types';

interface CognitiveCardProps {
  word: TeacherWord;
  onStartStudy: () => void;
  autoPlayAudio?: boolean;
}

export const CognitiveCard: React.FC<CognitiveCardProps> = ({
  word,
  onStartStudy,
  autoPlayAudio = true,
}) => {
  // 进入步骤 1 自动播放真人读音
  useEffect(() => {
    if (autoPlayAudio) {
      const timer = setTimeout(() => {
        playWordAudio(word.audioUsUrl, word.text);
      }, 300);
      return () => clearTimeout(timer);
    }
  }, [word, autoPlayAudio]);

  return (
    <div className="max-w-2xl mx-auto bg-white rounded-3xl p-6 sm:p-8 border border-slate-200/80 shadow-lg shadow-slate-200/40 animate-in fade-in zoom-in-95 duration-200 flex flex-col justify-between min-h-[500px]">
      <div>
        {/* 顶部阶段提示 */}
        <div className="flex items-center justify-between pb-4 border-b border-slate-100">
          <span className="flex items-center gap-1.5 text-xs font-semibold px-2.5 py-1 rounded-full bg-blue-50 text-blue-700 border border-blue-200/60">
            <Sparkles className="w-3.5 h-3.5" />
            步骤 1：单词认知与发音感知
          </span>
          <span className="text-xs text-slate-400">请仔细观察拼写、音节、词义与变形</span>
        </div>

        {/* 单词主体展示与分音节 */}
        <div className="text-center py-6 sm:py-8">
          <div className="flex items-center justify-center gap-3">
            <h1 className="text-3xl sm:text-5xl font-extrabold text-slate-900 tracking-tight font-serif">
              {word.text}
            </h1>
            <button
              onClick={() => playWordAudio(word.audioUsUrl, word.text)}
              className="p-2 sm:p-3 rounded-2xl text-blue-600 bg-blue-50 hover:bg-blue-100 hover:scale-105 transition-all shadow-sm"
              title="点击反复播放读音"
            >
              <Volume2 className="w-5 h-5 sm:w-6 sm:h-6" />
            </button>
          </div>

          {/* 音标与分音节切分展示 */}
          <div className="flex items-center justify-center gap-2 sm:gap-3 text-sm text-slate-500 mt-2">
            {word.syllables && word.syllables !== word.text && (
              <span className="font-mono text-blue-700 bg-blue-50 px-2 py-0.5 rounded-lg border border-blue-100 text-xs sm:text-sm font-semibold">
                {word.syllables}
              </span>
            )}
            <span className="font-mono text-slate-600">{word.phoneticUs}</span>
          </div>
        </div>

        {/* 释义与词形变化 */}
        <div className="space-y-4">
          {/* 中文释义 */}
          <div className="p-4 bg-slate-50/80 rounded-2xl border border-slate-100 space-y-1.5">
            <div className="text-xs font-semibold text-slate-400 uppercase tracking-wider mb-1">
              中文释义
            </div>
            {word.meanings.map((m, idx) => (
              <div key={idx} className="flex items-baseline gap-2 text-sm text-slate-800">
                <span className="font-bold text-blue-600 italic text-xs w-8">{m.pos}</span>
                <span className="font-medium">{m.zh}</span>
              </div>
            ))}
          </div>

          {/* 词形变化 */}
          {Object.keys(word.inflections).length > 0 && (
            <div className="p-4 bg-slate-50/80 rounded-2xl border border-slate-100">
              <div className="text-xs font-semibold text-slate-400 uppercase tracking-wider mb-2">
                词形变化形态
              </div>
              <div className="flex flex-wrap gap-2">
                {Object.entries(word.inflections).map(([k, v]) =>
                  typeof v === 'string' && v.trim() ? (
                    <div
                      key={k}
                      className="px-2.5 py-1 rounded-xl bg-white border border-slate-200 text-xs font-mono text-slate-700 shadow-2xs"
                    >
                      <span className="text-[10px] text-slate-400 mr-1.5">{k}:</span>
                      <span className="font-semibold text-slate-900">{v}</span>
                    </div>
                  ) : null
                )}
              </div>
            </div>
          )}

          {/* 双语例句 */}
          {word.examples.length > 0 && (
            <div className="p-4 bg-slate-50/80 rounded-2xl border border-slate-100">
              <div className="text-xs font-semibold text-slate-400 uppercase tracking-wider mb-2 flex items-center gap-1.5">
                <BookOpen className="w-3.5 h-3.5 text-blue-500" />
                语境双语例句
              </div>
              <div className="space-y-2.5">
                {word.examples.map((ex, idx) => (
                  <div key={idx} className="text-xs sm:text-sm">
                    <p className="font-medium text-slate-800">
                      {ex.en.split(new RegExp(`(${ex.targetInSentence})`, 'gi')).map((part, pIdx) =>
                        part.toLowerCase() === ex.targetInSentence.toLowerCase() ? (
                          <span
                            key={pIdx}
                            className="text-blue-600 font-bold underline decoration-blue-300 underline-offset-2"
                          >
                            {part}
                          </span>
                        ) : (
                          part
                        )
                      )}
                    </p>
                    <p className="text-slate-500 text-xs mt-0.5">{ex.zh}</p>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>
      </div>

      {/* 底部进入闯关主按钮 */}
      <div className="pt-6 mt-6 border-t border-slate-100">
        <button
          onClick={onStartStudy}
          className="w-full py-4 px-6 rounded-2xl font-bold text-base text-white bg-gradient-to-r from-blue-600 to-indigo-600 hover:from-blue-700 hover:to-indigo-700 shadow-xl shadow-blue-500/25 flex items-center justify-center gap-2 hover:scale-[1.01] transition-all"
        >
          <span>开始学习 (进入四步闯关)</span>
          <ArrowRight className="w-5 h-5" />
        </button>
      </div>
    </div>
  );
};
