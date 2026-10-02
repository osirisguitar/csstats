const STEAM_API_BASE = 'https://api.steampowered.com'

export function createPoller({ steamApiKey, steamId, appId = 730, intervalMs = 10000, onSnapshot, onError }) {
  let timer = null
  let running = false
  let lastSignature = null

  const fetchStats = async () => {
    const url = `${STEAM_API_BASE}/ISteamUserStats/GetUserStatsForGame/v0002/?appid=${appId}&key=${steamApiKey}&steamid=${steamId}`
    const res = await fetch(url)
    if (res.status === 429) {
      throw Object.assign(new Error('Rate limited by Steam API'), { statusCode: 429 })
    }
    if (!res.ok) {
      throw new Error(`Steam API responded with status ${res.status}`)
    }
    const body = await res.json()
    const stats = body?.playerstats?.stats
    if (!Array.isArray(stats)) {
      throw new Error('Unexpected Steam API response shape')
    }
    return stats
  }

  const toSnapshot = (stats) => {
    const map = new Map(stats.map((s) => [s.name, s.value]))
    return {
      steamId,
      kills: map.get('total_kills') ?? 0,
      deaths: map.get('total_deaths') ?? 0,
      wins: map.get('total_wins') ?? 0,
      mvps: map.get('total_mvps') ?? 0,
      matchesPlayed: map.get('total_matches_played') ?? 0,
      matchesWon: map.get('total_matches_won') ?? 0,
    }
  }

  const tick = async () => {
    if (running) return
    running = true
    try {
      const stats = await fetchStats()
      const snapshot = toSnapshot(stats)
      const signature = [snapshot.kills, snapshot.deaths, snapshot.wins, snapshot.mvps, snapshot.matchesPlayed, snapshot.matchesWon].join(':')
      if (signature === lastSignature) return
      lastSignature = signature
      await onSnapshot(snapshot)
    } catch (err) {
      onError?.(err)
    } finally {
      running = false
    }
  }

  return {
    start() {
      tick()
      timer = setInterval(tick, Math.max(intervalMs, 10000))
    },
    stop() {
      if (timer) clearInterval(timer)
    },
  }
}
