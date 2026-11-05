import Dexie, { type Table } from 'dexie';
import type { StudentWordProgress, TeacherWord } from '../../shared/types';

export interface OutboxSyncItem {
  id?: number;
  type: 'progress_update';
  normalizedText: string;
  payload: Partial<StudentWordProgress>;
  createdAt: number;
}

export class EnglishWordLocalDB extends Dexie {
  words!: Table<TeacherWord, string>; // primary key: normalizedText
  progress!: Table<StudentWordProgress, string>; // primary key: normalizedText
  syncOutbox!: Table<OutboxSyncItem, number>; // auto-increment id

  constructor() {
    super('EnglishWordLocalDB');
    this.version(1).stores({
      words: '&normalizedText, id, teacherId, text, createdAt',
      progress: '&normalizedText, status, ebbinghausStage, nextReviewDate, lastReviewedDate, updatedAt',
      syncOutbox: '++id, type, normalizedText, createdAt',
    });
  }
}

export const localDb = new EnglishWordLocalDB();
