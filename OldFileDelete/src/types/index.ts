export type TimeUnit = 'minutes' | 'hours' | 'days';

export type RuleAction = 
  | 'list'          // 列出：仅在匹配清单中展示，不做删除移动
  | 'keep'          // 保留：命中后锁定保留，短路终止后续规则
  | 'direct_trash'  // 直接删除：直接移入系统废纸篓
  | 'ask_trash';    // 询问后删除：应用内逐一弹窗询问确认

export interface DateAddedCondition {
  value: number;        // 填 0 或选 Any 表示不限时间
  unit: TimeUnit;       // 'minutes' | 'hours' | 'days'
  isAny: boolean;       // 是否不限制时间
}

export interface FolderRule {
  id: string;           // 规则唯一 UUID
  name: string;         // 规则友好名称，如 "Archive PDFs"
  enabled: boolean;     // 启用/停用开关 Toggle (默认 true)
  dateAdded: DateAddedCondition; // 添加到该文件夹的时间条件
  pattern: string;      // 通配符路径表达式 (如 "*.dmg, a*.j*, **/temp*")
  action: RuleAction;   // 处置动作 (4 选 1)
  order: number;        // 从上到下的优先匹配序号 (0, 1, 2...)
}

export interface FolderItem {
  id: string;           // 文件夹唯一 UUID
  name: string;         // 文件夹显示名称，如 "Downloads"
  path: string;         // 文件夹绝对物理路径，如 "/Users/aubreyhan/Downloads"
  isTrash: boolean;     // 是否为左侧顶部常驻废纸篓项 (如果是，右侧使用 TrashRuleConfig)
  isPreset: boolean;    // 是否为预置文件夹 (如默认预置 Downloads)
  topWhitelist: string; // 置顶保留规则内容 (逗号分隔的通配符表达式)
  rules: FolderRule[];  // 属于该文件夹的普通规则列表 (严格按 order 升序排列)
  createdAt: number;
}

export interface TrashRuleConfig {
  daysThreshold: number; // 默认初始值 0 (填 0 表示不执行)
  maxCapacityGb: number; // 默认初始值 0 (填 0 表示不执行)
}

export interface AskItem {
  id: string;           // 唯一任务 ID
  folderId: string;     // 来源文件夹 ID
  filePath: string;     // 文件的绝对路径
  fileName: string;     // 文件名
  sizeBytes: number;    // 文件大小
  dateAdded: number;    // 添加时间戳 (毫秒)
  ruleName: string;     // 触发的规则名称
}

export interface MatchListItem {
  filePath: string;     // 文件路径
  fileName: string;     // 文件名
  sizeBytes: number;    // 文件大小
  dateAdded: number;    // 添加时间戳
  ruleName: string;     // 命中的"列出"规则名称
}
