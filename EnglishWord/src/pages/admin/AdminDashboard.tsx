import React, { useState, useEffect } from 'react';
import { apiRequest } from '../../services/apiClient';
import { TeacherStudentsDrawer } from './TeacherStudentsDrawer';
import {
  Users,
  Plus,
  RotateCcw,
  Trash2,
  BookOpen,
  UserCheck,
  AlertCircle,
  CheckCircle,
  X,
  Search,
} from 'lucide-react';

export const AdminDashboard: React.FC = () => {
  const [teachers, setTeachers] = useState<any[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');
  const [searchTerm, setSearchTerm] = useState('');

  // 新建教师弹窗
  const [isAddModalOpen, setIsAddModalOpen] = useState(false);
  const [newUsername, setNewUsername] = useState('');
  const [newDisplayName, setNewDisplayName] = useState('');
  const [submitting, setSubmitting] = useState(false);

  // 选中的教师 (打开抽屉)
  const [selectedTeacher, setSelectedTeacher] = useState<any | null>(null);

  // 删除教师孤儿弹窗
  const [deleteWarning, setDeleteWarning] = useState<{
    isOpen: boolean;
    teacherId: string;
    teacherName: string;
    orphanCount: number;
  }>({ isOpen: false, teacherId: '', teacherName: '', orphanCount: 0 });

  const fetchTeachers = async () => {
    setLoading(true);
    try {
      const data = await apiRequest('/api/admin/teachers');
      setTeachers(data.teachers || []);
    } catch (err: any) {
      setError(err.message || '获取教师列表失败');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchTeachers();
  }, []);

  const handleCreateTeacher = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setSuccess('');
    setSubmitting(true);

    try {
      await apiRequest('/api/admin/teachers', {
        method: 'POST',
        body: JSON.stringify({
          username: newUsername,
          displayName: newDisplayName,
        }),
      });
      setSuccess(`教师 "${newUsername}" 创建成功，默认密码为 passw0rd`);
      setIsAddModalOpen(false);
      setNewUsername('');
      setNewDisplayName('');
      fetchTeachers();
    } catch (err: any) {
      setError(err.data?.message || err.message || '创建教师失败');
    } finally {
      setSubmitting(false);
    }
  };

  const handleResetPassword = async (teacherId: string, name: string) => {
    if (!window.confirm(`确认将教师 "${name}" 的密码重置为默认密码 passw0rd？`)) {
      return;
    }

    try {
      await apiRequest(`/api/admin/users/${teacherId}/reset-password`, { method: 'POST' });
      setSuccess(`教师 "${name}" 的密码已成功重置为 passw0rd`);
    } catch (err: any) {
      setError(err.data?.message || err.message || '重置密码失败');
    }
  };

  const handleDeleteTeacher = async (teacherId: string, name: string, deleteOrphans: boolean = false) => {
    setError('');
    setSuccess('');

    try {
      const url = `/api/admin/teachers/${teacherId}${deleteOrphans ? '?deleteOrphanStudents=true' : ''}`;
      await apiRequest(url, { method: 'DELETE' });
      setSuccess(`教师 "${name}" 已成功删除`);
      setDeleteWarning({ isOpen: false, teacherId: '', teacherName: '', orphanCount: 0 });
      fetchTeachers();
    } catch (err: any) {
      if (err.data?.error === 'ORPHAN_STUDENTS_EXIST') {
        setDeleteWarning({
          isOpen: true,
          teacherId,
          teacherName: name,
          orphanCount: err.data.orphanCount,
        });
      } else {
        setError(err.data?.message || err.message || '删除教师失败');
      }
    }
  };

  const filteredTeachers = teachers.filter(
    (t) =>
      t.username.toLowerCase().includes(searchTerm.toLowerCase()) ||
      t.displayName.toLowerCase().includes(searchTerm.toLowerCase())
  );

  return (
    <div className="space-y-6">
      {/* 顶部标题栏与添加按钮 */}
      <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4">
        <div>
          <h1 className="text-2xl font-bold text-slate-900 tracking-tight flex items-center gap-2">
            <Users className="w-7 h-7 text-blue-600" />
            多租户师生架构管理中心
          </h1>
          <p className="text-sm text-slate-500 mt-1">
            由管理员统一创建教师账号；进入教师视图为其指派或关联学生，构建多对多师生绑定。
          </p>
        </div>

        <button
          onClick={() => setIsAddModalOpen(true)}
          className="flex items-center gap-2 px-4 py-2.5 rounded-xl font-semibold text-sm text-white bg-blue-600 hover:bg-blue-700 shadow-md shadow-blue-500/20 transition-colors"
        >
          <Plus className="w-4 h-4" />
          添加新教师
        </button>
      </div>

      {/* 消息提示 */}
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

      {/* 搜索与过滤 */}
      <div className="relative max-w-md">
        <Search className="w-4 h-4 absolute left-3.5 top-3 text-slate-400" />
        <input
          type="text"
          value={searchTerm}
          onChange={(e) => setSearchTerm(e.target.value)}
          placeholder="搜索教师账号或显示姓名..."
          className="w-full pl-10 pr-4 py-2 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500 bg-white"
        />
      </div>

      {/* 教师卡片网格 */}
      {loading ? (
        <div className="text-center py-16 text-slate-400 text-sm">加载教师列表中...</div>
      ) : filteredTeachers.length === 0 ? (
        <div className="bg-white rounded-3xl p-12 text-center border border-slate-200/80">
          <div className="w-12 h-12 rounded-2xl bg-slate-100 flex items-center justify-center text-slate-400 mx-auto mb-3">
            <Users className="w-6 h-6" />
          </div>
          <p className="text-slate-700 font-semibold text-base">暂无教师账号</p>
          <p className="text-slate-400 text-xs mt-1">请点击右上角按钮添加系统第一名教师</p>
        </div>
      ) : (
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-5">
          {filteredTeachers.map((t) => (
            <div
              key={t.id}
              className="bg-white rounded-2xl p-5 border border-slate-200/80 shadow-sm hover:shadow-md hover:border-slate-300 transition-all flex flex-col justify-between"
            >
              <div>
                <div className="flex items-start justify-between gap-3 mb-3">
                  <div>
                    <h3 className="font-bold text-slate-900 text-base">{t.displayName}</h3>
                    <p className="text-xs text-slate-400">@{t.username}</p>
                  </div>
                  <span className="text-[11px] font-semibold px-2 py-0.5 rounded-full bg-blue-50 text-blue-600 border border-blue-100">
                    教师
                  </span>
                </div>

                <div className="grid grid-cols-2 gap-3 py-3 px-3.5 bg-slate-50/80 rounded-xl mb-4 border border-slate-100">
                  <div className="flex items-center gap-2">
                    <UserCheck className="w-4 h-4 text-emerald-600" />
                    <div>
                      <div className="text-xs text-slate-400">名下学生</div>
                      <div className="text-sm font-bold text-slate-800">{t.studentCount} 人</div>
                    </div>
                  </div>
                  <div className="flex items-center gap-2">
                    <BookOpen className="w-4 h-4 text-blue-600" />
                    <div>
                      <div className="text-xs text-slate-400">词库词数</div>
                      <div className="text-sm font-bold text-slate-800">{t.wordCount} 词</div>
                    </div>
                  </div>
                </div>
              </div>

              {/* 底部操作栏 */}
              <div className="pt-2 border-t border-slate-100 flex items-center justify-between gap-2">
                <button
                  onClick={() => setSelectedTeacher(t)}
                  className="flex-1 py-1.5 px-3 rounded-xl text-xs font-semibold text-blue-700 bg-blue-50 hover:bg-blue-100 border border-blue-200 transition-colors flex items-center justify-center gap-1.5"
                >
                  <Users className="w-3.5 h-3.5" />
                  管理名下学生
                </button>

                <div className="flex items-center gap-1">
                  <button
                    onClick={() => handleResetPassword(t.id, t.displayName)}
                    className="p-1.5 rounded-xl text-slate-400 hover:text-slate-700 hover:bg-slate-100"
                    title="重置密码为 passw0rd"
                  >
                    <RotateCcw className="w-4 h-4" />
                  </button>
                  <button
                    onClick={() => handleDeleteTeacher(t.id, t.displayName)}
                    className="p-1.5 rounded-xl text-slate-400 hover:text-red-600 hover:bg-red-50"
                    title="删除教师"
                  >
                    <Trash2 className="w-4 h-4" />
                  </button>
                </div>
              </div>
            </div>
          ))}
        </div>
      )}

      {/* 新建教师弹窗 */}
      {isAddModalOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50 p-4 backdrop-blur-sm">
          <div className="w-full max-w-md bg-white rounded-2xl p-6 shadow-2xl animate-in zoom-in-95 duration-150">
            <div className="flex items-center justify-between pb-3 border-b border-slate-100">
              <h3 className="font-bold text-slate-900 text-base">添加新教师账号</h3>
              <button
                onClick={() => setIsAddModalOpen(false)}
                className="p-1 rounded-lg text-slate-400 hover:text-slate-600"
              >
                <X className="w-5 h-5" />
              </button>
            </div>

            <form onSubmit={handleCreateTeacher} className="mt-4 space-y-4">
              <div>
                <label className="block text-xs font-semibold text-slate-600 mb-1">
                  教师登录用户名 (唯一) *
                </label>
                <input
                  type="text"
                  required
                  value={newUsername}
                  onChange={(e) => setNewUsername(e.target.value)}
                  className="w-full px-3.5 py-2 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500"
                  placeholder="如 teacher_zhang / teacher01"
                />
              </div>

              <div>
                <label className="block text-xs font-semibold text-slate-600 mb-1">
                  显示姓名 (真名/昵称，可选)
                </label>
                <input
                  type="text"
                  value={newDisplayName}
                  onChange={(e) => setNewDisplayName(e.target.value)}
                  className="w-full px-3.5 py-2 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500"
                  placeholder="如 张老师 (留空默认同用户名)"
                />
              </div>

              <div className="p-3 bg-slate-50 rounded-xl text-xs text-slate-500">
                新教师默认初始密码为 <code className="font-bold text-slate-800">passw0rd</code>
                ，教师可自行修改或由管理员随时重置。
              </div>

              <div className="flex justify-end gap-3 pt-2">
                <button
                  type="button"
                  onClick={() => setIsAddModalOpen(false)}
                  className="px-4 py-2 text-sm text-slate-600 hover:bg-slate-100 rounded-xl"
                >
                  取消
                </button>
                <button
                  type="submit"
                  disabled={submitting}
                  className="px-5 py-2 text-sm font-semibold text-white bg-blue-600 hover:bg-blue-700 disabled:opacity-50 rounded-xl shadow-sm"
                >
                  {submitting ? '创建中...' : '确认创建'}
                </button>
              </div>
            </form>
          </div>
        </div>
      )}

      {/* 教师删除孤儿学生警告弹窗 */}
      {deleteWarning.isOpen && (
        <div className="fixed inset-0 z-60 flex items-center justify-center bg-black/60 p-4">
          <div className="w-full max-w-md bg-white rounded-2xl p-6 shadow-2xl animate-in zoom-in-95 duration-150">
            <div className="flex items-center gap-2 text-red-600 font-bold text-base mb-2">
              <AlertCircle className="w-5 h-5 flex-shrink-0" />
              删除教师安全确认
            </div>
            <p className="text-sm text-slate-600 mb-6 leading-relaxed">
              教师 <span className="font-semibold text-slate-900">"{deleteWarning.teacherName}"</span> 名下存在{' '}
              <span className="font-bold text-red-600">{deleteWarning.orphanCount}</span> 名仅归属该教师的独占学生。
              若直接删除教师，这些学生将成为无归属游离账号。
              是否连带彻底清理这些学生的账号？
            </p>
            <div className="flex justify-end gap-3">
              <button
                onClick={() => setDeleteWarning({ isOpen: false, teacherId: '', teacherName: '', orphanCount: 0 })}
                className="px-4 py-2 text-sm font-medium text-slate-600 hover:bg-slate-100 rounded-xl"
              >
                取消删除
              </button>
              <button
                onClick={() => handleDeleteTeacher(deleteWarning.teacherId, deleteWarning.teacherName, true)}
                className="px-4 py-2 text-sm font-medium text-white bg-red-600 hover:bg-red-700 rounded-xl shadow-sm"
              >
                确认删除教师并清理独占学生
              </button>
            </div>
          </div>
        </div>
      )}

      {/* 教师名下学生管理抽屉 */}
      <TeacherStudentsDrawer
        teacher={selectedTeacher}
        onClose={() => setSelectedTeacher(null)}
        onRefreshParent={fetchTeachers}
      />
    </div>
  );
};
