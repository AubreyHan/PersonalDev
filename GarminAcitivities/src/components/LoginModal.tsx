import React, { useState } from "react";
import { useAuthStore } from "../store/authStore";
import { GarminRegion } from "../types";
import { ExternalLink, Globe, Key, KeyRound, Lock, Mail, ShieldAlert, Sparkles } from "lucide-react";

export const LoginModal: React.FC = () => {
  const {
    isLoggedIn,
    isLoading,
    mfaRequired,
    error,
    login,
    openWebLogin,
    loginWithToken,
    submitMfa,
    clearError,
  } = useAuthStore();

  const [region, setRegion] = useState<GarminRegion>("global");
  const [loginMode, setLoginMode] = useState<"direct" | "token">("direct");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [rememberMe, setRememberMe] = useState(true);
  const [cookieToken, setCookieToken] = useState("");
  const [mfaCode, setMfaCode] = useState("");

  if (isLoggedIn) return null;

  const handleDirectSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!email || !password) return;
    await login(region, email, password, rememberMe);
  };

  const handleTokenSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!cookieToken.trim()) return;
    await loginWithToken(region, cookieToken.trim());
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
        <div className="text-center mb-5">
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

        {/* Region Switcher */}
        {!mfaRequired && (
          <div className="mb-4">
            <label className="block text-xs font-medium text-gray-300 mb-1.5">选择服务区域</label>
            <div className="grid grid-cols-2 gap-2">
              <button
                type="button"
                onClick={() => setRegion("cn")}
                className={`p-2.5 rounded-lg border text-left flex items-center justify-between transition-all ${
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
                className={`p-2.5 rounded-lg border text-left flex items-center justify-between transition-all ${
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
        )}

        {/* Recommended Official Webview Login Button (Immune to Cloudflare Turnstile) */}
        {!mfaRequired && (
          <div className="mb-4 p-3 rounded-xl bg-gradient-to-r from-sky-950/40 to-blue-950/30 border border-sky-500/30">
            <div className="text-xs font-semibold text-sky-300 mb-1 flex items-center justify-between">
              <span>🌟 推荐方式：官方安全窗口登录</span>
              <span className="text-[10px] px-1.5 py-0.5 rounded bg-sky-500/20 text-sky-300 font-normal">
                免人机验证拦截
              </span>
            </div>
            <p className="text-[11px] text-gray-400 mb-2.5 leading-relaxed">
              针对 Garmin Cloudflare Turnstile 反爬风控，通过原生内置安全窗口加载官方页面，支持人机验证与 1Password 自动填充。
            </p>
            <button
              type="button"
              onClick={() => openWebLogin(region)}
              disabled={isLoading}
              className="w-full py-2 px-3 bg-sky-500 hover:bg-sky-400 text-white font-medium text-xs rounded-lg shadow-md shadow-sky-500/20 flex items-center justify-center space-x-1.5 transition-all cursor-pointer"
            >
              <ExternalLink className="w-3.5 h-3.5" />
              <span>打开 Garmin 官方安全登录窗口</span>
            </button>
          </div>
        )}

        {/* Error Alert */}
        {error && (
          <div className="mb-4 p-3 bg-red-950/40 border border-red-800/50 rounded-lg flex flex-col space-y-2 text-xs text-red-300">
            <div className="flex items-start space-x-2">
              <ShieldAlert className="w-4 h-4 flex-shrink-0 mt-0.5 text-red-400" />
              <div className="flex-1">
                {error.includes("captcha") || error.includes("turnstile")
                  ? "Garmin 官方触发了 Cloudflare 人机滑块验证，请直接点击上方【官方安全登录窗口】完成认证。"
                  : error}
              </div>
              <button onClick={clearError} className="text-gray-400 hover:text-white">✕</button>
            </div>
            {(error.includes("captcha") || error.includes("turnstile")) && (
              <button
                type="button"
                onClick={() => openWebLogin(region)}
                className="w-full py-1.5 bg-sky-500/20 hover:bg-sky-500/30 border border-sky-500/40 text-sky-300 rounded font-medium text-xs transition-colors"
              >
                立即打开官方安全窗口认证
              </button>
            )}
          </div>
        )}

        {/* Mode Tabs: Direct Password vs Token/Cookie */}
        {!mfaRequired && (
          <div className="flex items-center space-x-2 border-b border-gray-800 mb-3 text-xs">
            <button
              type="button"
              onClick={() => setLoginMode("direct")}
              className={`pb-2 px-1 font-medium transition-colors border-b-2 -mb-px ${
                loginMode === "direct"
                  ? "border-sky-500 text-white"
                  : "border-transparent text-gray-500 hover:text-gray-300"
              }`}
            >
              账号密码直接登录
            </button>
            <button
              type="button"
              onClick={() => setLoginMode("token")}
              className={`pb-2 px-1 font-medium transition-colors border-b-2 -mb-px ${
                loginMode === "token"
                  ? "border-sky-500 text-white"
                  : "border-transparent text-gray-500 hover:text-gray-300"
              }`}
            >
              Cookie / Token 登录 (类似 API Key)
            </button>
          </div>
        )}

        {/* Form Body */}
        {!mfaRequired ? (
          loginMode === "direct" ? (
            <form onSubmit={handleDirectSubmit} className="space-y-3">
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
                    className="w-full bg-[#161616] border border-gray-800 rounded-lg pl-9 pr-3 py-2 text-xs text-white placeholder-gray-600 focus:outline-none focus:border-sky-500"
                  />
                </div>
              </div>

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
                    className="w-full bg-[#161616] border border-gray-800 rounded-lg pl-9 pr-3 py-2 text-xs text-white placeholder-gray-600 focus:outline-none focus:border-sky-500"
                  />
                </div>
              </div>

              <div className="flex items-center space-x-2 pt-1">
                <input
                  id="rememberMe"
                  type="checkbox"
                  checked={rememberMe}
                  onChange={(e) => setRememberMe(e.target.checked)}
                  className="rounded border-gray-700 bg-gray-900 text-sky-500 focus:ring-0 cursor-pointer"
                />
                <label htmlFor="rememberMe" className="text-[11px] text-gray-400 cursor-pointer select-none">
                  记住登录凭据（存入系统钥匙串）
                </label>
              </div>

              <button
                type="submit"
                disabled={isLoading || !email || !password}
                className="w-full py-2 px-4 bg-gray-800 hover:bg-gray-700 disabled:opacity-50 text-white font-medium text-xs rounded-lg transition-colors cursor-pointer"
              >
                {isLoading ? "正在提交..." : "直接提交账号密码"}
              </button>
            </form>
          ) : (
            <form onSubmit={handleTokenSubmit} className="space-y-3">
              <div>
                <label className="block text-xs font-medium text-gray-300 mb-1 flex items-center justify-between">
                  <span>粘贴浏览器 Cookie 或 Session Token</span>
                  <Key className="w-3.5 h-3.5 text-sky-400" />
                </label>
                <textarea
                  rows={4}
                  required
                  placeholder="从网页版 Garmin Connect 控制台请求中复制 Cookie 字符串 (如 JWT_USER_EXPIRATION=...; GARMIN-SSO-GUID=...)"
                  value={cookieToken}
                  onChange={(e) => setCookieToken(e.target.value)}
                  className="w-full bg-[#161616] border border-gray-800 rounded-lg p-2.5 text-xs text-white placeholder-gray-600 focus:outline-none focus:border-sky-500 font-mono resize-none"
                />
                <p className="text-[10px] text-gray-500 mt-1">
                  该方式绕过所有 SSO 和人机挑战，直接建立长效会话并加密存入系统钥匙串。
                </p>
              </div>

              <button
                type="submit"
                disabled={isLoading || !cookieToken.trim()}
                className="w-full py-2 px-4 bg-sky-500 hover:bg-sky-400 disabled:opacity-50 text-white font-medium text-xs rounded-lg shadow-md shadow-sky-500/20 transition-all cursor-pointer"
              >
                {isLoading ? "正在验证 Token..." : "确认并建立会话"}
              </button>
            </form>
          )
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
                className="w-full bg-[#161616] border border-gray-800 rounded-lg px-4 py-3 text-center text-2xl tracking-[0.5em] font-mono text-white placeholder-gray-700 focus:outline-none focus:border-sky-500"
              />
            </div>

            <button
              type="submit"
              disabled={isLoading || mfaCode.length < 6}
              className="w-full py-2.5 px-4 bg-sky-500 hover:bg-sky-400 disabled:opacity-50 text-white font-medium text-sm rounded-lg shadow-lg shadow-sky-500/20 transition-all"
            >
              {isLoading ? "正在验证..." : "确认并完成认证"}
            </button>
          </form>
        )}
      </div>
    </div>
  );
};
