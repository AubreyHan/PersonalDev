# MAC_Config - macOS Homebrew 及应用自动化安装

本仓库提供一键式 macOS **Homebrew** 本体、环境变量及常用命令行工具与桌面应用的自动化安装配置。

## 目录结构

- [`install.sh`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/MAC_Config/install.sh)：自动化安装脚本（自动识别 Apple Silicon / Intel 架构、自动安装 Homebrew、自动写入 `~/.zprofile` 和 `~/.zshrc` 环境变量，并执行 `Brewfile` 批量安装）。
- [`Brewfile`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/MAC_Config/Brewfile)：声明式软件包清单，包含：
  - **核心命令行工具 (`brew`)**：`git`、`gh`、`wget`、`jq`、`ripgrep`、`fd`、`fzf`、`tree`、`htop`
  - **Homebrew 图形化管理应用 (`cask`)**：`applite`（可视化管理、搜索与一键安装 Homebrew 应用的客户端）
  - **可选常用桌面应用 (`cask`)**：在 `Brewfile` 中按类别预置，取消注释即可一键安装

## 快速使用

### 1. 一键安装全部（Homebrew + 环境变量 + Brewfile 应用）

```bash
chmod +x ./install.sh
./install.sh
```

### 2. 仅安装 Homebrew 本体与配置环境变量

```bash
./install.sh --brew-only
```

### 3. 仅根据 `Brewfile` 安装或同步应用

修改 `Brewfile` 后，运行以下命令即可自动安装新增的包或应用：

```bash
./install.sh --apps-only
# 或者直接使用 brew 命令：
brew bundle --file=./Brewfile
```

### 4. 导出当前本机已安装的 Homebrew 软件到 Brewfile

```bash
brew bundle dump --describe --force --file=./Brewfile
```
