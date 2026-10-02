import mariadb from 'mariadb'

const pool = mariadb.createPool({
  host: process.env.DB_HOST || '127.0.0.1',
  port: Number(process.env.DB_PORT || 3306),
  user: process.env.DB_USER || 'csstats',
  password: process.env.DB_PASSWORD || 'csstats',
  database: process.env.DB_NAME || 'csstats',
  connectionLimit: 5,
  // The operator-provisioned server uses cert-manager-issued certificates
  // that the node image does not trust, so skip verification.
  ...(process.env.DB_TLS === 'true' ? { ssl: { rejectUnauthorized: false } } : {}),
})

export async function initDb() {
  const conn = await pool.getConnection()
  try {
    await conn.query(`
      CREATE TABLE IF NOT EXISTS snapshots (
        id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
        steam_id VARCHAR(20) NOT NULL,
        captured_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
        kills INT UNSIGNED NOT NULL,
        deaths INT UNSIGNED NOT NULL,
        wins INT UNSIGNED NOT NULL,
        mvps INT UNSIGNED NOT NULL,
        matches_played INT UNSIGNED NOT NULL DEFAULT 0,
        matches_won INT UNSIGNED NOT NULL DEFAULT 0,
        INDEX idx_steam_captured (steam_id, captured_at)
      )
    `)
  } finally {
    conn.release()
  }
}

export async function insertSnapshot(snapshot) {
  const conn = await pool.getConnection()
  try {
    const result = await conn.query(
      `INSERT INTO snapshots (steam_id, kills, deaths, wins, mvps, matches_played, matches_won)
       VALUES (?, ?, ?, ?, ?, ?, ?)`,
      [
        snapshot.steamId,
        snapshot.kills,
        snapshot.deaths,
        snapshot.wins,
        snapshot.mvps,
        snapshot.matchesPlayed,
        snapshot.matchesWon,
      ],
    )
    return Number(result.insertId)
  } finally {
    conn.release()
  }
}

export async function getLatestSnapshot(steamId) {
  const conn = await pool.getConnection()
  try {
    const rows = await conn.query(
      `SELECT steam_id AS steamId, captured_at AS capturedAt,
              kills, deaths, wins, mvps, matches_played AS matchesPlayed,
              matches_won AS matchesWon
       FROM snapshots
       WHERE steam_id = ?
       ORDER BY captured_at DESC, id DESC
       LIMIT 1`,
      [steamId],
    )
    return rows[0] ?? null
  } finally {
    conn.release()
  }
}

export async function getHistory(steamId, limit = 1000) {
  const conn = await pool.getConnection()
  try {
    return await conn.query(
      `SELECT steam_id AS steamId, captured_at AS capturedAt,
              kills, deaths, wins, mvps, matches_played AS matchesPlayed,
              matches_won AS matchesWon
       FROM snapshots
       WHERE steam_id = ?
       ORDER BY captured_at ASC, id ASC
       LIMIT ?`,
      [steamId, limit],
    )
  } finally {
    conn.release()
  }
}

export default pool
