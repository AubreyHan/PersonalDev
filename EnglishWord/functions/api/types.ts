import type { D1Database } from '@cloudflare/workers-types';
import type { UserRole } from '../../shared/types';

export interface Env {
  DB: D1Database;
  AI?: any;
  JWT_SECRET?: string;
}

export interface AuthUser {
  id: string;
  username: string;
  displayName: string;
  role: UserRole;
}

export interface HonoVariables {
  user?: AuthUser;
}
