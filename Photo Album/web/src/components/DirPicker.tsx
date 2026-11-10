import { useEffect, useState } from 'react'
import { api, Dir } from '../api'

interface Props {
  label: string
  value: string
  onChange: (path: string) => void
  allowClear?: boolean
}

/** Tree-style browser over the NAS shared folders (/share). */
export default function DirPicker({ label, value, onChange, allowClear }: Props) {
  const [cwd, setCwd] = useState('')
  const [root, setRoot] = useState('')
  const [dirs, setDirs] = useState<Dir[]>([])
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)

  useEffect(() => {
    let alive = true
    setLoading(true)
    api
      .list(cwd)
      .then((r) => {
        if (!alive) return
        setRoot(r.root)
        setDirs(r.dirs)
        setError('')
      })
      .catch((e) => alive && setError(e.message))
      .finally(() => alive && setLoading(false))
    return () => {
      alive = false
    }
  }, [cwd])

  const current = cwd || root
  const atRoot = !cwd || cwd === root
  const parent = atRoot ? '' : current.slice(0, current.lastIndexOf('/')) || root

  return (
    <fieldset className="picker">
      <legend>{label}</legend>
      <div className="picker-bar">
        <button type="button" disabled={atRoot} onClick={() => setCwd(parent === root ? '' : parent)}>
          ↑ 上一级
        </button>
        <code className="picker-path">{current}</code>
        <button type="button" disabled={atRoot} onClick={() => onChange(current)}>
          选择此目录
        </button>
      </div>
      {error && <div className="alert error">{error}</div>}
      <ul className="picker-list">
        {loading && <li className="muted">加载中…</li>}
        {!loading && dirs.length === 0 && <li className="muted">（没有子目录）</li>}
        {!loading &&
          dirs.map((d) => (
            <li key={d.path} className={d.path === value ? 'selected' : ''}>
              <button type="button" className="link" onClick={() => setCwd(d.path)}>
                📁 {d.name}
              </button>
              <button type="button" className="small" onClick={() => onChange(d.path)}>
                选择
              </button>
            </li>
          ))}
      </ul>
      <div className="picker-value">
        已选择：{value ? <code>{value}</code> : <span className="muted">未选择</span>}
        {allowClear && value && (
          <>
            {' '}
            <button type="button" className="small" onClick={() => onChange('')}>
              清除选择
            </button>
          </>
        )}
      </div>
    </fieldset>
  )
}
