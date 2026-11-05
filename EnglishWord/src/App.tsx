import React from 'react';
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom';
import { useAuthStore } from './stores/useAuthStore';
import { AppLayout } from './components/common/AppLayout';
import { LoginPage } from './pages/LoginPage';
import { AdminDashboard } from './pages/admin/AdminDashboard';
import { TeacherWordList } from './pages/teacher/TeacherWordList';
import { TeacherAnalytics } from './pages/teacher/TeacherAnalytics';
import { StudentHome } from './pages/student/StudentHome';
import { StudyPage } from './pages/student/StudyPage';
import { ReviewPage } from './pages/student/ReviewPage';
import type { UserRole } from '@shared/types';

// 路由守卫高阶组件
const ProtectedRoute: React.FC<{
  children: React.ReactNode;
  allowedRoles?: UserRole[];
}> = ({ children, allowedRoles }) => {
  const { user } = useAuthStore();

  if (!user) {
    return <Navigate to="/login" replace />;
  }

  if (allowedRoles && !allowedRoles.includes(user.role)) {
    // 角色不匹配，重定向到该角色的主页面
    if (user.role === 'admin') return <Navigate to="/admin" replace />;
    if (user.role === 'teacher') return <Navigate to="/teacher/words" replace />;
    return <Navigate to="/student/home" replace />;
  }

  return <AppLayout>{children}</AppLayout>;
};

export const App: React.FC = () => {
  const { user } = useAuthStore();

  return (
    <BrowserRouter>
      <Routes>
        <Route path="/login" element={<LoginPage />} />

        {/* 根路径路由分发 */}
        <Route
          path="/"
          element={
            !user ? (
              <Navigate to="/login" replace />
            ) : user.role === 'admin' ? (
              <Navigate to="/admin" replace />
            ) : user.role === 'teacher' ? (
              <Navigate to="/teacher/words" replace />
            ) : (
              <Navigate to="/student/home" replace />
            )
          }
        />

        {/* Admin 路由 */}
        <Route
          path="/admin"
          element={
            <ProtectedRoute allowedRoles={['admin']}>
              <AdminDashboard />
            </ProtectedRoute>
          }
        />

        {/* Teacher 路由 */}
        <Route
          path="/teacher/words"
          element={
            <ProtectedRoute allowedRoles={['teacher']}>
              <TeacherWordList />
            </ProtectedRoute>
          }
        />
        <Route
          path="/teacher/analytics"
          element={
            <ProtectedRoute allowedRoles={['teacher']}>
              <TeacherAnalytics />
            </ProtectedRoute>
          }
        />

        {/* Student 路由 */}
        <Route
          path="/student"
          element={<Navigate to="/student/home" replace />}
        />
        <Route
          path="/student/home"
          element={
            <ProtectedRoute allowedRoles={['student']}>
              <StudentHome />
            </ProtectedRoute>
          }
        />
        <Route
          path="/student/study"
          element={
            <ProtectedRoute allowedRoles={['student']}>
              <StudyPage />
            </ProtectedRoute>
          }
        />
        <Route
          path="/student/review"
          element={
            <ProtectedRoute allowedRoles={['student']}>
              <ReviewPage />
            </ProtectedRoute>
          }
        />

        {/* 404 兜底 */}
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </BrowserRouter>
  );
};

export default App;
