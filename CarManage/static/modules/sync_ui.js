/**
 * 我的车管家 · 前端模块化子系统 (sync_ui.js)
 * 职责：PWA Service Worker 注册、云端同步状态与冲突管理 UI 模块
 */
(function (global) {
  global.AppModules = global.AppModules || {};
  global.AppModules.SyncUI = {
  registerServiceWorker() {
    if ('serviceWorker' in navigator) {
      let refreshing = false;
      navigator.serviceWorker.addEventListener('controllerchange', () => {
        if (typeof this.isUserInEntryOrModal === 'function' && this.isUserInEntryOrModal()) {
          console.log('[PWA] 检测到 Service Worker 控制器更新，当前处于录入界面，已阻止自动重载以保护输入内容');
          return;
        }
        if (!refreshing) {
          refreshing = true;
          console.log('[PWA] 检测到 Service Worker 控制器更新，自动重载页面以激活最新功能');
          window.location.reload();
        }
      });

      window.addEventListener('load', () => {
        navigator.serviceWorker.register('./sw.js')
          .then(reg => {
            console.log('[PWA] Service Worker 注册就绪，范围:', reg.scope);
            if (reg.update) {
              reg.update().catch(() => {});
            }
          })
          .catch(err => console.warn('[PWA] Service Worker 注册受限:', err));
      });
    }
  },

  async initSyncEngine() {
    if (window.CarSync) {
      CarSync.onStatusChange((status, pending) => {
        this.updateSyncBadge(status, pending);
      });
      await CarSync.init();
    }
  },

  updateSyncBadge(status, pendingCount) {
    const btn = document.getElementById('sync-status-btn');
    const dot = document.getElementById('sync-status-dot');
    const text = document.getElementById('sync-status-text');
    const modalStatus = document.getElementById('modal-network-status');
    const modalPending = document.getElementById('modal-pending-count');

    if (modalPending) modalPending.textContent = `${pendingCount} 笔待上传`;

    if (status === 'syncing') {
      if (btn) btn.className = 'inline-flex items-center space-x-1.5 px-2.5 py-1.5 rounded-lg border text-xs font-medium bg-blue-50 text-blue-700 border-blue-200 shadow-sm';
      if (dot) dot.className = 'w-2 h-2 rounded-full bg-blue-500 animate-pulse';
      if (text) text.textContent = '同步中...';
      if (modalStatus) modalStatus.innerHTML = '<span class="w-2 h-2 rounded-full bg-blue-500 animate-pulse"></span> 正在与云端双向同步';
    } else if (status === 'offline') {
      if (btn) btn.className = 'inline-flex items-center space-x-1.5 px-2.5 py-1.5 rounded-lg border text-xs font-medium bg-amber-50 text-amber-700 border-amber-200 shadow-sm';
      if (dot) dot.className = 'w-2 h-2 rounded-full bg-amber-500';
      if (text) text.textContent = pendingCount > 0 ? `离线 · ${pendingCount}笔待传` : '离线优先';
      if (modalStatus) modalStatus.innerHTML = '<span class="w-2 h-2 rounded-full bg-amber-500"></span> 离线优先模式 (本地已存，联网自动推送)';
    } else if (status === 'error') {
      if (btn) btn.className = 'inline-flex items-center space-x-1.5 px-2.5 py-1.5 rounded-lg border text-xs font-medium bg-rose-50 text-rose-700 border-rose-200 shadow-sm';
      if (dot) dot.className = 'w-2 h-2 rounded-full bg-rose-500';
      if (text) text.textContent = '云端未连接';
      const errMsg = window.CarSync && CarSync.lastError ? ` (${CarSync.lastError})` : '';
      if (modalStatus) modalStatus.innerHTML = `<span class="w-2 h-2 rounded-full bg-rose-500"></span> 暂未连接云端${errMsg}，本地离线数据可用`;
    } else {
      if (btn) btn.className = 'inline-flex items-center space-x-1.5 px-2.5 py-1.5 rounded-lg border text-xs font-medium bg-emerald-50 text-emerald-700 border-emerald-200 shadow-sm';
      if (dot) dot.className = 'w-2 h-2 rounded-full bg-emerald-500';
      if (text) text.textContent = '已同步';
      if (modalStatus) modalStatus.innerHTML = '<span class="w-2 h-2 rounded-full bg-emerald-500"></span> 在线就绪 · 数据已全量同步';
    }
  },

  async openSyncModal() {
    const modal = document.getElementById('sync-modal');
    if (modal) modal.classList.remove('hidden');
    const input = document.getElementById('cf-worker-url');
    if (input && window.CarDB) {
      const saved = await CarDB.getSetting('cloudflare_worker_url', '');
      input.value = saved || (window.CarSync ? CarSync.DEFAULT_WORKER_URL : 'https://car-tracker-api.aubreyhan.workers.dev');
    }
    if (window.CarSync) CarSync.updatePendingCount();
    await this.renderSyncConflicts();
  },

  async renderSyncConflicts() {
    const section = document.getElementById('sync-conflicts-section');
    const countEl = document.getElementById('sync-conflicts-count');
    const listEl = document.getElementById('sync-conflicts-list');
    if (!section || !listEl || !window.CarDB || !CarDB.getConflicts) return;

    const conflicts = await CarDB.getConflicts();
    if (!conflicts || conflicts.length === 0) {
      section.classList.add('hidden');
      listEl.innerHTML = '';
      if (countEl) countEl.textContent = '0';
      return;
    }

    section.classList.remove('hidden');
    if (countEl) countEl.textContent = String(conflicts.length);
    const entityLabel = {
      vehicles: '车辆档案',
      fuel: '油耗记录',
      electricity: '电耗记录',
      maintenance: '费用支出',
      status: '状态快照'
    };

    listEl.innerHTML = conflicts.map(c => {
      const label = entityLabel[c.entity] || c.entity;
      const timeStr = (c.occurred_at || '').replace('T', ' ').slice(0, 19);
      return `
        <div class="bg-white/90 rounded-lg p-2.5 border border-amber-200 flex items-center justify-between gap-2">
          <div>
            <div class="font-semibold text-slate-800">${label} #${c.record_id} · 版本 v${c.client_base_version || 1} ➔ v${c.server_version || 2}</div>
            <div class="text-[10px] text-slate-500 mt-0.5">发生于 ${timeStr} (已自动保护合并)</div>
          </div>
          <div class="flex items-center gap-1.5 shrink-0">
            <button type="button" onclick="App.resolveSyncConflict(${c.cid}, 'use_local')" class="px-2 py-1 bg-blue-50 hover:bg-blue-100 text-blue-700 rounded border border-blue-200 font-medium">用本地</button>
            <button type="button" onclick="App.resolveSyncConflict(${c.cid}, 'use_server')" class="px-2 py-1 bg-slate-100 hover:bg-slate-200 text-slate-700 rounded border border-slate-200 font-medium">用云端</button>
          </div>
        </div>
      `;
    }).join('');
  },

  async resolveSyncConflict(cid, strategy) {
    if (!window.CarSync || !CarSync.resolveConflict) return;
    const ok = await CarSync.resolveConflict(cid, strategy);
    if (ok) {
      this.showToast(strategy === 'use_server' ? '已恢复为云端版本！' : '已强制应用本地版本！', 'success');
      await this.renderSyncConflicts();
    } else {
      this.showToast('处理冲突记录失败，请检查网络连接', 'error');
    }
  },

  async clearSyncConflicts() {
    if (!window.CarDB || !CarDB.clearConflicts) return;
    await CarDB.clearConflicts();
    await this.renderSyncConflicts();
    this.showToast('冲突归档日志已清空', 'success');
  },

  async fillDefaultWorkerUrl() {
    const input = document.getElementById('cf-worker-url');
    if (input) {
      input.value = window.CarSync ? CarSync.DEFAULT_WORKER_URL : 'https://car-tracker-api.aubreyhan.workers.dev';
      await this.saveCloudflareUrl();
    }
  },

  closeSyncModal() {
    const modal = document.getElementById('sync-modal');
    if (modal) modal.classList.add('hidden');
  },

  async saveCloudflareUrl() {
    const input = document.getElementById('cf-worker-url');
    if (!input || !window.CarDB) return;
    let url = input.value.trim();
    if (url) {
      if (!url.startsWith('http://') && !url.startsWith('https://')) {
        url = 'https://' + url;
      }
      if (url.endsWith('.worker')) {
        url = url + 's.dev';
      } else if (url.endsWith('.workers')) {
        url = url + '.dev';
      }
      url = url.replace(/\/+$/, '');
      input.value = url;
    }
    await CarDB.setSetting('cloudflare_worker_url', url);
    this.showToast('Cloudflare Worker 地址已保存！', 'success');
    if (window.CarSync) CarSync.sync(false, true);
  },

  async testCloudflareConnection() {
    const input = document.getElementById('cf-worker-url');
    let url = input ? input.value.trim().replace(/\/+$/, '') : '';
    if (!url) {
      this.showToast('请先输入 Cloudflare Worker 完整地址', 'error');
      return;
    }
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      url = 'https://' + url;
    }
    if (url.endsWith('.worker')) {
      url = url + 's.dev';
    } else if (url.endsWith('.workers')) {
      url = url + '.dev';
    }
    if (input) input.value = url;
    this.showToast('正在测试云端连接...', 'info');
    try {
      const res = await fetch(`${url}/api/vehicles`, { mode: 'cors' });
      if (res.ok) {
        this.showToast('✅ Cloudflare D1 云端服务连接成功！', 'success');
      } else {
        this.showToast(`连接响应异常: HTTP ${res.status}`, 'error');
      }
    } catch (e) {
      this.showToast(`连接失败: ${e.message}`, 'error');
    }
  },

  async triggerManualSync() {
    if (window.CarSync) {
      this.showToast('正在连接云端双向同步...', 'info');
      await CarSync.sync(false, true);
      await this.renderSyncConflicts();
      if (CarSync.status === 'synced') {
        this.showToast('✅ 云端双向同步完成！', 'success');
      } else if (CarSync.status === 'error') {
        this.showToast(`❌ 同步失败: ${CarSync.lastError || '无法连接云端'}`, 'error');
      }
    }
  },

  async clearPendingQueue() {
    if (!confirm('确认清空本地待上传队列吗？（如遇队列卡死无法同步时推荐使用）')) return;
    if (window.CarSync) {
      await CarSync.clearQueue();
      this.showToast('本地待上传队列已清空！', 'success');
      if (window.CarSync) CarSync.sync(false, true);
    }
  }

  };
})(typeof window !== "undefined" ? window : globalThis);
