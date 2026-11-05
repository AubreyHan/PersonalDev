import React, { useEffect } from 'react';
import { useStudyStore } from '../../stores/useStudyStore';
import { CognitiveCard } from '../../components/study/CognitiveCard';
import { StepDictation } from '../../components/study/StepDictation';
import { StepMeaningQuiz } from '../../components/study/StepMeaningQuiz';
import { StepPronunciation } from '../../components/study/StepPronunciation';
import { StepClozeSentence } from '../../components/study/StepClozeSentence';
import {
  Sparkles,
  CheckCircle2,
  BookOpen,
  ArrowRight,
  Headphones,
  HelpCircle,
  Mic,
  PenTool,
  RotateCcw,
} from 'lucide-react';
import { Link } from 'react-router-dom';

export const StudyPage: React.FC = () => {
  const {
    words,
    currentIndex,
    currentSubStep,
    checkpoint,
    isLoading,
    isCompletedAll,
    loadWordsAndProgress,
    moveToSubStep,
    updateCheckpoint,
    completeCurrentWord,
  } = useStudyStore();

  useEffect(() => {
    loadWordsAndProgress();
  }, []);

  if (isLoading) {
    return (
      <div className="flex flex-col items-center justify-center py-24 text-slate-400">
        <div className="w-10 h-10 border-4 border-blue-600 border-t-transparent rounded-full animate-spin mb-4" />
        <p className="text-sm font-medium">正在拉取词库与本地学习断点...</p>
      </div>
    );
  }

  if (isCompletedAll || words.length === 0) {
    return (
      <div className="max-w-md mx-auto my-12 bg-white rounded-3xl p-8 border border-slate-200/80 shadow-xl text-center animate-in zoom-in-95">
        <div className="w-16 h-16 rounded-3xl bg-emerald-50 text-emerald-600 flex items-center justify-center mx-auto mb-4 border border-emerald-100">
          <CheckCircle2 className="w-9 h-9" />
        </div>
        <h2 className="text-2xl font-bold text-slate-900">恭喜！全部生词已学完</h2>
        <p className="text-sm text-slate-500 mt-2 leading-relaxed">
          您关联的词库中已无新词。所有已学单词已全部推入艾宾浩斯记忆复习池，系统将在到期时自动安排盲测复习。
        </p>

        <div className="mt-8 space-y-3">
          <Link
            to="/student/review"
            className="w-full py-3.5 px-4 rounded-xl font-bold text-sm text-white bg-blue-600 hover:bg-blue-700 flex items-center justify-center gap-2 shadow-md shadow-blue-500/20"
          >
            <RotateCcw className="w-4 h-4" />
            前往今日艾宾浩斯复习
          </Link>
        </div>
      </div>
    );
  }

  const currentWord = words[currentIndex];
  if (!currentWord) return null;

  const STEPS = [
    { key: '1', label: '认知感知', icon: BookOpen },
    { key: '2.1', label: '听写3遍', icon: Headphones },
    { key: '2.2', label: '中文翻译', icon: HelpCircle },
    { key: '2.3', label: '跟读3次', icon: Mic },
    { key: '2.4', label: '例句填空', icon: PenTool },
  ];

  return (
    <div className="space-y-6">
      {/* 顶部总体进度与步骤指示条 */}
      <div className="max-w-2xl mx-auto">
        <div className="flex items-center justify-between text-xs text-slate-500 mb-2 font-medium">
          <span className="flex items-center gap-1.5 font-bold text-slate-800">
            <Sparkles className="w-4 h-4 text-blue-600" />
            生词精学闯关
          </span>
          <span>
            当前生词: <strong className="text-blue-600 font-mono text-sm">{currentIndex + 1}</strong> / {words.length} 词
          </span>
        </div>

        {/* 五个子步骤条 */}
        <div className="grid grid-cols-5 gap-1.5 sm:gap-2">
          {STEPS.map((s, idx) => {
            const stepOrder = ['1', '2.1', '2.2', '2.3', '2.4'];
            const currentStepIdx = stepOrder.indexOf(currentSubStep);
            const isFinished = idx < currentStepIdx;
            const isCurrent = s.key === currentSubStep;
            const Icon = s.icon;

            return (
              <div
                key={s.key}
                className={`py-2 px-1 sm:px-2 rounded-xl border flex flex-col items-center justify-center gap-1 transition-all ${
                  isCurrent
                    ? 'bg-blue-600 text-white border-blue-600 shadow-sm'
                    : isFinished
                    ? 'bg-emerald-50 text-emerald-700 border-emerald-200'
                    : 'bg-white text-slate-400 border-slate-200'
                }`}
              >
                <Icon className="w-3.5 h-3.5" />
                <span className="text-[10px] sm:text-xs font-semibold tracking-tight">{s.label}</span>
              </div>
            );
          })}
        </div>
      </div>

      {/* 核心步骤分流渲染 */}
      {currentSubStep === '1' && (
        <CognitiveCard
          word={currentWord}
          onStartStudy={() => moveToSubStep('2.1')}
        />
      )}

      {currentSubStep === '2.1' && (
        <StepDictation
          word={currentWord}
          mode="study"
          initialRepCount={checkpoint?.dictationRepCount ?? 0}
          initialTargetIndex={checkpoint?.dictationTargetIndex ?? 0}
          onRepProgress={(targetIndex, reps) => {
            updateCheckpoint({
              dictationTargetIndex: targetIndex,
              dictationRepCount: reps,
            });
          }}
          onStepComplete={() => moveToSubStep('2.2')}
        />
      )}

      {currentSubStep === '2.2' && (
        <StepMeaningQuiz
          word={currentWord}
          allWordsPool={words}
          onStepComplete={() => moveToSubStep('2.3')}
        />
      )}

      {currentSubStep === '2.3' && (
        <StepPronunciation
          word={currentWord}
          mode="study"
          initialPassCount={checkpoint?.pronunciationPassCount ?? 0}
          onPassProgress={(passes) => {
            updateCheckpoint({ pronunciationPassCount: passes });
          }}
          onStepComplete={() => moveToSubStep('2.4')}
        />
      )}

      {currentSubStep === '2.4' && (
        <StepClozeSentence
          word={currentWord}
          initialExampleIndex={checkpoint?.clozeSentenceIndex ?? 0}
          onExampleProgress={(exIdx) => {
            updateCheckpoint({ clozeSentenceIndex: exIdx });
          }}
          onStepComplete={completeCurrentWord}
        />
      )}
    </div>
  );
};
