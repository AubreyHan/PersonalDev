import React, { useState } from 'react';
import { LetterSlotInput } from './LetterSlotInput';
import { PenTool, CheckCircle2, ArrowRight } from 'lucide-react';
import type { TeacherWord, WordExample } from '@shared/types';

interface StepClozeSentenceProps {
  word: TeacherWord;
  initialExampleIndex?: number;
  onExampleProgress?: (index: number) => void;
  onStepComplete: () => void;
  onPenaltyTriggered?: () => void;
}

export const StepClozeSentence: React.FC<StepClozeSentenceProps> = ({
  word,
  initialExampleIndex = 0,
  onExampleProgress,
  onStepComplete,
  onPenaltyTriggered,
}) => {
  const [currentIdx, setCurrentIdx] = useState(initialExampleIndex);

  const examples = word.examples.length > 0 ? word.examples : [
    {
      en: `This is a sentence for ${word.text}.`,
      zh: `这是关于 ${word.text} 的例句。`,
      targetInSentence: word.text,
    },
  ];

  const currentExample = examples[currentIdx] || examples[0];
  const targetWord = currentExample.targetInSentence || word.text;

  // 切分例句为前缀和后缀，中间插入挖空下划线输入格
  const parts = currentExample.en.split(new RegExp(`(${targetWord})`, 'i'));
  const prefix = parts[0] || '';
  const suffix = parts.slice(2).join('') || '';

  const handleSentenceSuccess = () => {
    if (currentIdx + 1 < examples.length) {
      setCurrentIdx(currentIdx + 1);
      if (onExampleProgress) onExampleProgress(currentIdx + 1);
    } else {
      setTimeout(onStepComplete, 300);
    }
  };

  return (
    <div className="max-w-xl mx-auto bg-white rounded-3xl p-6 sm:p-8 border border-slate-200/80 shadow-lg shadow-slate-200/40 animate-in fade-in zoom-in-95 duration-200 text-center">
      {/* 顶部阶段提示 */}
      <div className="flex items-center justify-between pb-4 border-b border-slate-100">
        <span className="flex items-center gap-1.5 text-xs font-semibold px-2.5 py-1 rounded-full bg-blue-50 text-blue-700 border border-blue-200/60">
          <PenTool className="w-3.5 h-3.5" />
          步骤 2.4：例句语境变形填空
        </span>
        <span className="text-xs text-slate-400">
          例句 {currentIdx + 1} / {examples.length}
        </span>
      </div>

      {/* 例句中文释义与挖空展示 */}
      <div className="py-6">
        <div className="text-xs font-semibold text-slate-400 uppercase tracking-wider mb-2">
          根据时态与语境拼写真确形态
        </div>

        {/* 中文翻译 */}
        <p className="text-sm font-semibold text-slate-700 mb-6 bg-slate-50 p-3 rounded-2xl border border-slate-100">
          {currentExample.zh}
        </p>

        {/* 英文例句与挖空格 */}
        <div className="text-base sm:text-lg font-medium text-slate-800 leading-relaxed mb-4">
          <span>{prefix} </span>
          <span className="inline-block text-blue-600 font-bold underline decoration-blue-400 decoration-2 underline-offset-4">
            [待填空]
          </span>
          <span> {suffix}</span>
        </div>

        {/* 挖空字母槽位输入 */}
        <LetterSlotInput
          key={`cloze-${currentIdx}`}
          targetText={targetWord}
          onComplete={handleSentenceSuccess}
          onError={onPenaltyTriggered}
          onHintUsed={onPenaltyTriggered}
          showHintButton={true}
        />
      </div>
    </div>
  );
};
