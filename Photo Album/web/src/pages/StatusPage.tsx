import { FormEvent, useEffect, useState } from 'react'
import { api, ApiError, Status } from '../api'

const MIN = 5
const MAX = 1440

function fmt(t?: string) {
  if (!t) return '—'
  const d = new Date(t)
  const p = (n: number) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`
}

const phaseText: Record<string, string> = {
  storage_unavailable: '存储不可用',
  indexing: '首次索引中',
  running: '运行中',
}

export default function StatusPage({ status, onChange }: { status: Status; onChange: (s: Status) => void }) {
  const [interval, setIntervalText] = useState(String(status.interval_minutes ?? 60))
  const [editing, setEditing] = useState(false)
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null)

  useEffect(() => {
    if (!editing) setIntervalText(String(status.interval_minutes ?? 60))
  }, [status.interval_minutes, editing])

  async function save(e: FormEvent) {
    e.preventDefault()
    const minutes = Number(interval)
    if (!Number.isInteger(minutes) || minutes < MIN || minutes > MAX) {
      setMsg({ ok: false, text: `扫描间隔必须是 ${MIN}–${MAX} 之间的整数（分钟）` })
      return
    }
    try {
      onChange(await api.setInterval(minutes))
      setEditing(false)
      setMsg({ ok: true, text: '已保存，下次扫描时间已按新间隔重新计算' })
    } catch (err) {
      setMsg({ ok: false, text: err instanceof ApiError ? err.message : String(err) })
    }
  }

  const pct = status.index_total > 0 ? Math.min(100, (status.index_done / status.index_total) * 100) : status.initial_index_done ? 100 : 0

  return (
    <div className="card">
      <h2>
        状态 <span className={`badge ${status.phase}`}>{phaseText[status.phase] ?? status.phase}</span>
        {status.scanning && <span className="badge scanning">正在扫描</span>}
      </h2>
      {status.phase === 'storage_unavailable' && (
        <div className="alert error">相册目录或数据库暂时无法访问，每 60 秒自动重试。{status.error && <div><code>{status.error}</code></div>}</div>
      )}
      {status.phase !== 'storage_unavailable' && status.error && <div className="alert warn">最近一次错误：<code>{status.error}</code></div>}

      <dl className="grid">
        <dt>相册目录</dt>
        <dd><code>{status.album_dir ?? '—'}</code></dd>
        <dt>临时导入目录</dt>
        <dd><code>{status.import_dir ?? '—'}</code></dd>
        <dt>首次索引</dt>
        <dd>
          <div className="progress"><div style={{ width: `${pct}%` }} /></div>
          <span className="muted">
            {status.initial_index_done ? '已完成' : '进行中'}（{status.index_done} / {status.index_total}）
          </span>
        </dd>
        <dt>上次扫描</dt>
        <dd>
          {fmt(status.last_scan_started_at)}
          {status.last_scan_finished_at && <span className="muted">（完成于 {fmt(status.last_scan_finished_at)}）</span>}
        </dd>
        <dt>下次扫描</dt>
        <dd>{status.phase === 'running' ? fmt(status.next_scan_at) || '即将开始' : '首次索引完成后立即开始'}</dd>
        <dt>扫描间隔</dt>
        <dd>
          <form className="inline" onSubmit={save}>
            <input
              type="number"
              min={MIN}
              max={MAX}
              step={1}
              value={interval}
              onFocus={() => setEditing(true)}
              onChange={(e) => {
                setEditing(true)
                setIntervalText(e.target.value)
              }}
            />
            <span>分钟</span>
            <button type="submit" className="primary small">保存</button>
          </form>
          {msg && <div className={`alert ${msg.ok ? 'ok' : 'error'}`}>{msg.text}</div>}
        </dd>
      </dl>
    </div>
  )
}
