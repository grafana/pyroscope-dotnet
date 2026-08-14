#!/usr/bin/env bash
# prepare-merge.sh <ref> <base-branch>
# Steps 1-9 of the merge-datadog skill.
# <ref> can be a tag (v3.38.0), a commit hash, or a remote ref (dd-trace-dotnet/main).
# <base-branch> is the local branch to base the merge on (e.g. main).
set -euxo pipefail

REF="${1:-}"
BASE="${2:-}"

if [ -z "$REF" ] || [ -z "$BASE" ]; then
  echo "Usage: $0 <ref> <base-branch>"
  echo "  e.g. $0 v3.38.0 main"
  echo "  e.g. $0 dd-trace-dotnet/main main"
  echo "  e.g. $0 abc1234 main"
  exit 1
fi

# Derive a clean version string for the branch name.
# Strip leading 'v', replace '/' with '-' so remote refs like dd-trace-dotnet/main become dd-trace-dotnet-main.
VERSION="${REF#v}"
VERSION="${VERSION//\//-}"
BRANCH="kk/fork-update-${VERSION}"

# ── Ensure remotes and fetch ─────────────────────────────────────────────────
ensure_remotes() {
  echo "==> Ensuring remotes and fetching..."
  local script_dir
  script_dir="$(cd "$(dirname "$0")" && pwd)"
  "$script_dir/find-previously-merged-version.sh" >/dev/null
}

# ── Verify the ref resolves to a commit ──────────────────────────────────────
verify_ref() {
  echo "==> Verifying ref '${REF}' resolves to a commit..."
  if ! git rev-parse --verify "${REF}^{commit}" &>/dev/null; then
    echo "ERROR: '${REF}' does not resolve to a commit. Aborting."
    exit 1
  fi
  echo "    Ref '${REF}' confirmed."
}

# ── Create merge branch from <base> ──────────────────────────────────────────
create_branch() {
  echo "==> Creating branch ${BRANCH} from ${BASE}..."
  if git rev-parse --verify "${BRANCH}" &>/dev/null; then
    git branch -D "${BRANCH}"
  fi
  git push origin --delete "${BRANCH}" 2>/dev/null || true
  git checkout -b "${BRANCH}" "${BASE}"
}

# ── Start the merge ──────────────────────────────────────────────────────────
start_merge() {
  echo "==> Starting merge of '${REF}' (--no-commit --no-ff)..."
  # Conflicts are expected; we handle them in subsequent steps.
  git merge "${REF}" --no-commit --no-ff || true
}

# ── Remove directories we don't carry in the fork ────────────────────────────
remove_fork_dirs() {
  echo "==> Removing directories not carried in the fork..."
  git rm -rf --ignore-unmatch \
    tracer \
    shared/src/Datadog.Trace.ClrProfiler.Native \
    shared/test \
    .azure-pipelines \
    .gitlab \
    docs \
    profiler/docs \
    profiler/src/Tools \
    .gitlab-ci.yml

  # Keep only demos used by the upstream-derived profiler integration suite.
  if [ -d profiler/src/Demos ]; then
    for path in profiler/src/Demos/*; do
      case "$path" in
        profiler/src/Demos/Directory.Build.props | \
        profiler/src/Demos/Samples.BuggyBits | \
        profiler/src/Demos/Samples.Computer01 | \
        profiler/src/Demos/Samples.ExceptionGenerator | \
        profiler/src/Demos/Samples.HttpRequest | \
        profiler/src/Demos/Samples.ParallelCountSites | \
        profiler/src/Demos/Samples.WaitHandles | \
        profiler/src/Demos/Samples.Website-AspNetCore01 | \
        profiler/src/Demos/Shared)
          ;;
        *)
          git rm -rf --ignore-unmatch "$path"
          ;;
      esac
    done
  fi

  # Keep the curated profiler integration suite and C++ unit tests, but remove
  # managed tests and integration areas that require deleted Datadog features.
  git rm -rf --ignore-unmatch \
    profiler/test/RuntimeMetrics.Tests \
    profiler/test/Directory.Build.props \
    profiler/test/Datadog.Profiler.IntegrationTests/ApplicationInfo \
    profiler/test/Datadog.Profiler.IntegrationTests/CodeHotspot \
    profiler/test/Datadog.Profiler.IntegrationTests/HeapSnapshot \
    profiler/test/Datadog.Profiler.IntegrationTests/ReferenceChain \
    profiler/test/Datadog.Profiler.IntegrationTests/SingleStepInstrumentation \
    profiler/test/Datadog.Profiler.IntegrationTests/Timeline \
    profiler/test/Datadog.Profiler.IntegrationTests/WindowsOnly \
    profiler/test/Datadog.Profiler.IntegrationTests/DebugInfo/GitMetadataTest.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/DebugInfo/LineNumberTest.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/Helpers/AgentEtwProxy.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/Helpers/TelemetryMetric.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/Helpers/TelemetryMetricsFileParser.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/Helpers/TelemetryMetricsFileParserHelpers.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/MetricsTest.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/MemoryFootprintTest.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/Network/HttpRequestMetricTest.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/LiveObjects \
    profiler/test/Datadog.Profiler.IntegrationTests/Logger \
    profiler/test/Datadog.Profiler.IntegrationTests/ProcessTagsTest.cs \
    profiler/test/Datadog.Profiler.IntegrationTests/Signature \
    profiler/test/Datadog.Profiler.IntegrationTests/TelemetryMetricTest.cs
}

# ── Remove files we replace with git submodules ──────────────────────────────
remove_submodule_files() {
  echo "==> Removing files replaced by git submodules..."
  git rm -rf --ignore-unmatch \
    build/cmake/FindSpdlog.cmake \
    shared/src/native-lib/spdlog \
    build/cmake/FindManagedLoader.cmake
}

# ── Resolve DU conflicts (deleted-by-us / updated-by-upstream) ───────────────
resolve_du_conflicts() {
  echo "==> Resolving DU (deleted-by-us) conflicts..."
  git status --porcelain | grep '^DU ' | cut -c4- | xargs -r git rm -f
  echo "    DU conflicts done"
}

# ── Remove upstream .github and .claude additions ────────────────────────────
remove_upstream_additions() {
  echo "==> Removing upstream .github and .claude additions..."

  local files
  files=$(git status --porcelain | grep '^A ' | grep '\.github' | cut -c4- || true)
  if [ -n "$files" ]; then
    echo "$files" | xargs -r git rm -f
    echo "    upstream .github additions removed"
  else
    echo "    no upstream .github additions found"
  fi

  local claude_files
  claude_files=$(git status --porcelain | grep '^A ' | grep '\.claude' | cut -c4- || true)
  if [ -n "$claude_files" ]; then
    echo "$claude_files" | xargs -r git rm -f
    echo "    upstream .claude additions removed"
  else
    echo "    no upstream .claude additions found"
  fi
}

# ── Resolve .github/CODEOWNERS to our fork version ───────────────────────────
resolve_codeowners() {
  echo "==> Resolving .github/CODEOWNERS..."
  if [ -f .github/CODEOWNERS ]; then
    git checkout --ours .github/CODEOWNERS
    git add .github/CODEOWNERS
    echo "    .github/CODEOWNERS resolved to our fork version"
  else
    echo "    .github/CODEOWNERS not present, nothing to do"
  fi
}

# ── Initialize and update git submodules ──────────────────────────────────────
update_submodules() {
  echo "==> Updating git submodules..."
  git submodule update --init --recursive --jobs 6
}

# ── Main ──────────────────────────────────────────────────────────────────────
ensure_remotes
verify_ref
create_branch
start_merge
remove_fork_dirs
remove_submodule_files
resolve_du_conflicts
remove_upstream_additions
resolve_codeowners
update_submodules

echo ""
echo "==> Steps complete. Branch: ${BRANCH}"
echo "    Remaining conflicts (if any):"
git diff --name-only --diff-filter=U || true
echo ""
echo "    Next: resolve remaining conflicts (steps 2-3 in SKILL.md), then build (step 4)."
