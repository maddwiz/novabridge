#!/usr/bin/env sh
set -eu

MODE="${NOVABRIDGE_CONTAINER_MODE:-mock}"

if [ "$MODE" = "mock" ]; then
  exec python /app/docker/mock_novabridge_server.py "$@"
fi

if [ "$MODE" = "mcp" ]; then
  exec python /app/mcp-server/novabridge_mcp.py "$@"
fi

exec "$@"
