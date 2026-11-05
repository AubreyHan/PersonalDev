import React, { useState, useEffect } from 'react';
import { useAuthStore } from '../../stores/useAuthStore';
import { ChangePasswordModal } from './ChangePasswordModal';
import {
  BookOpen,
  LogOut,
  KeyRound,
  Wifi,
  WifiOff,
  User as UserIcon,
  GraduationCap,
  Sparkles,
  RotateCcw,
  Users,
  Library,
  BarChart3,
} from 'lucide-react';
import { Link, useLocation } from 'react-router-dom';

export const AppLayout: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const { user, logout } = useAuthStore();
  const location = useLocation();
  const [isOnline, setIsOnline] = useState(navigator.onLine);
  const [isPwdModalOpen, setIsPwdModalOpen] = useState(false);

  useEffect(() => {
    const handleOnline = () => setIsOnline(true);
    const handleOffline = () => setIsOnline(false);
    window.addEventListener('online', handleOnline);
    window.addEventListener('offline', handleOffline);
    return () => {
      window.removeEventListener('online', handleOnline);
      window.removeEventListener('offline', handleOffline);
    };
  }, []);

  if (!user) return <>{children}</>;

  const roleNameMap = {
    admin: { label: '管理员', bg: 'bg-purple-100 text-purple-700 border-purple-200' },
    teacher: { label: '教师', bg: 'bg-blue-100 text-blue-700 border-blue-200' },
    student: { label: '学生', bg: 'bg-emerald-100 text-emerald-700 border-emerald-200' },
  };

  const currentRole = roleNameMap[user.role] || { label: user.role, bg: 'bg-slate-100 text-slate-700' };

  return (
    <div className="min-h-screen bg-slate-50 flex flex-col font-sans">
      {/* 顶部现代化导航栏 */}
      <header className="sticky top-0 z-40 bg-white/80 backdrop-blur-md border-b border-slate-200/80 px-4 sm:px-6">
        <div className="max-w-7xl mx-auto h-16 flex items-center justify-between gap-4">
          {/* Logo 与主标题 */}
          <Link to="/" className="flex items-center gap-2.5 group">
            <div className="w-10 h-10 rounded-xl bg-gradient-to-tr from-blue-600 to-indigo-500 flex items-center justify-center text-white shadow-md shadow-blue-500/20 group-hover:scale-105 transition-transform">
              <BookOpen className="w-5 h-5" />
            </div>
            <div>
              <span className="font-bold text-lg text-slate-900 tracking-tight flex items-center gap-1.5">
                EnglishWord
                <span className="text-[10px] uppercase font-bold tracking-wider px-1.5 py-0.5 rounded-full bg-blue-50 text-blue-600 border border-blue-200/60">
                  PWA
                </span>
              </span>
              <span className="block text-[11px] text-slate-500 -mt-0.5 hidden sm:block">
                离线优先 · 智能精学与艾宾浩斯复习
              </span>
            </div>
          </Link>

          {/* 角色专属导航菜单 */}
          <nav className="flex items-center gap-1 sm:gap-2">
            {user.role === 'admin' && (
              <Link
                to="/admin"
                className={`flex items-center gap-1.5 px-3 py-1.5 rounded-xl text-sm font-medium transition-colors ${
                  location.pathname === '/admin'
                    ? 'bg-blue-50 text-blue-600 font-semibold'
                    : 'text-slate-600 hover:text-slate-900 hover:bg-slate-100'
                }`}
              >
                <Users className="w-4 h-4" />
                <span>教师与学生管理</span>
              </Link>
            )}

            {user.role === 'teacher' && (
              <>
                <Link
                  to="/teacher/words"
                  className={`flex items-center gap-1.5 px-3 py-1.5 rounded-xl text-sm font-medium transition-colors ${
                    location.pathname.startsWith('/teacher/words')
                      ? 'bg-blue-50 text-blue-600 font-semibold'
                      : 'text-slate-600 hover:text-slate-900 hover:bg-slate-100'
                  }`}
                >
                  <Library className="w-4 h-4" />
                  <span>单词词库管理</span>
                </Link>
                <Link
                  to="/teacher/analytics"
                  className={`flex items-center gap-1.5 px-3 py-1.5 rounded-xl text-sm font-medium transition-colors ${
                    location.pathname.startsWith('/teacher/analytics')
                      ? 'bg-blue-50 text-blue-600 font-semibold'
                      : 'text-slate-600 hover:text-slate-900 hover:bg-slate-100'
                  }`}
                >
                  <BarChart3 className="w-4 h-4" />
                  <span>学生学情与错词榜</span>
                </Link>
              </>
            )}

            {user.role === 'student' && (
              <>
                <Link
                  to="/student/study"
                  className={`flex items-center gap-1.5 px-3 py-1.5 rounded-xl text-sm font-medium transition-colors ${
                    location.pathname.startsWith('/student/study')
                      ? 'bg-blue-50 text-blue-600 font-semibold'
                      : 'text-slate-600 hover:text-slate-900 hover:bg-slate-100'
                  }`}
                >
                  <Sparkles className="w-4 h-4" />
                  <span>单词学习</span>
                </Link>
                <Link
                  to="/student/review"
                  className={`flex items-center gap-1.5 px-3 py-1.5 rounded-xl text-sm font-medium transition-colors ${
                    location.pathname.startsWith('/student/review')
                      ? 'bg-blue-50 text-blue-600 font-semibold'
                      : 'text-slate-600 hover:text-slate-900 hover:bg-slate-100'
                  }`}
                >
                  <RotateCcw className="w-4 h-4" />
                  <span>艾宾浩斯复习</span>
                </Link>
              </>
            )}
          </nav>

          {/* 右侧：离线徽标 + 用户信息 + 操作 */}
          <div className="flex items-center gap-3">
            {/* 在线/离线指示器 */}
            <div
              className={`flex items-center gap-1 text-xs px-2 py-1 rounded-full border ${
                isOnline
                  ? 'bg-emerald-50 text-emerald-700 border-emerald-200'
                  : 'bg-amber-50 text-amber-700 border-amber-200 animate-pulse'
              }`}
              title={isOnline ? '云端在线同步已连接' : '当前处于离线模式，本地优先读写'}
            >
              {isOnline ? <Wifi className="w-3.5 h-3.5" /> : <WifiOff className="w-3.5 h-3.5" />}
              <span className="hidden md:inline">{isOnline ? '在线' : '离线模式'}</span>
            </div>

            {/* 用户角色与名称 */}
            <div className="flex items-center gap-2 pl-2 border-l border-slate-200">
              <div className="text-right hidden sm:block">
                <div className="text-sm font-medium text-slate-800 leading-tight">
                  {user.displayName || user.username}
                </div>
                <div className="text-[11px] text-slate-400">@{user.username}</div>
              </div>
              <span
                className={`text-[11px] font-semibold px-2 py-0.5 rounded-full border ${currentRole.bg}`}
              >
                {currentRole.label}
              </span>

              {/* 修改密码按钮 */}
              <button
                onClick={() => setIsPwdModalOpen(true)}
                title="修改密码"
                className="p-1.5 rounded-xl text-slate-400 hover:text-slate-700 hover:bg-slate-100 transition-colors"
              >
                <KeyRound className="w-4 h-4" />
              </button>

              {/* 退出登录 */}
              <button
                onClick={logout}
                title="退出登录"
                className="p-1.5 rounded-xl text-slate-400 hover:text-red-600 hover:bg-red-50 transition-colors"
              >
                <LogOut className="w-4 h-4" />
              </button>
            </div>
          </div>
        </div>
      </header>

      {/* 主体页面内容 */}
      <main className="flex-1 max-w-7xl w-full mx-auto p-4 sm:p-6 lg:p-8">
        {children}
      </main>

      {/* 密码修改模态框 */}
      <ChangePasswordModal isOpen={isPwdModalOpen} onClose={() => setIsPwdModalOpen(false)} />
    </div>
  );
};
