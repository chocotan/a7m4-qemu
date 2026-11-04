#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export PYTHONDONTWRITEBYTECODE=1
if [[ -z "${TMPDIR:-}" || "$TMPDIR" == /tmp ]]; then
  mkdir -p "$here/runtime/tmp"
  export TMPDIR="$here/runtime/tmp"
fi
exec python3 "$here/runtime/analysis/a7m4_cli.py" "$@"
