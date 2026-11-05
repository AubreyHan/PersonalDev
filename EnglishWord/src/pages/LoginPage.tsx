import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuthStore } from '../stores/useAuthStore';
import { BookOpen, Lock, User, AlertCircle, Info } from 'lucide-react';

export const LoginPage: React.FC = () => {
  const navigate = useNavigate();
  const { login, isLoading } = useAuthStore();
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');

    try {
      const user = await login(username, password);
      if (user.role === 'admin') {
        navigate('/admin');
      } else if (user.role === 'teacher') {
        navigate('/teacher/words');
      } else {
        navigate('/student/study');
      }
    } catch (err: any) {
      setError(err.data?.message || err.message || '登录失败，请检查账号密码');
    }
  };

  return (
    <div className="min-h-screen bg-gradient-to-br from-slate-100 via-blue-50/40 to-indigo-50/50 flex flex-col justify-center items-center p-4">
      <div className="w-full max-w-md">
        {/* Brand Header */}
        <div className="text-center mb-8">
          <div className="inline-flex items-center justify-center w-16 h-16 rounded-2xl bg-gradient-to-tr from-blue-600 to-indigo-600 text-white shadow-xl shadow-blue-500/25 mb-4">
            <BookOpen className="w-8 h-8" />
          </div>
          <h1 className="text-2xl font-bold tracking-tight text-slate-900 sm:text-3xl">
            EnglishWord 精学系统
          </h1>
          <p className="mt-1.5 text-sm text-slate-500">
            多租户离线优先 · 单词精学与艾宾浩斯复习
          </p>
        </div>

        {/* Login Card */}
        <div className="bg-white rounded-3xl p-8 shadow-xl shadow-slate-200/50 border border-slate-100">
          <form onSubmit={handleSubmit} className="space-y-5">
            {error && (
              <div className="flex items-center gap-2 p-3.5 text-sm text-red-700 bg-red-50 rounded-2xl border border-red-200 animate-shake">
                <AlertCircle className="w-4 h-4 flex-shrink-0" />
                <span>{error}</span>
              </div>
            )}

            <div>
              <label className="block text-xs font-semibold uppercase tracking-wider text-slate-500 mb-1.5">
                登录账号
              </label>
              <div className="relative">
                <div className="absolute inset-y-0 left-0 pl-3.5 flex items-center pointer-events-none text-slate-400">
                  <User className="w-4 h-4" />
                </div>
                <input
                  type="text"
                  required
                  value={username}
                  onChange={(e) => setUsername(e.target.value)}
                  className="w-full pl-10 pr-4 py-2.5 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500 transition-all"
                  placeholder="admin / 教师账号 / 学生账号"
                />
              </div>
            </div>

            <div>
              <label className="block text-xs font-semibold uppercase tracking-wider text-slate-500 mb-1.5">
                账号密码
              </label>
              <div className="relative">
                <div className="absolute inset-y-0 left-0 pl-3.5 flex items-center pointer-events-none text-slate-400">
                  <Lock className="w-4 h-4" />
                </div>
                <input
                  type="password"
                  required
                  value={password}
                  onChange={(e) => setPassword(e.target.value)}
                  className="w-full pl-10 pr-4 py-2.5 text-sm rounded-xl border border-slate-200 focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500 transition-all"
                  placeholder="请输入密码"
                />
              </div>
            </div>

            <button
              type="submit"
              disabled={isLoading}
              className="w-full mt-2 py-3 px-4 rounded-xl font-medium text-white bg-blue-600 hover:bg-blue-700 active:bg-blue-800 disabled:opacity-50 shadow-lg shadow-blue-500/25 transition-all text-sm flex items-center justify-center gap-2"
            >
              {isLoading ? '登录中...' : '立即登录'}
            </button>
          </form>

          {/* 提示信息 */}
          <div className="mt-6 pt-5 border-t border-slate-100 flex items-start gap-2.5 text-xs text-slate-500 bg-slate-50 p-3.5 rounded-2xl">
            <Info className="w-4 h-4 text-blue-500 flex-shrink-0 mt-0.5" />
            <div className="space-y-1">
              <div>
                <span className="font-semibold text-slate-700">系统管理员：</span>
                账号 <code className="bg-slate-200/80 px-1 py-0.5 rounded text-slate-800">admin</code>，初始密码 <code className="bg-slate-200/80 px-1 py-0.5 rounded text-slate-800">P@ss=w0rd</code>
              </div>
              <div>
                <span className="font-semibold text-slate-700">新教师与学生：</span>
                默认密码均为 <code className="bg-slate-200/80 px-1 py-0.5 rounded text-slate-800">passw0rd</code>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
