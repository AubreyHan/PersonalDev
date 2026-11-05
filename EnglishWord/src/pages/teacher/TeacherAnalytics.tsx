import React, { useState, useEffect } from 'react';
import { apiRequest } from '../../services/apiClient';
import {
  BarChart3,
  User,
  AlertTriangle,
  RotateCcw,
  CheckCircle2,
  Clock,
  BookOpen,
  X,
  Volume2,
} from 'lucide-react';
import { playWordAudio } from '../../db/audioCache';
import type { StudentAnalyticsSummary, StudentWeakWord, WordMeaning } from '@shared/types';

export const TeacherAnalytics: React.FC = () => {
  const [summaries, setSummaries] = useState<StudentAnalyticsSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  // 选中的学生 (查看薄弱错词榜抽屉)
  const [selectedStudent, setSelectedStudent] = useState<StudentAnalyticsSummary | null>(null);
  const [weakWords, setWeakWords] = useState<StudentWeakWord[]>([]);
  const [loadingWeakWords, setLoadingWeakWords] = useState(false);

  const fetchSummaries = async () => {
    setLoading(true);
    try {
      const data = await apiRequest('/api/teacher/analytics/students');
      setSummaries(data.summaries || []);
    } catch (err: any) {
      setError(err.message || '获取学情数据失败');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchSummaries();
  }, []);

  const openWeakWordsDrawer = async (student: StudentAnalyticsSummary) => {
    setSelectedStudent(student);
    setLoadingWeakWords(true);
    try {
      const data = await apiRequest(`/api/teacher/analytics/students/${student.studentId}/weak-words`);
      setWeakWords(data.weakWords || []);
    } catch (err: any) {
      console.error('获取错词榜失败', err);
    } finally {
      setLoadingWeakWords(false);
    }
  };

  const getStepLabel = (step: string | null) => {
    switch (step) {
      case '2.1':
        return '听写拼写';
      case '2.2':
        return '中文翻译';
      case '2.3':
        return '跟读发音';
      case '2.4':
        return '例句填空';
      default:
        return '综合考核';
    }
  };

  return (
    <div className="space-y-6">
      {/* 顶部标题栏 */}
      <div>
        <h1 className="text-2xl font-bold text-slate-900 tracking-tight flex items-center gap-2">
          <BarChart3 className="w-7 h-7 text-blue-600" />
          学生单词学情与薄弱错词看板
        </h1>
        <p className="text-sm text-slate-500 mt-1">
          实时监测名下每位学生的学习进度（未学 / 艾宾浩斯复习中 / 彻底掌握）及今日复习完成度；点击学生查看多次降级的顽固错词榜单。
        </p>
      </div>

      {error && (
        <div className="p-4 bg-red-50 border border-red-200 rounded-2xl text-red-700 text-sm">
          {error}
        </div>
      )}

      {/* 学生列表表格 */}
      <div className="bg-white rounded-3xl border border-slate-200/80 shadow-sm overflow-hidden">
        {loading ? (
          <div className="text-center py-16 text-slate-400 text-sm">加载学生学情中...</div>
        ) : summaries.length === 0 ? (
          <div className="text-center py-16 text-slate-400 text-sm">暂无名下学生数据</div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-left border-collapse">
              <thead>
                <tr className="bg-slate-50/80 border-b border-slate-200 text-[11px] font-semibold text-slate-500 uppercase tracking-wider">
                  <th className="py-3.5 px-6">学生信息</th>
                  <th className="py-3.5 px-4 text-center">本词库进度 (未学/复习/掌握)</th>
                  <th className="py-3.5 px-4 text-center">今日待复习完成情况</th>
                  <th className="py-3.5 px-4 text-center">最近学习时间</th>
                  <th className="py-3.5 px-6 text-right">操作</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100 text-sm">
                {summaries.map((s) => {
                  const masterPercent = s.totalWords > 0 ? Math.round((s.masteredCount / s.totalWords) * 100) : 0;
                  const reviewPercent = s.totalWords > 0 ? Math.round((s.reviewingCount / s.totalWords) * 100) : 0;
                  const unlearnPercent = Math.max(0, 100 - masterPercent - reviewPercent);

                  return (
                    <tr key={s.studentId} className="hover:bg-slate-50/60 transition-colors">
                      <td className="py-4 px-6">
                        <div className="font-semibold text-slate-900">{s.displayName}</div>
                        <div className="text-xs text-slate-400">@{s.username}</div>
                      </td>

                      <td className="py-4 px-4">
                        <div className="max-w-xs mx-auto">
                          {/* 进度条 */}
                          <div className="h-2 w-full bg-slate-100 rounded-full overflow-hidden flex">
                            <div style={{ width: `${masterPercent}%` }} className="bg-emerald-500" title={`已掌握: ${s.masteredCount}`} />
                            <div style={{ width: `${reviewPercent}%` }} className="bg-blue-500" title={`复习中: ${s.reviewingCount}`} />
                            <div style={{ width: `${unlearnPercent}%` }} className="bg-slate-200" title={`未学: ${s.unlearnedCount}`} />
                          </div>
                          <div className="flex justify-between text-[11px] text-slate-500 mt-1.5 font-medium">
                            <span className="text-slate-400">未学 {s.unlearnedCount}</span>
                            <span className="text-blue-600">复习中 {s.reviewingCount}</span>
                            <span className="text-emerald-600">掌握 {s.masteredCount}</span>
                          </div>
                        </div>
                      </td>

                      <td className="py-4 px-4 text-center">
                        <span className={`inline-flex items-center gap-1 px-2.5 py-1 rounded-full text-xs font-semibold ${
                          s.todayDueReviewCount === 0
                            ? 'bg-emerald-50 text-emerald-700 border border-emerald-200'
                            : 'bg-amber-50 text-amber-700 border border-amber-200'
                        }`}>
                          {s.todayDueReviewCount === 0 ? (
                            <>
                              <CheckCircle2 className="w-3.5 h-3.5" />
                              今日复习已全部清零
                            </>
                          ) : (
                            <>
                              <Clock className="w-3.5 h-3.5" />
                              今日尚余 {s.todayDueReviewCount} 词待复习
                            </>
                          )}
                        </span>
                      </td>

                      <td className="py-4 px-4 text-center text-xs text-slate-500">
                        {s.lastActiveAt ? new Date(s.lastActiveAt).toLocaleString() : '暂无记录'}
                      </td>

                      <td className="py-4 px-6 text-right">
                        <button
                          onClick={() => openWeakWordsDrawer(s)}
                          className="px-3 py-1.5 rounded-xl text-xs font-semibold text-blue-700 bg-blue-50 hover:bg-blue-100 border border-blue-200 transition-colors inline-flex items-center gap-1"
                        >
                          <AlertTriangle className="w-3.5 h-3.5 text-amber-500" />
                          薄弱错词榜
                        </button>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        )}
      </div>

      {/* 薄弱错词详情抽屉 */}
      {selectedStudent && (
        <div className="fixed inset-0 z-50 overflow-hidden bg-black/40 backdrop-blur-sm flex justify-end">
          <div className="w-full max-w-xl bg-white h-full shadow-2xl flex flex-col animate-in slide-in-from-right duration-200">
            {/* 抽屉头部 */}
            <div className="px-6 py-5 border-b border-slate-100 flex items-center justify-between bg-slate-50/70">
              <div>
                <div className="flex items-center gap-2">
                  <h2 className="text-lg font-bold text-slate-900">{selectedStudent.displayName} 的顽固错词榜单</h2>
                  <span className="text-xs px-2 py-0.5 rounded-full bg-slate-200 text-slate-700">
                    @{selectedStudent.username}
                  </span>
                </div>
                <p className="text-xs text-slate-500 mt-0.5">
                  统计复习中多次拼错、看提示或被重置回 Stage 1 的词汇，辅助针对性教学
                </p>
              </div>
              <button
                onClick={() => setSelectedStudent(null)}
                className="p-1.5 rounded-xl text-slate-400 hover:text-slate-600 hover:bg-slate-200/60"
              >
                <X className="w-5 h-5" />
              </button>
            </div>

            {/* 抽屉内容 */}
            <div className="flex-1 overflow-y-auto p-6">
              {loadingWeakWords ? (
                <div className="text-center py-16 text-slate-400 text-sm">加载错词分析中...</div>
              ) : weakWords.length === 0 ? (
                <div className="text-center py-16 text-slate-400">
                  <CheckCircle2 className="w-12 h-12 text-emerald-500 mx-auto mb-3" />
                  <p className="font-semibold text-slate-700">该学生暂无显著高频错词记录！</p>
                  <p className="text-xs text-slate-400 mt-1">目前所有复习考核掌握良好</p>
                </div>
              ) : (
                <div className="space-y-3">
                  {weakWords.map((w, idx) => (
                    <div
                      key={idx}
                      className="p-4 rounded-2xl border border-slate-200 bg-white hover:border-slate-300 shadow-sm flex items-start justify-between gap-4"
                    >
                      <div>
                        <div className="flex items-center gap-2">
                          <span className="font-bold text-base text-slate-900">{w.text}</span>
                          <button
                            onClick={() => playWordAudio(undefined, w.text)}
                            className="p-1 text-blue-600 hover:bg-blue-50 rounded"
                          >
                            <Volume2 className="w-4 h-4" />
                          </button>
                          <span className="text-xs px-2 py-0.5 rounded-full bg-slate-100 text-slate-600 font-mono">
                            Stage {w.currentStage}
                          </span>
                        </div>

                        <div className="text-xs text-slate-600 mt-1 line-clamp-1">
                          {w.meanings.map((m: WordMeaning) => `${m.pos} ${m.zh}`).join('； ')}
                        </div>

                        <div className="text-[11px] text-amber-700 bg-amber-50 px-2 py-1 rounded-lg mt-2 inline-flex items-center gap-1.5 border border-amber-200/60">
                          <span>容易卡壳环节：{getStepLabel(w.lastErrorStep)}</span>
                        </div>
                      </div>

                      <div className="text-right flex-shrink-0">
                        <div className="text-xs font-bold text-red-600">
                          累计出错 {w.errorCount} 次
                        </div>
                        <div className="text-[11px] text-slate-400 mt-0.5">
                          重置回第1阶段 {w.stageResetCount} 次
                        </div>
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
