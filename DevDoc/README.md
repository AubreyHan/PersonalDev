# 开发需求与设计文档库 (DevDoc)

本仓库专门用于沉淀与管理各个独立开发项目的**产品需求 (PRD)、技术架构方案 (Architecture)、详细规格 (Spec)** 以及**面向 Antigravity AI 的 Vibe Coding 实施路线图 (Implementation Plan)**。

---

## 目录组织规范

每个开发项目在根目录下独占一个子目录：

```text
DevDoc/
├── .agents/
│   └── skills/
│       └── vibe-spec-architect/    # 交互式需求梳理与 Vibe Coding 文档管理 Skill
├── <project-1-slug>/               # 项目 1 文档目录
│   ├── README.md                   # 项目概览与索引
│   ├── PRD.md                      # 产品需求与验收标准
│   ├── ARCHITECTURE.md             # 技术架构设计与选型
│   ├── SPEC.md                     # 详细接口/数据模型定义
│   └── IMPLEMENTATION_PLAN.md      # Vibe Coding 原子任务拆解
└── <project-2-slug>/               # 项目 2 文档目录
    └── ...
```

---

## 已收录项目 (Projects)

- [m5-vehicle-tracker](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/m5-vehicle-tracker/README.md)：基于 M5Stack Core2 + u-blox NEO-M9N 的车载高精度轨迹记录仪与多功能数字仪表系统。
- [github-auto-sync](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/github-auto-sync/README.md)：基于 Tauri 2.0 (Rust) + React 的轻量级桌面系统托盘应用，支持多目录 Git 仓库自动递归发现、动态监听与 4 种模式策略化自动同步。

---

## 如何使用内置 Skill 协作

本项目已配置 Workspace Skill：`vibe-spec-architect`。

在与 Antigravity 对话时：
1. **提出新项目需求**：例如直接说明：*“我想做一个新的 CLI 工具，用来自动同步 Markdown 笔记到 Notion，帮我梳理需求并建立项目文档。”*
2. **交互式迭代**：通过多轮对话补充边界、技术选型或修改功能。
3. **自动建档**：系统会在对应的 `./<project-slug>/` 下生成或更新结构化文档。
4. **Vibe Coding 直达**：在后续实际编码阶段，直接引用对应的 `IMPLEMENTATION_PLAN.md` 即可引导 AI 分阶段精准编码。
