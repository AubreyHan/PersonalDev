---
name: vibe-spec-architect
description: >-
  Interactive requirement elicitation and vibe-coding documentation manager.
  Use this skill whenever the user is discussing, drafting, refining, or modifying
  development requirements, PRDs, technical architecture, or vibe-coding task specs.
  It interactively gathers details, structures documents for direct execution by Antigravity agents,
  and organizes all documentation into dedicated per-project subdirectories.
---

# Vibe Spec Architect (需求与 Vibe Coding 文档架构师)

本 Skill 旨在协助用户以**交互式沟通**的方式，将模糊或迭代中的软件开发需求，梳理并沉淀为**结构严谨、上下文完备、可直接用于 Antigravity 等 AI 编程助手进行 Vibe Coding 的高质量文档体系**。

每个独立开发项目在工作区根目录下均拥有一个独立的子目录，所有该项目的需求、架构与实施计划均收归其中。

---

## 核心设计原则 (Core Principles)

1. **交互式演进，单项依次确认 (One-by-One Confirmation)**：
   - **严禁一次性抛出长篇问题清单**轰炸用户。
   - **如果有多项内容需要确认，必须以交互方式一项一项单独提出**。
   - 每次只聚焦当前最关键的 1 个问题或决策点（可附带你的推荐建议或简要选项供用户参考），等待用户回复并确认后，再推进下一项。
   - 用户提出修改时，同步更新全套受影响的文档，并保持版本一致性。

2. **面向 Vibe Coding 优化 (AI-Executable)**：
   - AI 进行 Vibe Coding 最容易失败的原因是：**缺少上下文、边界不清晰、任务粒度过大、没有明确的验收标准**。
   - 生成的文档必须具备：
     - **清晰的技术栈与依赖声明**（避免模型随机选用不兼容库）。
     - **模块目录树与职责划分**（避免文件组织混乱）。
     - **原子化执行任务清单 (Atomic Tasks)**（每一步可独立交付与验证）。
     - **确定性的验证标准与命令**（如 `npm test`, `curl ...`, 单元测试要求）。
     - **明确的 Non-Goals 与边界限制**（防止模型幻觉或过度工程化）。

3. **纯文档定位，严禁主动编码 (Documentation Only, No Proactive Coding)**：
   - 本仓库仅作为需求与设计文档中心。
   - **严禁主动去创建业务代码或做实际功能开发**。
   - 所有的输出和修改仅限 Markdown 文档（PRD、架构图、接口规范、实施计划等）。
   - 编写的 `IMPLEMENTATION_PLAN.md` 仅作为未来在独立工程仓库中交付给 AI 进行 Vibe Coding 的指导手册，不在本项目中执行实现。

4. **单项目独立子目录 (Project Isolation)**：
   - 每个项目在仓库根目录下拥有独立的目录：`./<project-slug>/`。
   - 命名规范：使用英文小写字母与中划线（如 `ai-resume-builder`、`wechat-bot`）。
   - 所有与该项目相关的 PRD、架构、接口规范、实施路线图全部收拢在该子目录下。

---

## 项目文档标准目录结构 (Directory Structure)

为新项目创建目录后，标准文件结构如下：

```text
<project-slug>/
├── README.md               # 项目概览：项目背景、当前状态、快速上手、文档导航
├── PRD.md                  # 产品需求文档：用户角色、核心用例、功能规格、验收标准
├── ARCHITECTURE.md         # 技术架构设计：技术选型、目录结构、模块关系、数据流
├── SPEC.md                 # 详细设计与规范：数据模型、接口定义 (API)、状态机、边界异常
└── IMPLEMENTATION_PLAN.md  # Vibe Coding 实施路线图：拆解到原子任务与验证步骤的执行清单
```

*(小型或原型项目可先输出整合型 `VIBE_SPEC.md`，随后随复杂度上升平滑拆分)*

---

## 交互工作流 (Workflow Stages)

### 阶段一：项目初始化与基本诉求梳理 (Intake & Init)

1. **确定项目标识与目录**：
   - 询问或根据用户描述提炼出项目的唯一英文代号（`project-slug`）。
   - 检查工作区下是否存在该子目录。如不存在，自动创建 `./<project-slug>/`。
2. **初步意图对齐**：
   - 了解项目的核心目标（解决什么问题、为谁服务）。
   - 了解应用形态（Web应用、CLI工具、Chrome插件、后端微服务、移动端等）。

### 阶段二：深入交互与需求细化 (Interactive Deep Dive)

> [!IMPORTANT]
> **多项确认原则**：若存在多个维度或细节需要与用户对齐，**务必以单次仅提 1 项的交互方式依次确认**。每轮对话提出 1 个明确问题（可附带推荐方案供用户选择），待用户回复后再推进下一项，严禁一次性提出多个问题。

通过单项推进的交互提问（必要时可推荐使用 `/grill-me` 深度对齐），逐层明确以下维度：

- **功能范围 (Scope)**：
  - MVP（最小可行性产品）的核心功能是什么？
  - 哪些是次要功能？哪些是明确的 **Non-Goals**（本阶段坚决不做）？
- **用户旅程与交互流程 (User Flow & UX)**：
  - 用户从打开应用到完成核心目标的关键链路是什么？
  - 核心界面包含哪些组件与操作反馈？
- **技术栈偏好 (Tech Stack)**：
  - 语言与框架偏好（如 React + Vite + Tailwind / Next.js / Go / FastAPI / Node 等）。
  - 数据存储（SQLite / PostgreSQL / Redis / 本地文件 / LocalStorage 等）。
  - 第三方 API 与鉴权方式。
- **边界与异常 (Edge Cases)**：
  - 弱网、鉴权失败、空数据、超大输入等异常场景如何处理？

### 阶段三：生成 Vibe-Coding 专用文档体系 (Artifact Generation)

依据细化后的共识，在 `./<project-slug>/` 目录下生成或更新文档：

1. **`PRD.md`**：
   - 业务目标与价值。
   - 角色定义与用户故事（As a... I want to... So that...）。
   - 功能需求列表与详尽的验收条件（Acceptance Criteria，使用 Given/When/Then 或明确的 Checklist）。
2. **`ARCHITECTURE.md`**：
   - 系统整体架构图（使用 Mermaid 语法）。
   - 推荐的代码工程目录结构（包含各核心目录的职责说明）。
   - 技术选型及选型理由。
   - 数据流图与核心组件通信机制。
3. **`SPEC.md`**：
   - 数据模型与字段类型定义（TypeScript 接口或 SQL DDL）。
   - API 接口规范（请求方法、路径、Request Body、Response Body、HTTP 状态码）。
   - 关键算法或业务规则。
4. **`IMPLEMENTATION_PLAN.md` (核心 Vibe Coding 指南)**：
   - 将开发过程拆分为有序的阶段（如 Phase 1: 基础脚手架与数据模型, Phase 2: 核心功能, Phase 3: UI与状态打通, Phase 4: 边界优化与测试）。
   - 每个任务必须是**原子化 (Atomic)** 的，附带具体的**验证命令或检查点**。
   - 方便用户在开新会话做开发时直接下达指令，例如：*“请根据 `<project-slug>/IMPLEMENTATION_PLAN.md` 中的 Phase 1.1 进行编码实现”*。

### 阶段四：持续迭代与同步更新 (Iterative Refinement)

- 当用户在后续对话中补充、修改或推翻某些需求时：
  1. 确认修改的具体影响面（是影响 PRD、影响接口设计还是仅调整实施步骤）。
  2. 精准定位并使用编辑工具同步更新对应子目录中的文件。
  3. 在 `README.md` 或文档头部记录简短的变更日志（Changelog），确保上下文连续。

---

## Vibe Coding 规范检查清单 (Self-Verification Checklist)

在交付文档前，检查以下内容是否满足要求：

- [ ] 是否在独立子目录 `./<project-slug>/` 中？
- [ ] 任务是否已被拆分为单次 Prompt 可安全完成的颗粒度？
- [ ] 每个功能点是否有确切的验收标准（拒绝“性能良好”、“界面美观”等模糊词汇）？
- [ ] 技术依赖与版本是否有明确说明？
- [ ] 目录结构是否提供并标注了文件对应关系？
- [ ] 是否排除了不必要的冗余代码，专注于规范与上下文？
