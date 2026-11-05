import React, { useEffect } from 'react';
import { useReviewStore } from '../../stores/useReviewStore';
import { StepDictation } from '../../components/study/StepDictation';
import { StepMeaningQuiz } from '../../components/study/StepMeaningQuiz';
import { StepPronunciation } from '../../components/study/StepPronunciation';
import { StepClozeSentence } from '../../components/study/StepClozeSentence';
import {
  RotateCcw,
  CheckCircle2,
  AlertTriangle,
  Sparkles,
  Award,
  Calendar,
  Layers,
  X,
  Headphones,
  HelpCircle,
  Mic,
  PenTool,
} from 'lucide-react';

export const ReviewPage: React.FC = () => {
  const {
    reviewQueue,
    currentIndex,
    currentSubStep,
    isReviewing,
    isSpotCheck,
    todayDueCount,
    todayCompletedCount,
    masteredCount,
    stageDistribution,
    isLoading,
    penaltyActive,
    loadReviewDashboard,
    startTodayReview,
    startMasteredSpotCheck,
    moveToSubStep,
    triggerPenalty,
    completeCurrentWord,
    exitReview,
  } = useReviewStore();

  useEffect(() => {
    loadReviewDashboard();
  }, []);

  if (isLoading) {
    return (
      <div className="flex flex-col items-center justify-center py-24 text-slate-400">
        <div className="w-10 h-10 border-4 border-blue-600 border-t-transparent rounded-full animate-spin mb-4" />
        <p className="text-sm font-medium">正在计算今日艾宾浩斯复习调度...</p>
      </div>
    );
  }

  // 1. 复习主面板 (未进入考核时)
  if (!isReviewing) {
    const STAGE_INTERVAL_LABELS: Record<number, string> = {
      1: '+1 天 (首次复习)',
      2: '+2 天',
      3: '+4 天',
      4: '+7 天 (周周期)',
      5: '+15 天 (半月)',
      6: '+30 天 (毕业考)',
    };

    return (
      <div className="max-w-4xl mx-auto space-y-6">
        {/* 顶部标题栏 */}
        <div>
          <h1 className="text-2xl font-bold text-slate-900 tracking-tight flex items-center gap-2">
            <RotateCcw className="w-7 h-7 text-blue-600" />
            艾宾浩斯记忆遗忘曲线复习中枢
          </h1>
          <p className="text-sm text-slate-500 mt-1">
            严密盲测防剧透：系统在开考前**绝不提前暴露今日词单**；若当天未复习自动顺延一天且周期顺推，绝不挤压撞车。
          </p>
        </div>

        {/* 核心指标看板 */}
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-4">
          <div className="p-6 rounded-3xl bg-white border border-slate-200/80 shadow-sm flex items-center gap-4">
            <div className="w-12 h-12 rounded-2xl bg-amber-50 text-amber-600 flex items-center justify-center border border-amber-100 flex-shrink-0">
              <Calendar className="w-6 h-6" />
            </div>
            <div>
              <div className="text-xs text-slate-400 font-semibold uppercase">今日待复习</div>
              <div className="text-2xl sm:text-3xl font-extrabold text-slate-900 tracking-tight">
                {todayDueCount} <span className="text-sm font-normal text-slate-400">词</span>
              </div>
            </div>
          </div>

          <div className="p-6 rounded-3xl bg-white border border-slate-200/80 shadow-sm flex items-center gap-4">
            <div className="w-12 h-12 rounded-2xl bg-blue-50 text-blue-600 flex items-center justify-center border border-blue-100 flex-shrink-0">
              <CheckCircle2 className="w-6 h-6" />
            </div>
            <div>
              <div className="text-xs text-slate-400 font-semibold uppercase">今日已完成</div>
              <div className="text-2xl sm:text-3xl font-extrabold text-slate-900 tracking-tight">
                {todayCompletedCount} <span className="text-sm font-normal text-slate-400">词</span>
              </div>
            </div>
          </div>

          <div className="p-6 rounded-3xl bg-white border border-slate-200/80 shadow-sm flex items-center gap-4">
            <div className="w-12 h-12 rounded-2xl bg-emerald-50 text-emerald-600 flex items-center justify-center border border-emerald-100 flex-shrink-0">
              <Award className="w-6 h-6" />
            </div>
            <div>
              <div className="text-xs text-slate-400 font-semibold uppercase">已彻底掌握 (毕业)</div>
              <div className="text-2xl sm:text-3xl font-extrabold text-slate-900 tracking-tight">
                {masteredCount} <span className="text-sm font-normal text-slate-400">词</span>
              </div>
            </div>
          </div>
        </div>

        {/* 艾宾浩斯 6 阶段词汇分布网格 */}
        <div className="bg-white rounded-3xl p-6 border border-slate-200/80 shadow-sm">
          <div className="flex items-center gap-2 mb-4">
            <Layers className="w-5 h-5 text-blue-600" />
            <h2 className="font-bold text-slate-900 text-base">艾宾浩斯 6 阶段记忆池分布</h2>
          </div>

          <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-6 gap-3">
            {[1, 2, 3, 4, 5, 6].map((stage) => (
              <div
                key={stage}
                className="p-3.5 rounded-2xl bg-slate-50 border border-slate-200/60 text-center flex flex-col justify-between"
              >
                <div>
                  <span className="text-xs font-bold text-blue-700 bg-blue-100/70 px-2 py-0.5 rounded-full font-mono">
                    Stage {stage}
                  </span>
                  <div className="text-[11px] text-slate-400 mt-1">{STAGE_INTERVAL_LABELS[stage]}</div>
                </div>
                <div className="text-xl font-extrabold text-slate-800 mt-2 font-mono">
                  {stageDistribution[stage] || 0} 词
                </div>
              </div>
            ))}
          </div>
        </div>

        {/* 开始复习操作区 */}
        <div className="bg-white rounded-3xl p-8 border border-slate-200/80 shadow-sm text-center">
          {todayDueCount > 0 ? (
            <div className="max-w-md mx-auto space-y-4">
              <h3 className="text-xl font-bold text-slate-900">
                今日共有 {todayDueCount} 个单词到期待复习
              </h3>
              <p className="text-xs text-slate-500 leading-relaxed">
                点击开始后，系统将按随机顺序（Shuffle）逐词考核听写、翻译、跟读与例句；1 遍通关即晋级下一阶段，出错将当场 3 遍强化并追加至今日队尾回炉！
              </p>
              <button
                onClick={startTodayReview}
                className="w-full py-4 px-6 rounded-2xl font-bold text-base text-white bg-blue-600 hover:bg-blue-700 shadow-xl shadow-blue-500/25 flex items-center justify-center gap-2 transition-all hover:scale-[1.01]"
              >
                <RotateCcw className="w-5 h-5" />
                开始今日随机盲测复习
              </button>
            </div>
          ) : (
            <div className="max-w-md mx-auto space-y-3">
              <div className="w-12 h-12 rounded-2xl bg-emerald-50 text-emerald-600 flex items-center justify-center mx-auto mb-2 border border-emerald-100">
                <CheckCircle2 className="w-6 h-6" />
              </div>
              <h3 className="text-lg font-bold text-slate-900">今日复习任务已全部达标！</h3>
              <p className="text-xs text-slate-500">
                记忆曲线保持完美，明天将继续准时安排新的复习任务。
              </p>
            </div>
          )}

          {/* 已掌握词库抽查 */}
          {masteredCount > 0 && (
            <div className="mt-8 pt-6 border-t border-slate-100 max-w-md mx-auto">
              <div className="text-xs text-slate-400 mb-2">长期记忆防遗忘抽检</div>
              <button
                onClick={startMasteredSpotCheck}
                className="w-full py-2.5 px-4 rounded-xl text-xs font-semibold text-slate-700 bg-slate-100 hover:bg-slate-200 border border-slate-200/80 flex items-center justify-center gap-2 transition-colors"
              >
                <Award className="w-4 h-4 text-amber-500" />
                随机抽查 10 个已彻底掌握词汇 (错题复活)
              </button>
            </div>
          )}
        </div>
      </div>
    );
  }

  // 2. 复习考核中 (逐词盲测)
  const currentItem = reviewQueue[currentIndex];
  if (!currentItem) return null;

  const STEPS = [
    { key: '2.1', label: '盲打听写', icon: Headphones },
    { key: '2.2', label: '中文释义', icon: HelpCircle },
    { key: '2.3', label: '跟读评测', icon: Mic },
    { key: '2.4', label: '例句填空', icon: PenTool },
  ];

  return (
    <div className="space-y-6">
      {/* 顶部复习导航栏 */}
      <div className="max-w-xl mx-auto flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span className="text-xs font-bold text-slate-800 flex items-center gap-1.5">
            <RotateCcw className="w-4 h-4 text-blue-600" />
            {isSpotCheck ? '已掌握词库随机抽查' : '今日艾宾浩斯盲测'}
          </span>
          <span className="text-xs font-mono font-bold text-blue-600 bg-blue-50 px-2 py-0.5 rounded-full border border-blue-100">
            {currentIndex + 1} / {reviewQueue.length} 词
          </span>
        </div>

        <button
          onClick={exitReview}
          className="p-1 rounded-xl text-slate-400 hover:text-slate-600 hover:bg-slate-100"
          title="退出复习"
        >
          <X className="w-5 h-5" />
        </button>
      </div>

      {/* 强化惩罚与回炉提醒气泡 */}
      {penaltyActive && (
        <div className="max-w-xl mx-auto p-3.5 rounded-2xl bg-amber-50 border border-amber-200 text-amber-800 text-xs font-medium flex items-center gap-2 animate-in fade-in">
          <AlertTriangle className="w-4 h-4 flex-shrink-0 text-amber-600" />
          <span>
            本题出错过，已触发 3 遍强化惩罚并重置回第 1 阶段（次日必考）；且已加入今日队尾回炉池，今天结束前需再考一次！
          </span>
        </div>
      )}

      {/* 四步步骤条 */}
      <div className="max-w-xl mx-auto grid grid-cols-4 gap-1.5">
        {STEPS.map((s, idx) => {
          const stepOrder = ['2.1', '2.2', '2.3', '2.4'];
          const currentStepIdx = stepOrder.indexOf(currentSubStep);
          const isFinished = idx < currentStepIdx;
          const isCurrent = s.key === currentSubStep;
          const Icon = s.icon;

          return (
            <div
              key={s.key}
              className={`py-1.5 px-2 rounded-xl border flex flex-col items-center justify-center gap-1 transition-all ${
                isCurrent
                  ? 'bg-blue-600 text-white border-blue-600 shadow-sm'
                  : isFinished
                  ? 'bg-emerald-50 text-emerald-700 border-emerald-200'
                  : 'bg-white text-slate-400 border-slate-200'
              }`}
            >
              <Icon className="w-3.5 h-3.5" />
              <span className="text-[10px] font-semibold">{s.label}</span>
            </div>
          );
        })}
      </div>

      {/* 四步考核分流渲染 (无认知展示页，直接开考) */}
      {currentSubStep === '2.1' && (
        <StepDictation
          word={currentItem.word}
          mode="review"
          onPenaltyTriggered={() => triggerPenalty('2.1')}
          onStepComplete={() => moveToSubStep('2.2')}
        />
      )}

      {currentSubStep === '2.2' && (
        <StepMeaningQuiz
          word={currentItem.word}
          allWordsPool={reviewQueue.map((item) => item.word)}
          onPenaltyTriggered={() => triggerPenalty('2.2')}
          onStepComplete={() => moveToSubStep('2.3')}
        />
      )}

      {currentSubStep === '2.3' && (
        <StepPronunciation
          word={currentItem.word}
          mode="review"
          onPenaltyTriggered={() => triggerPenalty('2.3')}
          onStepComplete={() => moveToSubStep('2.4')}
        />
      )}

      {currentSubStep === '2.4' && (
        <StepClozeSentence
          word={currentItem.word}
          onPenaltyTriggered={() => triggerPenalty('2.4')}
          onStepComplete={completeCurrentWord}
        />
      )}
    </div>
  );
};
