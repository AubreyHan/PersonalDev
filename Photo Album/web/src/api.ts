export type Phase = 'unconfigured' | 'storage_unavailable' | 'indexing' | 'running'

export interface Status {
  phase: Phase
  error?: string
  album_dir?: string
  import_dir?: string
  interval_minutes?: number
  initial_index_done: boolean
  index_done: number
  index_total: number
  last_scan_started_at?: string
  last_scan_finished_at?: string
  next_scan_at?: string
  scanning: boolean
}

export interface Dir {
  name: string
  path: string
}

export class ApiError extends Error {
  messages: string[]
  constructor(messages: string[]) {
    super(messages.join('；'))
    this.messages = messages
  }
}

async function call<T>(method: string, url: string, body?: unknown): Promise<T> {
  const res = await fetch(url, {
    method,
    headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  })
  const data = await res.json().catch(() => ({}))
  if (!res.ok) {
    const msgs: string[] = data.messages?.length ? data.messages : [data.error || `请求失败（${res.status}）`]
    throw new ApiError(msgs)
  }
  return data as T
}

export const api = {
  status: () => call<Status>('GET', '/api/status'),
  list: (path: string) =>
    call<{ root: string; dirs: Dir[] }>('GET', '/api/fs/list?path=' + encodeURIComponent(path)),
  setup: (album_dir: string, import_dir: string, interval_minutes: number) =>
    call<{ ok: boolean }>('POST', '/api/setup', { album_dir, import_dir, interval_minutes }),
  setInterval: (interval_minutes: number) =>
    call<Status>('PUT', '/api/settings/interval', { interval_minutes }),
}
