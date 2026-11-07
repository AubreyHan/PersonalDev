import { FormEvent, useState } from 'react'
import { api, ApiError } from '../api'
import DirPicker from '../components/DirPicker'

const MIN = 5
const MAX = 1440

export default function SetupPage({ onDone }: { onDone: () => void }) {
  const [album, setAlbum] = useState('')
  const [inbox, setInbox] = useState('')
  const [interval, setIntervalText] = useState('60')
  const [errors, setErrors] = useState<string[]>([])
  const [busy, setBusy] = useState(false)

  async function submit(e: FormEvent) {
    e.preventDefault()
    const minutes = Number(interval)
    const local: string[] = []
    if (!album) local.push('请选择相册目录')
    if (!inbox) local.push('请选择临时导入目录')
    if (!Number.isInteger(minutes) || minutes < MIN || minutes > MAX) {
      local.push(`扫描间隔必须是 ${MIN}–${MAX} 之间的整数（分钟）`)
    }
    if (local.length) {
      setErrors(local)
      return
    }
    setBusy(true)
    try {
      await api.setup(album, inbox, minutes)
      setErrors([])
      onDone()
    } catch (err) {
      setErrors(err instanceof ApiError ? err.messages : [String(err)])
    } finally {
      setBusy(false)
    }
  }

  return (
    <form className="card" onSubmit={submit}>
      <h2>首次运行配置</h2>
      <p className="muted">请选择相册目录和临时导入目录。放入临时导入目录的照片会按扫描间隔自动导入相册。</p>
      <DirPicker label="相册目录" value={album} onChange={setAlbum} />
      <DirPicker label="临时导入目录" value={inbox} onChange={setInbox} />
      <label className="field">
        扫描间隔（分钟，{MIN}–{MAX}）
        <input type="number" min={MIN} max={MAX} step={1} value={interval} onChange={(e) => setIntervalText(e.target.value)} />
      </label>
      {errors.length > 0 && (
        <ul className="alert error">
          {errors.map((m) => (
            <li key={m}>{m}</li>
          ))}
        </ul>
      )}
      <button type="submit" className="primary" disabled={busy}>
        {busy ? '提交中…' : '完成配置'}
      </button>
    </form>
  )
}
