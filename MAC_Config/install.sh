#!/usr/bin/env bash
# ==============================================================================
# macOS Homebrew 及应用自动化安装脚本
# 用法:
#   ./install.sh              # 安装 Homebrew、配置环境变量并安装 Brewfile 中的应用
#   ./install.sh --brew-only  # 仅安装 Homebrew 本体与配置环境变量
#   ./install.sh --apps-only  # 仅根据 Brewfile 安装/更新软件包与应用
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BREWFILE_PATH="${SCRIPT_DIR}/Brewfile"

# 颜色输出定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

info()    { echo -e "${BLUE}[INFO]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# 清理临时文件
ASKPASS_SCRIPT=""
BREW_INSTALLER_SCRIPT=""
cleanup() {
  if [[ -n "${ASKPASS_SCRIPT}" && -f "${ASKPASS_SCRIPT}" ]]; then
    rm -f "${ASKPASS_SCRIPT}"
  fi
  if [[ -n "${BREW_INSTALLER_SCRIPT}" && -f "${BREW_INSTALLER_SCRIPT}" ]]; then
    rm -f "${BREW_INSTALLER_SCRIPT}"
  fi
}
trap cleanup EXIT

# 确定架构对应的 Homebrew 路径
ARCH="$(uname -m)"
if [[ "${ARCH}" == "arm64" ]]; then
  BREW_PREFIX="/opt/homebrew"
else
  BREW_PREFIX="/usr/local"
fi
BREW_BIN="${BREW_PREFIX}/bin/brew"

# 配置 macOS 原生弹窗密码授权 (SUDO_ASKPASS)
setup_sudo_askpass_if_needed() {
  if /usr/bin/sudo -n true 2>/dev/null; then
    return 0
  fi

  if [[ -z "${SUDO_ASKPASS-}" || ! -x "${SUDO_ASKPASS-}" ]]; then
    ASKPASS_SCRIPT="$(mktemp -t brew_askpass.XXXXXX)"
    cat > "${ASKPASS_SCRIPT}" <<'EOF'
#!/usr/bin/env bash
osascript -e 'text returned of (display dialog "安装 Homebrew 需要管理员权限，请输入您的 Mac 登录密码：" default answer "" with hidden answer with title "Homebrew 自动安装授权" buttons {"取消", "好"} default button "好")' 2>/dev/null
EOF
    chmod 700 "${ASKPASS_SCRIPT}"
    export SUDO_ASKPASS="${ASKPASS_SCRIPT}"
  fi
  export NONINTERACTIVE=1
  info "将在需要管理员权限时弹出 macOS 系统密码授权窗口，请在弹窗中输入开机密码..."
}

# 1. 安装 Homebrew
install_homebrew() {
  if [[ -x "${BREW_BIN}" ]] || command -v brew >/dev/null 2>&1; then
    success "检测到 Homebrew 已安装: $(command -v brew || echo "${BREW_BIN}")"
  else
    info "未检测到 Homebrew，开始从 GitHub 官方源自动安装..."
    setup_sudo_askpass_if_needed

    BREW_INSTALLER_SCRIPT="$(mktemp -t brew_install.XXXXXX)"
    curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh -o "${BREW_INSTALLER_SCRIPT}"
    NONINTERACTIVE=1 /bin/bash "${BREW_INSTALLER_SCRIPT}"
    success "Homebrew 本体安装完成！"
  fi
}

# 2. 配置 Shell 环境变量
configure_shell_env() {
  if [[ ! -x "${BREW_BIN}" ]]; then
    if command -v brew >/dev/null 2>&1; then
      BREW_BIN="$(command -v brew)"
    else
      error "未找到 brew 可执行文件 (${BREW_BIN})，请检查安装日志。"
      exit 1
    fi
  fi

  # 在当前脚本会话中生效
  eval "$("${BREW_BIN}" shellenv)"

  local shellenv_line="eval \"\$(${BREW_BIN} shellenv)\""
  for rc_file in "${HOME}/.zprofile" "${HOME}/.zshrc"; do
    if [[ ! -f "${rc_file}" ]]; then
      touch "${rc_file}"
    fi
    if ! grep -Fq "${BREW_BIN} shellenv" "${rc_file}" 2>/dev/null; then
      {
        echo ""
        echo "# Homebrew environment"
        echo "${shellenv_line}"
      } >> "${rc_file}"
      info "已将 Homebrew 环境变量写入 ${rc_file}"
    else
      info "${rc_file} 中已包含 Homebrew 环境变量配置，跳过重复写入"
    fi
  done

  success "当前 Homebrew 版本: $(brew --version | head -n 1)"
}

# 3. 安装 Brewfile 中的软件包与应用
install_brewfile_apps() {
  if [[ ! -f "${BREWFILE_PATH}" ]]; then
    warn "未找到 ${BREWFILE_PATH}，跳过应用安装。"
    return 0
  fi

  info "正在更新 Homebrew 索引并安装 Brewfile 中的软件包与应用..."
  setup_sudo_askpass_if_needed

  brew bundle --file="${BREWFILE_PATH}"
  success "Brewfile 中的所有工具与应用已安装完成！"
}

main() {
  local mode="${1:-all}"

  case "${mode}" in
    --brew-only)
      install_homebrew
      configure_shell_env
      ;;
    --apps-only)
      configure_shell_env
      install_brewfile_apps
      ;;
    all|--all)
      install_homebrew
      configure_shell_env
      install_brewfile_apps
      ;;
    -h|--help)
      echo "用法: $0 [--all | --brew-only | --apps-only]"
      exit 0
      ;;
    *)
      error "未知参数: ${mode}"
      echo "用法: $0 [--all | --brew-only | --apps-only]"
      exit 1
      ;;
  esac

  echo ""
  success "全部自动化配置任务已完成！"
}

main "$@"
