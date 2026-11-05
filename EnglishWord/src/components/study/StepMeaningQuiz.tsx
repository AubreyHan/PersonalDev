import React, { useState, useEffect } from 'react';
import { Volume2, HelpCircle, CheckCircle2, AlertCircle } from 'lucide-react';
import { playWordAudio } from '../../db/audioCache';
import type { TeacherWord } from '@shared/types';

interface StepMeaningQuizProps {
  word: TeacherWord;
  allWordsPool?: TeacherWord[];
  onStepComplete: () => void;
  onPenaltyTriggered?: () => void;
}

const BUILTIN_DISTRACTORS = [
  '决定；决心；下定决心',
  '发展；开发；研制',
  '特别；尤其；格外',
  '环境；周围状况；自然环境',
  '产生；发生；造成',
  '包括；包含；涉及',
  '提高；增加；增强',
  '管理；经营；处理',
  '提供；供给；准备',
  '要求；需要；命令',
  '描述；形容；描写',
  '发现；发觉；意会',
  '建议；提议；推荐',
  '表达；表示；表情',
  '相信；信任；信仰',
];

export const StepMeaningQuiz: React.FC<StepMeaningQuizProps> = ({
  word,
  allWordsPool = [],
  onStepComplete,
  onPenaltyTriggered,
}) => {
  const [options, setOptions] = useState<string[]>([]);
  const [correctAnswer, setCorrectAnswer] = useState('');
  const [selectedAnswer, setSelectedAnswer] = useState<string | null>(null);
  const [isWrong, setIsWrong] = useState(false);

  // 生成 1 个正确选项 + 3 个干扰项
  const generateQuiz = () => {
    const correctZh = word.meanings.map((m) => `${m.pos} ${m.zh}`).join('； ') || '（暂无详细释义）';
    setCorrectAnswer(correctZh);

    // 收集干扰项
    const distractors: string[] = [];
    // 优先从词库中抽取其它单词的中文释义
    const otherWords = allWordsPool.filter((w) => w.normalizedText !== word.normalizedText);
    for (const w of otherWords) {
      const zh = w.meanings.map((m) => `${m.pos} ${m.zh}`).join('； ');
      if (zh && !distractors.includes(zh) && zh !== correctZh) {
        distractors.push(zh);
      }
      if (distractors.length >= 3) break;
    }

    // 若词库数量不足，从内置干扰池补齐
    let bIdx = 0;
    while (distractors.length < 3 && bIdx < BUILTIN_DISTRACTORS.length) {
      const cand = BUILTIN_DISTRACTORS[bIdx++];
      if (!distractors.includes(cand) && cand !== correctZh) {
        distractors.push(cand);
      }
    }

    const mixed = [correctZh, ...distractors.slice(0, 3)];
    // Fisher-Yates 随机打乱
    for (let i = mixed.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [mixed[i], mixed[j]] = [mixed[j], mixed[i]];
    }

    setOptions(mixed);
    setSelectedAnswer(null);
    setIsWrong(false);
  };

  useEffect(() => {
    generateQuiz();
    playWordAudio(word.audioUsUrl, word.text);
  }, [word]);

  const handleSelectOption = (opt: string) => {
    setSelectedAnswer(opt);

    if (opt === correctAnswer) {
      // 选对
      setIsWrong(false);
      setTimeout(onStepComplete, 350);
    } else {
      // 选错
      setIsWrong(true);
      if (onPenaltyTriggered) onPenaltyTriggered();
      setTimeout(() => {
        // 重新洗牌重选
        generateQuiz();
      }, 700);
    }
  };

  return (
    <div className="max-w-xl mx-auto bg-white rounded-3xl p-6 sm:p-8 border border-slate-200/80 shadow-lg shadow-slate-200/40 animate-in fade-in zoom-in-95 duration-200 text-center">
      {/* 顶部阶段提示 */}
      <div className="flex items-center justify-between pb-4 border-b border-slate-100">
        <span className="flex items-center gap-1.5 text-xs font-semibold px-2.5 py-1 rounded-full bg-blue-50 text-blue-700 border border-blue-200/60">
          <HelpCircle className="w-3.5 h-3.5" />
          步骤 2.2：单词翻译四选一
        </span>
        <span className="text-xs text-slate-400">选择正确的中文含义</span>
      </div>

      {/* 考题展示 */}
      <div className="py-6">
        <div className="flex items-center justify-center gap-3">
          <h2 className="text-3xl font-extrabold text-slate-900 tracking-tight">{word.text}</h2>
          <button
            onClick={() => playWordAudio(word.audioUsUrl, word.text)}
            className="p-1.5 rounded-xl text-blue-600 bg-blue-50 hover:bg-blue-100"
          >
            <Volume2 className="w-5 h-5" />
          </button>
        </div>
        <p className="text-xs text-slate-400 font-mono mt-1">{word.phoneticUs}</p>
      </div>

      {/* 选项按钮 */}
      <div className="space-y-3">
        {options.map((opt, idx) => {
          const isSelected = selectedAnswer === opt;
          const isCorrect = opt === correctAnswer;

          let btnStyle = 'bg-slate-50 border-slate-200 text-slate-700 hover:border-blue-400 hover:bg-blue-50/50';
          if (isSelected) {
            if (isCorrect) {
              btnStyle = 'bg-emerald-50 border-emerald-500 text-emerald-800 ring-2 ring-emerald-500/20';
            } else {
              btnStyle = 'bg-red-50 border-red-500 text-red-800 animate-shake';
            }
          }

          return (
            <button
              key={idx}
              onClick={() => handleSelectOption(opt)}
              disabled={selectedAnswer !== null}
              className={`w-full p-4 rounded-2xl border text-left text-sm font-medium transition-all flex items-center justify-between shadow-2xs ${btnStyle}`}
            >
              <span>{opt}</span>
              {isSelected && isCorrect && <CheckCircle2 className="w-5 h-5 text-emerald-600" />}
              {isSelected && !isCorrect && <AlertCircle className="w-5 h-5 text-red-600" />}
            </button>
          );
        })}
      </div>
    </div>
  );
};
