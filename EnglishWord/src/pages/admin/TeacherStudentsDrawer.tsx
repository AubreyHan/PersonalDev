import React, { useState, useEffect } from 'react';
import { apiRequest } from '../../services/apiClient';
import {
  X,
  UserPlus,
  Link as LinkIcon,
  RotateCcw,
  UserMinus,
  Trash2,
  AlertTriangle,
  CheckCircle2,
  Search,
} from 'lucide-react';

interface TeacherStudentsDrawerProps {
  teacher: { id: string; username: string; displayName: string } | null;
  onClose: () => void;
  onRefreshParent: () => void;
}

export const TeacherStudentsDrawer: React.FC<TeacherStudentsDrawerProps> = ({
  teacher,
  onClose,
  onRefreshParent,
}) => {
  const [activeTab, setActiveTab] = useState<'bound' | 'create' | 'bind_existing'>('bound');
  const [boundStudents, setBoundStudents] = useState<any[]>([]);
  const [availableStudents, setAvailableStudents] = useState<any[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');

  // 新建学生表单
  const [newUsername, setNewUsername] = useState('');
  const [newDisplayName, setNewDisplayName] = useState('');
  const [creating, setCreating] = useState(false);

  // 关联已有学生多选
  const [selectedStudentIds, setSelectedStudentIds] = useState<string[]>([]);
  const [existingFilter, setExistingFilter] = useState('');
  const [binding, setBinding] = useState(false);

  // 孤儿警告弹窗
  const [warningModal, setWarningModal] = useState<{
    isOpen: boolean;
    studentId: string;
    studentName: string;
    message: string;
  }>({ isOpen: false, studentId: '', studentName: '', message: '' });

  const loadStudents = async () => {
    if (!teacher) return;
    setLoading(true);
    setError('');
    try {
      const data = await apiRequest(`/api/admin/teachers/${teacher.id}/students`);
      setBoundStudents(data.boundStudents || []);
      setAvailableStudents(data.availableStudents || []);
    } catch (err: any) {
      setError(err.message || '加载学生数据失败');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    if (teacher) {
      loadStudents();
      setActiveTab('bound');
      setSelectedStudentIds([]);
    }
  }, [teacher]);

  if (!teacher) return null;

  // 创建新学生
  const handleCreateStudent = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setSuccess('');
    setCreating(true);

    try {
      await apiRequest(`/api/admin/teachers/${teacher.id}/students`, {
        method: 'POST',
        body: JSON.stringify({
          mode: 'create',
          username: newUsername,
          displayName: newDisplayName,
        }),
      });
      setSuccess(`学生 "${newUsername}" 创建成功，默认密码为 passw0rd`);
      setNewUsername('');
      setNewDisplayName('');
      setActiveTab('bound');
      await loadStudents();
      onRefreshParent();
    } catch (err: any) {
      setError(err.data?.message || err.message || '创建学生失败');
    } finally {
      setCreating(false);
    }
  };

  // 绑定已有学生
  const handleBindExisting = async () => {
    if (selectedStudentIds.length === 0) return;
    setError('');
    setSuccess('');
    setBinding(true);

    try {
      await apiRequest(`/api/admin/teachers/${teacher.id}/students`, {
        method: 'POST',
        body: JSON.stringify({
          mode: 'bind_existing',
          studentIds: selectedStudentIds,
        }),
      });
      setSuccess(`成功绑定 ${selectedStudentIds.length} 名学生到 ${teacher.displayName} 老师名下`);
      setSelectedStudentIds([]);
      setActiveTab('bound');
      await loadStudents();
      onRefreshParent();
    } catch (err: any) {
      setError(err.data?.message || err.message || '绑定失败');
    } finally {
      setBinding(false);
    }
  };

  // 移出解绑
  const handleUnbind = async (studentId: string, studentName: string, force: boolean = false) => {
    setError('');
    setSuccess('');

    try {
      const url = `/api/admin/teachers/${teacher.id}/students/${studentId}?action=unbind${force ? '&force=true' : ''}`;
      const res = await apiRequest(url, { method: 'DELETE' });
      setSuccess(res.message || '操作成功');
      setWarningModal({ isOpen: false, studentId: '', studentName: '', message: '' });
      await loadStudents();
      onRefreshParent();
    } catch (err: any) {
      if (err.data?.error === 'LAST_TEACHER_WARNING') {
        setWarningModal({
          isOpen: true,
          studentId,
          studentName,
          message: err.data.message,
        });
      } else {
        setError(err.data?.message || err.message || '解绑失败');
      }
    }
  };

  // 彻底删除账号
  const handleDeleteStudent = async (studentId: string, studentName: string) => {
    if (!window.confirm(`确认彻底删除学生账号 "${studentName}"？该操作将物理清理其所有学习记录与绑定关系且不可恢复！`)) {
      return;
    }

    try {
      await apiRequest(`/api/admin/teachers/${teacher.id}/students/${studentId}?action=delete_account`, {
        method: 'DELETE',
      });
      setSuccess(`学生 "${studentName}" 账号已彻底删除`);
      await loadStudents();
      onRefreshParent();
    } catch (err: any) {
      setError(err.data?.message || err.message || '删除账号失败');
    }
  };

  // 重置密码
  const handleResetPassword = async (userId: string, userName: string) => {
    if (!window.confirm(`确认将学生 "${userName}" 的密码重置为默认密码 passw0rd？`)) {
      return;
    }

    try {
      await apiRequest(`/api/admin/users/${userId}/reset-password`, { method: 'POST' });
      setSuccess(`学生 "${userName}" 密码已重置为 passw0rd`);
    } catch (err: any) {
      setError(err.data?.message || err.message || '密码重置失败');
    }
  };

  const filteredAvailable = availableStudents.filter(
    (s) =>
      s.username.toLowerCase().includes(existingFilter.toLowerCase()) ||
      s.displayName.toLowerCase().includes(existingFilter.toLowerCase())
  );

  return (
    <div className="fixed inset-0 z-50 overflow-hidden bg-black/40 backdrop-blur-sm flex justify-end">
      <div className="w-full max-w-2xl bg-white h-full shadow-2xl flex flex-col animate-in slide-in-from-right duration-200">
        {/* Header */}
        <div className="px-6 py-5 border-b border-slate-100 flex items-center justify-between bg-slate-50/70">
          <div>
            <div className="flex items-center gap-2">
              <h2 className="text-lg font-bold text-slate-900">{teacher.displayName} 老师的名下学生</h2>
              <span className="text-xs px-2 py-0.5 rounded-full bg-blue-100 text-blue-700 font-medium">
                @{teacher.username}
              </span>
            </div>
            <p className="text-xs text-slate-500 mt-0.5">
              遵循多租户规则：必须在教师名下新建或关联已有学生
            </p>
          </div>
          <button
            onClick={onClose}
            className="p-1.5 rounded-xl text-slate-400 hover:text-slate-600 hover:bg-slate-200/60"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Tab 导航 */}
        <div className="flex border-b border-slate-200 px-6 bg-white gap-6">
          <button
            onClick={() => setActiveTab('bound')}
            className={`py-3 text-sm font-semibold border-b-2 transition-colors flex items-center gap-1.5 ${
              activeTab === 'bound'
                ? 'border-blue-600 text-blue-600'
                : 'border-transparent text-slate-500 hover:text-slate-800'
            }`}
          >
            已归属学生 ({boundStudents.length})
          </button>
          <button
            onClick={() => setActiveTab('create')}
            className={`py-3 text-sm font-semibold border-b-2 transition-colors flex items-center gap-1.5 ${
              activeTab === 'create'
                ? 'border-blue-600 text-blue-600'
                : 'border-transparent text-slate-500 hover:text-slate-800'
            }`}
          >
            <UserPlus className="w-4 h-4" />
            在名下新建学生
          </button>
          <button
            onClick={() => setActiveTab('bind_existing')}
            className={`py-3 text-sm font-semibold border-b-2 transition-colors flex items-center gap-1.5 ${
              activeTab === 'bind_existing'
                ? 'border-blue-600 text-blue-600'
                : 'border-transparent text-slate-500 hover:text-slate-800'
            }`}
          >
            <LinkIcon className="w-4 h-4" />
            关联系统已有学生 ({availableStudents.length})
          </button>
        </div>

        {/* 提示反馈信息 */}
        {error && (
          <div className="mx-6 mt-4 p-3 rounded-xl bg-red-50 border border-red-200 text-red-700 text-xs flex items-center gap-2">
            <AlertTriangle className="w-4 h-4 flex-shrink-0" />
            <span>{error}</span>
          </div>
        )}
        {success && (
          <div className="mx-6 mt-4 p-3 rounded-xl bg-emerald-50 border border-emerald-200 text-emerald-700 text-xs flex items-center gap-2">
            <CheckCircle2 className="w-4 h-4 flex-shrink-0" />
            <span>{success}</span>
          </div>
        )}

        {/* Tab 内容区 */}
        <div className="flex-1 overflow-y-auto p-6">
          {activeTab === 'bound' && (
            <div>
              {loading ? (
                <div className="text-center py-12 text-slate-400 text-sm">加载学生列表中...</div>
              ) : boundStudents.length === 0 ? (
                <div className="text-center py-12 text-slate-400">
                  <p className="text-sm">该教师名下暂无归属学生</p>
                  <p className="text-xs mt-1 text-slate-400">请点击上方按钮新建或关联学生</p>
                </div>
              ) : (
                <div className="space-y-3">
                  {boundStudents.map((s) => (
                    <div
                      key={s.id}
                      className="p-4 rounded-2xl border border-slate-200 bg-white hover:border-slate-300 shadow-sm flex items-center justify-between gap-4 transition-all"
                    >
                      <div>
                        <div className="flex items-center gap-2">
                          <span className="font-semibold text-slate-800 text-sm">{s.displayName}</span>
                          <span className="text-xs text-slate-400">@{s.username}</span>
                          <span className="text-[11px] px-2 py-0.5 rounded-full bg-slate-100 text-slate-600 border border-slate-200">
                            已跟随 {s.totalTeachersCount} 位教师
                          </span>
                        </div>
                        <div className="text-[11px] text-slate-400 mt-1">
                          绑定时间：{new Date(s.boundAt).toLocaleDateString()}
                        </div>
                      </div>

                      <div className="flex items-center gap-1.5">
                        <button
                          onClick={() => handleResetPassword(s.id, s.displayName)}
                          className="px-2.5 py-1 text-xs font-medium rounded-lg text-slate-600 hover:bg-slate-100 border border-slate-200 flex items-center gap-1"
                          title="重置密码为 passw0rd"
                        >
                          <RotateCcw className="w-3.5 h-3.5" />
                          重置密码
                        </button>
                        <button
                          onClick={() => handleUnbind(s.id, s.displayName)}
                          className="px-2.5 py-1 text-xs font-medium rounded-lg text-amber-700 bg-amber-50 hover:bg-amber-100 border border-amber-200 flex items-center gap-1"
                          title="移出该教师名下（解除绑定）"
                        >
                          <UserMinus className="w-3.5 h-3.5" />
                          移出解绑
                        </button>
                        <button
                          onClick={() => handleDeleteStudent(s.id, s.displayName)}
                          className="p-1 text-slate-400 hover:text-red-600 hover:bg-red-50 rounded-lg"
                          title="彻底删除该学生账号"
                        >
                          <Trash2 className="w-4 h-4" />
                        </button>
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}

          {activeTab === 'create' && (
            <form onSubmit={handleCreateStudent} className="space-y-4 max-w-lg">
              <div className="p-4 bg-blue-50/60 rounded-2xl border border-blue-100 text-xs text-blue-700">
                在该教师名下创建新学生账号后，系统将自动建立绑定关联。新学生初始默认密码为{' '}
                <code className="bg-blue-200/60 px-1 py-0.5 rounded font-mono font-bold">passw0rd</code>
                ，学生登录后可自行修改。
              </div>

              <div>
                <label className="block text-xs font-semibold text-slate-600 mb-1">
                  学生登录账号 (唯一用户名) *
                </label>
                <input
                  type="text"
                  required
                  value={newUsername}
                  onChange={(e) => setNewUsername(e.target.value)}
                  className="w-full px-3.5 py-2.5 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500"
                  placeholder="如 stu_wang / student01"
                />
              </div>

              <div>
                <label className="block text-xs font-semibold text-slate-600 mb-1">
                  显示姓名 (昵称/真名，可选)
                </label>
                <input
                  type="text"
                  value={newDisplayName}
                  onChange={(e) => setNewDisplayName(e.target.value)}
                  className="w-full px-3.5 py-2.5 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500"
                  placeholder="如 王小明 (留空默认同用户名)"
                />
              </div>

              <button
                type="submit"
                disabled={creating}
                className="w-full py-2.5 px-4 rounded-xl text-sm font-semibold text-white bg-blue-600 hover:bg-blue-700 disabled:opacity-50 transition-colors shadow-sm"
              >
                {creating ? '创建并绑定中...' : '确认创建并绑定到该教师'}
              </button>
            </form>
          )}

          {activeTab === 'bind_existing' && (
            <div className="space-y-4">
              <div className="p-3.5 bg-slate-50 rounded-2xl border border-slate-200 text-xs text-slate-600">
                支持多对多师生绑定：勾选系统中其他教师名下的学生，将其同时关联到当前教师名下。学生将能同时学习多位老师管理的词库。
              </div>

              {/* 搜索框 */}
              <div className="relative">
                <Search className="w-4 h-4 absolute left-3.5 top-3 text-slate-400" />
                <input
                  type="text"
                  value={existingFilter}
                  onChange={(e) => setExistingFilter(e.target.value)}
                  placeholder="搜索系统中已有学生..."
                  className="w-full pl-10 pr-4 py-2 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500"
                />
              </div>

              {filteredAvailable.length === 0 ? (
                <div className="text-center py-10 text-slate-400 text-sm">
                  没有可关联的其他学生
                </div>
              ) : (
                <div className="border border-slate-200 rounded-2xl divide-y divide-slate-100 max-h-80 overflow-y-auto">
                  {filteredAvailable.map((s) => (
                    <label
                      key={s.id}
                      className="flex items-center gap-3 p-3.5 hover:bg-slate-50 cursor-pointer transition-colors"
                    >
                      <input
                        type="checkbox"
                        checked={selectedStudentIds.includes(s.id)}
                        onChange={(e) => {
                          if (e.target.checked) {
                            setSelectedStudentIds([...selectedStudentIds, s.id]);
                          } else {
                            setSelectedStudentIds(selectedStudentIds.filter((id) => id !== s.id));
                          }
                        }}
                        className="rounded border-slate-300 text-blue-600 focus:ring-blue-500 w-4 h-4"
                      />
                      <div className="flex-1">
                        <div className="text-sm font-semibold text-slate-800">{s.displayName}</div>
                        <div className="text-xs text-slate-400">@{s.username}</div>
                      </div>
                      <span className="text-xs text-slate-500">已跟随 {s.totalTeachersCount} 位教师</span>
                    </label>
                  ))}
                </div>
              )}

              <button
                onClick={handleBindExisting}
                disabled={binding || selectedStudentIds.length === 0}
                className="w-full py-2.5 px-4 rounded-xl text-sm font-semibold text-white bg-blue-600 hover:bg-blue-700 disabled:opacity-50 transition-colors shadow-sm"
              >
                {binding ? '绑定中...' : `确认关联选中的 ${selectedStudentIds.length} 名学生`}
              </button>
            </div>
          )}
        </div>
      </div>

      {/* 孤儿唯一归属提示弹窗 */}
      {warningModal.isOpen && (
        <div className="fixed inset-0 z-60 flex items-center justify-center bg-black/60 p-4">
          <div className="w-full max-w-md bg-white rounded-2xl p-6 shadow-2xl animate-in zoom-in-95 duration-150">
            <div className="flex items-center gap-2 text-amber-600 font-bold text-base mb-2">
              <AlertTriangle className="w-5 h-5 flex-shrink-0" />
              解绑唯一教师归属确认
            </div>
            <p className="text-sm text-slate-600 mb-6 leading-relaxed">
              学生 <span className="font-semibold text-slate-900">"{warningModal.studentName}"</span> 当前仅归属这一名教师。
              若移出解绑，该学生将成为无归属孤儿。系统将**彻底删除该学生账号及相关学习记录**。确认继续操作吗？
            </p>
            <div className="flex justify-end gap-3">
              <button
                onClick={() => setWarningModal({ isOpen: false, studentId: '', studentName: '', message: '' })}
                className="px-4 py-2 text-sm font-medium text-slate-600 hover:bg-slate-100 rounded-xl"
              >
                取消
              </button>
              <button
                onClick={() => handleUnbind(warningModal.studentId, warningModal.studentName, true)}
                className="px-4 py-2 text-sm font-medium text-white bg-red-600 hover:bg-red-700 rounded-xl shadow-sm"
              >
                确认强制移出并彻底删除
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
