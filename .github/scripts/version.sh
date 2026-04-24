#!/usr/bin/env bash
set -e
grep '<PackageVersion>' Pyroscope/Pyroscope/Pyroscope.csproj | sed 's/.*<PackageVersion>\(.*\)<\/PackageVersion>.*/\1/'
