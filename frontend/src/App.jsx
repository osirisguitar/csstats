import { useEffect, useState } from 'react'
import {
  ResponsiveContainer,
  LineChart,
  Line,
  XAxis,
  YAxis,
  Tooltip,
  CartesianGrid,
  ReferenceLine,
} from 'recharts'

const HISTORY_REFRESH_MS = 30000
const CURRENT_REFRESH_MS = 10000

function useInterval(callback, delay) {
  useEffect(() => {
    if (!delay) return undefined
    const id = setInterval(callback, delay)
    return () => clearInterval(id)
  }, [callback, delay])
}

async function fetchJson(url) {
  const res = await fetch(url)
  if (!res.ok) throw new Error(`${res.status} ${res.statusText}`)
  return res.json()
}

function StatCard({ label, value, detail, accent }) {
  return (
    <div className={`stat-card${accent ? ` accent-${accent}` : ''}`}>
      <div className="stat-label">{label}</div>
      <div className="stat-value">{value}</div>
      {detail && <div className="stat-detail">{detail}</div>}
    </div>
  )
}

function formatTime(iso) {
  return new Date(iso).toLocaleString([], {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  })
}

export default function App() {
  const [current, setCurrent] = useState(null)
  const [history, setHistory] = useState([])
  const [error, setError] = useState(null)
  const [lastUpdated, setLastUpdated] = useState(null)

  useEffect(() => {
    fetchJson('/api/history')
      .then((rows) => {
        setHistory(rows)
        if (rows.length > 0) setCurrent(rows[rows.length - 1])
      })
      .catch((err) => setError(err.message))
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  useInterval(() => {
    fetchJson('/api/current')
      .then((data) => {
        if (data && !error) setCurrent(data)
        setLastUpdated(new Date())
        setError(null)
      })
      .catch((err) => setError(err.message))
  }, CURRENT_REFRESH_MS)

  useInterval(() => {
    fetchJson('/api/history')
      .then((rows) => {
        setHistory(rows)
        if (rows.length > 0) setCurrent(rows[rows.length - 1])
      })
      .catch(() => {})
  }, HISTORY_REFRESH_MS)

  const chartData = history.map((row) => ({
    time: new Date(row.capturedAt).getTime(),
    label: formatTime(row.capturedAt),
    kdr: Number(row.kdr.toFixed(3)),
    winPct: row.matchesPlayed > 0
      ? Number(((row.matchesWon / row.matchesPlayed) * 100).toFixed(1))
      : null,
    wins: row.wins,
    mvps: row.mvps,
  }))

  const hasData = current != null

  return (
    <div className="dashboard">
      <header className="header">
        <h1>CS2 Stats Dashboard</h1>
        <div className="header-right">
          {lastUpdated && (
            <span className="updated">Updated {lastUpdated.toLocaleTimeString()}</span>
          )}
          <span className={`status-dot${error ? ' down' : ' up'}`} title={error ?? 'Live'} />
        </div>
      </header>

      {error && <div className="error-banner">Error: {error}</div>}

      {!hasData && !error && <p className="empty">Waiting for first snapshot from the poller&hellip;</p>}

      {hasData && (
        <>
          <section className="stats-grid">
            <StatCard
              label="K/D Ratio"
              value={current.kdr.toFixed(2)}
              detail={`${current.kills} kills / ${current.deaths} deaths`}
              accent="green"
            />
            <StatCard
              label="Wins"
              value={current.wins}
              detail={current.matchesPlayed > 0
                ? `${Math.round((current.matchesWon / current.matchesPlayed) * 100)}% of ${current.matchesPlayed} matches won`
                : undefined}
              accent="blue"
            />
            <StatCard
              label="MVPs"
              value={current.mvps}
              detail={current.matchesPlayed > 0
                ? `${(current.mvps / current.matchesPlayed).toFixed(2)} per match`
                : undefined}
              accent="gold"
            />
          </section>

          <section className="chart-panel">
            <h2>K/D ratio over time</h2>
            {chartData.length < 2 ? (
              <p className="empty">Collecting history&hellip; points appear as stats change.</p>
            ) : (
              <ResponsiveContainer width="100%" height={360}>
                <LineChart data={chartData} margin={{ top: 10, right: 20, bottom: 0, left: 0 }}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#2a2f3a" />
                  <XAxis dataKey="label" stroke="#8b93a7" tick={{ fontSize: 12 }} minTickGap={40} />
                  <YAxis stroke="#8b93a7" tick={{ fontSize: 12 }} domain={['auto', 'auto']} />
                  <Tooltip
                    contentStyle={{ background: '#141822', border: '1px solid #2a2f3a', borderRadius: 8 }}
                    labelStyle={{ color: '#8b93a7' }}
                    formatter={(value) => [value, 'K/D']}
                  />
                  <ReferenceLine y={1} stroke="#555e73" strokeDasharray="4 4" />
                  <Line
                    type="monotone"
                    dataKey="kdr"
                    stroke="#4ade80"
                    strokeWidth={2}
                    dot={chartData.length < 40 ? { r: 3 } : false}
                    activeDot={{ r: 5 }}
                    isAnimationActive={false}
                  />
                </LineChart>
              </ResponsiveContainer>
            )}
            <p className="chart-note">
              {history.length} snapshot{history.length === 1 ? '' : 's'} stored (new points appear only when stats change).
            </p>
          </section>

          <section className="chart-panel">
            <h2>Win % over time</h2>
            {chartData.length < 2 ? (
              <p className="empty">Collecting history&hellip; points appear as stats change.</p>
            ) : (
              <ResponsiveContainer width="100%" height={360}>
                <LineChart data={chartData} margin={{ top: 10, right: 20, bottom: 0, left: 0 }}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#2a2f3a" />
                  <XAxis dataKey="label" stroke="#8b93a7" tick={{ fontSize: 12 }} minTickGap={40} />
                  <YAxis
                    stroke="#8b93a7"
                    tick={{ fontSize: 12 }}
                    domain={[0, 100]}
                    tickFormatter={(value) => `${value}%`}
                    unit="%"
                  />
                  <Tooltip
                    contentStyle={{ background: '#141822', border: '1px solid #2a2f3a', borderRadius: 8 }}
                    labelStyle={{ color: '#8b93a7' }}
                    formatter={(value) => [`${value}%`, 'Win %']}
                  />
                  <ReferenceLine y={50} stroke="#555e73" strokeDasharray="4 4" />
                  <Line
                    type="monotone"
                    dataKey="winPct"
                    connectNulls
                    stroke="#60a5fa"
                    strokeWidth={2}
                    dot={chartData.length < 40 ? { r: 3 } : false}
                    activeDot={{ r: 5 }}
                    isAnimationActive={false}
                  />
                </LineChart>
              </ResponsiveContainer>
            )}
            <p className="chart-note">
              {history.length} snapshot{history.length === 1 ? '' : 's'} stored (new points appear only when stats change).
            </p>
          </section>
        </>
      )}
    </div>
  )
}
