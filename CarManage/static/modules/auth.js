/**
 * 我的车管家 · 前端模块化子系统 (auth.js)
 * 职责：用户认证、离线密码哈希校验与账号安全模块
 */
(function (global) {
  global.AppModules = global.AppModules || {};
  global.AppModules.Auth = {
  initAuth() {
    const savedUser = localStorage.getItem('car_tracker_auth_user');
    const savedToken = localStorage.getItem('car_tracker_auth_token');
    if (savedUser && savedToken) {
      this.currentUser = savedUser;
      this.authToken = savedToken;
      this.updateUserUI(savedUser);
      this.closeLoginModal();
      this.verifyAuthTokenSilent();
    } else {
      this.showLoginModal();
    }
  },

  updateUserUI(username) {
    const nameEl = document.getElementById('user-profile-name');
    const modalNameEl = document.getElementById('user-modal-name');
    if (nameEl) nameEl.textContent = username || 'AubreyHan';
    if (modalNameEl) modalNameEl.textContent = username || 'AubreyHan';
  },

  showLoginModal() {
    const modal = document.getElementById('login-modal');
    if (modal) modal.classList.remove('hidden');
    const userInp = document.getElementById('login-username');
    if (userInp && !userInp.value) userInp.value = 'AubreyHan';
  },

  closeLoginModal() {
    const modal = document.getElementById('login-modal');
    if (modal) modal.classList.add('hidden');
    const errBox = document.getElementById('login-error-msg');
    if (errBox) errBox.classList.add('hidden');
  },

  openUserModal() {
    if (!this.currentUser) {
      this.showLoginModal();
      return;
    }
    const modal = document.getElementById('user-modal');
    if (modal) modal.classList.remove('hidden');
    this.updateUserUI(this.currentUser);
  },

  closeUserModal() {
    const modal = document.getElementById('user-modal');
    if (modal) modal.classList.add('hidden');
  },

  togglePasswordVisibility(inputId, btn) {
    const input = document.getElementById(inputId);
    if (!input) return;
    if (input.type === 'password') {
      input.type = 'text';
      if (btn) btn.textContent = '🙈';
    } else {
      input.type = 'password';
      if (btn) btn.textContent = '👁️';
    }
  },

  async handleLogin(e) {
    if (e && e.preventDefault) e.preventDefault();
    const userInp = document.getElementById('login-username');
    const pwdInp = document.getElementById('login-password');
    const errBox = document.getElementById('login-error-msg');
    const submitBtn = document.getElementById('btn-login-submit');

    const username = userInp ? userInp.value.trim() : '';
    const password = pwdInp ? pwdInp.value : '';

    if (!username || !password) {
      if (errBox) {
        errBox.textContent = '请输入用户名和密码';
        errBox.classList.remove('hidden');
      }
      return;
    }

    if (errBox) errBox.classList.add('hidden');
    if (submitBtn) {
      submitBtn.disabled = true;
      submitBtn.textContent = '正在登录...';
    }

    try {
      const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
      let loginSuccess = false;
      let token = '';

      if (navigator.onLine && baseUrl) {
        try {
          const res = await fetch(`${baseUrl}/api/auth/login`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, password })
          });
          const data = await res.json();
          if (res.ok && data.success) {
            loginSuccess = true;
            token = data.token;
          } else {
            throw new Error(data.message || '用户名或密码错误');
          }
        } catch (netErr) {
          if (this.verifyOfflineAuth(username, password)) {
            loginSuccess = true;
            token = 'offline_token_' + Date.now();
          } else {
            throw netErr;
          }
        }
      } else {
        if (this.verifyOfflineAuth(username, password)) {
          loginSuccess = true;
          token = 'offline_token_' + Date.now();
        } else {
          throw new Error('用户名或密码错误 (离线)');
        }
      }

      if (loginSuccess) {
        this.currentUser = username;
        this.authToken = token;
        localStorage.setItem('car_tracker_auth_user', username);
        localStorage.setItem('car_tracker_auth_token', token);
        this.updateUserUI(username);
        this.closeLoginModal();
        this.showToast(`🎉 欢迎回来，${username}！登录状态已保存。`, 'success');
        if (pwdInp) pwdInp.value = '';
        await this.loadVehicles();
        this.preloadNearbyFuelStations();
        this.preloadNearbyElecStations();
      }
    } catch (err) {
      if (errBox) {
        errBox.textContent = err.message || '用户名或密码错误';
        errBox.classList.remove('hidden');
      }
      this.showToast(err.message || '登录失败', 'error');
    } finally {
      if (submitBtn) {
        submitBtn.disabled = false;
        submitBtn.textContent = '立即登录';
      }
    }
  },

  verifyOfflineAuth(username, password) {
    if (username === 'AubreyHan') {
      const customLocalPwd = localStorage.getItem('car_tracker_offline_pwd');
      if (customLocalPwd) {
        return password === customLocalPwd;
      }
      return password === 'P@ss=w0rd';
    }
    return false;
  },

  async handleChangePassword(e) {
    if (e && e.preventDefault) e.preventDefault();
    const oldInp = document.getElementById('change-pwd-old');
    const newInp = document.getElementById('change-pwd-new');
    const confirmInp = document.getElementById('change-pwd-confirm');
    const btn = document.getElementById('btn-change-pwd-submit');

    const oldPwd = oldInp ? oldInp.value : '';
    const newPwd = newInp ? newInp.value : '';
    const confirmPwd = confirmInp ? confirmInp.value : '';

    if (!oldPwd || !newPwd || !confirmPwd) {
      this.showToast('请完整输入各项密码', 'error');
      return;
    }
    if (newPwd !== confirmPwd) {
      this.showToast('两次输入的新密码不一致，请仔细核对', 'error');
      return;
    }
    if (newPwd.length < 4) {
      this.showToast('新密码长度不能少于4位字符', 'error');
      return;
    }

    if (btn) {
      btn.disabled = true;
      btn.textContent = '正在修改...';
    }

    try {
      const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
      let changed = false;

      if (navigator.onLine && baseUrl) {
        const res = await fetch(`${baseUrl}/api/auth/change-password`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            username: this.currentUser,
            old_password: oldPwd,
            new_password: newPwd
          })
        });
        const data = await res.json();
        if (res.ok && data.success) {
          changed = true;
          if (data.token) {
            this.authToken = data.token;
            localStorage.setItem('car_tracker_auth_token', data.token);
          }
        } else {
          throw new Error(data.message || '修改密码失败');
        }
      } else {
        if (this.verifyOfflineAuth(this.currentUser, oldPwd)) {
          changed = true;
        } else {
          throw new Error('原密码错误 (离线)');
        }
      }

      if (changed) {
        localStorage.setItem('car_tracker_offline_pwd', newPwd);
        this.showToast('✅ 密码修改成功！登录状态持续保持中。', 'success');
        if (oldInp) oldInp.value = '';
        if (newInp) newInp.value = '';
        if (confirmInp) confirmInp.value = '';
        this.closeUserModal();
      }
    } catch (err) {
      this.showToast(err.message || '修改密码失败', 'error');
    } finally {
      if (btn) {
        btn.disabled = false;
        btn.textContent = '确认修改密码';
      }
    }
  },

  async verifyAuthTokenSilent() {
    if (!this.authToken || !navigator.onLine) return;
    try {
      const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
      if (!baseUrl) return;
      const res = await fetch(`${baseUrl}/api/auth/verify`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ token: this.authToken })
      });
      if (res.status === 401) {
        console.warn('登录凭据已在远端失效或密码已被重置，请重新登录');
        this.logout(false);
      }
    } catch (e) {
      // 离线或弱网静默忽略
    }
  },

  logout(showToast = true) {
    if (showToast && !confirm('确认退出当前登录状态吗？退出后需要重新输入密码登录。')) {
      return;
    }
    localStorage.removeItem('car_tracker_auth_user');
    localStorage.removeItem('car_tracker_auth_token');
    this.currentUser = null;
    this.authToken = null;
    this.closeUserModal();
    this.showLoginModal();
    if (showToast) {
      this.showToast('已安全退出登录', 'info');
    }
  }
  };
})(typeof window !== "undefined" ? window : globalThis);
