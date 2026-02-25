#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
"${ROOT_DIR}/scripts/one_click.sh" "$@"
