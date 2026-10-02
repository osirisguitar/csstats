import 'dotenv/config'
import Fastify from 'fastify'
import cors from '@fastify/cors'
import { initDb, insertSnapshot, getLatestSnapshot, getHistory } from './db.js'
import { createPoller } from './poller.js'

const port = Number(process.env.PORT || 3000)
const steamApiKey = process.env.STEAM_API_KEY
const steamId = process.env.STEAM_ID
const appId = Number(process.env.CS2_APP_ID || 730)
const pollIntervalMs = Number(process.env.POLL_INTERVAL_MS || 10000)

if (!steamApiKey || !steamId) {
  console.error('STEAM_API_KEY and STEAM_ID must be set (see .env.example)')
  process.exit(1)
}

const app = Fastify({ logger: true })
await app.register(cors, { origin: true })

app.get('/api/current', async () => {
  const snapshot = await getLatestSnapshot(steamId)
  if (!snapshot) return null
  const kdr = snapshot.deaths > 0 ? snapshot.kills / snapshot.deaths : snapshot.kills
  return { ...snapshot, kdr }
})

app.get('/api/history', async (req) => {
  const limit = Math.min(Number(req.query?.limit) || 1000, 5000)
  const rows = await getHistory(steamId, limit)
  return rows.map((row) => ({
    ...row,
    kdr: row.deaths > 0 ? row.kills / row.deaths : row.kills,
  }))
})

try {
  await initDb()
  console.log('Database ready')

  const poller = createPoller({
    steamApiKey,
    steamId,
    appId,
    intervalMs: pollIntervalMs,
    onSnapshot: (snapshot) => {
      insertSnapshot(snapshot)
        .then((id) => console.log(`Stored snapshot #${id}`))
        .catch((err) => console.error('Failed to store snapshot:', err.message))
    },
    onError: (err) => console.error('Poll failed:', err.message),
  })
  poller.start()

  const shutdown = () => {
    app.close().finally(() => process.exit(0))
  }
  process.on('SIGINT', shutdown)
  process.on('SIGTERM', shutdown)

  await app.listen({ port, host: '0.0.0.0' })
} catch (err) {
  app.log.error(err)
  process.exit(1)
}
