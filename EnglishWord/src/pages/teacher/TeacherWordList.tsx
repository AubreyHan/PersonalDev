import React, { useState, useEffect } from 'react';
import { apiRequest } from '../../services/apiClient';
import { playWordAudio } from '../../db/audioCache';
import {
  Library,
  Plus,
  Search,
  Volume2,
  Trash2,
  Sparkles,
  AlertCircle,
  CheckCircle,
  X,
  ExternalLink,
} from 'lucide-react';
import type { TeacherWord, WordMeaning, WordExample, WordInflections } from '../../shared/types';

export const TeacherWordList: React.FC = () => {
  const [words, setWords] = useState<TeacherWord[]>([]);
  const [loading, setLoading] = useState(true);
  const [search, setSearch] = useState('');
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');

  // 录入/编辑弹窗
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [isEnriching, setIsEnriching] = useState(false);
  const [editingWordId, setEditingWordId] = useState<string | null>(null);

  // 弹窗表单状态
  const [formText, setFormText] = useState('');
  const [formSyllables, setFormSyllables] = useState('');
  const [formPhoneticUs, setFormPhoneticUs] = useState('');
  const [formAudioUsUrl, setFormAudioUsUrl] = useState('');
  const [formMeanings, setFormMeanings] = useState<WordMeaning[]>([]);
  const [formInflections, setFormInflections] = useState<WordInflections>({});
  const [formHomophones, setFormHomophones] = useState<string[]>([]);
  const [formExamples, setFormExamples] = useState<WordExample[]>([]);
  const [formTags, setFormTags] = useState<string[]>([]);
  const [saving, setSaving] = useState(false);

  const fetchWords = async () => {
    setLoading(true);
    try {
      const q = search ? `?q=${encodeURIComponent(search)}` : '';
      const data = await apiRequest(`/api/teacher/words${q}`);
      setWords(data.words || []);
    } catch (err: any) {
      setError(err.message || '获取词库列表失败');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchWords();
  }, [search]);

  // 打开新建弹窗
  const handleOpenCreate = () => {
    setEditingWordId(null);
    setFormText('');
    setFormSyllables('');
    setFormPhoneticUs('');
    setFormAudioUsUrl('');
    setFormMeanings([{ pos: 'n.', zh: '' }]);
    setFormInflections({});
    setFormHomophones([]);
    setFormExamples([{ en: '', zh: '', targetInSentence: '' }]);
    setFormTags([]);
    setIsModalOpen(true);
  };

  // 智能补全
  const handleEnrich = async () => {
    if (!formText.trim()) {
      setError('请先输入英文单词或词组');
      return;
    }

    setError('');
    setIsEnriching(true);
    try {
      const res = await apiRequest('/api/teacher/words/enrich', {
        method: 'POST',
        body: JSON.stringify({ text: formText.trim() }),
      });
      const w = res.word;
      setFormSyllables(w.syllables || w.text);
      setFormPhoneticUs(w.phoneticUs || '');
      setFormAudioUsUrl(w.audioUsUrl || '');
      setFormMeanings(w.meanings || []);
      setFormInflections(w.inflections || {});
      setFormHomophones(w.homophones || []);
      setFormExamples(w.examples || []);
      setFormTags(w.tags || []);
      setSuccess('智能补全成功！音节、释义、变形与例句已填充，您可进一步微调');
    } catch (err: any) {
      setError(err.data?.message || err.message || '智能补全失败');
    } finally {
      setIsEnriching(false);
    }
  };

  // 保存词条
  const handleSaveWord = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setSuccess('');
    setSaving(true);

    const payload = {
      text: formText.trim(),
      syllables: formSyllables || formText.trim(),
      phoneticUs: formPhoneticUs,
      audioUsUrl: formAudioUsUrl,
      meanings: formMeanings.filter((m) => m.zh.trim()),
      inflections: formInflections,
      homophones: formHomophones,
      examples: formExamples.filter((ex) => ex.en.trim()),
      tags: formTags,
    };

    try {
      if (editingWordId) {
        await apiRequest(`/api/teacher/words/${editingWordId}`, {
          method: 'PUT',
          body: JSON.stringify(payload),
        });
        setSuccess('词条更新成功！');
      } else {
        await apiRequest('/api/teacher/words', {
          method: 'POST',
          body: JSON.stringify(payload),
        });
        setSuccess(`词条 "${formText}" 已成功添加至您的词库！`);
      }
      setIsModalOpen(false);
      fetchWords();
    } catch (err: any) {
      setError(err.data?.message || err.message || '保存词条失败');
    } finally {
      setSaving(false);
    }
  };

  // 删除词条
  const handleDeleteWord = async (id: string, text: string) => {
    if (!window.confirm(`确认删除词条 "${text}"？`)) return;

    try {
      await apiRequest(`/api/teacher/words/${id}`, { method: 'DELETE' });
      setSuccess(`词条 "${text}" 已删除`);
      fetchWords();
    } catch (err: any) {
      setError(err.data?.message || err.message || '删除失败');
    }
  };

  return (
    <div className="space-y-6">
      {/* 顶部标题栏 */}
      <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4">
        <div>
          <h1 className="text-2xl font-bold text-slate-900 tracking-tight flex items-center gap-2">
            <Library className="w-7 h-7 text-blue-600" />
            生词与词组管理中心
          </h1>
          <p className="text-sm text-slate-500 mt-1">
            单条智能录入：自动补全音节切分、音标、全套词形变化、同音词表与带挖空双语例句；录入后默认对名下全体学生生效。
          </p>
        </div>

        <button
          onClick={handleOpenCreate}
          className="flex items-center gap-2 px-4 py-2.5 rounded-xl font-semibold text-sm text-white bg-blue-600 hover:bg-blue-700 shadow-md shadow-blue-500/20 transition-colors"
        >
          <Plus className="w-4 h-4" />
          添加生词/词组
        </button>
      </div>

      {/* 消息通知 */}
      {error && (
        <div className="p-4 bg-red-50 border border-red-200 rounded-2xl text-red-700 text-sm flex items-center gap-2.5">
          <AlertCircle className="w-5 h-5 flex-shrink-0" />
          <span>{error}</span>
        </div>
      )}
      {success && (
        <div className="p-4 bg-emerald-50 border border-emerald-200 rounded-2xl text-emerald-700 text-sm flex items-center gap-2.5">
          <CheckCircle className="w-5 h-5 flex-shrink-0" />
          <span>{success}</span>
        </div>
      )}

      {/* 搜索栏 */}
      <div className="relative max-w-md">
        <Search className="w-4 h-4 absolute left-3.5 top-3 text-slate-400" />
        <input
          type="text"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          placeholder="搜索英文单词、词组或中文释义..."
          className="w-full pl-10 pr-4 py-2 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500 bg-white"
        />
      </div>

      {/* 词库网格展示 */}
      {loading ? (
        <div className="text-center py-16 text-slate-400 text-sm">加载词库中...</div>
      ) : words.length === 0 ? (
        <div className="bg-white rounded-3xl p-12 text-center border border-slate-200/80">
          <div className="w-12 h-12 rounded-2xl bg-slate-100 flex items-center justify-center text-slate-400 mx-auto mb-3">
            <Library className="w-6 h-6" />
          </div>
          <p className="text-slate-700 font-semibold text-base">词库暂无词条</p>
          <p className="text-slate-400 text-xs mt-1">点击右上角「添加生词/词组」，体验智能一键补全</p>
        </div>
      ) : (
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-5">
          {words.map((w) => (
            <div
              key={w.id}
              className="bg-white rounded-2xl p-5 border border-slate-200/80 shadow-sm hover:shadow-md transition-all flex flex-col justify-between"
            >
              <div>
                {/* 单词与发音 */}
                <div className="flex items-start justify-between gap-2">
                  <div>
                    <div className="flex items-center gap-2">
                      <span className="font-bold text-lg text-slate-900 tracking-tight">{w.text}</span>
                      <button
                        onClick={() => playWordAudio(w.audioUsUrl, w.text)}
                        className="p-1 rounded-lg text-blue-600 hover:bg-blue-50 transition-colors"
                        title="播放真人读音"
                      >
                        <Volume2 className="w-4 h-4" />
                      </button>
                    </div>
                    {/* 分音节拆解与音标 */}
                    <div className="flex items-center gap-2 text-xs text-slate-500 mt-0.5">
                      {w.syllables && w.syllables !== w.text && (
                        <span className="font-mono text-blue-600 bg-blue-50/80 px-1.5 py-0.5 rounded">
                          {w.syllables}
                        </span>
                      )}
                      <span>{w.phoneticUs}</span>
                    </div>
                  </div>

                  <button
                    onClick={() => handleDeleteWord(w.id, w.text)}
                    className="p-1.5 text-slate-300 hover:text-red-600 hover:bg-red-50 rounded-lg transition-colors"
                  >
                    <Trash2 className="w-4 h-4" />
                  </button>
                </div>

                {/* 中文释义 */}
                <div className="mt-3 space-y-1">
                  {w.meanings.slice(0, 3).map((m, idx) => (
                    <div key={idx} className="text-xs text-slate-700 flex items-baseline gap-1.5">
                      <span className="font-semibold text-blue-600 italic">{m.pos}</span>
                      <span className="line-clamp-1">{m.zh}</span>
                    </div>
                  ))}
                </div>

                {/* 词形变化预览 */}
                {Object.keys(w.inflections).length > 0 && (
                  <div className="mt-3 flex flex-wrap gap-1">
                    {Object.entries(w.inflections).map(
                      ([k, v]) =>
                        v && (
                          <span
                            key={k}
                            className="text-[10px] px-1.5 py-0.5 rounded bg-slate-100 text-slate-600 font-mono"
                          >
                            {v}
                          </span>
                        )
                    )}
                  </div>
                )}

                {/* 双语例句展示 */}
                {w.examples.length > 0 && (
                  <div className="mt-3 p-2.5 rounded-xl bg-slate-50 border border-slate-100 text-xs text-slate-600">
                    <p className="italic text-slate-800 line-clamp-2">"{w.examples[0].en}"</p>
                    <p className="text-slate-400 text-[11px] mt-0.5 line-clamp-1">{w.examples[0].zh}</p>
                  </div>
                )}
              </div>

              <div className="mt-4 pt-2 border-t border-slate-100 text-[11px] text-slate-400 flex items-center justify-between">
                <span>添加日期：{new Date(w.createdAt).toLocaleDateString()}</span>
                {w.homophones.length > 0 && (
                  <span className="text-amber-600 font-medium">同音词：{w.homophones.join(', ')}</span>
                )}
              </div>
            </div>
          ))}
        </div>
      )}

      {/* 单条录入与智能补全弹窗 */}
      {isModalOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50 p-4 backdrop-blur-sm overflow-y-auto">
          <div className="w-full max-w-2xl bg-white rounded-3xl p-6 shadow-2xl animate-in zoom-in-95 duration-150 max-h-[90vh] flex flex-col my-8">
            <div className="flex items-center justify-between pb-3 border-b border-slate-100">
              <div className="flex items-center gap-2">
                <Sparkles className="w-5 h-5 text-blue-600" />
                <h3 className="font-bold text-slate-900 text-lg">
                  {editingWordId ? '编辑词条' : '单条智能录入生词/词组'}
                </h3>
              </div>
              <button
                onClick={() => setIsModalOpen(false)}
                className="p-1 rounded-lg text-slate-400 hover:text-slate-600"
              >
                <X className="w-5 h-5" />
              </button>
            </div>

            <form onSubmit={handleSaveWord} className="mt-4 space-y-4 overflow-y-auto flex-1 pr-1">
              {/* 核心输入与一键补全 */}
              <div className="flex gap-2">
                <div className="flex-1">
                  <label className="block text-xs font-semibold text-slate-600 mb-1">
                    英文单词或词组 *
                  </label>
                  <input
                    type="text"
                    required
                    value={formText}
                    onChange={(e) => setFormText(e.target.value)}
                    className="w-full px-3.5 py-2.5 text-base font-medium rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500"
                    placeholder="如 information / look forward to"
                  />
                </div>
                <div className="flex items-end">
                  <button
                    type="button"
                    onClick={handleEnrich}
                    disabled={isEnriching || !formText.trim()}
                    className="h-[42px] px-4 rounded-xl text-sm font-semibold text-white bg-gradient-to-r from-blue-600 to-indigo-600 hover:from-blue-700 hover:to-indigo-700 disabled:opacity-50 flex items-center gap-1.5 shadow-md shadow-blue-500/20 transition-all"
                  >
                    <Sparkles className="w-4 h-4" />
                    {isEnriching ? 'AI 补全中...' : '智能补全'}
                  </button>
                </div>
              </div>

              {/* 音节与音标 */}
              <div className="grid grid-cols-2 gap-3">
                <div>
                  <label className="block text-xs font-semibold text-slate-600 mb-1">
                    音节拆解 (如 in·for·ma·tion)
                  </label>
                  <input
                    type="text"
                    value={formSyllables}
                    onChange={(e) => setFormSyllables(e.target.value)}
                    className="w-full px-3 py-2 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20"
                    placeholder="in·for·ma·tion"
                  />
                </div>
                <div>
                  <label className="block text-xs font-semibold text-slate-600 mb-1">
                    美式音标 (如 /ˌɪnfərˈmeɪʃn/)
                  </label>
                  <input
                    type="text"
                    value={formPhoneticUs}
                    onChange={(e) => setFormPhoneticUs(e.target.value)}
                    className="w-full px-3 py-2 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20"
                    placeholder="/.../"
                  />
                </div>
              </div>

              {/* 中文释义列表 */}
              <div>
                <label className="block text-xs font-semibold text-slate-600 mb-1">中文释义列表</label>
                <div className="space-y-2">
                  {formMeanings.map((m, idx) => (
                    <div key={idx} className="flex gap-2">
                      <input
                        type="text"
                        value={m.pos}
                        onChange={(e) => {
                          const updated = [...formMeanings];
                          updated[idx].pos = e.target.value;
                          setFormMeanings(updated);
                        }}
                        className="w-20 px-2.5 py-1.5 text-xs rounded-xl border border-slate-200 text-center"
                        placeholder="词性 n."
                      />
                      <input
                        type="text"
                        value={m.zh}
                        onChange={(e) => {
                          const updated = [...formMeanings];
                          updated[idx].zh = e.target.value;
                          setFormMeanings(updated);
                        }}
                        className="flex-1 px-3 py-1.5 text-xs rounded-xl border border-slate-200"
                        placeholder="中文释义"
                      />
                      <button
                        type="button"
                        onClick={() => setFormMeanings(formMeanings.filter((_, i) => i !== idx))}
                        className="p-1.5 text-slate-400 hover:text-red-500"
                      >
                        <Trash2 className="w-4 h-4" />
                      </button>
                    </div>
                  ))}
                  <button
                    type="button"
                    onClick={() => setFormMeanings([...formMeanings, { pos: 'n.', zh: '' }])}
                    className="text-xs text-blue-600 hover:underline font-medium"
                  >
                    + 添加一条释义
                  </button>
                </div>
              </div>

              {/* 词形变化 */}
              <div>
                <label className="block text-xs font-semibold text-slate-600 mb-1">常见词形变化</label>
                <div className="grid grid-cols-2 sm:grid-cols-4 gap-2">
                  <div>
                    <span className="text-[10px] text-slate-400">复数 (plural)</span>
                    <input
                      type="text"
                      value={formInflections.plural || ''}
                      onChange={(e) => setFormInflections({ ...formInflections, plural: e.target.value })}
                      className="w-full px-2.5 py-1.5 text-xs rounded-lg border border-slate-200"
                    />
                  </div>
                  <div>
                    <span className="text-[10px] text-slate-400">过去式 (past)</span>
                    <input
                      type="text"
                      value={formInflections.pastTense || ''}
                      onChange={(e) => setFormInflections({ ...formInflections, pastTense: e.target.value })}
                      className="w-full px-2.5 py-1.5 text-xs rounded-lg border border-slate-200"
                    />
                  </div>
                  <div>
                    <span className="text-[10px] text-slate-400">过去分词 (p.p.)</span>
                    <input
                      type="text"
                      value={formInflections.pastParticiple || ''}
                      onChange={(e) => setFormInflections({ ...formInflections, pastParticiple: e.target.value })}
                      className="w-full px-2.5 py-1.5 text-xs rounded-lg border border-slate-200"
                    />
                  </div>
                  <div>
                    <span className="text-[10px] text-slate-400">现在分词 (-ing)</span>
                    <input
                      type="text"
                      value={formInflections.presentParticiple || ''}
                      onChange={(e) => setFormInflections({ ...formInflections, presentParticiple: e.target.value })}
                      className="w-full px-2.5 py-1.5 text-xs rounded-lg border border-slate-200"
                    />
                  </div>
                </div>
              </div>

              {/* 带挖空标记的双语例句 */}
              <div>
                <label className="block text-xs font-semibold text-slate-600 mb-1">
                  双语例句（需标明句中出现的目标词或其变形，供 2.4 填空使用）
                </label>
                <div className="space-y-3">
                  {formExamples.map((ex, idx) => (
                    <div key={idx} className="p-3 bg-slate-50 rounded-xl border border-slate-200 space-y-2">
                      <input
                        type="text"
                        value={ex.en}
                        onChange={(e) => {
                          const updated = [...formExamples];
                          updated[idx].en = e.target.value;
                          setFormExamples(updated);
                        }}
                        className="w-full px-3 py-1.5 text-xs rounded-lg border border-slate-200"
                        placeholder="英文例句，如 She wrote a letter."
                      />
                      <div className="flex gap-2">
                        <input
                          type="text"
                          value={ex.zh}
                          onChange={(e) => {
                            const updated = [...formExamples];
                            updated[idx].zh = e.target.value;
                            setFormExamples(updated);
                          }}
                          className="flex-1 px-3 py-1.5 text-xs rounded-lg border border-slate-200"
                          placeholder="中文翻译"
                        />
                        <input
                          type="text"
                          value={ex.targetInSentence}
                          onChange={(e) => {
                            const updated = [...formExamples];
                            updated[idx].targetInSentence = e.target.value;
                            setFormExamples(updated);
                          }}
                          className="w-32 px-3 py-1.5 text-xs rounded-lg border border-slate-200 text-blue-600 font-semibold"
                          placeholder="句中目标形态(如 wrote)"
                        />
                      </div>
                    </div>
                  ))}
                </div>
              </div>

              {/* 底部按钮 */}
              <div className="flex justify-end gap-3 pt-4 border-t border-slate-100">
                <button
                  type="button"
                  onClick={() => setIsModalOpen(false)}
                  className="px-4 py-2 text-sm text-slate-600 hover:bg-slate-100 rounded-xl"
                >
                  取消
                </button>
                <button
                  type="submit"
                  disabled={saving}
                  className="px-5 py-2 text-sm font-semibold text-white bg-blue-600 hover:bg-blue-700 disabled:opacity-50 rounded-xl shadow-sm"
                >
                  {saving ? '保存中...' : '确认保存到词库'}
                </button>
              </div>
            </form>
          </div>
        </div>
      )}
    </div>
  );
};
