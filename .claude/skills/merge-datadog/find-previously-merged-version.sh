#!/usr/bin/env bash
# Find previously merged versions from git log.
# Also ensures the 'dd-trace-dotnet' and 'upstream' remotes are correctly configured.
set -euo pipefail

DATADOG_URL="git@github.com:DataDog/dd-trace-dotnet.git"
GRAFANA_URL="git@github.com:grafana/pyroscope-dotnet.git"

ensure_remote() {
  local name="$1" expected_url="$2" repo_match="$3"
  if ! git remote get-url "$name" &>/dev/null; then
    git remote add "$name" "$expected_url"
  else
    local actual_url
    actual_url=$(git remote get-url "$name")
    # Match by repository path (case-insensitive) rather than the exact URL.
    # Claude Code web sessions rewrite GitHub remotes (url.*.insteadOf plus a
    # local git proxy), so `git remote get-url` may resolve to an
    # https://github.com/<owner>/<repo> or http://local_proxy.../git/<owner>/<repo>
    # form instead of the git@github.com:<owner>/<repo> URL we would `add`.
    # They all point at the same repository, so a substring check is enough.
    if ! printf '%s' "$actual_url" | grep -qiF "$repo_match"; then
      echo "ERROR: '$name' remote points to '$actual_url', expected it to reference '$repo_match'." >&2
      exit 1
    fi
  fi
}

ensure_remote dd-trace-dotnet "$DATADOG_URL" "DataDog/dd-trace-dotnet"
ensure_remote upstream  "$GRAFANA_URL" "grafana/pyroscope-dotnet"

git fetch upstream --quiet
git fetch dd-trace-dotnet --tags --quiet
git log --oneline upstream/main | grep -iE "merge (tag|upstream|datadog)" | grep -oP 'v\d+\.\d+\.\d+' | sort -V | tail -5
