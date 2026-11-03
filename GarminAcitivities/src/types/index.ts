export type GarminRegion = "cn" | "global";

export interface UserProfile {
  displayName: string;
  fullName?: string;
  profileImageUrl?: string;
  region: GarminRegion;
}

export interface ActivityListItem {
  activityId: number;
  activityName: string;
  activityType: string;
  startTimeLocal: string;
  distanceMeters: number;
  durationSeconds: number;
  avgSpeedMps: number;
  avgHrBpm?: number;
  maxHrBpm?: number;
  elevationGainMeters: number;
  isExported: boolean;
  localFilePath?: string;
}

export interface ActivityQuery {
  start: number;
  limit: number;
  activityType?: string;
  startDate?: string;
  endDate?: string;
}

export interface ActivitiesPageResponse {
  totalCount: number;
  items: ActivityListItem[];
  fromCache: boolean;
  availableTypes: string[];
  syncError?: string;
}

export interface TaskProgressEvent {
  activityId: number;
  status: "downloading" | "saved" | "failed";
  progressPercent: number;
  savedPath?: string;
  errorMessage?: string;
}

export interface QueueSummaryEvent {
  totalTasks: number;
  completedTasks: number;
  failedTasks: number;
  remainingTasks: number;
  isPaused: boolean;
}

export interface AppSettings {
  exportDirectory: string;
  defaultFormat: string;
  namingTemplate: string;
  concurrency: number;
  delayJitterMs: number;
}
