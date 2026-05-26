#!/usr/bin/env bash
# Find previously merged versions from git log.
# Also ensures the 'dd-trace-dotnet' and 'upstream' remotes are correctly configured.
set -euo pipefail

DATADOG_URL="https://github.com/DataDog/dd-trace-dotnet.git"
GRAFANA_URL="https://github.com/grafana/pyroscope-dotnet.git"

ensure_remote() {
  local name="$1" expected_url="$2"
  if ! git remote get-url "$name" &>/dev/null; then
    git remote add "$name" "$expected_url"
  else
    local actual_url
    actual_url=$(git remote get-url "$name")
    if [ "$actual_url" != "$expected_url" ]; then
      echo "ERROR: '$name' remote points to '$actual_url', expected '$expected_url'." >&2
      exit 1
    fi
  fi
}

ensure_remote dd-trace-dotnet "$DATADOG_URL"
ensure_remote upstream  "$GRAFANA_URL"

git fetch upstream --quiet
git fetch dd-trace-dotnet --tags --quiet
git log --oneline upstream/main | grep -iE "merge (tag|upstream|datadog)" | grep -oP 'v\d+\.\d+\.\d+' | sort -V | tail -5
