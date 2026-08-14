#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
image="${UPSTREAM_PROFILER_TEST_IMAGE:-pyroscope-dotnet-profiler-integration-test}"
results_dir="${UPSTREAM_PROFILER_TEST_RESULTS:-${repo_root}/artifacts/upstream-profiler-integration}"
network="pyroscope-profiler-tests-$$"
ldap_container="pyroscope-profiler-tests-ldap-$$"

cleanup() {
  docker rm -f "${ldap_container}" >/dev/null 2>&1 || true
  docker network rm "${network}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

mkdir -p "${results_dir}"

docker build \
  --platform linux/amd64 \
  --target profiler-integration-test-runner \
  --tag "${image}" \
  --file "${repo_root}/Pyroscope.Dockerfile" \
  "${repo_root}"

docker network create "${network}" >/dev/null
docker run --detach --rm \
  --platform linux/amd64 \
  --name "${ldap_container}" \
  --network "${network}" \
  --network-alias openldap-server \
  --env LDAP_ORGANISATION=Datadog \
  --env LDAP_DOMAIN=dd-trace-dotnet.com \
  --env LDAP_ADMIN_PASSWORD=Passw0rd \
  --env LDAP_BASE_DN=dc=dd-trace-dotnet,dc=com \
  osixia/openldap:latest@sha256:3f68751292b43564a2586fc29fb7337573e2dad692b92d4e78e49ad5c22e567b \
  >/dev/null

ldap_ready=false
for _ in $(seq 1 30); do
  if docker exec "${ldap_container}" ldapsearch -x -H ldap://localhost -b dc=dd-trace-dotnet,dc=com >/dev/null 2>&1; then
    ldap_ready=true
    break
  fi
  sleep 2
done

if [[ "${ldap_ready}" != true ]]; then
  echo "OpenLDAP did not become ready within 60 seconds." >&2
  docker logs "${ldap_container}" >&2
  exit 1
fi

run_tests() {
  local name="$1"
  shift
  docker run --rm \
    --platform linux/amd64 \
    --cap-add SYS_PTRACE \
    --security-opt seccomp=unconfined \
    --network "${network}" \
    --volume "${results_dir}:/results" \
    --env LDAP_SERVER=openldap-server:389 \
    --env "RESULTS_DIR=/results/${name}" \
    --env "DD_TESTING_OUPUT_DIR=/results/${name}/output" \
    "$@" \
    "${image}"
}

run_tests main
run_tests cpu-high \
  --cpus 2 \
  --env CONTAINER_CPUS=2 \
  --env 'UPSTREAM_PROFILER_TEST_FILTER=(Category=CpuLimitTest)'
run_tests cpu-low \
  --cpus 0.5 \
  --env CONTAINER_CPUS=0.5 \
  --env 'UPSTREAM_PROFILER_TEST_FILTER=(Category=CpuLimitTest)'
