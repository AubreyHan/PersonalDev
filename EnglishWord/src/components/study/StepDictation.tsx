import React, { useState, useEffect } from 'react';
import { LetterSlotInput } from './LetterSlotInput';
import { Volume2, Sparkles, AlertCircle, Headphones } from 'lucide-react';
import { playWordAudio } from '../../db/audioCache';
import { mergeWordInflections } from '@shared/inflections';
import { isHomophoneMatch } from '@shared/phonetics';
import type { TeacherWord, MergedInflectionItem } from '@shared/types';

interface StepDictationProps {
  word: TeacherWord;
  mode: 'study' | 'review';
  initialRepCount?: number;
  initialTargetIndex?: number;
  onRepProgress?: (targetIndex: number, currentRep: number) => void;
  onStepComplete: () => void;
  onPenaltyTriggered?: () => void;
}

export const StepDictation: React.FC<StepDictationProps> = ({
  word,
  mode,
  initialRepCount = 0,
  initialTargetIndex = 0,
  onRepProgress,
  onStepComplete,
  onPenaltyTriggered,
}) => {
  // 合并后的词形变化列表
  const [mergedInflections, setMergedInflections] = useState<MergedInflectionItem[]>([]);
  // 当前考核的目标索引: 0 为原词, 1..N 为词形变化
  const [currentTargetIndex, setCurrentTargetIndex] = useState(initialTargetIndex);
  // 当前项已成功盲打遍数
  const [currentReps, setCurrentReps] = useState(initialRepCount);
  // 目标完成遍数 (学习模式强制 3 遍；复习模式默认 1 遍，出错/看提示升级为 3 遍)
  const [requiredReps, setRequiredReps] = useState(mode === 'study' ? 3 : 1);
  const [homophoneWarning, setHomophoneWarning] = useState<string | null>(null);

  // 初始化变形列表
  useEffect(() => {
    const list = mergeWordInflections(word.text, word.inflections);
    setMergedInflections(list);
    setCurrentTargetIndex(initialTargetIndex);
    setCurrentReps(initialRepCount);
    setRequiredReps(mode === 'study' ? 3 : 1);
  }, [word, mode]);

  // 获取当前正在听写的文本与说明
  const currentTarget =
    currentTargetIndex === 0
      ? { label: '英文原词', form: word.text }
      : {
          label: mergedInflections[currentTargetIndex - 1]?.labels.join(' / ') || '词形变化',
          form: mergedInflections[currentTargetIndex - 1]?.form || word.text,
        };

  // 播放当前发音
  const handlePlayAudio = () => {
    playWordAudio(word.audioUsUrl, currentTarget.form);
  };

  useEffect(() => {
    handlePlayAudio();
  }, [currentTargetIndex]);

  // 单次盲打填满成功
  const handleSingleSuccess = () => {
    const nextReps = currentReps + 1;
    setCurrentReps(nextReps);
    if (onRepProgress) {
      onRepProgress(currentTargetIndex, nextReps);
    }

    if (nextReps >= requiredReps) {
      // 当前项已打满所需遍数，检查是否有后续词形变化
      if (currentTargetIndex < mergedInflections.length) {
        // 渐进式弹出下一个变形
        setCurrentTargetIndex((prev) => prev + 1);
        setCurrentReps(0);
      } else {
        // 原词及所有变形均听写通关
        setTimeout(onStepComplete, 300);
      }
    }
  };

  // 用户点击看一眼提示
  const handleHintUsed = () => {
    // 触发惩罚：重置当前项已打遍数
    setCurrentReps(0);
    if (mode === 'review') {
      setRequiredReps(3); // 复习模式出错升级为 3 遍强化
      if (onPenaltyTriggered) onPenaltyTriggered();
    }
  };

  const handleError = () => {
    if (mode === 'review') {
      setRequiredReps(3);
      if (onPenaltyTriggered) onPenaltyTriggered();
    }
  };

  const posHint = word.meanings.map((m) => m.pos).join(' / ');

  return (
    <div className="max-w-xl mx-auto bg-white rounded-3xl p-6 sm:p-8 border border-slate-200/80 shadow-lg shadow-slate-200/40 animate-in fade-in zoom-in-95 duration-200 text-center">
      {/* 顶部阶段提示 */}
      <div className="flex items-center justify-between pb-4 border-b border-slate-100">
        <span className="flex items-center gap-1.5 text-xs font-semibold px-2.5 py-1 rounded-full bg-blue-50 text-blue-700 border border-blue-200/60">
          <Headphones className="w-3.5 h-3.5" />
          步骤 2.1：盲打听写 (原词与词形变化)
        </span>
        <div className="flex items-center gap-1.5">
          <span className="text-xs text-slate-500 font-medium">当前遍数:</span>
          <span className="text-xs font-mono font-bold text-blue-600 bg-blue-50 px-2 py-0.5 rounded-full border border-blue-100">
            {currentReps} / {requiredReps} 遍
          </span>
        </div>
      </div>

      {/* 提示区 */}
      <div className="py-6">
        {mode === 'review' && (
          <div className="mb-4 inline-flex items-center gap-1.5 px-3 py-1 rounded-full bg-slate-100 text-slate-600 text-xs font-semibold">
            <span>词性提示：{posHint || '单词'}</span>
          </div>
        )}

        <div className="text-xs font-semibold text-slate-400 uppercase tracking-wider mb-2">
          请拼写：{currentTarget.label}
        </div>

        {/* 听音按钮 */}
        <div className="flex justify-center my-4">
          <button
            onClick={handlePlayAudio}
            className="w-16 h-16 rounded-full bg-gradient-to-tr from-blue-600 to-indigo-600 text-white flex items-center justify-center shadow-lg shadow-blue-500/30 hover:scale-105 active:scale-95 transition-all"
            title="点击重听发音"
          >
            <Volume2 className="w-8 h-8" />
          </button>
        </div>

        <p className="text-xs text-slate-400">
          全盲听写模式（隐藏英文与中文），直接敲击键盘输入正确字母
        </p>

        {/* 核心下划线字母输入槽 */}
        <LetterSlotInput
          key={`${currentTargetIndex}-${currentReps}`}
          targetText={currentTarget.form}
          onComplete={handleSingleSuccess}
          onError={handleError}
          onHintUsed={handleHintUsed}
          hintWarning={homophoneWarning}
          showHintButton={true}
        />
      </div>

      {/* 底部词形变化进度提示 */}
      {mergedInflections.length > 0 && (
        <div className="pt-4 border-t border-slate-100 flex items-center justify-center gap-2">
          <span className={`text-[11px] px-2 py-0.5 rounded-full font-medium ${
            currentTargetIndex === 0 ? 'bg-blue-600 text-white' : 'bg-slate-100 text-slate-600'
          }`}>
            原词
          </span>
          {mergedInflections.map((item, idx) => (
            <span
              key={idx}
              className={`text-[11px] px-2 py-0.5 rounded-full font-medium ${
                currentTargetIndex === idx + 1
                  ? 'bg-blue-600 text-white'
                  : currentTargetIndex > idx + 1
                  ? 'bg-emerald-100 text-emerald-700'
                  : 'bg-slate-100 text-slate-400'
              }`}
            >
              {item.labels.join('/')}
            </span>
          ))}
        </div>
      )}
    </div>
  );
};
