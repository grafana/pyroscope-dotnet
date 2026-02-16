#!/usr/bin/env bash
# Remove any upstream .github files that were added during merge
set -euo pipefail
git status --porcelain | grep '^A ' | grep .github | cut -c4- | xargs -r git rm -f
echo "upstream .github additions removed"
