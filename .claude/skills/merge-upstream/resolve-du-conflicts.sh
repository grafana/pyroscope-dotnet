#!/usr/bin/env bash
# Clean up files deleted by us but modified upstream (DU conflicts)
set -euo pipefail
git status --porcelain | grep '^DU ' | cut -c4- | xargs -r git rm -f
echo "DU conflicts done"
