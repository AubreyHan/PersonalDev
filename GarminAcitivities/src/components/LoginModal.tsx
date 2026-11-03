import React, { useState } from "react";
import { useAuthStore } from "../store/authStore";
import { GarminRegion } from "../types";
import { Globe, Lock, Mail, ShieldAlert, Sparkles, KeyRound } from "lucide-react";

export const LoginModal: React.FC = () => {
  const { isLoggedIn, isLoading, mfaRequired, error, login, submitMfa, clearError } = useAuthStore();

  const [region, setRegion] = useState<GarminRegion>("cn");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [rememberMe, setRememberMe] = useState(true);
  const [mfaCode, setMfaCode] = useState("");

  if (isLoggedIn) return null;

  const handleLoginSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!email || !password) return;
    await login(region, email, password, rememberMe);
  };

  const handleMfaSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!mfaCode || mfaCode.length < 6) return;
    await submitMfa(mfaCode);
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 backdrop-blur-sm p-4">
      <div className="w-full max-w-md bg-[#1e1e1e] border border-gray-800 rounded-2xl shadow-2xl p-6 text-gray-100 animate-in fade-in zoom-in-95 duration-200">
        {/* Header */}
        <div className="text-center mb-6">
          <div className="w-12 h-12 bg-sky-500/10 border border-sky-500/20 text-sky-400 rounded-xl mx-auto flex items-center justify-center mb-3">
            {mfaRequired ? <KeyRound className="w-6 h-6" /> : <Sparkles className="w-6 h-6" />}
          </div>
          <h2 className="text-xl font-bold text-white tracking-tight">
            {mfaRequired ? "输入二次验证码 (MFA)" : "连接 Garmin 账号"}
          </h2>
          <p className="text-xs text-gray-400 mt-1">
            {mfaRequired
              ? "已向您的关联设备或邮箱发送 6 位验证码，请输入以完成登录"
              : "凭据直接与 Garmin 官方 SSO 通信，经由系统安全钥匙串加密"}
          </p>
        </div>

        {/* Error Alert */}
        {error && (
          <div className="mb-4 p-3 bg-red-950/40 border border-red-800/50 rounded-lg flex items-start space-x-2 text-xs text-red-300">
            <ShieldAlert className="w-4 h-4 flex-shrink-0 mt-0.5 text-red-400" />
            <div className="flex-1">{error}</div>
            <button onClick={clearError} className="text-gray-400 hover:text-white">✕</button>
          </div>
        )}

        {/* Form */}
        {!mfaRequired ? (
          <form onSubmit={handleLoginSubmit} className="space-y-4">
            {/* Region Switcher */}
            <div>
              <label className="block text-xs font-medium text-gray-300 mb-1.5">选择服务区域</label>
              <div className="grid grid-cols-2 gap-2">
                <button
                  type="button"
                  onClick={() => setRegion("cn")}
                  className={`p-3 rounded-lg border text-left flex items-center justify-between transition-all ${
                    region === "cn"
                      ? "bg-sky-500/15 border-sky-500 text-white shadow-sm shadow-sky-500/10"
                      : "bg-[#252525] border-gray-800 text-gray-400 hover:border-gray-700"
                  }`}
                >
                  <div>
                    <div className="text-xs font-semibold text-white">中国区 (China)</div>
                    <div className="text-[10px] text-gray-400">connect.garmin.cn</div>
                  </div>
                  <Globe className={`w-4 h-4 ${region === "cn" ? "text-sky-400" : "text-gray-600"}`} />
                </button>

                <button
                  type="button"
                  onClick={() => setRegion("global")}
                  className={`p-3 rounded-lg border text-left flex items-center justify-between transition-all ${
                    region === "global"
                      ? "bg-sky-500/15 border-sky-500 text-white shadow-sm shadow-sky-500/10"
                      : "bg-[#252525] border-gray-800 text-gray-400 hover:border-gray-700"
                  }`}
                >
                  <div>
                    <div className="text-xs font-semibold text-white">国际区 (Global)</div>
                    <div className="text-[10px] text-gray-400">connect.garmin.com</div>
                  </div>
                  <Globe className={`w-4 h-4 ${region === "global" ? "text-sky-400" : "text-gray-600"}`} />
                </button>
              </div>
            </div>

            {/* Email */}
            <div>
              <label className="block text-xs font-medium text-gray-300 mb-1">账号邮箱</label>
              <div className="relative">
                <Mail className="w-4 h-4 text-gray-500 absolute left-3 top-2.5" />
                <input
                  type="email"
                  required
                  placeholder="runner@example.com"
                  value={email}
                  onChange={(e) => setEmail(e.target.value)}
                  className="w-full bg-[#161616] border border-gray-800 rounded-lg pl-9 pr-3 py-2 text-sm text-white placeholder-gray-600 focus:outline-none focus:border-sky-500 transition-colors"
                />
              </div>
            </div>

            {/* Password */}
            <div>
              <label className="block text-xs font-medium text-gray-300 mb-1">密码</label>
              <div className="relative">
                <Lock className="w-4 h-4 text-gray-500 absolute left-3 top-2.5" />
                <input
                  type="password"
                  required
                  placeholder="••••••••"
                  value={password}
                  onChange={(e) => setPassword(e.target.value)}
                  className="w-full bg-[#161616] border border-gray-800 rounded-lg pl-9 pr-3 py-2 text-sm text-white placeholder-gray-600 focus:outline-none focus:border-sky-500 transition-colors"
                />
              </div>
            </div>

            {/* Remember me */}
            <div className="flex items-center space-x-2 pt-1">
              <input
                id="rememberMe"
                type="checkbox"
                checked={rememberMe}
                onChange={(e) => setRememberMe(e.target.checked)}
                className="rounded border-gray-700 bg-gray-900 text-sky-500 focus:ring-0 focus:ring-offset-0 cursor-pointer"
              />
              <label htmlFor="rememberMe" className="text-xs text-gray-400 cursor-pointer select-none">
                记住登录凭据（存入系统钥匙串，自动免密续约）
              </label>
            </div>

            {/* Submit */}
            <button
              type="submit"
              disabled={isLoading || !email || !password}
              className="w-full py-2.5 px-4 bg-sky-500 hover:bg-sky-400 disabled:opacity-50 text-white font-medium text-sm rounded-lg shadow-lg shadow-sky-500/20 transition-all flex items-center justify-center space-x-2 cursor-pointer disabled:cursor-not-allowed"
            >
              {isLoading ? (
                <>
                  <div className="w-4 h-4 border-2 border-white/30 border-t-white rounded-full animate-spin" />
                  <span>正在登录并握手...</span>
                </>
              ) : (
                <span>立即登录</span>
              )}
            </button>
          </form>
        ) : (
          <form onSubmit={handleMfaSubmit} className="space-y-4">
            <div>
              <label className="block text-xs font-medium text-gray-300 mb-2">6 位数字验证码</label>
              <input
                type="text"
                maxLength={6}
                autoFocus
                placeholder="000000"
                value={mfaCode}
                onChange={(e) => setMfaCode(e.target.value.replace(/\D/g, ""))}
                className="w-full bg-[#161616] border border-gray-800 rounded-lg px-4 py-3 text-center text-2xl tracking-[0.5em] font-mono text-white placeholder-gray-700 focus:outline-none focus:border-sky-500 transition-colors"
              />
            </div>

            <button
              type="submit"
              disabled={isLoading || mfaCode.length < 6}
              className="w-full py-2.5 px-4 bg-sky-500 hover:bg-sky-400 disabled:opacity-50 text-white font-medium text-sm rounded-lg shadow-lg shadow-sky-500/20 transition-all flex items-center justify-center space-x-2 cursor-pointer disabled:cursor-not-allowed"
            >
              {isLoading ? (
                <>
                  <div className="w-4 h-4 border-2 border-white/30 border-t-white rounded-full animate-spin" />
                  <span>正在验证...</span>
                </>
              ) : (
                <span>确认并完成认证</span>
              )}
            </button>
          </form>
        )}
      </div>
    </div>
  );
};
