#!/usr/bin/env bash
# Find previously merged versions from git log
set -euo pipefail
git log --oneline main | grep -iE "merge (tag|upstream|datadog)" | grep -oP 'v\d+\.\d+\.\d+' | sort -V | tail -5
