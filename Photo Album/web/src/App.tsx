import { useCallback, useEffect, useState } from 'react'
import { api, Status } from './api'
import SetupPage from './pages/SetupPage'
import StatusPage from './pages/StatusPage'

const POLL_MS = 5000

export default function App() {
  const [status, setStatus] = useState<Status | null>(null)
  const [loadError, setLoadError] = useState('')

  const refresh = useCallback(async () => {
    try {
      setStatus(await api.status())
      setLoadError('')
    } catch (e) {
      setLoadError(e instanceof Error ? e.message : String(e))
    }
  }, [])

  useEffect(() => {
    refresh()
    const id = window.setInterval(refresh, POLL_MS)
    return () => window.clearInterval(id)
  }, [refresh])

  // Keep the URL in sync with the page being shown (/setup or /status).
  const page = status?.phase === 'unconfigured' ? '/setup' : '/status'
  useEffect(() => {
    if (status && window.location.pathname !== page) {
      window.history.replaceState(null, '', page)
    }
  }, [status, page])

  return (
    <div className="shell">
      <header>
        <h1>Photo Album</h1>
      </header>
      <main>
        {loadError && <div className="alert error">无法连接服务：{loadError}</div>}
        {!status && !loadError && <p className="muted">加载中…</p>}
        {status && status.phase === 'unconfigured' && <SetupPage onDone={refresh} />}
        {status && status.phase !== 'unconfigured' && <StatusPage status={status} onChange={setStatus} />}
      </main>
    </div>
  )
}
