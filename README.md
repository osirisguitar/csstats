# CS2 Stats Dashboard

Dashboard for CS2 player stats (K/D, wins, MVPs) with historical K/D tracking.

A Fastify backend polls the Steam `GetUserStatsForGame` API every 10s (minimum interval), stores snapshots in
MariaDB, and serves them to a React dashboard.

## Layout

- `backend/` – Fastify API + Steam poller (Node >= 20)
- `frontend/` – React + Vite + Recharts dashboard
- `docker-compose.yml` – MariaDB

## Setup

```sh
# 1. Start MariaDB
docker compose up -d

# 2. Backend
cp backend/.env.example backend/.env   # adjust if needed
cd backend && npm install && npm run dev   # listens on :3000

# 3. Frontend
cd frontend && npm install && npm run dev   # listens on :5173, proxies /api to :3000
```

## Configuration (backend/.env)

| Variable           | Default | Description                       |
| ------------------ | ------- | --------------------------------- |
| `PORT`             | `3000`  | API port                          |
| `STEAM_API_KEY`    | –       | Steam Web API key                 |
| `STEAM_ID`         | –       | 64-bit Steam ID to track          |
| `POLL_INTERVAL_MS` | `10000` | Poll interval (clamped to >= 10s) |
| `DB_*`             | –       | MariaDB connection settings       |

## API

- `GET /api/current` – latest snapshot with computed `kdr`
- `GET /api/history?limit=1000` – snapshots (ascending) for the K/D chart

Snapshots are only stored when the tracked stats change, so the chart shows actual development rather than
repeated identical points.

## CYD

A platform io application to show the stats dashboard on a "Cheap Yellow Display". The one I have is not an
original CYD, it's a Freenove clone. It likely needs a little bit of adaptation (display drivers etc.)
depending on your flavor of CYD.

1. Install pio (MacOS: `pip install platformio`)
2. cd ./cyd
3. Connect CYD
4. pio run -t upload

Tap on screen to switch between dashboard and KDR history diagram.
