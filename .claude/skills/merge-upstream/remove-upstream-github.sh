#!/usr/bin/env bash
# Remove any upstream .github files that were added during merge
set -euo pipefail
files=$(git status --porcelain | grep '^A ' | grep .github | cut -c4- || true)
if [ -n "$files" ]; then
  echo "$files" | xargs -r git rm -f
  echo "upstream .github additions removed"
else
  echo "no upstream .github additions found"
fi
