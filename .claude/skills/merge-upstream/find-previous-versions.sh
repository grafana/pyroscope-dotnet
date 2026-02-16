#!/usr/bin/env bash
# Find previously merged upstream versions from git log
set -euo pipefail
git log --oneline main | grep -iE "merge (tag|upstream)" | grep -oP 'v\d+\.\d+\.\d+' | sort -V | tail -5
