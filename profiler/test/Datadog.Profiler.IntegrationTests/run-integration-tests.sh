#!/usr/bin/env bash
set -euo pipefail

results_dir="${RESULTS_DIR:-/results}"
filter="${UPSTREAM_PROFILER_TEST_FILTER:-(Category!=CpuLimitTest)}"
project="profiler/test/Datadog.Profiler.IntegrationTests/Datadog.Profiler.IntegrationTests.csproj"

if [[ -z "${results_dir}" || "${results_dir}" == "/" ]]; then
  echo "Refusing to clear unsafe results directory: '${results_dir}'" >&2
  exit 2
fi

rm -rf -- "${results_dir}"
mkdir -p "${results_dir}" "${DD_TESTING_OUPUT_DIR}"

dotnet test "${project}" \
  -c Release \
  -p:Platform=x64 \
  --framework net10.0 \
  --no-build \
  --filter "${filter}" \
  --blame-hang \
  --blame-hang-timeout 5m \
  --logger "trx;LogFileName=upstream-profiler-integration.trx" \
  --results-directory "${results_dir}"
