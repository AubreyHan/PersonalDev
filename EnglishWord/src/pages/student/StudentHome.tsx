import React, { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { localDb } from '../../db';
import { triggerSync } from '../../db/syncEngine';
import { isWordDueForReview, getTodayDateStr } from '@shared/ebbinghaus';
import {
  Sparkles,
  RotateCcw,
  AlertTriangle,
  BookOpen,
  Award,
  Layers,
  ArrowRight,
  Clock,
} from 'lucide-react';

export const StudentHome: React.FC = () => {
  const [stats, setStats] = useState({
    totalWords: 0,
    unlearnedCount: 0,
    reviewingCount: 0,
    masteredCount: 0,
    todayDueReviewCount: 0,
  });
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const load = async () => {
      triggerSync().catch(() => {});
      const todayStr = getTodayDateStr();
      const allWords = await localDb.words.toArray();
      const allProgress = await localDb.progress.toArray();
      const progressMap = new Map(allProgress.map((p) => [p.normalizedText, p]));

      let unlearned = 0;
      let reviewing = 0;
      let mastered = 0;
      let due = 0;

      for (const w of allWords) {
        const p = progressMap.get(w.normalizedText);
        if (!p || p.status === 'unlearned') {
          unlearned++;
        } else if (p.status === 'learning') {
          unlearned++;
        } else if (p.status === 'reviewing') {
          reviewing++;
          if (isWordDueForReview(p, todayStr)) {
            due++;
          }
        } else if (p.status === 'mastered') {
          mastered++;
        }
      }

      setStats({
        totalWords: allWords.length,
        unlearnedCount: unlearned,
        reviewingCount: reviewing,
        masteredCount: mastered,
        todayDueReviewCount: due,
      });
      setLoading(false);
    };

    load();
  }, []);

  return (
    <div className="max-w-4xl mx-auto space-y-6">
      {/* 优先复习防积压温馨提醒 (核心防雪球机制) */}
      {stats.todayDueReviewCount > 0 && (
        <div className="p-4 sm:p-5 rounded-3xl bg-amber-50 border border-amber-200/80 text-amber-900 shadow-sm flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 animate-in fade-in">
          <div className="flex items-start gap-3">
            <div className="p-2 rounded-2xl bg-amber-100 text-amber-700 mt-0.5">
              <Clock className="w-5 h-5" />
            </div>
            <div>
              <h3 className="font-bold text-base text-amber-900">
                优先复习提醒：今日尚有 {stats.todayDueReviewCount} 个单词待复习
              </h3>
              <p className="text-xs text-amber-700/90 mt-0.5">
                艾宾浩斯记忆最忌“只学新词不巩固”。建议先完成今日复习再学新词，保持记忆曲线最佳吸收效率！
              </p>
            </div>
          </div>

          <Link
            to="/student/review"
            className="flex-shrink-0 px-4 py-2.5 rounded-xl font-bold text-xs text-white bg-amber-600 hover:bg-amber-700 transition-colors shadow-sm flex items-center gap-1.5"
          >
            <span>立即去复习</span>
            <ArrowRight className="w-4 h-4" />
          </Link>
        </div>
      )}

      {/* 两大主模块入口卡片 */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
        {/* 卡片 1: 单词学习 */}
        <div className="bg-white rounded-3xl p-6 sm:p-8 border border-slate-200/80 shadow-sm hover:shadow-md transition-all flex flex-col justify-between">
          <div>
            <div className="w-12 h-12 rounded-2xl bg-blue-50 text-blue-600 flex items-center justify-center mb-4 border border-blue-100">
              <Sparkles className="w-6 h-6" />
            </div>
            <h2 className="text-xl font-bold text-slate-900">单词精学闯关</h2>
            <p className="text-xs text-slate-500 mt-1.5 leading-relaxed">
              听说读写四步闭环：认知预览 → 盲听原词及变形打字 3 遍 → 词义翻译四选一 → 真人发音跟读 3 星 → 例句语境变形填空。
            </p>

            <div className="mt-5 p-3.5 bg-slate-50 rounded-2xl border border-slate-100 text-xs text-slate-600 space-y-1">
              <div>
                待学新词：<strong className="text-blue-600 font-bold">{stats.unlearnedCount}</strong> 词
              </div>
              <div className="text-[11px] text-slate-400">支持断点续学，中途退出随时恢复</div>
            </div>
          </div>

          <Link
            to="/student/study"
            className="mt-6 w-full py-3.5 px-4 rounded-2xl font-bold text-sm text-white bg-blue-600 hover:bg-blue-700 flex items-center justify-center gap-2 shadow-md shadow-blue-500/20 transition-all hover:scale-[1.01]"
          >
            <span>开始学习新词</span>
            <ArrowRight className="w-4 h-4" />
          </Link>
        </div>

        {/* 卡片 2: 艾宾浩斯复习 */}
        <div
          className={`bg-white rounded-3xl p-6 sm:p-8 border shadow-sm hover:shadow-md transition-all flex flex-col justify-between ${
            stats.todayDueReviewCount > 0
              ? 'border-amber-300 ring-2 ring-amber-400/20 shadow-amber-500/10'
              : 'border-slate-200/80'
          }`}
        >
          <div>
            <div className="w-12 h-12 rounded-2xl bg-indigo-50 text-indigo-600 flex items-center justify-center mb-4 border border-indigo-100">
              <RotateCcw className="w-6 h-6" />
            </div>
            <div className="flex items-center gap-2">
              <h2 className="text-xl font-bold text-slate-900">艾宾浩斯复习</h2>
              {stats.todayDueReviewCount > 0 && (
                <span className="text-xs font-bold px-2 py-0.5 rounded-full bg-amber-100 text-amber-700">
                  今日有到期词
                </span>
              )}
            </div>
            <p className="text-xs text-slate-500 mt-1.5 leading-relaxed">
              严格 6 阶段遗忘周期（+1, 2, 4, 7, 15, 30天）；全盲测不提前暴露词单；未复习自动顺延；出错 3 遍强化并追加队尾回炉重考。
            </p>

            <div className="mt-5 p-3.5 bg-slate-50 rounded-2xl border border-slate-100 text-xs text-slate-600 space-y-1">
              <div>
                今日待复习：<strong className="text-amber-600 font-bold">{stats.todayDueReviewCount}</strong> 词
              </div>
              <div className="text-[11px] text-slate-400">已彻底掌握：{stats.masteredCount} 词</div>
            </div>
          </div>

          <Link
            to="/student/review"
            className={`mt-6 w-full py-3.5 px-4 rounded-2xl font-bold text-sm text-white flex items-center justify-center gap-2 shadow-md transition-all hover:scale-[1.01] ${
              stats.todayDueReviewCount > 0
                ? 'bg-amber-600 hover:bg-amber-700 shadow-amber-500/20'
                : 'bg-indigo-600 hover:bg-indigo-700 shadow-indigo-500/20'
            }`}
          >
            <span>进入艾宾浩斯复习</span>
            <ArrowRight className="w-4 h-4" />
          </Link>
        </div>
      </div>
    </div>
  );
};
