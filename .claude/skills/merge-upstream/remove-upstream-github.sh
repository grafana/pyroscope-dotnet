#!/usr/bin/env bash
# Remove any upstream .github and .claude files that were added during merge
set -euo pipefail

# Remove upstream .github additions
files=$(git status --porcelain | grep '^A ' | grep .github | cut -c4- || true)
if [ -n "$files" ]; then
  echo "$files" | xargs -r git rm -f
  echo "upstream .github additions removed"
else
  echo "no upstream .github additions found"
fi

# Remove upstream .claude additions
claude_files=$(git status --porcelain | grep '^A ' | grep .claude | cut -c4- || true)
if [ -n "$claude_files" ]; then
  echo "$claude_files" | xargs -r git rm -f
  echo "upstream .claude additions removed"
else
  echo "no upstream .claude additions found"
fi
