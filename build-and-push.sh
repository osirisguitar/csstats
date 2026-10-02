#!/usr/bin/env bash
set -euo pipefail

PLATFORM="linux/amd64"
REGISTRY="osirisguitar"

cd "$(dirname "$0")"

echo "==> Building and pushing ${REGISTRY}/csstats-backend"
docker build --platform "${PLATFORM}" -t "${REGISTRY}/csstats-backend:latest" backend
docker push "${REGISTRY}/csstats-backend:latest"

echo "==> Building and pushing ${REGISTRY}/csstats-frontend"
docker build --platform "${PLATFORM}" -t "${REGISTRY}/csstats-frontend:latest" frontend
docker push "${REGISTRY}/csstats-frontend:latest"

echo "==> Done"
