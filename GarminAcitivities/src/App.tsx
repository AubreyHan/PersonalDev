import { useEffect } from "react";
import { useAuthStore } from "./store/authStore";
import { useActivityStore } from "./store/activityStore";
import { useExportStore } from "./store/exportStore";
import { Activity, Download, Globe, LogOut, Settings, User } from "lucide-react";
import { LoginModal } from "./components/LoginModal";
import { FilterBar } from "./components/FilterBar";
import { ActivityTable } from "./components/ActivityTable";
import { ExportDrawer } from "./components/ExportDrawer";
import { SettingsDialog } from "./components/SettingsDialog";

export default function App() {
  const { user, isLoggedIn, checkAuth, logout } = useAuthStore();
  const { fetchActivities } = useActivityStore();
  const { isDrawerOpen, toggleDrawer, openSettings, initEventListeners, fetchSettings, summary } =
    useExportStore();

  useEffect(() => {
    checkAuth();
    fetchSettings();
  }, [checkAuth, fetchSettings]);

  useEffect(() => {
    if (isLoggedIn) {
      fetchActivities();
    }
  }, [isLoggedIn, fetchActivities]);

  useEffect(() => {
    let unlistens: (() => void)[] = [];
    initEventListeners().then((fns) => {
      unlistens = fns;
    });
    return () => {
      unlistens.forEach((fn) => fn());
    };
  }, [initEventListeners]);

  const activeDownloadCount = summary ? summary.remainingTasks : 0;

  return (
    <div className="h-screen w-screen bg-[#121212] text-gray-100 flex flex-col overflow-hidden font-sans select-none">
      {/* App Header */}
      <header className="h-14 border-b border-gray-800 px-6 flex items-center justify-between bg-[#161616] flex-shrink-0">
        <div className="flex items-center space-x-3">
          <div className="w-8 h-8 rounded-lg bg-sky-500/15 text-sky-400 border border-sky-500/25 flex items-center justify-center shadow-sm">
            <Activity className="w-4 h-4" />
          </div>
          <span className="font-bold text-sm tracking-wide text-white">
            Garmin Activity Exporter
          </span>
          {user && (
            <span className="text-[10px] px-2 py-0.5 rounded-full bg-sky-500/10 text-sky-400 border border-sky-500/25 flex items-center space-x-1 font-mono">
              <Globe className="w-2.5 h-2.5" />
              <span>{user.region === "cn" ? "中国区 (garmin.cn)" : "国际区 (garmin.com)"}</span>
            </span>
          )}
        </div>

        {/* Right Nav */}
        <div className="flex items-center space-x-3 text-xs">
          {user && (
            <div className="flex items-center space-x-1.5 px-2.5 py-1 rounded-md bg-gray-800/60 border border-gray-700/60 text-gray-200">
              <User className="w-3.5 h-3.5 text-gray-400" />
              <span className="font-medium">{user.displayName}</span>
            </div>
          )}

          {/* Export Drawer Trigger */}
          <button
            onClick={toggleDrawer}
            className={`flex items-center space-x-1.5 px-3 py-1.5 rounded-lg border transition-all ${
              isDrawerOpen
                ? "bg-sky-500/20 border-sky-500 text-sky-300"
                : "bg-gray-800/80 border-gray-700 hover:border-gray-600 text-gray-300"
            }`}
          >
            <Download className="w-3.5 h-3.5" />
            <span>导出队列</span>
            {activeDownloadCount > 0 && (
              <span className="ml-1 w-4 h-4 rounded-full bg-sky-500 text-white text-[10px] font-bold flex items-center justify-center animate-pulse">
                {activeDownloadCount}
              </span>
            )}
          </button>

          {/* Settings Trigger */}
          <button
            onClick={openSettings}
            className="p-2 rounded-lg bg-gray-800/80 border border-gray-700 hover:border-gray-600 text-gray-300 hover:text-white transition-colors"
            title="偏好设置"
          >
            <Settings className="w-4 h-4" />
          </button>

          {/* Logout */}
          {user && (
            <button
              onClick={() => logout()}
              className="p-2 rounded-lg bg-gray-800/80 border border-gray-700 hover:border-red-900/50 hover:bg-red-950/30 text-gray-400 hover:text-red-400 transition-colors"
              title="退出登录"
            >
              <LogOut className="w-4 h-4" />
            </button>
          )}
        </div>
      </header>

      {/* Main Table & Filter */}
      {isLoggedIn ? (
        <div className="flex-1 flex flex-col overflow-hidden">
          <FilterBar />
          <ActivityTable />
        </div>
      ) : (
        <div className="flex-1 flex items-center justify-center p-8 text-center">
          <div className="max-w-md space-y-4">
            <div className="w-16 h-16 rounded-2xl bg-sky-500/10 text-sky-400 mx-auto flex items-center justify-center border border-sky-500/20">
              <Activity className="w-8 h-8" />
            </div>
            <h2 className="text-xl font-bold text-white">等待登录 Garmin 账号</h2>
            <p className="text-xs text-gray-400">
              请在登录弹窗中输入您的 Garmin Connect 账号凭据或验证码，系统将通过钥匙串自动保存会话。
            </p>
          </div>
        </div>
      )}

      {/* Modals & Drawers */}
      <LoginModal />
      <ExportDrawer />
      <SettingsDialog />
    </div>
  );
}
